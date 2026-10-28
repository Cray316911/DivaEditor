// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <memory>
#include <string>

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace diva {

class GpuDevice;
class SurfacePool;

// Demuxes a file and decodes its best video stream, on the GPU (D3D11VA) when the GPU
// supports the codec/profile and on the CPU otherwise. Either way frames come out as
// AV_PIX_FMT_D3D11 (data[0] = ID3D11Texture2D, data[1] = array slice) in NV12 or P010,
// shader-readable, so the rest of the pipeline never needs to know which path ran.
class HwDecoder
{
public:
    HwDecoder();
    ~HwDecoder();

    HwDecoder(const HwDecoder &) = delete;
    HwDecoder &operator=(const HwDecoder &) = delete;

    bool open(const std::string &utf8Path, std::shared_ptr<GpuDevice> gpu, std::string *error);
    void close();

    // Next frame in presentation order; free it with av_frame_free.
    // Returns nullptr at the end of the stream or on error (see lastError()).
    AVFrame *decodeNext();
    bool seekToStart();
    // Jumps to the keyframe at or before `seconds`; the next frames decoded start there.
    bool seekTo(double seconds);
    // Decode keyframes only (fast scanning of long recordings).
    void setKeyframesOnly(bool on);

    int width() const;
    int height() const;
    AVRational frameRate() const;
    double durationSeconds() const;
    // Presentation time of a decoded frame in seconds, relative to the stream start.
    double frameSeconds(const AVFrame *frame) const;
    std::string codecName() const;
    bool isHardware() const { return m_hardware; }
    const std::string &lastError() const { return m_error; }

private:
    AVFrame *uploadSoftwareFrame(AVFrame *frame);

    std::shared_ptr<GpuDevice> m_gpu;
    AVFormatContext *m_format = nullptr;
    AVCodecContext *m_codec = nullptr;
    AVPacket *m_packet = nullptr;
    int m_stream = -1;
    bool m_draining = false;
    bool m_hardware = false;
    std::string m_error;

    // Software path: CPU frame -> NV12 -> staging texture -> pooled GPU texture.
    SwsContext *m_sws = nullptr;
    AVFrame *m_nv12 = nullptr;
    std::shared_ptr<SurfacePool> m_uploadPool;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_staging;
};

} // namespace diva
