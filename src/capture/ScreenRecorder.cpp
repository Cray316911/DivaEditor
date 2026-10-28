// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScreenRecorder.h"

#include "AudioCapture.h"
#include "compositor/RgbToNv12.h"
#include "gpu/GpuDevice.h"
#include "media/HwEncoder.h"
#include "media/Muxer.h"
#include "util/Errors.h"

#include <unknwn.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace diva {

namespace {

constexpr int kAudioRate = 48000;
// Audio packets may wander this far from the video clock before we resync (pad or drop).
constexpr int64_t kAudioSlack = kAudioRate / 50; // 20 ms

std::vector<HMONITOR> monitorsPrimaryFirst()
{
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HMONITOR> *>(data)->push_back(monitor);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    std::stable_partition(monitors.begin(), monitors.end(), [](HMONITOR monitor) {
        MONITORINFO info{sizeof info};
        return GetMonitorInfoW(monitor, &info) && (info.dwFlags & MONITORINFOF_PRIMARY);
    });
    return monitors;
}

int64_t qpc()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

} // namespace

// One audio source (system sound or microphone) encoded to AAC on its own track.
struct AudioTrack
{
    AudioCapture capture;
    AVCodecContext *encoder = nullptr;
    AVAudioFifo *fifo = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    int stream = -1;
    int64_t written = 0; // samples placed on the recording timeline
    int64_t encoded = 0;
    std::mutex mutex;

    ~AudioTrack()
    {
        capture.stop();
        avcodec_free_context(&encoder);
        if (fifo)
            av_audio_fifo_free(fifo);
        av_frame_free(&frame);
        av_packet_free(&packet);
    }

    bool open(bool globalHeader, std::string *error)
    {
        const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        encoder = avcodec_alloc_context3(codec);
        encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
        encoder->sample_rate = kAudioRate;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&encoder->ch_layout, &stereo);
        encoder->bit_rate = 192000;
        encoder->time_base = {1, kAudioRate};
        if (globalHeader)
            encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(encoder, codec, nullptr) < 0)
            return fail(error, "cannot open AAC encoder");
        fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, kAudioRate);
        frame = av_frame_alloc();
        frame->nb_samples = encoder->frame_size > 0 ? encoder->frame_size : 1024;
        frame->format = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_copy(&frame->ch_layout, &encoder->ch_layout);
        frame->sample_rate = kAudioRate;
        av_frame_get_buffer(frame, 0);
        packet = av_packet_alloc();
        return true;
    }

    void writeSilence(int64_t samples)
    {
        std::vector<float> zeros(size_t(std::min<int64_t>(samples, kAudioRate)), 0.0f);
        while (samples > 0) {
            const int n = int(std::min<int64_t>(samples, int64_t(zeros.size())));
            void *planes[2] = {zeros.data(), zeros.data()};
            av_audio_fifo_write(fifo, planes, n);
            samples -= n;
            written += n;
        }
    }

    // Places `frames` samples whose first sample belongs at `position` on the recording timeline.
    void push(const float *left, const float *right, int frames, int64_t position, Muxer &muxer)
    {
        std::lock_guard lock(mutex);
        int skip = 0;
        if (position > written + kAudioSlack) {
            writeSilence(position - written); // the device was silent (loopback sends nothing)
        } else if (position + frames <= written - kAudioSlack) {
            return; // entirely before what we already have
        } else if (position < written - kAudioSlack) {
            skip = int(written - position);
        }
        void *planes[2] = {const_cast<float *>(left + skip), const_cast<float *>(right + skip)};
        av_audio_fifo_write(fifo, planes, frames - skip);
        written += frames - skip;
        drain(muxer, false);
    }

    void drain(Muxer &muxer, bool flush)
    {
        const int size = frame->nb_samples;
        while (av_audio_fifo_size(fifo) >= size || (flush && av_audio_fifo_size(fifo) > 0)) {
            av_frame_make_writable(frame);
            const int got = av_audio_fifo_read(fifo, reinterpret_cast<void **>(frame->data), size);
            for (int ch = 0; ch < 2; ++ch)
                std::fill(reinterpret_cast<float *>(frame->data[ch]) + got,
                          reinterpret_cast<float *>(frame->data[ch]) + size, 0.0f);
            frame->pts = encoded;
            encoded += size;
            send(frame, muxer);
        }
        if (flush)
            send(nullptr, muxer);
    }

    void send(const AVFrame *f, Muxer &muxer)
    {
        if (avcodec_send_frame(encoder, f) < 0)
            return;
        while (avcodec_receive_packet(encoder, packet) == 0) {
            muxer.write(packet, stream);
            av_packet_unref(packet);
        }
    }
};

