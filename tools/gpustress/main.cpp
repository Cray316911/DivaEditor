// SPDX-License-Identifier: GPL-3.0-or-later
//
// gpustress: renders a heavy full-screen shader as fast as possible, like a GPU-bound
// game, and reports the frame rate. Run it with and without divarec to see what
// recording costs a game.
//
//   gpustress [seconds=15] [iterations=1024]

#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

const char kShader[] = R"(
cbuffer Params : register(b0) { float2 resolution; float time; float iterations; };
float4 vsMain(uint id : SV_VertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 psMain(float4 pos : SV_Position) : SV_Target
{
    float2 uv = pos.xy / resolution;
    float3 c = 0;
    for (float i = 0; i < iterations; i += 1) {
        uv = abs(uv * 1.31 - 0.655 + 0.08 * sin(time + i * 0.01));
        c = frac(c + 0.013 * float3(uv, 1 - uv.y) + sin(dot(uv, float2(12.9898, 78.233))) * 0.001);
    }
    return float4(c, 1);
}
)";

struct alignas(16) Params
{
    float resolution[2];
    float time;
    float iterations;
};

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int main(int argc, char **argv)
{
    const double seconds = argc > 1 ? std::atof(argv[1]) : 15.0;
    const float iterations = argc > 2 ? float(std::atof(argv[2])) : 1024.0f;
    const int width = 1600, height = 900;

    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DivaGpuStress";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT rect{0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"GPU stress (game stand-in)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60,
                              60, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance,
                              nullptr);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                      nullptr, &ctx);
    ComPtr<IDXGIFactory5> factory;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    BOOL tearing = FALSE;
    factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof tearing);

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    ComPtr<IDXGISwapChain1> swapChain;
    if (FAILED(factory->CreateSwapChainForHwnd(device.Get(), hwnd, &sd, nullptr, nullptr, &swapChain))) {
        std::fprintf(stderr, "swap chain creation failed\n");
        return 1;
    }
    ComPtr<ID3D11Texture2D> backBuffer;
    swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    ComPtr<ID3D11RenderTargetView> rtv;
    device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv);

    ComPtr<ID3DBlob> vsBlob, psBlob, errors;
    if (FAILED(D3DCompile(kShader, sizeof kShader - 1, nullptr, nullptr, nullptr, "vsMain", "vs_5_0", 0, 0, &vsBlob,
                          &errors))
        || FAILED(D3DCompile(kShader, sizeof kShader - 1, nullptr, nullptr, nullptr, "psMain", "ps_5_0", 0, 0,
                             &psBlob, &errors))) {
        std::fprintf(stderr, "shader: %s\n", errors ? (const char *)errors->GetBufferPointer() : "?");
        return 1;
    }
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
    device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);
    D3D11_BUFFER_DESC cbd{sizeof(Params), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    ComPtr<ID3D11Buffer> cb;
    device->CreateBuffer(&cbd, nullptr, &cb);

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto last = start;
    std::vector<double> frameMs;
    frameMs.reserve(200000);
    MSG msg{};
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        const auto now = Clock::now();
        const double t = std::chrono::duration<double>(now - start).count();
        if (t >= seconds)
            break;

        Params p{{float(width), float(height)}, float(t), iterations};
        ctx->UpdateSubresource(cb.Get(), 0, nullptr, &p, 0, 0);
        D3D11_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
        ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
        ctx->Draw(3, 0);
        swapChain->Present(0, tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);

        const auto after = Clock::now();
        frameMs.push_back(std::chrono::duration<double, std::milli>(after - last).count());
        last = after;
    }
    DestroyWindow(hwnd);

    const double total = std::chrono::duration<double>(last - start).count();
    std::vector<double> sorted = frameMs;
    std::sort(sorted.begin(), sorted.end());
    const double p99 = sorted.empty() ? 0 : sorted[size_t(sorted.size() * 0.99)];
    std::printf("frames %zu in %.2f s: average %.1f fps, 1%% low %.1f fps\n", frameMs.size(), total,
                frameMs.size() / total, p99 > 0 ? 1000.0 / p99 : 0.0);
    return 0;
}
