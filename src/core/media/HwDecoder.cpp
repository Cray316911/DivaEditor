// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/HwDecoder.h"

#include "gpu/GpuDevice.h"
#include "gpu/SurfacePool.h"
#include "util/Errors.h"

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace diva {

namespace {

// Decoded frames held outside the decoder at once (playback queue + frame on screen).
constexpr int kExtraPoolFrames = 12;

// Prefers D3D11VA; if the GPU cannot decode this stream (old GPU, unusual profile) it
// returns a software format so decoding continues on the CPU instead of failing.
AVPixelFormat pickFormat(AVCodecContext *ctx, const AVPixelFormat *formats)
{
    for (const AVPixelFormat *f = formats; *f != AV_PIX_FMT_NONE; ++f) {
        if (*f != AV_PIX_FMT_D3D11 || !ctx->hw_device_ctx)
            continue;
        AVBufferRef *framesRef = nullptr;
        if (avcodec_get_hw_frames_parameters(ctx, ctx->hw_device_ctx, AV_PIX_FMT_D3D11, &framesRef) < 0)
            break;
        auto *frames = reinterpret_cast<AVHWFramesContext *>(framesRef->data);
        auto *d3d = static_cast<AVD3D11VAFramesContext *>(frames->hwctx);
        // Let the compositor sample decoded surfaces directly instead of copying them.
        d3d->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        frames->initial_pool_size += kExtraPoolFrames;
        if (av_hwframe_ctx_init(framesRef) < 0) {
            av_buffer_unref(&framesRef);
            break;
        }
        av_buffer_unref(&ctx->hw_frames_ctx);
        ctx->hw_frames_ctx = framesRef;
        return AV_PIX_FMT_D3D11;
    }
    for (const AVPixelFormat *f = formats; *f != AV_PIX_FMT_NONE; ++f) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*f);
        if (desc && !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            return *f;
    }
    return AV_PIX_FMT_NONE;
}

} // namespace

HwDecoder::HwDecoder() = default;

HwDecoder::~HwDecoder()
{
    close();
}

bool HwDecoder::open(const std::string &utf8Path, std::shared_ptr<GpuDevice> gpu, std::string *error)
{
    close();
    m_gpu = std::move(gpu);

    int ret = avformat_open_input(&m_format, utf8Path.c_str(), nullptr, nullptr);
    if (ret < 0)
        return fail(error, avError("cannot open " + utf8Path, ret));
    ret = avformat_find_stream_info(m_format, nullptr);
    if (ret < 0)
        return fail(error, avError("cannot read stream info", ret));

    const AVCodec *codec = nullptr;
    m_stream = av_find_best_stream(m_format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (m_stream < 0 || !codec)
        return fail(error, "no decodable video stream");

    // The default decoder may be a software-only one (libdav1d for AV1); prefer one with D3D11VA.
    const AVCodec *hwCodec = nullptr;
    void *it = nullptr;
    while (const AVCodec *candidate = av_codec_iterate(&it)) {
        if (candidate->id != codec->id || !av_codec_is_decoder(candidate))
            continue;
        for (int i = 0; const AVCodecHWConfig *config = avcodec_get_hw_config(candidate, i); ++i) {
            if (config->device_type == AV_HWDEVICE_TYPE_D3D11VA
                && (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                hwCodec = candidate;
        }
        if (hwCodec)
            break;
    }
    if (hwCodec)
        codec = hwCodec;

    const AVStream *stream = m_format->streams[m_stream];
    m_codec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codec, stream->codecpar);
    m_codec->pkt_timebase = stream->time_base;
    m_codec->get_format = pickFormat;
    if (hwCodec) {
        m_codec->hw_device_ctx = av_buffer_ref(m_gpu->ffmpegDevice());
        m_codec->thread_count = 1;
    } else {
        m_codec->thread_count = 0; // software decode: use all cores
    }
    ret = avcodec_open2(m_codec, codec, nullptr);
    if (ret < 0)
        return fail(error, avError("cannot open decoder", ret));

    m_hardware = hwCodec != nullptr;
    m_packet = av_packet_alloc();
    return true;
}

void HwDecoder::close()
{
    av_packet_free(&m_packet);
    avcodec_free_context(&m_codec);
    avformat_close_input(&m_format);
    sws_freeContext(m_sws);
    m_sws = nullptr;
    av_frame_free(&m_nv12);
    m_uploadPool.reset();
    m_staging.Reset();
    m_stream = -1;
    m_draining = false;
}

AVFrame *HwDecoder::decodeNext()
{
    if (!m_codec)
        return nullptr;
    AVFrame *frame = av_frame_alloc();
    for (;;) {
        int ret = avcodec_receive_frame(m_codec, frame);
        if (ret == 0) {
            if (frame->format == AV_PIX_FMT_D3D11)
                return frame;
            // The GPU could not decode this stream: hand the CPU frame to the GPU.
            m_hardware = false;
            AVFrame *uploaded = uploadSoftwareFrame(frame);
            av_frame_free(&frame);
            return uploaded;
        }
        if (ret == AVERROR_EOF)
            break;
        if (ret != AVERROR(EAGAIN)) {
            fail(&m_error, avError("decode", ret));
            break;
        }
        if (m_draining)
            break;
        ret = av_read_frame(m_format, m_packet);
        if (ret < 0) {
            avcodec_send_packet(m_codec, nullptr);
            m_draining = true;
            continue;
        }
        if (m_packet->stream_index == m_stream) {
            ret = avcodec_send_packet(m_codec, m_packet);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_INVALIDDATA) {
                av_packet_unref(m_packet);
                fail(&m_error, avError("send packet", ret));
                break;
            }
        }
        av_packet_unref(m_packet);
    }
    av_frame_free(&frame);
    return nullptr;
}

AVFrame *HwDecoder::uploadSoftwareFrame(AVFrame *frame)
{
    const int w = frame->width, h = frame->height;
    if (!m_uploadPool || m_uploadPool->width() != w || m_uploadPool->height() != h) {
        m_uploadPool = SurfacePool::create(m_gpu->device(), DXGI_FORMAT_NV12, w, h, D3D11_BIND_SHADER_RESOURCE);
        D3D11_TEXTURE2D_DESC sd{};
        sd.Width = UINT((w + 1) & ~1);
        sd.Height = UINT((h + 1) & ~1);
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        sd.Format = DXGI_FORMAT_NV12;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        m_staging.Reset();
        if (FAILED(m_gpu->device()->CreateTexture2D(&sd, nullptr, &m_staging))) {
            fail(&m_error, "cannot create upload texture");
            return nullptr;
        }
        av_frame_free(&m_nv12);
    }

    const AVFrame *nv12 = frame;
    if (frame->format != AV_PIX_FMT_NV12) {
        if (!m_nv12) {
            m_nv12 = av_frame_alloc();
            m_nv12->format = AV_PIX_FMT_NV12;
            m_nv12->width = w;
            m_nv12->height = h;
            av_frame_get_buffer(m_nv12, 0);
        }
        m_sws = sws_getCachedContext(m_sws, w, h, AVPixelFormat(frame->format), w, h, AV_PIX_FMT_NV12,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws) {
            fail(&m_error, "no conversion to NV12");
            return nullptr;
        }
        sws_scale(m_sws, frame->data, frame->linesize, 0, h, m_nv12->data, m_nv12->linesize);
        nv12 = m_nv12;
    }

    ID3D11DeviceContext *ctx = m_gpu->context();
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx->Map(m_staging.Get(), 0, D3D11_MAP_WRITE, 0, &mapped))) {
        fail(&m_error, "cannot map upload texture");
        return nullptr;
    }
    D3D11_TEXTURE2D_DESC sd;
    m_staging->GetDesc(&sd);
    auto *dst = static_cast<uint8_t *>(mapped.pData);
    for (int y = 0; y < h; ++y)
        std::memcpy(dst + size_t(y) * mapped.RowPitch, nv12->data[0] + size_t(y) * nv12->linesize[0], size_t(w));
    uint8_t *uv = dst + size_t(mapped.RowPitch) * sd.Height;
    for (int y = 0; y < (h + 1) / 2; ++y)
        std::memcpy(uv + size_t(y) * mapped.RowPitch, nv12->data[1] + size_t(y) * nv12->linesize[1],
                    size_t((w + 1) & ~1));
    ctx->Unmap(m_staging.Get(), 0);

    AVFrame *out = m_uploadPool->acquire(&m_error);
    if (!out)
        return nullptr;
    ctx->CopyResource(reinterpret_cast<ID3D11Texture2D *>(out->data[0]), m_staging.Get());
    out->pts = frame->pts;
    out->best_effort_timestamp = frame->best_effort_timestamp;
    out->colorspace = frame->colorspace;
    out->color_range = frame->color_range;
    out->color_primaries = frame->color_primaries;
    out->color_trc = frame->color_trc;
    out->flags = frame->flags;
    return out;
}

