// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct AVFrame;

namespace diva {

// D3D11 textures of one format handed out as AV_PIX_FMT_D3D11 frames (data[0] = texture,
// data[1] = 0) and recycled when the last AVFrame reference is dropped. Used where FFmpeg
// does not own the surfaces: software-decoded frames uploaded to the GPU, and frames for
// encoders that take system memory (QSV, CPU).
class SurfacePool : public std::enable_shared_from_this<SurfacePool>
{
public:
    static std::shared_ptr<SurfacePool> create(ID3D11Device *device, DXGI_FORMAT format, int width, int height,
                                               UINT bindFlags);

    AVFrame *acquire(std::string *error);
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    SurfacePool() = default;
    static void release(void *opaque, uint8_t *data);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    D3D11_TEXTURE2D_DESC m_desc{};
    int m_width = 0;
    int m_height = 0;
    std::mutex m_mutex;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> m_textures;
    std::vector<bool> m_busy;
};

} // namespace diva
