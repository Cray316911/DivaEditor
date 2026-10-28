// SPDX-License-Identifier: GPL-3.0-or-later
#include "compositor/Nv12Renderer.h"

#include "util/Errors.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

using Microsoft::WRL::ComPtr;

namespace diva {

namespace {

const char kShader[] = R"(
cbuffer Params : register(b0)
{
    float4 dstRect;   // NDC: left, top, right, bottom
    float4 srcRect;   // UV:  left, top, right, bottom
    float4 rowR;      // YUV -> RGB matrix rows applied to (y, u, v, 1)
    float4 rowG;
    float4 rowB;
};

Texture2DArray<float>  texY  : register(t0);
Texture2DArray<float2> texUV : register(t1);
SamplerState samp : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut vsMain(uint id : SV_VertexID)
{
    float2 t = float2(id & 1, id >> 1);
    VSOut o;
    o.pos = float4(lerp(dstRect.x, dstRect.z, t.x), lerp(dstRect.y, dstRect.w, t.y), 0, 1);
    o.uv = lerp(srcRect.xy, srcRect.zw, t);
    return o;
}

float3 sampleYuv(float2 uv)
{
    return float3(texY.SampleLevel(samp, float3(uv, 0), 0), texUV.SampleLevel(samp, float3(uv, 0), 0));
}

float4 psMain(VSOut i) : SV_Target
{
    float w, h, layers;
    texY.GetDimensions(w, h, layers);
    float2 dx = ddx(i.uv);
    float2 dy = ddy(i.uv);
    float3 yuv;
    // Shrinking (e.g. 4K into the canvas): average 4 taps so small text stays readable.
    if (max(length(dx * float2(w, h)), length(dy * float2(w, h))) > 1.0) {
        float2 a = 0.25 * (dx + dy);
        float2 b = 0.25 * (dx - dy);
        yuv = 0.25 * (sampleYuv(i.uv + a) + sampleYuv(i.uv - a) + sampleYuv(i.uv + b) + sampleYuv(i.uv - b));
    } else {
        yuv = sampleYuv(i.uv);
    }
    float4 v = float4(yuv, 1);
    return float4(saturate(float3(dot(rowR, v), dot(rowG, v), dot(rowB, v))), 1);
}
)";

struct alignas(16) Constants
{
    float dst[4];
    float src[4];
    float rows[3][4];
};

// Row-major YUV->RGB matrix (with offsets in column 3) for the frame's colour space.
void colorRows(const AVFrame *frame, float rows[3][4])
{
    double kr = 0.2126, kb = 0.0722; // BT.709
    switch (frame->colorspace) {
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M: kr = 0.299; kb = 0.114; break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: kr = 0.2627; kb = 0.0593; break;
    case AVCOL_SPC_BT709: break;
    default:
        if (frame->height < 720) {
            kr = 0.299;
            kb = 0.114;
        }
        break;
    }
    const double kg = 1.0 - kr - kb;
    const bool full = frame->color_range == AVCOL_RANGE_JPEG;
    const double ys = full ? 1.0 : 255.0 / 219.0;
    const double cs = full ? 1.0 : 255.0 / 224.0;
    const double yo = full ? 0.0 : 16.0 / 255.0;
    const double co = 128.0 / 255.0;
    const double rV = 2.0 * (1.0 - kr) * cs;
    const double gU = -2.0 * kb * (1.0 - kb) / kg * cs;
    const double gV = -2.0 * kr * (1.0 - kr) / kg * cs;
    const double bU = 2.0 * (1.0 - kb) * cs;
    const double m[3][4] = {
        {ys, 0.0, rV, -ys * yo - rV * co},
        {ys, gU, gV, -ys * yo - (gU + gV) * co},
        {ys, bU, 0.0, -ys * yo - bU * co},
    };
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            rows[r][c] = float(m[r][c]);
}

bool compile(const char *entry, const char *target, ComPtr<ID3DBlob> *blob, std::string *error)
{
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(kShader, sizeof kShader - 1, "Nv12Renderer", nullptr, nullptr, entry, target,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob->GetAddressOf(), &errors);
    if (FAILED(hr)) {
        std::string message = hrError("D3DCompile", hr);
        if (errors)
            message += std::string(": ") + static_cast<const char *>(errors->GetBufferPointer());
        return fail(error, message);
    }
    return true;
}

} // namespace

