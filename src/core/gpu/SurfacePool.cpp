// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpu/SurfacePool.h"

#include "util/Errors.h"

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
}

namespace diva {

namespace {

struct ReleaseContext
{
    std::shared_ptr<SurfacePool> pool;
    size_t index;
};

} // namespace

std::shared_ptr<SurfacePool> SurfacePool::create(ID3D11Device *device, DXGI_FORMAT format, int width, int height,
                                                 UINT bindFlags)
{
    std::shared_ptr<SurfacePool> pool(new SurfacePool);
    pool->m_device = device;
    pool->m_width = width;
    pool->m_height = height;
    D3D11_TEXTURE2D_DESC &d = pool->m_desc;
    // Planar YUV textures need even dimensions.
    d.Width = UINT((width + 1) & ~1);
    d.Height = UINT((height + 1) & ~1);
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = format;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = bindFlags;
    return pool;
}

AVFrame *SurfacePool::acquire(std::string *error)
{
    ID3D11Texture2D *texture = nullptr;
    size_t index = 0;
    {
        std::lock_guard lock(m_mutex);
        for (index = 0; index < m_busy.size() && m_busy[index]; ++index) {
        }
        if (index == m_textures.size()) {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> created;
            const HRESULT hr = m_device->CreateTexture2D(&m_desc, nullptr, &created);
            if (FAILED(hr)) {
                fail(error, hrError("CreateTexture2D (surface pool)", hr));
                return nullptr;
            }
            m_textures.push_back(created);
            m_busy.push_back(false);
        }
        m_busy[index] = true;
        texture = m_textures[index].Get();
    }

    AVFrame *frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_D3D11;
    frame->width = m_width;
    frame->height = m_height;
    frame->data[0] = reinterpret_cast<uint8_t *>(texture);
    frame->data[1] = nullptr;
    auto *ctx = new ReleaseContext{shared_from_this(), index};
    frame->buf[0] = av_buffer_create(reinterpret_cast<uint8_t *>(texture), 1, &SurfacePool::release, ctx, 0);
    return frame;
}

void SurfacePool::release(void *opaque, uint8_t *)
{
    auto *ctx = static_cast<ReleaseContext *>(opaque);
    {
        std::lock_guard lock(ctx->pool->m_mutex);
        ctx->pool->m_busy[ctx->index] = false;
    }
    delete ctx;
}

} // namespace diva