struct ScreenRecorder::Impl
{
    RecorderSettings settings;
    std::shared_ptr<GpuDevice> gpu;
    HwEncoder encoder;
    Muxer muxer;
    RgbToNv12 converter;
    int stream = -1;
    int captureWidth = 0;  // monitor size
    int captureHeight = 0;
    int width = 0;         // encoded size (even)
    int height = 0;
    ComPtr<ID3D11Texture2D> canvas;
    bool directInput = true; // capture thread only
    bool rateCapped = false;

    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};

    std::unique_ptr<AudioTrack> systemAudio;
    std::unique_ptr<AudioTrack> microphone;
    std::string audioWarning;

    std::mutex frameMutex;
    AVFrame *latest = nullptr;
    std::atomic<bool> accepting{false};
    std::atomic<int> inCallback{0};

    std::thread encodeThread;
    std::atomic<bool> stopRequested{false};
    std::string threadError;
    std::atomic<int64_t> captured{0};
    std::atomic<int64_t> written{0};
    std::atomic<int64_t> lateTicks{0};
    std::atomic<double> maxLateMs{0};
    std::atomic<int64_t> startQpc{0};
    std::atomic<int64_t> endQpc{0};
    std::atomic<bool> paused{false};
    std::atomic<int64_t> pausedTotal{0}; // QPC ticks spent paused
    int64_t pauseStarted = 0;
    int64_t qpcFrequency = 1;

    ~Impl() { av_frame_free(&latest); }

    // Position on the recording timeline (pauses removed), in seconds.
    double mediaSeconds(int64_t at) const
    {
        const int64_t start = startQpc;
        return start ? double(at - start - pausedTotal) / qpcFrequency : -1;
    }

    void onAudio(AudioTrack *track, const float *left, const float *right, int frames, int64_t at)
    {
        if (!startQpc || paused || stopRequested)
            return;
        const double t = mediaSeconds(at);
        track->push(left, right, frames, int64_t(std::llround(t * kAudioRate)), muxer);
    }

    // Capture thread: convert the new screen image into an encoder surface right away.
    void onFrame(const wgc::Direct3D11CaptureFramePool &sender)
    {
        ++inCallback;
        auto frame = sender.TryGetNextFrame();
        if (frame && accepting && !paused) {
            ComPtr<ID3D11Texture2D> texture;
            auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            D3D11_TEXTURE2D_DESC td{};
            if (SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&texture))))
                texture->GetDesc(&td);
            // A resolution change mid-recording is not handled yet: such frames are skipped.
            AVFrame *surface = nullptr;
            if (texture && int(td.Width) == captureWidth && int(td.Height) == captureHeight)
                surface = encoder.acquireFrame(nullptr);
            if (surface) {
                // Convert straight from the capture surface; copy first only if the driver refuses.
                ID3D11Texture2D *source = texture.Get();
                if (!directInput) {
                    gpu->context()->CopyResource(canvas.Get(), texture.Get());
                    source = canvas.Get();
                }
                bool converted = converter.convert(source, frameTexture(surface), frameSlice(surface));
                if (!converted && directInput) {
                    directInput = false;
                    gpu->context()->CopyResource(canvas.Get(), texture.Get());
                    converted = converter.convert(canvas.Get(), frameTexture(surface), frameSlice(surface));
                }
                if (converted) {
                    std::lock_guard lock(frameMutex);
                    std::swap(latest, surface);
                    ++captured;
                }
                av_frame_free(&surface);
            }
        }
        if (frame)
            frame.Close();
        --inCallback;
    }

    // Encoder thread: one frame per tick on a high-resolution timer (constant frame rate).
    void encodeLoop()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                              TIMER_ALL_ACCESS);
        if (!timer)
            timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);

        // Start the clock on the first captured image so the file opens with real content.
        for (;;) {
            if (stopRequested) {
                CloseHandle(timer);
                return;
            }
            {
                std::lock_guard lock(frameMutex);
                if (latest)
                    break;
            }
            Sleep(1);
        }

        const PacketSink sink = [this](AVPacket *packet) { return muxer.write(packet, stream, &threadError); };
        const double periodTicks = double(qpcFrequency) / settings.fps;
        startQpc = qpc();
        for (int64_t i = 0; !stopRequested;) {
            if (paused) {
                Sleep(5);
                continue;
            }
            const int64_t target = startQpc + pausedTotal + int64_t(i * periodTicks);
            int64_t now = qpc();
            if (now < target) {
                LARGE_INTEGER due;
                due.QuadPart = -int64_t((target - now) * 10'000'000.0 / qpcFrequency);
                SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0);
                WaitForSingleObject(timer, INFINITE);
                now = qpc();
            }
            if (paused)
                continue;
            const double lateMs = (now - target) * 1000.0 / qpcFrequency;
            if (lateMs > 1000.0 / settings.fps)
                ++lateTicks;
            if (lateMs > maxLateMs)
                maxLateMs = lateMs;

            AVFrame *frame;
            {
                std::lock_guard lock(frameMutex);
                frame = av_frame_clone(latest);
            }
            frame->pts = i;
            const bool ok = encoder.encode(frame, sink, &threadError);
            av_frame_free(&frame);
            if (!ok)
                break;
            ++written;
            ++i;
        }
        endQpc = qpc();
        encoder.finish(sink, threadError.empty() ? &threadError : nullptr);
        CloseHandle(timer);
    }
};