bool HwDecoder::seekToStart()
{
    if (!m_codec)
        return false;
    const AVStream *stream = m_format->streams[m_stream];
    const int64_t start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    const int ret = av_seek_frame(m_format, m_stream, start, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
        return fail(&m_error, avError("seek", ret));
    avcodec_flush_buffers(m_codec);
    m_draining = false;
    return true;
}

bool HwDecoder::seekTo(double seconds)
{
    if (!m_codec)
        return false;
    const AVStream *stream = m_format->streams[m_stream];
    int64_t ts = int64_t(seconds / av_q2d(stream->time_base));
    if (stream->start_time != AV_NOPTS_VALUE)
        ts += stream->start_time;
    const int ret = av_seek_frame(m_format, m_stream, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
        return fail(&m_error, avError("seek", ret));
    avcodec_flush_buffers(m_codec);
    m_draining = false;
    return true;
}

void HwDecoder::setKeyframesOnly(bool on)
{
    if (m_codec)
        m_codec->skip_frame = on ? AVDISCARD_NONKEY : AVDISCARD_DEFAULT;
}

int HwDecoder::width() const
{
    return m_codec ? m_codec->width : 0;
}

int HwDecoder::height() const
{
    return m_codec ? m_codec->height : 0;
}

AVRational HwDecoder::frameRate() const
{
    if (!m_format)
        return {0, 1};
    return av_guess_frame_rate(m_format, m_format->streams[m_stream], nullptr);
}

double HwDecoder::durationSeconds() const
{
    if (!m_format)
        return 0;
    const AVStream *stream = m_format->streams[m_stream];
    if (stream->duration != AV_NOPTS_VALUE)
        return stream->duration * av_q2d(stream->time_base);
    return m_format->duration != AV_NOPTS_VALUE ? m_format->duration / double(AV_TIME_BASE) : 0;
}

double HwDecoder::frameSeconds(const AVFrame *frame) const
{
    int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
    if (pts == AV_NOPTS_VALUE || !m_format)
        return 0;
    const AVStream *stream = m_format->streams[m_stream];
    if (stream->start_time != AV_NOPTS_VALUE)
        pts -= stream->start_time;
    return pts * av_q2d(stream->time_base);
}

std::string HwDecoder::codecName() const
{
    return m_codec && m_codec->codec ? m_codec->codec->name : "";
}

} // namespace diva
