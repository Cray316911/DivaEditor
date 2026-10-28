// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioReader.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace diva {

AudioReader::~AudioReader()
{
    for (Stream &s : m_streams) {
        avcodec_free_context(&s.codec);
        swr_free(&s.swr);
        if (s.fifo)
            av_audio_fifo_free(s.fifo);
    }
    av_packet_free(&m_packet);
    av_frame_free(&m_frame);
    avformat_close_input(&m_format);
}

bool AudioReader::open(const QString &path, QString *error)
{
    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&m_format, utf8.constData(), nullptr, nullptr) < 0) {
        if (error)
            *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    avformat_find_stream_info(m_format, nullptr);
    const int video = av_find_best_stream(m_format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video >= 0 && m_format->streams[video]->start_time != AV_NOPTS_VALUE)
        m_origin = m_format->streams[video]->start_time * av_q2d(m_format->streams[video]->time_base);

    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    for (unsigned i = 0; i < m_format->nb_streams; ++i) {
        AVStream *st = m_format->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
            st->discard = AVDISCARD_ALL;
            continue;
        }
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec)
            continue;
        Stream s;
        s.index = int(i);
        s.codec = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(s.codec, st->codecpar);
        s.codec->pkt_timebase = st->time_base;
        if (avcodec_open2(s.codec, codec, nullptr) < 0) {
            avcodec_free_context(&s.codec);
            continue;
        }
        if (swr_alloc_set_opts2(&s.swr, &stereo, AV_SAMPLE_FMT_FLTP, kRate, &s.codec->ch_layout,
                                s.codec->sample_fmt, s.codec->sample_rate, 0, nullptr) < 0
            || swr_init(s.swr) < 0) {
            avcodec_free_context(&s.codec);
            swr_free(&s.swr);
            continue;
        }
        s.fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, kRate);
        m_streams.append(s);
    }
    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    return true;
}

void AudioReader::seek(double seconds)
{
    if (m_streams.isEmpty())
        return;
    const int64_t ts = int64_t((seconds + m_origin) * AV_TIME_BASE);
    av_seek_frame(m_format, -1, std::max<int64_t>(0, ts - AV_TIME_BASE / 2), AVSEEK_FLAG_BACKWARD);
    for (Stream &s : m_streams) {
        avcodec_flush_buffers(s.codec);
        av_audio_fifo_reset(s.fifo);
        s.skipUntil = seconds;
        s.done = false;
    }
    m_demuxDone = false;
}

void AudioReader::decodeFrames(Stream &s)
{
    const AVRational tb = m_format->streams[s.index]->time_base;
    while (avcodec_receive_frame(s.codec, m_frame) == 0) {
        const int64_t pts = m_frame->best_effort_timestamp != AV_NOPTS_VALUE ? m_frame->best_effort_timestamp
                                                                             : m_frame->pts;
        const double frameTime = pts != AV_NOPTS_VALUE ? pts * av_q2d(tb) - m_origin : s.skipUntil;
        const int capacity = swr_get_out_samples(s.swr, m_frame->nb_samples);
        m_tmpL.resize(std::max(capacity, 1));
        m_tmpR.resize(std::max(capacity, 1));
        uint8_t *out[2] = {reinterpret_cast<uint8_t *>(m_tmpL.data()), reinterpret_cast<uint8_t *>(m_tmpR.data())};
        int got = swr_convert(s.swr, out, capacity, const_cast<const uint8_t **>(m_frame->extended_data),
                              m_frame->nb_samples);
        av_frame_unref(m_frame);
        if (got <= 0)
            continue;
        // After a seek, drop everything before the requested time (sample accurate).
        int skip = 0;
        if (s.skipUntil > frameTime) {
            skip = std::min(got, int(std::lround((s.skipUntil - frameTime) * kRate)));
        }
        if (skip < got) {
            void *planes[2] = {m_tmpL.data() + skip, m_tmpR.data() + skip};
            av_audio_fifo_write(s.fifo, planes, got - skip);
            s.skipUntil = 0;
        }
    }
}

bool AudioReader::fill(int count)
{
    auto needsMore = [&] {
        for (const Stream &s : m_streams)
            if (!s.done && av_audio_fifo_size(s.fifo) < count)
                return true;
        return false;
    };
    while (needsMore()) {
        if (m_demuxDone) {
            for (Stream &s : m_streams) {
                avcodec_send_packet(s.codec, nullptr);
                decodeFrames(s);
                s.done = true;
            }
            break;
        }
        if (av_read_frame(m_format, m_packet) < 0) {
            m_demuxDone = true;
            continue;
        }
        for (Stream &s : m_streams) {
            if (s.index == m_packet->stream_index) {
                if (avcodec_send_packet(s.codec, m_packet) >= 0)
                    decodeFrames(s);
                break;
            }
        }
        av_packet_unref(m_packet);
    }
    return true;
}

void AudioReader::read(float *left, float *right, int count)
{
    std::fill(left, left + count, 0.0f);
    std::fill(right, right + count, 0.0f);
    if (m_streams.isEmpty() || count <= 0)
        return;
    fill(count);
    QVector<float> l(count), r(count);
    for (Stream &s : m_streams) {
        void *planes[2] = {l.data(), r.data()};
        const int got = av_audio_fifo_read(s.fifo, planes, count);
        for (int i = 0; i < got; ++i) {
            left[i] += l[i];
            right[i] += r[i];
        }
    }
}

} // namespace diva