ScreenRecorder::ScreenRecorder() = default;

ScreenRecorder::~ScreenRecorder()
{
    if (d)
        stop(nullptr);
}

std::vector<MonitorInfo> ScreenRecorder::monitors()
{
    std::vector<MonitorInfo> list;
    for (HMONITOR monitor : monitorsPrimaryFirst()) {
        MONITORINFOEXA info{};
        info.cbSize = sizeof info;
        GetMonitorInfoA(monitor, &info);
        DEVMODEA mode{};
        mode.dmSize = sizeof mode;
        EnumDisplaySettingsA(info.szDevice, ENUM_CURRENT_SETTINGS, &mode);
        MonitorInfo m;
        m.name = info.szDevice;
        m.x = info.rcMonitor.left;
        m.y = info.rcMonitor.top;
        // Physical pixels (the capture size), independent of display scaling.
        m.width = mode.dmPelsWidth ? int(mode.dmPelsWidth) : info.rcMonitor.right - info.rcMonitor.left;
        m.height = mode.dmPelsHeight ? int(mode.dmPelsHeight) : info.rcMonitor.bottom - info.rcMonitor.top;
        m.refreshHz = int(mode.dmDisplayFrequency);
        m.primary = info.dwFlags & MONITORINFOF_PRIMARY;
        list.push_back(m);
    }
    return list;
}

std::vector<std::string> ScreenRecorder::monitorNames()
{
    std::vector<std::string> names;
    for (const MonitorInfo &m : monitors())
        names.push_back(m.name + "  " + std::to_string(m.width) + "x" + std::to_string(m.height) + " @ "
                        + std::to_string(m.refreshHz) + " Hz" + (m.primary ? "  (primary)" : ""));
    return names;
}

