// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <utility>

struct AVFrame;

namespace diva {

struct RectF
{
    float x = 0, y = 0, w = 0, h = 0;
};

// Draws decoded NV12 / P010 GPU frames into an RGBA render target: YUV->RGB and
// scaling in one shader pass, sampling the decoder's own surfaces (no copies).
// This is the first layer of the compositor; transforms and effects build on it.
class Nv12Renderer
{
public:
    bool init(ID3D11Device *device, std::string *error);

    // dst is in target pixels. The caller restores pipeline state if it needs to.
    bool draw(ID3D11DeviceContext *ctx, const AVFrame *frame, ID3D11RenderTargetView *target,
              int targetWidth, int targetHeight, const RectF &dst, bool clear, std::string *error = nullptr);

    // Drops cached views (they keep decoder surfaces alive); call when the source changes.
    void clearCache() { m_views.clear(); }

    // Largest rect with the source aspect ratio centred in the target.
    static RectF fit(int srcWidth, int srcHeight, int dstWidth, int dstHeight);

private:
    struct PlaneViews
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> luma;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> chroma;
    };
    const PlaneViews *viewsFor(ID3D11Texture2D *texture, UINT slice, DXGI_FORMAT format, std::string *error);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ps;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constants;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depth;
    std::map<std::pair<ID3D11Texture2D *, UINT>, PlaneViews> m_views;
};

} // namespace diva
