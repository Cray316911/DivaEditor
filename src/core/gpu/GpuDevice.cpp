// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpu/GpuDevice.h"

#include "util/Errors.h"

#include <d3d11_4.h>
#include <dxgi1_6.h>

#include <cstdlib>
#include <iterator>
#include <thread>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

namespace diva {

namespace {

std::string toUtf8(const wchar_t *text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string out(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

AVD3D11VADeviceContext *d3d11va(AVBufferRef *device)
{
    auto *hw = reinterpret_cast<AVHWDeviceContext *>(device->data);
    return static_cast<AVD3D11VADeviceContext *>(hw->hwctx);
}

} // namespace

std::vector<AdapterInfo> GpuDevice::adapters()
{
    std::vector<AdapterInfo> list;
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return list;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                   IID_PPV_ARGS(&adapter)));
         ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        list.push_back({toUtf8(desc.Description), desc.VendorId, desc.DedicatedVideoMemory / (1024 * 1024)});
    }
    return list;
}

std::shared_ptr<GpuDevice> GpuDevice::create(std::string *error)
{
    std::shared_ptr<GpuDevice> gpu(new GpuDevice);

    // Laptops with integrated + dedicated graphics list the integrated GPU first; ask DXGI
    // for the high-performance one (it has the strongest decoder/encoder). DIVA_ADAPTER=<n>
    // overrides the choice, counting in that same order.
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIFactory6> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        char forced[16] = {};
        UINT index = GetEnvironmentVariableA("DIVA_ADAPTER", forced, sizeof forced) ? UINT(std::atoi(forced)) : 0;
        for (UINT i = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                       IID_PPV_ARGS(&adapter)));
             ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;
            if (index-- == 0)
                break;
            adapter.Reset();
        }
    }

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    HRESULT hr = D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr, flags, levels, UINT(std::size(levels)), D3D11_SDK_VERSION,
                                   &gpu->m_device, nullptr, &gpu->m_context);
    if (FAILED(hr)) {
        fail(error, hrError("D3D11CreateDevice", hr));
        return nullptr;
    }

    // Decoder threads, the render thread and the encoder all use the immediate context.
    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(gpu->m_context.As(&multithread)))
        multithread->SetMultithreadProtected(TRUE);

    if (FAILED(gpu->m_device.As(&gpu->m_videoDevice)) || FAILED(gpu->m_context.As(&gpu->m_videoContext))) {
        fail(error, "GPU driver has no D3D11 video support");
        return nullptr;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> used;
    DXGI_ADAPTER_DESC desc{};
    if (SUCCEEDED(gpu->m_device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&used))
        && SUCCEEDED(used->GetDesc(&desc))) {
        gpu->m_adapterName = toUtf8(desc.Description);
        switch (desc.VendorId) {
        case 0x1002:
        case 0x1022: gpu->m_vendor = GpuVendor::Amd; break;
        case 0x10DE: gpu->m_vendor = GpuVendor::Nvidia; break;
        case 0x8086: gpu->m_vendor = GpuVendor::Intel; break;
        default: break;
        }
    }

    AVBufferRef *ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!ref) {
        fail(error, "av_hwdevice_ctx_alloc failed");
        return nullptr;
    }
    AVD3D11VADeviceContext *ctx = d3d11va(ref);
    // FFmpeg releases these references when the context is freed.
    ctx->device = gpu->m_device.Get();
    ctx->device->AddRef();
    ctx->device_context = gpu->m_context.Get();
    ctx->device_context->AddRef();
    const int ret = av_hwdevice_ctx_init(ref);
    if (ret < 0) {
        av_buffer_unref(&ref);
        fail(error, avError("av_hwdevice_ctx_init", ret));
        return nullptr;
    }
    gpu->m_ffDevice = ref;
    return gpu;
}

GpuDevice::~GpuDevice()
{
    av_buffer_unref(&m_ffDevice);
}

void GpuDevice::lockVideo() const
{
    AVD3D11VADeviceContext *ctx = d3d11va(m_ffDevice);
    ctx->lock(ctx->lock_ctx);
}

void GpuDevice::unlockVideo() const
{
    AVD3D11VADeviceContext *ctx = d3d11va(m_ffDevice);
    ctx->unlock(ctx->lock_ctx);
}

void GpuDevice::setGpuPriority(int priority) const
{
    ComPtr<IDXGIDevice> dxgiDevice;
    if (SUCCEEDED(m_device.As(&dxgiDevice)))
        dxgiDevice->SetGPUThreadPriority(priority);
}

void GpuDevice::waitIdle() const
{
    D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
    ComPtr<ID3D11Query> query;
    if (FAILED(m_device->CreateQuery(&desc, &query)))
        return;
    m_context->End(query.Get());
    BOOL done = FALSE;
    while (m_context->GetData(query.Get(), &done, sizeof done, 0) == S_FALSE)
        std::this_thread::yield();
}

} // namespace diva
