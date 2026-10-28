// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimelineRenderer.h"

#include "AudioReader.h"
#include "FrameSource.h"
#include "TransitionRenderer.h"
#include "compositor/Nv12Renderer.h"
#include "compositor/RgbToNv12.h"
#include "gpu/GpuDevice.h"
#include "media/HwEncoder.h"
#include "media/Muxer.h"

#include <chrono>
#include <cmath>
#include <map>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

namespace diva {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Layer
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
};

bool makeLayer(ID3D11Device *device, int w, int h, Layer *layer)
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(w);
    td.Height = UINT(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(device->CreateTexture2D(&td, nullptr, &layer->texture))
           && SUCCEEDED(device->CreateRenderTargetView(layer->texture.Get(), nullptr, &layer->rtv))
           && SUCCEEDED(device->CreateShaderResourceView(layer->texture.Get(), nullptr, &layer->srv));
}

bool isDip(const QString &type)
{
    return type == QLatin1String("fade_black") || type == QLatin1String("flash");
}

// Audio follows the picture: equal-power crossfade, or a dip for fade-through-colour looks.
double gainIn(const QString &type, double p)
{
    p = std::clamp(p, 0.0, 1.0);
    return isDip(type) ? std::max(0.0, 2 * p - 1) : std::sin(p * kPi / 2);
}

double gainOut(const QString &type, double p)
{
    p = std::clamp(p, 0.0, 1.0);
    return isDip(type) ? std::max(0.0, 1 - 2 * p) : std::cos(p * kPi / 2);
}

} // namespace