bool Nv12Renderer::init(ID3D11Device *device, std::string *error)
{
    m_device = device;
    ComPtr<ID3DBlob> vs, ps;
    if (!compile("vsMain", "vs_5_0", &vs, error) || !compile("psMain", "ps_5_0", &ps, error))
        return false;
    HRESULT hr = device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_vs);
    if (SUCCEEDED(hr))
        hr = device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_ps);
    if (FAILED(hr))
        return fail(error, hrError("create shaders", hr));

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cb, nullptr, &m_constants);

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(hr))
        hr = device->CreateSamplerState(&sd, &m_sampler);

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (SUCCEEDED(hr))
        hr = device->CreateRasterizerState(&rd, &m_rasterizer);

    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.StencilEnable = FALSE;
    if (SUCCEEDED(hr))
        hr = device->CreateDepthStencilState(&dd, &m_depth);
    if (FAILED(hr))
        return fail(error, hrError("create renderer state", hr));
    return true;
}

const Nv12Renderer::PlaneViews *Nv12Renderer::viewsFor(ID3D11Texture2D *texture, UINT slice, DXGI_FORMAT format,
                                                       std::string *error)
{
    const auto key = std::make_pair(texture, slice);
    if (auto it = m_views.find(key); it != m_views.end())
        return &it->second;

    const bool tenBit = format == DXGI_FORMAT_P010 || format == DXGI_FORMAT_P016;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    desc.Texture2DArray.MipLevels = 1;
    desc.Texture2DArray.FirstArraySlice = slice;
    desc.Texture2DArray.ArraySize = 1;

    PlaneViews views;
    desc.Format = tenBit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    HRESULT hr = m_device->CreateShaderResourceView(texture, &desc, &views.luma);
    if (SUCCEEDED(hr)) {
        desc.Format = tenBit ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
        hr = m_device->CreateShaderResourceView(texture, &desc, &views.chroma);
    }
    if (FAILED(hr)) {
        fail(error, hrError("CreateShaderResourceView on decoder surface", hr));
        return nullptr;
    }
    // Decoder pools are small and fixed; a stale cache only means a source switch.
    if (m_views.size() > 256)
        m_views.clear();
    return &m_views.emplace(key, std::move(views)).first->second;
}

bool Nv12Renderer::draw(ID3D11DeviceContext *ctx, const AVFrame *frame, ID3D11RenderTargetView *target,
                        int targetWidth, int targetHeight, const RectF &dst, bool clear, std::string *error)
{
    if (!frame || frame->format != AV_PIX_FMT_D3D11)
        return fail(error, "not a D3D11 frame");
    auto *texture = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
    const UINT slice = static_cast<UINT>(reinterpret_cast<intptr_t>(frame->data[1]));
    D3D11_TEXTURE2D_DESC td;
    texture->GetDesc(&td);
    const PlaneViews *views = viewsFor(texture, slice, td.Format, error);
    if (!views)
        return false;

    Constants c{};
    const float tw = float(targetWidth), th = float(targetHeight);
    c.dst[0] = dst.x / tw * 2.0f - 1.0f;
    c.dst[1] = 1.0f - dst.y / th * 2.0f;
    c.dst[2] = (dst.x + dst.w) / tw * 2.0f - 1.0f;
    c.dst[3] = 1.0f - (dst.y + dst.h) / th * 2.0f;
    // Decoder surfaces are padded (e.g. 2160 -> 2176 rows); sample only the picture.
    c.src[0] = 0.0f;
    c.src[1] = 0.0f;
    c.src[2] = float(frame->width) / float(td.Width);
    c.src[3] = float(frame->height) / float(td.Height);
    colorRows(frame, c.rows);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx->Map(m_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return fail(error, "map constants failed");
    std::memcpy(mapped.pData, &c, sizeof c);
    ctx->Unmap(m_constants.Get(), 0);

    if (clear) {
        const float black[4] = {0, 0, 0, 1};
        ctx->ClearRenderTargetView(target, black);
    }
    D3D11_VIEWPORT vp{0, 0, tw, th, 0, 1};
    ID3D11ShaderResourceView *srvs[2] = {views->luma.Get(), views->chroma.Get()};
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, m_constants.GetAddressOf());
    ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, m_constants.GetAddressOf());
    ctx->PSSetShaderResources(0, 2, srvs);
    ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    ctx->RSSetState(m_rasterizer.Get());
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, &target, nullptr);
    ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx->OMSetDepthStencilState(m_depth.Get(), 0);
    ctx->Draw(4, 0);

    // Unbind so the decoder can write the surface again without a hazard warning.
    ID3D11ShaderResourceView *none[2] = {};
    ctx->PSSetShaderResources(0, 2, none);
    return true;
}

RectF Nv12Renderer::fit(int srcWidth, int srcHeight, int dstWidth, int dstHeight)
{
    if (srcWidth <= 0 || srcHeight <= 0)
        return {0, 0, float(dstWidth), float(dstHeight)};
    const float scale = std::min(float(dstWidth) / srcWidth, float(dstHeight) / srcHeight);
    const float w = srcWidth * scale, h = srcHeight * scale;
    return {(dstWidth - w) * 0.5f, (dstHeight - h) * 0.5f, w, h};
}

} // namespace diva
