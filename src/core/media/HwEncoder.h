// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gpu/GpuDevice.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace diva {

class SurfacePool;

struct EncoderSettings
{
    std::string codec = "hevc";      // h264 | hevc | av1
    std::string backend = "auto";    // auto | amf | nvenc | qsv | cpu
    int width = 1920;
    int height = 1080;
    AVRational fps{60, 1};
    std::string rateControl = "vbr"; // cbr | vbr | cqp
    int64_t bitrate = 20'000'000;
    int64_t maxBitrate = 0;          // vbr peak; 0 = 1.5 x bitrate
    int qp = 22;                     // cqp only
    int gopFrames = 0;               // 0 = one keyframe per second
    std::string preset = "balanced"; // speed | balanced | quality
    bool realtime = false;           // recording: low-latency tuning, no lookahead
    bool globalHeader = true;        // mp4/mkv/mov want codec extradata
};

// Receives each encoded packet; return false to abort.
using PacketSink = std::function<bool(AVPacket *)>;

struct EncoderProbe
{
    std::string codec;
    std::string name;
    bool available = false;
    bool hardware = false;
    std::string error; // why it did not open (driver too old, no such GPU, ...)
};

// Video encoder fed with NV12 D3D11 surfaces from its own pool. AMF and NVENC read the
// surfaces directly (no CPU copy). QSV and the CPU encoders (x264 / x265 / SVT-AV1) take
// system memory, so for them the surface is read back first. With backend "auto" the GPU
// vendor's encoder is tried first and the CPU encoder is the fallback, so export and
// recording work on any GPU.
class HwEncoder
{
public:
    HwEncoder();
    ~HwEncoder();

    HwEncoder(const HwEncoder &) = delete;
    HwEncoder &operator=(const HwEncoder &) = delete;

    static std::string backendFor(GpuVendor vendor);
    // Which encoders open on this machine (1080p60 test), hardware first.
    static std::vector<EncoderProbe> probe(const std::shared_ptr<GpuDevice> &gpu);

    bool open(const EncoderSettings &settings, std::shared_ptr<GpuDevice> gpu, std::string *error);
    AVCodecContext *context() const { return m_ctx; }
    const std::string &name() const { return m_name; }
    bool isHardware() const { return m_hardware; }

    // An NV12 surface to render the next frame into (av_frame_free when done).
    AVFrame *acquireFrame(std::string *error);
    // Submits frame (the caller keeps ownership) and forwards finished packets to sink.
    bool encode(const AVFrame *frame, const PacketSink &sink, std::string *error);
    // Flushes delayed packets; call once at the end.
    bool finish(const PacketSink &sink, std::string *error);

private:
    bool openBackend(const EncoderSettings &s, const std::string &backend, std::string *error);
    bool readBack(const AVFrame *surface, std::string *error);
    bool drain(const PacketSink &sink, int *received, std::string *error);

    std::shared_ptr<GpuDevice> m_gpu;
    AVCodecContext *m_ctx = nullptr;
    AVPacket *m_packet = nullptr;
    std::string m_name;
    bool m_hardware = false;

    // System-memory encoders (QSV, CPU).
    bool m_systemMemory = false;
    std::shared_ptr<SurfacePool> m_pool;
    ComPtr<ID3D11Texture2D> m_staging;
    AVFrame *m_cpuFrame = nullptr;
};

// D3D11 texture and array slice of an AV_PIX_FMT_D3D11 frame.
ID3D11Texture2D *frameTexture(const AVFrame *frame);
UINT frameSlice(const AVFrame *frame);

} // namespace diva