bool ScreenRecorder::start(const RecorderSettings &settings, std::string *error)
{
    if (d)
        return fail(error, "already recording");
    if (!wgc::GraphicsCaptureSession::IsSupported())
        return fail(error, "Windows.Graphics.Capture is not supported on this system");
    const std::vector<HMONITOR> monitorHandles = monitorsPrimaryFirst();
    if (settings.monitor < 0 || settings.monitor >= int(monitorHandles.size()))
        return fail(error, "no monitor #" + std::to_string(settings.monitor));

    auto impl = std::make_unique<Impl>();
    Impl &r = *impl;
    r.settings = settings;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    r.qpcFrequency = frequency.QuadPart;

    r.gpu = GpuDevice::create(error);
    if (!r.gpu)
        return false;
    r.gpu->setGpuPriority(settings.gpuPriority);

    try {
        ComPtr<IDXGIDevice> dxgiDevice;
        r.gpu->device()->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        winrt::com_ptr<::IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put()));
        const auto device = inspectable.as<wd3d::IDirect3DDevice>();

        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForMonitor(monitorHandles[settings.monitor],
                                                       winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                       winrt::put_abi(r.item)));
        const auto size = r.item.Size();
        r.captureWidth = size.Width;
        r.captureHeight = size.Height;

        RECT source{0, 0, size.Width, size.Height};
        const ScreenRegion &region = settings.region;
        if (region.width > 0 && region.height > 0) {
            source.left = std::clamp(region.x, 0, size.Width - 2);
            source.top = std::clamp(region.y, 0, size.Height - 2);
            source.right = std::clamp(region.x + region.width, int(source.left) + 2, size.Width);
            source.bottom = std::clamp(region.y + region.height, int(source.top) + 2, size.Height);
        }
        // Encoders want even dimensions; the video processor drops the odd row/column.
        r.width = int(source.right - source.left) & ~1;
        r.height = int(source.bottom - source.top) & ~1;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(size.Width);
        td.Height = UINT(size.Height);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = r.gpu->device()->CreateTexture2D(&td, nullptr, &r.canvas);
        if (FAILED(hr))
            return fail(error, hrError("create capture texture", hr));
        if (!r.converter.init(*r.gpu, size.Width, size.Height, r.width, r.height, error, &source))
            return false;

        if (!r.muxer.open(settings.outputPath, error))
            return false;
        EncoderSettings es;
        es.codec = settings.codec;
        es.backend = settings.backend;
        es.width = r.width;
        es.height = r.height;
        es.fps = {settings.fps, 1};
        es.realtime = true;
        es.preset = "balanced";
        es.globalHeader = r.muxer.needsGlobalHeader();
        // Screen and game content needs more bits than camera footage at the same size.
        const double bpp = settings.codec == "h264" ? 0.2 : 0.14;
        es.bitrate = settings.bitrate > 0 ? settings.bitrate : int64_t(r.width * double(r.height) * settings.fps * bpp);
        if (!r.encoder.open(es, r.gpu, error))
            return false;
        r.stream = r.muxer.addStream(r.encoder.context());

        // Audio devices open now so their tracks exist before the file header is written.
        auto openTrack = [&](bool loopback) -> std::unique_ptr<AudioTrack> {
            auto track = std::make_unique<AudioTrack>();
            std::string audioError;
            if (!track->open(r.muxer.needsGlobalHeader(), &audioError)) {
                r.audioWarning += audioError + "; ";
                return nullptr;
            }
            AudioTrack *raw = track.get();
            if (!track->capture.start(loopback, [impl = &r, raw](const float *l, const float *rr, int n, int64_t at) {
                    impl->onAudio(raw, l, rr, n, at);
                }, &audioError)) {
                r.audioWarning += (loopback ? "Sistem sesi: " : "Mikrofon: ") + audioError + "; ";
                return nullptr;
            }
            track->stream = r.muxer.addStream(track->encoder);
            r.muxer.setStreamTitle(track->stream, loopback ? "Sistem sesi" : "Mikrofon");
            return track;
        };
        if (settings.systemAudio)
            r.systemAudio = openTrack(true);
        if (settings.microphone)
            r.microphone = openTrack(false);

        if (r.stream < 0 || !r.muxer.start(error))
            return false;

        r.pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        r.session = r.pool.CreateCaptureSession(r.item);
        r.session.IsCursorCaptureEnabled(settings.cursor);
        try {
            r.session.IsBorderRequired(false);
        } catch (...) {
        }
        try {
            // Windows 11 24H2+: never deliver more frames than we record (e.g. a 144 Hz screen -> 60 fps).
            r.session.MinUpdateInterval(winrt::Windows::Foundation::TimeSpan(10'000'000 / settings.fps));
            r.rateCapped = true;
        } catch (...) {
        }
        r.pool.FrameArrived([impl = &r](const wgc::Direct3D11CaptureFramePool &sender, auto &&) {
            impl->onFrame(sender);
        });
        r.accepting = true;
        r.session.StartCapture();
    } catch (const winrt::hresult_error &e) {
        return fail(error, "screen capture failed: " + winrt::to_string(e.message()));
    }

    r.encodeThread = std::thread(&Impl::encodeLoop, &r);
    d = std::move(impl);
    return true;
}

