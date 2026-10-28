// SPDX-License-Identifier: GPL-3.0-or-later
//
// divabench: headless checks of the GPU media pipeline.
//
//   divabench probe                      which hardware encoders open on this machine
//   divabench decode <file>              hardware decode throughput
//   divabench export <in> <out> [opts]   decode -> compositor -> encode, all on the GPU
//       --codec h264|hevc|av1  --backend auto|amf|nvenc|qsv  --size WxH  --fps N
//       --bitrate 50M  --rc vbr|cbr|cqp  --qp N

#include "compositor/Nv12Renderer.h"
#include "compositor/RgbToNv12.h"
#include "gpu/GpuDevice.h"
#include "media/HwDecoder.h"
#include "media/HwEncoder.h"
#include "media/Muxer.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
}

using namespace diva;
using Clock = std::chrono::steady_clock;

namespace {

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

int64_t parseBitrate(const std::string &text)
{
    double value = std::atof(text.c_str());
    const char suffix = text.empty() ? 0 : char(std::toupper(text.back()));
    if (suffix == 'M')
        value *= 1e6;
    else if (suffix == 'K')
        value *= 1e3;
    return int64_t(value);
}

int probe(const std::shared_ptr<GpuDevice> &gpu)
{
    av_log_set_level(AV_LOG_QUIET);
    std::printf("GPUs (high-performance first):\n");
    for (const AdapterInfo &a : GpuDevice::adapters())
        std::printf("  %s  (vendor 0x%04X, %llu MB)\n", a.name.c_str(), a.vendorId,
                    (unsigned long long)a.dedicatedMemoryMB);
    std::printf("Using: %s\n\nEncoders (1920x1080 @ 60):\n", gpu->adapterName().c_str());
    for (const EncoderProbe &p : HwEncoder::probe(gpu))
        std::printf("  %-5s %-12s %-4s %s\n", p.codec.c_str(), p.name.c_str(), p.hardware ? "GPU" : "CPU",
                    p.available ? "OK" : ("not available: " + p.error).c_str());
    return 0;
}

int decode(const std::shared_ptr<GpuDevice> &gpu, const std::string &path)
{
    HwDecoder decoder;
    std::string error;
    if (!decoder.open(path, gpu, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("%s: %dx%d %s, %.3f fps\n", path.c_str(), decoder.width(), decoder.height(),
                decoder.codecName().c_str(), av_q2d(decoder.frameRate()));
    const auto start = Clock::now();
    int64_t frames = 0;
    while (AVFrame *frame = decoder.decodeNext()) {
        ++frames;
        av_frame_free(&frame);
    }
    gpu->waitIdle();
    const double elapsed = secondsSince(start);
    if (!decoder.lastError().empty())
        std::fprintf(stderr, "decode error: %s\n", decoder.lastError().c_str());
    const double fps = frames / elapsed;
    std::printf("decoded %lld frames in %.2f s = %.1f fps (%.1fx realtime)\n", (long long)frames, elapsed, fps,
                fps / av_q2d(decoder.frameRate()));
    return 0;
}

int exportFile(const std::shared_ptr<GpuDevice> &gpu, const std::string &in, const std::string &out,
               const std::map<std::string, std::string> &opts)
{
    std::string error;
    HwDecoder decoder;
    if (!decoder.open(in, gpu, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    EncoderSettings s;
    s.width = decoder.width();
    s.height = decoder.height();
    s.fps = decoder.frameRate().num > 0 ? decoder.frameRate() : AVRational{60, 1};
    auto opt = [&](const char *key) -> const std::string * {
        auto it = opts.find(key);
        return it == opts.end() ? nullptr : &it->second;
    };
    if (auto v = opt("codec")) s.codec = *v;
    if (auto v = opt("backend")) s.backend = *v;
    if (auto v = opt("rc")) s.rateControl = *v;
    if (auto v = opt("preset")) s.preset = *v;
    if (auto v = opt("qp")) s.qp = std::atoi(v->c_str());
    if (auto v = opt("fps")) s.fps = {std::atoi(v->c_str()), 1};
    if (auto v = opt("size"); v && v->find('x') != std::string::npos) {
        s.width = std::atoi(v->c_str());
        s.height = std::atoi(v->c_str() + v->find('x') + 1);
    }
    s.width &= ~1;
    s.height &= ~1;
    // Default bitrate: ~0.1 bit per pixel for HEVC/AV1, a bit more for H.264.
    const double bpp = s.codec == "h264" ? 0.15 : 0.1;
    s.bitrate = int64_t(s.width * double(s.height) * av_q2d(s.fps) * bpp);
    if (auto v = opt("bitrate")) s.bitrate = parseBitrate(*v);

    Muxer muxer;
    if (!muxer.open(out, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    s.globalHeader = muxer.needsGlobalHeader();
    HwEncoder encoder;
    if (!encoder.open(s, gpu, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const int stream = muxer.addStream(encoder.context());
    if (stream < 0 || !muxer.start(&error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    Nv12Renderer renderer;
    RgbToNv12 converter;
    if (!renderer.init(gpu->device(), &error) || !converter.init(*gpu, s.width, s.height, s.width, s.height, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(s.width);
    td.Height = UINT(s.height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> canvas;
    ComPtr<ID3D11RenderTargetView> canvasView;
    if (FAILED(gpu->device()->CreateTexture2D(&td, nullptr, &canvas))
        || FAILED(gpu->device()->CreateRenderTargetView(canvas.Get(), nullptr, &canvasView))) {
        std::fprintf(stderr, "cannot create canvas texture\n");
        return 1;
    }

    std::printf("%s -> %s\n  %dx%d @ %.2f fps, %s, %.1f Mbit/s %s\n", in.c_str(), out.c_str(), s.width, s.height,
                av_q2d(s.fps), encoder.name().c_str(), s.bitrate / 1e6, s.rateControl.c_str());

    const RectF dst = Nv12Renderer::fit(decoder.width(), decoder.height(), s.width, s.height);
    const PacketSink sink = [&](AVPacket *packet) { return muxer.write(packet, stream, &error); };
    const double outFrame = 1.0 / av_q2d(s.fps);
    const double srcFrame = decoder.frameRate().num > 0 ? 1.0 / av_q2d(decoder.frameRate()) : outFrame;

    const auto start = Clock::now();
    AVFrame *current = decoder.decodeNext();
    AVFrame *next = current ? decoder.decodeNext() : nullptr;
    int64_t written = 0;
    bool ok = current != nullptr;
    // Output frame i shows the newest source frame at or before i / fps (handles 30->60->120).
    for (int64_t i = 0; ok; ++i) {
        const double t = i * outFrame;
        while (next && decoder.frameSeconds(next) <= t + 1e-6) {
            av_frame_free(&current);
            current = next;
            next = decoder.decodeNext();
        }
        if (!next && t >= decoder.frameSeconds(current) + srcFrame - 1e-6)
            break;

        ok = renderer.draw(gpu->context(), current, canvasView.Get(), s.width, s.height, dst, true, &error);
        AVFrame *surface = ok ? encoder.acquireFrame(&error) : nullptr;
        ok = surface && converter.convert(canvas.Get(), frameTexture(surface), frameSlice(surface), &error);
        if (ok) {
            surface->pts = i;
            ok = encoder.encode(surface, sink, &error);
        }
        av_frame_free(&surface);
        ++written;
        if (written % 120 == 0)
            std::printf("\r  %lld frames, %.1f fps   ", (long long)written, written / secondsSince(start));
    }
    av_frame_free(&current);
    av_frame_free(&next);
    ok = ok && encoder.finish(sink, &error) && muxer.finish(&error);
    const double elapsed = secondsSince(start);
    if (!ok) {
        std::fprintf(stderr, "\nexport failed: %s\n", error.c_str());
        return 1;
    }
    const double fps = written / elapsed;
    const double videoSeconds = written * outFrame;
    std::printf("\r  exported %lld frames (%.2f s of video) in %.2f s = %.1f fps, %.2fx realtime\n",
                (long long)written, videoSeconds, elapsed, fps, videoSeconds / elapsed);
    const std::filesystem::path outPath(std::u8string(out.begin(), out.end()));
    std::printf("  output size: %.1f MB\n", std::filesystem::file_size(outPath) / 1e6);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::printf("usage: divabench probe | decode <file> | export <in> <out> [--codec hevc] [--backend auto]\n"
                    "                 [--size WxH] [--fps N] [--bitrate 50M] [--rc vbr|cbr|cqp] [--qp N]\n");
        return 1;
    }
    av_log_set_level(AV_LOG_ERROR);
    std::string error;
    auto gpu = GpuDevice::create(&error);
    if (!gpu) {
        std::fprintf(stderr, "GPU init failed: %s\n", error.c_str());
        return 1;
    }
    if (args[0] == "probe")
        return probe(gpu);
    if (args[0] == "decode" && args.size() >= 2)
        return decode(gpu, args[1]);
    if (args[0] == "export" && args.size() >= 3) {
        std::map<std::string, std::string> opts;
        for (size_t i = 3; i + 1 < args.size(); i += 2)
            if (args[i].rfind("--", 0) == 0)
                opts[args[i].substr(2)] = args[i + 1];
        return exportFile(gpu, args[1], args[2], opts);
    }
    std::fprintf(stderr, "unknown command\n");
    return 1;
}
