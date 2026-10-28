// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <thread>

namespace diva {

// Captures the default speakers (WASAPI loopback: everything you hear) or the default
// microphone, converted to 48 kHz stereo float. Each packet carries the QPC time of its
// first sample so it can be placed next to the video frames.
class AudioCapture
{
public:
    using Sink = std::function<void(const float *left, const float *right, int frames, int64_t qpc)>;

    AudioCapture() = default;
    ~AudioCapture();
    AudioCapture(const AudioCapture &) = delete;
    AudioCapture &operator=(const AudioCapture &) = delete;

    // Opens the device and starts capturing; returns once the device is open (or failed).
    bool start(bool loopback, Sink sink, std::string *error);
    void stop();
    const std::string &deviceName() const { return m_deviceName; }

private:
    void run(bool loopback, std::promise<std::string> ready);

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    Sink m_sink;
    std::string m_deviceName;
};

} // namespace diva
