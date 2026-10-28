// SPDX-License-Identifier: GPL-3.0-or-later
#include "compositor/RgbToNv12.h"

#include "gpu/GpuDevice.h"
#include "util/Errors.h"

using Microsoft::WRL::ComPtr;

namespace diva {

bool RgbToNv12::init(const GpuDevice &gpu, int inWidth, int inHeight, int outWidth, int outHeight,
                     std::string *error, const RECT *source)
{
    m_gpu = &gpu;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputFrameRate = {60, 1};
    desc.InputWidth = UINT(inWidth);
    desc.InputHeight = UINT(inHeight);
    desc.OutputFrameRate = {60, 1};
    desc.OutputWidth = UINT(outWidth);
    desc.OutputHeight = UINT(outHeight);
    desc.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
    HRESULT hr = gpu.videoDevice()->CreateVideoProcessorEnumerator(&desc, &m_enum);
    if (FAILED(hr))
        return fail(error, hrError("CreateVideoProcessorEnumerator", hr));

    UINT support = 0;
    if (FAILED(m_enum->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &support))
        || !(support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT))
        return fail(error, "video processor cannot read BGRA");
    if (FAILED(m_enum->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &support))
        || !(support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))
        return fail(error, "video processor cannot write NV12");

    hr = gpu.videoDevice()->CreateVideoProcessor(m_enum.Get(), 0, &m_processor);
    if (FAILED(hr))
        return fail(error, hrError("CreateVideoProcessor", hr));

    VideoLock lock(gpu);
    ID3D11VideoContext1 *vc = gpu.videoContext();
    const RECT src = source ? *source : RECT{0, 0, inWidth, inHeight};
    const RECT dst{0, 0, outWidth, outHeight};
    vc->VideoProcessorSetStreamFrameFormat(m_processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    vc->VideoProcessorSetStreamSourceRect(m_processor.Get(), 0, TRUE, &src);
    vc->VideoProcessorSetStreamDestRect(m_processor.Get(), 0, TRUE, &dst);
    vc->VideoProcessorSetOutputTargetRect(m_processor.Get(), TRUE, &dst);
    vc->VideoProcessorSetStreamAutoProcessingMode(m_processor.Get(), 0, FALSE);
    vc->VideoProcessorSetStreamColorSpace1(m_processor.Get(), 0, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    vc->VideoProcessorSetOutputColorSpace1(m_processor.Get(), DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    return true;
}

bool RgbToNv12::convert(ID3D11Texture2D *bgra, ID3D11Texture2D *nv12, UINT nv12Slice, std::string *error)
{
    ID3D11VideoDevice *vd = m_gpu->videoDevice();

    auto &input = m_inputs[bgra];
    if (!input) {
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC desc{};
        desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        const HRESULT hr = vd->CreateVideoProcessorInputView(bgra, m_enum.Get(), &desc, &input);
        if (FAILED(hr)) {
            m_inputs.erase(bgra);
            return fail(error, hrError("CreateVideoProcessorInputView", hr));
        }
    }

    auto &output = m_outputs[{nv12, nv12Slice}];
    if (!output) {
        D3D11_TEXTURE2D_DESC td;
        nv12->GetDesc(&td);
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC desc{};
        if (td.ArraySize > 1) {
            desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.FirstArraySlice = nv12Slice;
            desc.Texture2DArray.ArraySize = 1;
        } else {
            desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        }
        const HRESULT hr = vd->CreateVideoProcessorOutputView(nv12, m_enum.Get(), &desc, &output);
        if (FAILED(hr)) {
            m_outputs.erase({nv12, nv12Slice});
            return fail(error, hrError("CreateVideoProcessorOutputView", hr));
        }
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = input.Get();
    VideoLock lock(*m_gpu);
    const HRESULT hr = m_gpu->videoContext()->VideoProcessorBlt(m_processor.Get(), output.Get(), 0, 1, &stream);
    if (FAILED(hr))
        return fail(error, hrError("VideoProcessorBlt", hr));
    return true;
}

} // namespace diva
