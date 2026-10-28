// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct AVBufferRef;

namespace diva {

using Microsoft::WRL::ComPtr;

enum class GpuVendor { Unknown, Amd, Nvidia, Intel };

struct AdapterInfo
{
    std::string name;
    uint32_t vendorId = 0;
    uint64_t dedicatedMemoryMB = 0;
};

// One D3D11 device shared by the decoder, compositor, encoder and the Qt Quick
// scene graph, so video frames never leave VRAM on their way to the screen or file.
class GpuDevice
{
public:
    // Picks the high-performance GPU (the dedicated one on hybrid laptops).
    static std::shared_ptr<GpuDevice> create(std::string *error);
    // Hardware adapters, high-performance first.
    static std::vector<AdapterInfo> adapters();
    ~GpuDevice();

    GpuDevice(const GpuDevice &) = delete;
    GpuDevice &operator=(const GpuDevice &) = delete;

    ID3D11Device *device() const { return m_device.Get(); }
    ID3D11DeviceContext *context() const { return m_context.Get(); }
    ID3D11VideoDevice *videoDevice() const { return m_videoDevice.Get(); }
    ID3D11VideoContext1 *videoContext() const { return m_videoContext.Get(); }
    GpuVendor vendor() const { return m_vendor; }
    const std::string &adapterName() const { return m_adapterName; }

    // FFmpeg AV_HWDEVICE_TYPE_D3D11VA context wrapping device().
    AVBufferRef *ffmpegDevice() const { return m_ffDevice; }

    // Serialises ID3D11VideoContext calls with FFmpeg's decoders and encoders.
    void lockVideo() const;
    void unlockVideo() const;

    // Scheduling priority of this device's GPU work relative to other processes (-7..7).
    void setGpuPriority(int priority) const;

    // Blocks until the GPU has finished all work submitted so far.
    void waitIdle() const;

private:
    GpuDevice() = default;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11VideoDevice> m_videoDevice;
    ComPtr<ID3D11VideoContext1> m_videoContext;
    AVBufferRef *m_ffDevice = nullptr;
    GpuVendor m_vendor = GpuVendor::Unknown;
    std::string m_adapterName;
};

class VideoLock
{
public:
    explicit VideoLock(const GpuDevice &gpu) : m_gpu(gpu) { m_gpu.lockVideo(); }
    ~VideoLock() { m_gpu.unlockVideo(); }
    VideoLock(const VideoLock &) = delete;
    VideoLock &operator=(const VideoLock &) = delete;

private:
    const GpuDevice &m_gpu;
};

} // namespace diva
