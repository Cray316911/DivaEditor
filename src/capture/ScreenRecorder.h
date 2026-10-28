// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace diva {

struct ScreenRegion
{
    int x = 0, y = 0, width = 0, height = 0; // monitor pixels; width 0 = whole monitor
};

struct RecorderSettings
{
    int monitor = 0;             // 0 = primary, then the other monitors in system order
    ScreenRegion region;
    int fps = 60;
    std::string codec = "hevc";  // h264 | hevc | av1
    std::string backend = "auto";
    int64_t bitrate = 0;         // 0 = picked from resolution and frame rate
    bool cursor = true;
    bool systemAudio = true;     // what the speakers play (WASAPI loopback)
    bool microphone = false;
    int gpuPriority = -7;        // -7..0: below the game, so its frames are never held up by ours
    std::string outputPath;      // .mkv survives crashes and power loss mid-recording
};

struct RecorderStats
{
    int64_t framesWritten = 0;   // constant-rate frames in the file
    int64_t framesCaptured = 0;  // distinct screen updates converted on the GPU
    int64_t lateTicks = 0;       // frame ticks that ran more than one frame late
    double maxLateMs = 0;
    double seconds = 0;          // recorded time (pauses excluded)
    int width = 0;
    int height = 0;
    bool paused = false;
    bool rateCappedByWindows = false; // GraphicsCaptureSession.MinUpdateInterval took effect
    std::string encoder;
    std::string systemAudioDevice; // empty when not recording it
    std::string microphoneDevice;
    std::string audioWarning;      // a requested audio source that could not be opened
};

struct MonitorInfo
{
    std::string name;
    int x = 0, y = 0, width = 0, height = 0;
    int refreshHz = 0;
    bool primary = false;
};

// Records a monitor (or a region of it) at a constant frame rate with almost no load on
// the game being played:
//  - Windows.Graphics.Capture (no injection into the game, anti-cheat safe)
//  - Windows caps capture at the target rate, so a 144 Hz screen costs no more than 60 Hz
//  - frames stay in VRAM; BGRA->NV12 runs on the video processor, encoding on the
//    dedicated encoder block (VCN / NVENC / QSV), not on the shader cores the game uses
//  - a precise timer writes CFR frames, repeating the last surface when the screen is static
// System sound and microphone go to separate audio tracks.
class ScreenRecorder
{
public:
    ScreenRecorder();
    ~ScreenRecorder();

    static std::vector<MonitorInfo> monitors();
    static std::vector<std::string> monitorNames();

    bool start(const RecorderSettings &settings, std::string *error);
    void pause();
    void resume();
    bool stop(std::string *error);
    bool isRecording() const { return d != nullptr; }
    RecorderStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    RecorderStats m_lastStats;
};

} // namespace diva
