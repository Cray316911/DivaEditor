// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <utility>

namespace diva {

class GpuDevice;

// Converts (and optionally scales) a BGRA texture into an NV12 encoder surface with
// the D3D11 video processor: fixed-function hardware on most GPUs, so it barely
// competes with games or the compositor for shader time.
class RgbToNv12
{
public:
    // `source` crops the input (e.g. a screen region); null = the whole input.
    bool init(const GpuDevice &gpu, int inWidth, int inHeight, int outWidth, int outHeight, std::string *error,
              const RECT *source = nullptr);
    bool convert(ID3D11Texture2D *bgra, ID3D11Texture2D *nv12, UINT nv12Slice, std::string *error = nullptr);

private:
    const GpuDevice *m_gpu = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_enum;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_processor;
    std::map<ID3D11Texture2D *, Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView>> m_inputs;
    std::map<std::pair<ID3D11Texture2D *, UINT>, Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView>> m_outputs;
};

} // namespace diva
