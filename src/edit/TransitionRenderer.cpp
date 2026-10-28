// SPDX-License-Identifier: GPL-3.0-or-later
#include "TransitionRenderer.h"

#include "util/Errors.h"

#include <d3dcompiler.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

namespace diva {

namespace {

// Kind indices follow transitionKinds(): dissolve, fade_black, flash, wipe_left, wipe_right,
// slide_left, slide_right, iris, zoom.
const char kShader[] = R"(
cbuffer Params : register(b0) { float progress; int kind; float aspect; float pad; };
Texture2D<float4> texA : register(t0);
Texture2D<float4> texB : register(t1);
SamplerState samp : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut vsMain(uint id : SV_VertexID)
{
    float2 t = float2(id & 1, id >> 1);
    VSOut o;
    o.pos = float4(t.x * 2 - 1, 1 - t.y * 2, 0, 1);
    o.uv = t;
    return o;
}

float4 A(float2 uv) { return texA.SampleLevel(samp, uv, 0); }
float4 B(float2 uv) { return texB.SampleLevel(samp, uv, 0); }

float4 psMain(VSOut i) : SV_Target
{
    float p = smoothstep(0.0, 1.0, saturate(progress));
    float2 uv = i.uv;
    float4 c;
    if (kind == 1) {
        c = p < 0.5 ? lerp(A(uv), float4(0, 0, 0, 1), p * 2) : lerp(float4(0, 0, 0, 1), B(uv), p * 2 - 1);
    } else if (kind == 2) {
        c = p < 0.5 ? lerp(A(uv), float4(1, 1, 1, 1), p * 2) : lerp(float4(1, 1, 1, 1), B(uv), p * 2 - 1);
    } else if (kind == 3 || kind == 4) {
        float x = kind == 3 ? uv.x : 1 - uv.x;
        float edge = p * 1.04 - 0.02;
        c = lerp(A(uv), B(uv), 1 - smoothstep(edge - 0.01, edge + 0.01, x));
    } else if (kind == 5) {
        c = uv.x < 1 - p ? A(uv + float2(p, 0)) : B(uv - float2(1 - p, 0));
    } else if (kind == 6) {
        c = uv.x > p ? A(uv - float2(p, 0)) : B(uv + float2(1 - p, 0));
    } else if (kind == 7) {
        float d = length((uv - 0.5) * float2(aspect, 1));
        float r = p * 0.52 * sqrt(aspect * aspect + 1);
        c = lerp(A(uv), B(uv), 1 - smoothstep(r - 0.01, r + 0.01, d));
    } else if (kind == 8) {
        float2 ua = 0.5 + (uv - 0.5) / (1 + 0.6 * p);
        float2 ub = 0.5 + (uv - 0.5) / (1.25 - 0.25 * p);
        c = lerp(A(ua), B(ub), p);
    } else {
        c = lerp(A(uv), B(uv), p);
    }
    return float4(c.rgb, 1);
}
)";

struct alignas(16) Constants
{
    float progress;
    int kind;
    float aspect;
    float pad;
};

bool compile(const char *entry, const char *target, ComPtr<ID3DBlob> *blob, std::string *error)
{
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(kShader, sizeof kShader - 1, "TransitionRenderer", nullptr, nullptr, entry,
                                  target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob->GetAddressOf(), &errors);
    if (FAILED(hr)) {
        std::string message = hrError("D3DCompile", hr);
        if (errors)
            message += std::string(": ") + static_cast<const char *>(errors->GetBufferPointer());
        return fail(error, message);
    }
    return true;
}

} // namespace

bool TransitionRenderer::init(ID3D11Device *device, std::string *error)
{
    ComPtr<ID3DBlob> vs, ps;
    if (!compile("vsMain", "vs_5_0", &vs, error) || !compile("psMain", "ps_5_0", &ps, error))
        return false;
    HRESULT hr = device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_vs);
    if (SUCCEEDED(hr))
        hr = device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_ps);
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr))
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
    if (SUCCEEDED(hr))
        hr = device->CreateDepthStencilState(&dd, &m_depth);
    if (FAILED(hr))
        return fail(error, hrError("create transition renderer", hr));
    return true;
}

void TransitionRenderer::draw(ID3D11DeviceContext *ctx, ID3D11ShaderResourceView *a, ID3D11ShaderResourceView *b,
                              ID3D11RenderTargetView *target, int width, int height, int kind, float progress)
{
    Constants c{progress, kind, float(width) / float(height), 0};
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ctx->Map(m_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &c, sizeof c);
        ctx->Unmap(m_constants.Get(), 0);
    }
    D3D11_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
    ID3D11ShaderResourceView *srvs[2] = {a, b};
    // Bind the target first: an input that is still the current render target would be
    // silently unbound by D3D11 (read/write hazard) and sample as black.
    ctx->OMSetRenderTargets(1, &target, nullptr);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(m_vs.Get(), nullptr, 0);
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
    ID3D11ShaderResourceView *none[2] = {};
    ctx->PSSetShaderResources(0, 2, none);
}

} // namespace diva
