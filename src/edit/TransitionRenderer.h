// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <string>

namespace diva {

// Blends two rendered RGBA frames into a target with one of the transitionKinds() looks.
class TransitionRenderer
{
public:
    bool init(ID3D11Device *device, std::string *error);
    // kind = index into transitionKinds(); progress 0 (all A) .. 1 (all B).
    void draw(ID3D11DeviceContext *ctx, ID3D11ShaderResourceView *a, ID3D11ShaderResourceView *b,
              ID3D11RenderTargetView *target, int width, int height, int kind, float progress);

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ps;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constants;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depth;
};

} // namespace diva
