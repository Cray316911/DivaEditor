// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioCapture.h"

#include <windows.h>

#include <initguid.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
// After mmdeviceapi.h, which brings in DEFINE_PROPERTYKEY.
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include <cstdio>
#include <vector>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

using Microsoft::WRL::ComPtr;

namespace diva {

namespace {

constexpr int kRate = 48000;

std::string toUtf8(const wchar_t *text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string out(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

AVSampleFormat sampleFormatOf(const WAVEFORMATEX *wfx)
{
    bool isFloat = wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(wfx);
        isFloat = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    if (isFloat && wfx->wBitsPerSample == 32)
        return AV_SAMPLE_FMT_FLT;
    if (!isFloat && wfx->wBitsPerSample == 16)
        return AV_SAMPLE_FMT_S16;
    if (!isFloat && wfx->wBitsPerSample == 32)
        return AV_SAMPLE_FMT_S32;
    return AV_SAMPLE_FMT_NONE;
}

} // namespace

AudioCapture::~AudioCapture()
{
    stop();
}

bool AudioCapture::start(bool loopback, Sink sink, std::string *error)
{
    m_sink = std::move(sink);
    m_stop = false;
    std::promise<std::string> ready;
    std::future<std::string> result = ready.get_future();
    m_thread = std::thread(&AudioCapture::run, this, loopback, std::move(ready));
    const std::string failure = result.get();
    if (!failure.empty()) {
        m_thread.join();
        if (error)
            *error = failure;
        return false;
    }
    return true;
}

void AudioCapture::stop()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
}

void AudioCapture::run(bool loopback, std::promise<std::string> ready)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> capture;
        WAVEFORMATEX *wfx = nullptr;
        SwrContext *swr = nullptr;

        auto failWith = [&](const char *what, HRESULT hr) {
            char buf[128];
            std::snprintf(buf, sizeof buf, "%s (0x%08lX)", what, static_cast<unsigned long>(hr));
            ready.set_value(buf);
        };
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(hr))
            hr = enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &device);
        if (FAILED(hr)) {
            failWith(loopback ? "no playback device" : "no microphone", hr);
            CoUninitialize();
            return;
        }
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR)
                m_deviceName = toUtf8(name.pwszVal);
            PropVariantClear(&name);
        }
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
        if (SUCCEEDED(hr))
            hr = client->GetMixFormat(&wfx);
        if (SUCCEEDED(hr))
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0,
                                    2'000'000 /* 200 ms buffer */, 0, wfx, nullptr);
        if (SUCCEEDED(hr))
            hr = client->GetService(IID_PPV_ARGS(&capture));
        const AVSampleFormat format = wfx ? sampleFormatOf(wfx) : AV_SAMPLE_FMT_NONE;
        if (SUCCEEDED(hr) && format == AV_SAMPLE_FMT_NONE)
            hr = E_NOTIMPL;
        AVChannelLayout inLayout{};
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (SUCCEEDED(hr)) {
            av_channel_layout_default(&inLayout, wfx->nChannels);
            if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLTP, kRate, &inLayout, format,
                                    int(wfx->nSamplesPerSec), 0, nullptr) < 0
                || swr_init(swr) < 0)
                hr = E_FAIL;
        }
        if (SUCCEEDED(hr))
            hr = client->Start();
        if (FAILED(hr)) {
            failWith("audio device setup failed", hr);
            swr_free(&swr);
            CoTaskMemFree(wfx);
            CoUninitialize();
            return;
        }
        ready.set_value({});

        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        const int bytesPerFrame = wfx->nBlockAlign;
        std::vector<float> left, right;
        std::vector<uint8_t> silence;
        while (!m_stop) {
            Sleep(5);
            UINT32 packet = 0;
            while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE *data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                UINT64 qpc100ns = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, &qpc100ns)))
                    break;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    silence.assign(size_t(frames) * bytesPerFrame, 0);
                    data = silence.data();
                }
                const int capacity = swr_get_out_samples(swr, int(frames)) + 64;
                left.resize(size_t(capacity));
                right.resize(size_t(capacity));
                uint8_t *out[2] = {reinterpret_cast<uint8_t *>(left.data()), reinterpret_cast<uint8_t *>(right.data())};
                const uint8_t *in[1] = {data};
                const int got = swr_convert(swr, out, capacity, in, int(frames));
                capture->ReleaseBuffer(frames);
                // WASAPI reports the first sample's QPC time in 100 ns units.
                const int64_t qpc = int64_t(double(qpc100ns) * frequency.QuadPart / 1e7);
                if (got > 0 && m_sink)
                    m_sink(left.data(), right.data(), got, qpc);
            }
        }
        client->Stop();
        swr_free(&swr);
        av_channel_layout_uninit(&inLayout);
        CoTaskMemFree(wfx);
    }
    CoUninitialize();
}

} // namespace diva
