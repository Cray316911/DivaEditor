// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/HwEncoder.h"

#include "gpu/SurfacePool.h"
#include "util/Errors.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

namespace diva {

namespace {

std::string cpuEncoderFor(const std::string &codec)
{
    if (codec == "h264")
        return "libx264";
    if (codec == "hevc")
        return "libx265";
    return "libsvtav1";
}

void setBackendOptions(const std::string &backend, const std::string &name, const EncoderSettings &s,
                       AVCodecContext *ctx, AVDictionary **opts)
{
    const bool cqp = s.rateControl == "cqp";
    const bool cbr = s.rateControl == "cbr";
    const int level = s.preset == "speed" ? 0 : s.preset == "quality" ? 2 : 1;
    static const char *const amfQuality[] = {"speed", "balanced", "quality"};
    static const char *const nvencPreset[] = {"p2", "p4", "p6"};
    static const char *const qsvPreset[] = {"veryfast", "medium", "slower"};
    static const char *const x26xPreset[] = {"superfast", "veryfast", "medium"};
    static const char *const svtPreset[] = {"12", "10", "8"};
    if (backend == "amf") {
        av_dict_set(opts, "usage", s.realtime ? "lowlatency" : "transcoding", 0);
        av_dict_set(opts, "quality", amfQuality[level], 0);
        av_dict_set(opts, "rc", cqp ? "cqp" : cbr ? "cbr" : "vbr_peak", 0);
        if (cqp) {
            av_dict_set_int(opts, "qp_i", s.qp, 0);
            av_dict_set_int(opts, "qp_p", s.qp, 0);
        }
    } else if (backend == "nvenc") {
        av_dict_set(opts, "preset", nvencPreset[level], 0);
        av_dict_set(opts, "tune", s.realtime ? "ll" : "hq", 0);
        av_dict_set(opts, "rc", cqp ? "constqp" : cbr ? "cbr" : "vbr", 0);
        if (cqp)
            av_dict_set_int(opts, "qp", s.qp, 0);
    } else if (backend == "qsv") {
        av_dict_set(opts, "preset", qsvPreset[level], 0);
        if (cqp) {
            ctx->flags |= AV_CODEC_FLAG_QSCALE;
            ctx->global_quality = s.qp * FF_QP2LAMBDA;
        }
    } else if (name == "libsvtav1") {
        av_dict_set(opts, "preset", svtPreset[level], 0);
        if (cqp)
            av_dict_set_int(opts, "crf", s.qp + 8, 0);
    } else { // libx264 / libx265
        av_dict_set(opts, "preset", x26xPreset[level], 0);
        if (s.realtime)
            av_dict_set(opts, "tune", "zerolatency", 0);
        if (cqp)
            av_dict_set_int(opts, "crf", s.qp, 0);
    }
}

} // namespace

ID3D11Texture2D *frameTexture(const AVFrame *frame)
{
    return reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
}

UINT frameSlice(const AVFrame *frame)
{
    return static_cast<UINT>(reinterpret_cast<intptr_t>(frame->data[1]));
}

HwEncoder::HwEncoder() = default;

HwEncoder::~HwEncoder()
{
    av_frame_free(&m_cpuFrame);
    av_packet_free(&m_packet);
    avcodec_free_context(&m_ctx);
}

std::string HwEncoder::backendFor(GpuVendor vendor)
{
    switch (vendor) {
    case GpuVendor::Nvidia: return "nvenc";
    case GpuVendor::Intel: return "qsv";
    case GpuVendor::Amd: return "amf";
    case GpuVendor::Unknown: break;
    }
    return "cpu";
}

std::vector<EncoderProbe> HwEncoder::probe(const std::shared_ptr<GpuDevice> &gpu)
{
    std::vector<EncoderProbe> result;
    for (const char *backend : {"amf", "nvenc", "qsv", "cpu"}) {
        for (const char *codec : {"h264", "hevc", "av1"}) {
            EncoderSettings s;
            s.codec = codec;
            s.backend = backend;
            HwEncoder encoder;
            std::string error;
            EncoderProbe p;
            p.codec = codec;
            p.hardware = std::string(backend) != "cpu";
            p.available = encoder.open(s, gpu, &error);
            p.error = error;
            p.name = p.available ? encoder.name()
                                 : (p.hardware ? std::string(codec) + "_" + backend : cpuEncoderFor(codec));
            result.push_back(p);
        }
    }
    return result;
}

bool HwEncoder::open(const EncoderSettings &s, std::shared_ptr<GpuDevice> gpu, std::string *error)
{
    m_gpu = std::move(gpu);
    if (s.backend != "auto")
        return openBackend(s, s.backend, error);
    const std::string preferred = backendFor(m_gpu->vendor());
    std::string hwError;
    if (preferred != "cpu" && openBackend(s, preferred, &hwError))
        return true;
    // No usable hardware encoder (old GPU, driver too old, codec unsupported): use the CPU.
    if (openBackend(s, "cpu", error))
        return true;
    if (error && !hwError.empty())
        *error = hwError + "; " + *error;
    return false;
}

bool HwEncoder::openBackend(const EncoderSettings &s, const std::string &backend, std::string *error)
{
    av_frame_free(&m_cpuFrame);
    av_packet_free(&m_packet);
    avcodec_free_context(&m_ctx);
    m_pool.reset();
    m_staging.Reset();

    m_name = backend == "cpu" ? cpuEncoderFor(s.codec) : s.codec + "_" + backend;
    m_hardware = backend != "cpu";
    m_systemMemory = backend == "qsv" || backend == "cpu";
    const AVCodec *codec = avcodec_find_encoder_by_name(m_name.c_str());
    if (!codec)
        return fail(error, "FFmpeg has no encoder named " + m_name);

    m_ctx = avcodec_alloc_context3(codec);
    m_ctx->width = s.width;
    m_ctx->height = s.height;
    m_ctx->time_base = av_inv_q(s.fps);
    m_ctx->framerate = s.fps;
    m_ctx->gop_size = s.gopFrames > 0 ? s.gopFrames : std::max(1, int(std::lround(av_q2d(s.fps))));
    m_ctx->max_b_frames = 0;
    m_ctx->color_primaries = AVCOL_PRI_BT709;
    m_ctx->color_trc = AVCOL_TRC_BT709;
    m_ctx->colorspace = AVCOL_SPC_BT709;
    m_ctx->color_range = AVCOL_RANGE_MPEG;
    if (s.rateControl != "cqp") {
        m_ctx->bit_rate = s.bitrate;
        m_ctx->rc_max_rate = s.rateControl == "cbr" ? s.bitrate
                             : s.maxBitrate > 0     ? s.maxBitrate
                                                    : s.bitrate * 3 / 2;
        m_ctx->rc_buffer_size = int(std::min<int64_t>(m_ctx->rc_max_rate * 2, INT32_MAX));
    }
    if (s.globalHeader)
        m_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (m_systemMemory) {
        // x265 and SVT-AV1 want planar 4:2:0; QSV and x264 take NV12 as is.
        const AVPixelFormat cpuFormat =
            (m_name == "libx265" || m_name == "libsvtav1") ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;
        m_ctx->pix_fmt = cpuFormat;
        m_ctx->thread_count = 0;
        m_pool = SurfacePool::create(m_gpu->device(), DXGI_FORMAT_NV12, s.width, s.height, D3D11_BIND_RENDER_TARGET);
        D3D11_TEXTURE2D_DESC sd{};
        sd.Width = UINT((s.width + 1) & ~1);
        sd.Height = UINT((s.height + 1) & ~1);
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        sd.Format = DXGI_FORMAT_NV12;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        HRESULT hr = m_gpu->device()->CreateTexture2D(&sd, nullptr, &m_staging);
        if (FAILED(hr))
            return fail(error, hrError("create readback texture", hr));
        m_cpuFrame = av_frame_alloc();
        m_cpuFrame->format = cpuFormat;
        m_cpuFrame->width = s.width;
        m_cpuFrame->height = s.height;
        if (av_frame_get_buffer(m_cpuFrame, 0) < 0)
            return fail(error, "cannot allocate encoder frame");
    } else {
        AVBufferRef *frames = av_hwframe_ctx_alloc(m_gpu->ffmpegDevice());
        if (!frames)
            return fail(error, "av_hwframe_ctx_alloc failed");
        auto *fc = reinterpret_cast<AVHWFramesContext *>(frames->data);
        fc->format = AV_PIX_FMT_D3D11;
        fc->sw_format = AV_PIX_FMT_NV12;
        fc->width = s.width;
        fc->height = s.height;
        // The video processor writes the converted frame straight into these surfaces. Drivers
        // reject NV12 render-target *arrays*, so let the pool grow one texture at a time instead.
        fc->initial_pool_size = 0;
        static_cast<AVD3D11VAFramesContext *>(fc->hwctx)->BindFlags = D3D11_BIND_RENDER_TARGET;
        const int ret = av_hwframe_ctx_init(frames);
        if (ret < 0) {
            av_buffer_unref(&frames);
            return fail(error, avError("encoder surface pool", ret));
        }
        m_ctx->pix_fmt = AV_PIX_FMT_D3D11;
        m_ctx->sw_pix_fmt = AV_PIX_FMT_NV12;
        m_ctx->hw_frames_ctx = frames;
    }

    AVDictionary *opts = nullptr;
    setBackendOptions(backend, m_name, s, m_ctx, &opts);
    const int ret = avcodec_open2(m_ctx, codec, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        avcodec_free_context(&m_ctx);
        return fail(error, avError("cannot open " + m_name, ret));
    }
    m_packet = av_packet_alloc();
    return true;
}

AVFrame *HwEncoder::acquireFrame(std::string *error)
{
    if (m_systemMemory)
        return m_pool->acquire(error);
    AVFrame *frame = av_frame_alloc();
    const int ret = av_hwframe_get_buffer(m_ctx->hw_frames_ctx, frame, 0);
    if (ret < 0) {
        av_frame_free(&frame);
        fail(error, avError("no free encoder surface", ret));
        return nullptr;
    }
    return frame;
}

bool HwEncoder::readBack(const AVFrame *surface, std::string *error)
{
    ID3D11DeviceContext *ctx = m_gpu->context();
    ctx->CopyResource(m_staging.Get(), frameTexture(surface));
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return fail(error, "cannot map readback texture");
    if (av_frame_make_writable(m_cpuFrame) < 0) {
        ctx->Unmap(m_staging.Get(), 0);
        return fail(error, "encoder frame not writable");
    }
    D3D11_TEXTURE2D_DESC sd;
    m_staging->GetDesc(&sd);
    const auto *src = static_cast<const uint8_t *>(mapped.pData);
    const int w = m_cpuFrame->width, h = m_cpuFrame->height;
    for (int y = 0; y < h; ++y)
        std::memcpy(m_cpuFrame->data[0] + size_t(y) * m_cpuFrame->linesize[0], src + size_t(y) * mapped.RowPitch,
                    size_t(w));
    const uint8_t *uv = src + size_t(mapped.RowPitch) * sd.Height;
    if (m_cpuFrame->format == AV_PIX_FMT_NV12) {
        for (int y = 0; y < (h + 1) / 2; ++y)
            std::memcpy(m_cpuFrame->data[1] + size_t(y) * m_cpuFrame->linesize[1], uv + size_t(y) * mapped.RowPitch,
                        size_t((w + 1) & ~1));
    } else {
        // De-interleave NV12 chroma into the U and V planes.
        for (int y = 0; y < (h + 1) / 2; ++y) {
            const uint8_t *row = uv + size_t(y) * mapped.RowPitch;
            uint8_t *u = m_cpuFrame->data[1] + size_t(y) * m_cpuFrame->linesize[1];
            uint8_t *v = m_cpuFrame->data[2] + size_t(y) * m_cpuFrame->linesize[2];
            for (int x = 0; x < (w + 1) / 2; ++x) {
                u[x] = row[2 * x];
                v[x] = row[2 * x + 1];
            }
        }
    }
    ctx->Unmap(m_staging.Get(), 0);
    m_cpuFrame->pts = surface->pts;
    m_cpuFrame->color_primaries = AVCOL_PRI_BT709;
    m_cpuFrame->color_trc = AVCOL_TRC_BT709;
    m_cpuFrame->colorspace = AVCOL_SPC_BT709;
    m_cpuFrame->color_range = AVCOL_RANGE_MPEG;
    return true;
}

bool HwEncoder::drain(const PacketSink &sink, int *received, std::string *error)
{
    for (;;) {
        const int ret = avcodec_receive_packet(m_ctx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return true;
        if (ret < 0)
            return fail(error, avError("encode", ret));
        ++*received;
        const bool ok = sink(m_packet);
        av_packet_unref(m_packet);
        if (!ok)
            return fail(error, "could not write encoded packet");
    }
}

bool HwEncoder::encode(const AVFrame *frame, const PacketSink &sink, std::string *error)
{
    const AVFrame *input = frame;
    if (frame && m_systemMemory) {
        if (!readBack(frame, error))
            return false;
        input = m_cpuFrame;
    }
    for (;;) {
        const int ret = avcodec_send_frame(m_ctx, input);
        if (ret == 0)
            break;
        if (ret != AVERROR(EAGAIN))
            return fail(error, avError("encode", ret));
        // Encoder queue is full: collect output, yielding while the GPU catches up.
        int received = 0;
        if (!drain(sink, &received, error))
            return false;
        if (received == 0)
            std::this_thread::yield();
    }
    int received = 0;
    return drain(sink, &received, error);
}

bool HwEncoder::finish(const PacketSink &sink, std::string *error)
{
    int ret = avcodec_send_frame(m_ctx, nullptr);
    if (ret < 0 && ret != AVERROR_EOF)
        return fail(error, avError("flush encoder", ret));
    for (;;) {
        ret = avcodec_receive_packet(m_ctx, m_packet);
        if (ret == AVERROR_EOF)
            return true;
        if (ret == AVERROR(EAGAIN)) {
            std::this_thread::yield();
            continue;
        }
        if (ret < 0)
            return fail(error, avError("flush encoder", ret));
        const bool ok = sink(m_packet);
        av_packet_unref(m_packet);
        if (!ok)
            return fail(error, "could not write encoded packet");
    }
}

} // namespace diva
