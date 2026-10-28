// SPDX-License-Identifier: GPL-3.0-or-later
//
// divarec: record a monitor from the command line.
//
//   divarec --list
//   divarec [--monitor N] [--fps 60] [--codec hevc] [--bitrate 20M] [--seconds S] [--no-cursor] out.mkv
//
// Without --seconds it records until Enter is pressed.

#include "ScreenRecorder.h"

#include <unknwn.h>
#include <winrt/base.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace diva;

// Ask hybrid-graphics laptops (NVIDIA Optimus, AMD PowerXpress) to run us on the dedicated GPU.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace {

double processCpuSeconds()
{
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    auto seconds = [](const FILETIME &t) {
        return (uint64_t(t.dwHighDateTime) << 32 | t.dwLowDateTime) / 1e7;
    };
    return seconds(kernel) + seconds(user);
}

} // namespace

int main(int argc, char **argv)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    std::vector<std::string> args(argv + 1, argv + argc);

    RecorderSettings settings;
    double seconds = 0;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string &a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == "--list") {
            int index = 0;
            for (const std::string &name : ScreenRecorder::monitorNames())
                std::printf("%d: %s\n", index++, name.c_str());
            return 0;
        } else if (a == "--monitor" && hasValue) {
            settings.monitor = std::atoi(args[++i].c_str());
        } else if (a == "--fps" && hasValue) {
            settings.fps = std::atoi(args[++i].c_str());
        } else if (a == "--codec" && hasValue) {
            settings.codec = args[++i];
        } else if (a == "--bitrate" && hasValue) {
            const std::string &v = args[++i];
            const double scale = v.back() == 'M' || v.back() == 'm' ? 1e6 : v.back() == 'K' || v.back() == 'k' ? 1e3 : 1;
            settings.bitrate = int64_t(std::atof(v.c_str()) * scale);
        } else if (a == "--seconds" && hasValue) {
            seconds = std::atof(args[++i].c_str());
        } else if (a == "--mic") {
            settings.microphone = true;
        } else if (a == "--no-system-audio") {
            settings.systemAudio = false;
        } else if (a == "--region" && hasValue) {
            // x,y,width,height in monitor pixels
            std::sscanf(args[++i].c_str(), "%d,%d,%d,%d", &settings.region.x, &settings.region.y,
                        &settings.region.width, &settings.region.height);
        } else if (a == "--gpu-priority" && hasValue) {
            settings.gpuPriority = std::atoi(args[++i].c_str());
        } else if (a == "--no-cursor") {
            settings.cursor = false;
        } else {
            settings.outputPath = a;
        }
    }
    if (settings.outputPath.empty()) {
        std::printf("usage: divarec [--list] [--monitor N] [--region x,y,w,h] [--fps 60] [--codec h264|hevc|av1]\n"
                    "               [--bitrate 20M] [--mic] [--no-system-audio] [--seconds S] [--no-cursor]\n"
                    "               output.mkv\n");
        return 1;
    }

    ScreenRecorder recorder;
    std::string error;
    const double cpuStart = processCpuSeconds();
    const auto wallStart = std::chrono::steady_clock::now();
    if (!recorder.start(settings, &error)) {
        std::fprintf(stderr, "cannot start recording: %s\n", error.c_str());
        return 1;
    }
    RecorderStats s = recorder.stats();
    std::printf("Recording monitor %d (%dx%d) at %d fps with %s -> %s\n", settings.monitor, s.width, s.height,
                settings.fps, s.encoder.c_str(), settings.outputPath.c_str());
    std::printf("Capture rate capped by Windows: %s\n", s.rateCappedByWindows ? "yes" : "no (older Windows)");
    std::printf("System audio: %s\nMicrophone:   %s\n",
                s.systemAudioDevice.empty() ? "off" : s.systemAudioDevice.c_str(),
                s.microphoneDevice.empty() ? "off" : s.microphoneDevice.c_str());
    if (!s.audioWarning.empty())
        std::printf("Audio warning: %s\n", s.audioWarning.c_str());
    if (seconds > 0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    } else {
        std::printf("Press Enter to stop.\n");
        std::getchar();
    }

    const bool ok = recorder.stop(&error);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
    const double cpu = processCpuSeconds() - cpuStart;
    s = recorder.stats();
    if (!ok)
        std::fprintf(stderr, "recording error: %s\n", error.c_str());

    SYSTEM_INFO info;
    GetSystemInfo(&info);
    std::printf("\nduration        %.2f s\n", s.seconds);
    std::printf("frames written  %lld (%.2f fps, constant rate)\n", (long long)s.framesWritten,
                s.seconds > 0 ? s.framesWritten / s.seconds : 0.0);
    std::printf("screen updates  %lld captured\n", (long long)s.framesCaptured);
    std::printf("late ticks      %lld (worst %.2f ms behind)\n", (long long)s.lateTicks, s.maxLateMs);
    std::printf("recorder CPU    %.2f%% of all %lu cores (%.2f%% of one core)\n",
                100.0 * cpu / wall / info.dwNumberOfProcessors, info.dwNumberOfProcessors, 100.0 * cpu / wall);
    std::error_code ec;
    const auto size = std::filesystem::file_size(std::filesystem::path(settings.outputPath), ec);
    if (!ec)
        std::printf("file            %.1f MB\n", size / 1e6);
    return ok ? 0 : 1;
}