bool renderTimeline(const Project &project, const RenderSettings &rs, const RenderProgress &progress,
                    QString *error)
{
    auto failWith = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const QList<PlacedClip> placed = project.layout();
    if (placed.isEmpty())
        return failWith(QStringLiteral("Timeline boş."));

    std::string err;
    auto gpu = GpuDevice::create(&err);
    if (!gpu)
        return failWith(QString::fromStdString(err));
    ID3D11DeviceContext *ctx = gpu->context();

    const int W = (rs.width > 0 ? rs.width : project.width) & ~1;
    const int H = (rs.height > 0 ? rs.height : project.height) & ~1;
    const double fps = rs.fps > 0 ? rs.fps : (project.fps > 0 ? project.fps : 60.0);
    const AVRational fpsQ = av_d2q(fps, 100000);

    Muxer muxer;
    if (!muxer.open(rs.outputPath.toStdString(), &err))
        return failWith(QString::fromStdString(err));

    EncoderSettings es;
    es.codec = rs.codec.toStdString();
    es.backend = rs.backend.toStdString();
    es.preset = rs.preset.toStdString();
    es.width = W;
    es.height = H;
    es.fps = fpsQ;
    es.globalHeader = muxer.needsGlobalHeader();
    const double bpp = es.codec == "h264" ? 0.15 : 0.1;
    es.bitrate = rs.bitrate > 0 ? rs.bitrate : int64_t(W * double(H) * fps * bpp);
    HwEncoder encoder;
    if (!encoder.open(es, gpu, &err))
        return failWith(QString::fromStdString(err));
    const int videoStream = muxer.addStream(encoder.context());

    bool anyAudio = false;
    for (const MediaInfo &m : project.media())
        anyAudio = anyAudio || m.audioStreams > 0;
    AVCodecContext *aac = nullptr;
    int audioStream = -1;
    if (anyAudio) {
        const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        aac = avcodec_alloc_context3(codec);
        aac->sample_fmt = AV_SAMPLE_FMT_FLTP;
        aac->sample_rate = AudioReader::kRate;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&aac->ch_layout, &stereo);
        aac->bit_rate = 192000;
        aac->time_base = {1, AudioReader::kRate};
        if (muxer.needsGlobalHeader())
            aac->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(aac, codec, nullptr) < 0) {
            avcodec_free_context(&aac);
            return failWith(QStringLiteral("AAC encoder açılamadı."));
        }
        audioStream = muxer.addStream(aac);
    }
    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext *)> aacGuard(aac, [](AVCodecContext *c) {
        avcodec_free_context(&c);
    });
    if (videoStream < 0 || !muxer.start(&err))
        return failWith(QString::fromStdString(err));

    Layer canvas, layerA, layerB;
    if (!makeLayer(gpu->device(), W, H, &canvas) || !makeLayer(gpu->device(), W, H, &layerA)
        || !makeLayer(gpu->device(), W, H, &layerB))
        return failWith(QStringLiteral("GPU dokuları oluşturulamadı."));
    Nv12Renderer nv12;
    TransitionRenderer transitions;
    RgbToNv12 converter;
    if (!nv12.init(gpu->device(), &err) || !transitions.init(gpu->device(), &err)
        || !converter.init(*gpu, W, H, W, H, &err))
        return failWith(QString::fromStdString(err));

    std::map<int, std::unique_ptr<FrameSource>> videoSources;
    std::map<int, std::unique_ptr<AudioReader>> audioSources;
    QString sourceError;

    auto videoFor = [&](int k) -> FrameSource * {
        auto &slot = videoSources[k];
        if (!slot) {
            slot = std::make_unique<FrameSource>();
            const MediaInfo *m = project.mediaById(placed[k].clip.mediaId);
            if (!m || !slot->open(m->path, gpu, &sourceError))
                return nullptr;
        }
        return slot.get();
    };
    auto drawClip = [&](int k, double t, ID3D11RenderTargetView *target) {
        const float black[4] = {0, 0, 0, 1};
        FrameSource *src = videoFor(k);
        const AVFrame *frame = src ? src->frameAt(placed[k].clip.in + (t - placed[k].start)) : nullptr;
        if (!frame) {
            ctx->ClearRenderTargetView(target, black);
            return true;
        }
        const RectF dst = Nv12Renderer::fit(frame->width, frame->height, W, H);
        return nv12.draw(ctx, frame, target, W, H, dst, true, &err);
    };

    const PacketSink videoSink = [&](AVPacket *p) { return muxer.write(p, videoStream, &err); };
    const PacketSink audioSink = [&](AVPacket *p) { return muxer.write(p, audioStream, &err); };
    AVPacket *audioPacket = av_packet_alloc();
    AVFrame *audioFrame = av_frame_alloc();
    const int frameSize = aac ? (aac->frame_size > 0 ? aac->frame_size : 1024) : 1024;
    if (aac) {
        audioFrame->nb_samples = frameSize;
        audioFrame->format = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_copy(&audioFrame->ch_layout, &aac->ch_layout);
        audioFrame->sample_rate = AudioReader::kRate;
        av_frame_get_buffer(audioFrame, 0);
    }
    std::vector<float> mixL(frameSize), mixR(frameSize), tmpL(frameSize), tmpR(frameSize);
    int64_t samplesDone = 0;

    auto encodeAudio = [&](const AVFrame *frame) {
        if (avcodec_send_frame(aac, frame) < 0)
            return false;
        while (avcodec_receive_packet(aac, audioPacket) == 0) {
            const bool ok = audioSink(audioPacket);
            av_packet_unref(audioPacket);
            if (!ok)
                return false;
        }
        return true;
    };
    auto mixAudioBlock = [&](int64_t first, int n) {
        std::fill(mixL.begin(), mixL.end(), 0.0f);
        std::fill(mixR.begin(), mixR.end(), 0.0f);
        for (int k = 0; k < placed.size(); ++k) {
            const PlacedClip &pc = placed[k];
            const int64_t s0 = std::max<int64_t>(first, int64_t(std::ceil(pc.start * AudioReader::kRate)));
            const int64_t s1 = std::min<int64_t>(first + n, int64_t(std::floor(pc.end * AudioReader::kRate)));
            if (s1 <= s0)
                continue;
            auto &reader = audioSources[k];
            if (!reader) {
                reader = std::make_unique<AudioReader>();
                const MediaInfo *m = project.mediaById(pc.clip.mediaId);
                if (m && m->audioStreams > 0 && reader->open(m->path, nullptr))
                    reader->seek(pc.clip.in + (double(s0) / AudioReader::kRate - pc.start));
            }
            reader->read(tmpL.data(), tmpR.data(), int(s1 - s0));
            const Transition &in = pc.clip.transitionIn;
            const bool hasIn = k > 0 && !in.type.isEmpty() && in.duration > 0;
            const Transition *next = k + 1 < placed.size() ? &placed[k + 1].clip.transitionIn : nullptr;
            const bool hasOut = next && !next->type.isEmpty() && next->duration > 0;
            for (int64_t s = s0; s < s1; ++s) {
                const double t = double(s) / AudioReader::kRate;
                double g = 1;
                if (hasIn && t < pc.start + in.duration)
                    g *= gainIn(in.type, (t - pc.start) / in.duration);
                if (hasOut && t >= placed[k + 1].start)
                    g *= gainOut(next->type, (t - placed[k + 1].start) / next->duration);
                mixL[size_t(s - first)] += float(tmpL[size_t(s - s0)] * g);
                mixR[size_t(s - first)] += float(tmpR[size_t(s - s0)] * g);
            }
        }
    };

    const double total = placed.last().end;
    const int64_t frames = int64_t(std::ceil(total * fps - 1e-6));
    const auto started = std::chrono::steady_clock::now();
    bool ok = true;
    for (int64_t i = 0; ok && i < frames; ++i) {
        const double t = i / fps;
        int a = -1, b = -1;
        for (int k = 0; k < placed.size(); ++k) {
            if (t >= placed[k].start && t < placed[k].end) {
                if (a < 0)
                    a = k;
                else
                    b = k;
            }
        }
        if (b >= 0) {
            const Transition &tr = placed[b].clip.transitionIn;
            const float p = float((t - placed[b].start) / std::max(tr.duration, 1e-6));
            ok = drawClip(a, t, layerA.rtv.Get()) && drawClip(b, t, layerB.rtv.Get());
            transitions.draw(ctx, layerA.srv.Get(), layerB.srv.Get(), canvas.rtv.Get(), W, H,
                             std::max(0, transitionIndex(tr.type)), p);
        } else if (a >= 0) {
            ok = drawClip(a, t, canvas.rtv.Get());
        } else {
            const float black[4] = {0, 0, 0, 1};
            ctx->ClearRenderTargetView(canvas.rtv.Get(), black);
        }
        AVFrame *surface = ok ? encoder.acquireFrame(&err) : nullptr;
        ok = surface && converter.convert(canvas.texture.Get(), frameTexture(surface), frameSlice(surface), &err);
        if (ok) {
            surface->pts = i;
            ok = encoder.encode(surface, videoSink, &err);
        }
        av_frame_free(&surface);

        if (ok && aac) {
            const int64_t target = int64_t(std::llround((i + 1) * double(AudioReader::kRate) / fps));
            while (ok && samplesDone + frameSize <= target) {
                mixAudioBlock(samplesDone, frameSize);
                av_frame_make_writable(audioFrame);
                std::copy(mixL.begin(), mixL.end(), reinterpret_cast<float *>(audioFrame->data[0]));
                std::copy(mixR.begin(), mixR.end(), reinterpret_cast<float *>(audioFrame->data[1]));
                audioFrame->pts = samplesDone;
                ok = encodeAudio(audioFrame);
                samplesDone += frameSize;
            }
        }

        // Close decoders of clips that are over.
        for (auto it = videoSources.begin(); it != videoSources.end();)
            it = placed[it->first].end < t - 0.5 ? videoSources.erase(it) : std::next(it);
        for (auto it = audioSources.begin(); it != audioSources.end();)
            it = placed[it->first].end < t - 0.5 ? audioSources.erase(it) : std::next(it);

        if (progress && (i % 15 == 0 || i == frames - 1)) {
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            RenderStatus status;
            status.fraction = double(i + 1) / frames;
            status.fps = elapsed > 0 ? (i + 1) / elapsed : 0;
            status.etaSeconds = status.fps > 0 ? (frames - i - 1) / status.fps : 0;
            status.encoder = QString::fromStdString(encoder.name());
            if (!progress(status)) {
                ok = false;
                err = "İptal edildi";
            }
        }
    }
    if (ok)
        ok = encoder.finish(videoSink, &err);
    if (ok && aac) {
        ok = encodeAudio(nullptr);
    }
    av_frame_free(&audioFrame);
    av_packet_free(&audioPacket);
    const bool finished = muxer.finish(&err);
    if (!ok || !finished)
        return failWith(QString::fromStdString(err));
    return true;
}

} // namespace diva