void ScreenRecorder::pause()
{
    if (!d || d->paused)
        return;
    d->pauseStarted = qpc();
    d->paused = true;
}

void ScreenRecorder::resume()
{
    if (!d || !d->paused)
        return;
    d->pausedTotal += qpc() - d->pauseStarted;
    d->paused = false;
}

bool ScreenRecorder::stop(std::string *error)
{
    if (!d)
        return true;
    Impl &r = *d;
    if (r.paused)
        resume();
    r.accepting = false;
    try {
        if (r.session)
            r.session.Close();
        if (r.pool)
            r.pool.Close();
    } catch (...) {
    }
    while (r.inCallback > 0)
        std::this_thread::yield();
    r.stopRequested = true;
    if (r.encodeThread.joinable())
        r.encodeThread.join();
    // Audio ends with the video: pad to the same length, then flush the encoders.
    const int64_t videoSamples = int64_t(std::llround(double(r.written) / r.settings.fps * kAudioRate));
    for (AudioTrack *track : {r.systemAudio.get(), r.microphone.get()}) {
        if (!track)
            continue;
        track->capture.stop();
        std::lock_guard lock(track->mutex);
        if (track->written < videoSamples)
            track->writeSilence(videoSamples - track->written);
        track->drain(r.muxer, true);
    }
    std::string muxError;
    const bool finished = r.muxer.finish(&muxError);
    const bool ok = finished && r.threadError.empty();
    if (!r.threadError.empty())
        fail(error, r.threadError);
    else if (!finished)
        fail(error, muxError);
    m_lastStats = stats();
    d.reset();
    return ok;
}

RecorderStats ScreenRecorder::stats() const
{
    if (!d)
        return m_lastStats;
    const Impl &r = *d;
    RecorderStats s;
    s.framesWritten = r.written;
    s.framesCaptured = r.captured;
    s.lateTicks = r.lateTicks;
    s.maxLateMs = r.maxLateMs;
    s.seconds = double(r.written) / r.settings.fps;
    s.width = r.width;
    s.height = r.height;
    s.paused = r.paused;
    s.rateCappedByWindows = r.rateCapped;
    s.encoder = r.encoder.name();
    s.systemAudioDevice = r.systemAudio ? r.systemAudio->capture.deviceName() : std::string();
    s.microphoneDevice = r.microphone ? r.microphone->capture.deviceName() : std::string();
    s.audioWarning = r.audioWarning;
    return s;
}

} // namespace diva
