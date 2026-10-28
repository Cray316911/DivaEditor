// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/Muxer.h"

#include "util/Errors.h"

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace diva {

Muxer::Muxer() = default;

Muxer::~Muxer()
{
    if (m_started && !m_finished)
        finish(nullptr);
    if (m_format) {
        if (!(m_format->oformat->flags & AVFMT_NOFILE))
            avio_closep(&m_format->pb);
        avformat_free_context(m_format);
    }
}

bool Muxer::open(const std::string &utf8Path, std::string *error)
{
    int ret = avformat_alloc_output_context2(&m_format, nullptr, nullptr, utf8Path.c_str());
    if (ret < 0 || !m_format)
        return fail(error, avError("unsupported output format for " + utf8Path, ret));
    if (!(m_format->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_format->pb, utf8Path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0)
            return fail(error, avError("cannot create " + utf8Path, ret));
    }
    return true;
}

bool Muxer::needsGlobalHeader() const
{
    return m_format && (m_format->oformat->flags & AVFMT_GLOBALHEADER);
}

int Muxer::addStream(const AVCodecContext *encoder)
{
    AVStream *stream = avformat_new_stream(m_format, nullptr);
    if (!stream)
        return -1;
    avcodec_parameters_from_context(stream->codecpar, encoder);
    stream->time_base = encoder->time_base;
    if (encoder->codec_type == AVMEDIA_TYPE_VIDEO)
        stream->avg_frame_rate = encoder->framerate;
    // "hvc1" keeps HEVC files playable in QuickTime and Apple editors.
    const char *name = m_format->oformat->name;
    if (encoder->codec_id == AV_CODEC_ID_HEVC && (std::strstr(name, "mp4") || std::strstr(name, "mov")))
        stream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
    m_encoderTimeBases.push_back(encoder->time_base);
    return stream->index;
}

void Muxer::setStreamTitle(int stream, const std::string &title)
{
    if (stream >= 0 && stream < int(m_format->nb_streams))
        av_dict_set(&m_format->streams[stream]->metadata, "title", title.c_str(), 0);
}

bool Muxer::start(std::string *error)
{
    AVDictionary *opts = nullptr;
    if (std::strstr(m_format->oformat->name, "mp4") || std::strstr(m_format->oformat->name, "mov"))
        av_dict_set(&opts, "movflags", "+faststart", 0);
    const int ret = avformat_write_header(m_format, &opts);
    av_dict_free(&opts);
    if (ret < 0)
        return fail(error, avError("write header", ret));
    m_started = true;
    return true;
}

bool Muxer::write(AVPacket *packet, int stream, std::string *error)
{
    std::lock_guard lock(m_mutex);
    av_packet_rescale_ts(packet, m_encoderTimeBases[stream], m_format->streams[stream]->time_base);
    packet->stream_index = stream;
    const int ret = av_interleaved_write_frame(m_format, packet);
    if (ret < 0)
        return fail(error, avError("write packet", ret));
    return true;
}

bool Muxer::finish(std::string *error)
{
    std::lock_guard lock(m_mutex);
    if (!m_started || m_finished)
        return true;
    m_finished = true;
    const int ret = av_write_trailer(m_format);
    if (ret < 0)
        return fail(error, avError("write trailer", ret));
    return true;
}

} // namespace diva
