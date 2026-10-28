// SPDX-License-Identifier: GPL-3.0-or-later
#include "VideoItem.h"

#include "Privacy.h"
#include "compositor/Nv12Renderer.h"
#include "gpu/GpuDevice.h"
#include "media/HwDecoder.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QtQuick/qsgtexture_platform.h>

#include <algorithm>
#include <chrono>

extern "C" {
#include <libavutil/frame.h>
}

using Microsoft::WRL::ComPtr;

namespace {

constexpr size_t kQueueDepth = 8;

std::shared_ptr<diva::GpuDevice> g_gpu;

int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

struct VideoItem::RenderState
{
    diva::Nv12Renderer renderer;
    bool rendererReady = false;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> target;
    QSGTexture *sgTexture = nullptr;
    QSize size;
    bool targetDirty = false;
    AVFrame *current = nullptr;
    double currentTime = 0;
    bool currentShown = false;
    bool stalled = false;
    int generation = -1;

    ~RenderState()
    {
        av_frame_free(&current);
        delete sgTexture;
    }
};

VideoItem::VideoItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    m_uiTimer.setInterval(33);
    connect(&m_uiTimer, &QTimer::timeout, this, [this] {
        if (m_playing)
            emit positionChanged();
        if (++m_tick % 15 == 0)
            updateStats();
    });
    m_uiTimer.start();
}

VideoItem::~VideoItem()
{
    stopDecoder();
}

void VideoItem::setSharedGpu(std::shared_ptr<diva::GpuDevice> gpu)
{
    g_gpu = std::move(gpu);
}

QString VideoItem::gpuName() const
{
    if (privacyMode())
        return QStringLiteral("Ekran kartı");
    return g_gpu ? QString::fromStdString(g_gpu->adapterName()) : QString();
}

void VideoItem::setSource(const QUrl &source)
{
    if (source == m_source)
        return;
    stopDecoder();
    m_source = source;
    m_error.clear();
    m_codec.clear();
    m_videoSize = {};
    m_duration = 0;
    m_position = 0;

    if (!source.isEmpty()) {
        const QString path = source.isLocalFile() ? source.toLocalFile() : source.toString();
        auto decoder = std::make_unique<diva::HwDecoder>();
        std::string error;
        if (!g_gpu) {
            error = "GPU is not initialised";
        } else if (decoder->open(path.toStdString(), g_gpu, &error)) {
            m_decoder = std::move(decoder);
            m_videoSize = QSize(m_decoder->width(), m_decoder->height());
            const AVRational rate = m_decoder->frameRate();
            m_fps = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 60.0;
            m_frameSeconds = 1.0 / m_fps;
            m_duration = m_decoder->durationSeconds();
            m_codec = QString::fromStdString(m_decoder->codecName()).toUpper();
            ++m_generation;
            m_presented = 0;
            m_dropped = 0;
            m_stalls = 0;
            m_statsPresented = 0;
            m_sourceStartNs = m_statsNs = nowNs();
            m_seekTarget = 0;
            m_stop = false;
            m_thread = std::thread(&VideoItem::decodeLoop, this);
            m_pausedNs = 0;
            m_clockRunning = false;
            m_startOnFirstFrame = true;
            m_playing = true;
        }
        if (!error.empty()) {
            m_error = QString::fromStdString(error);
            qWarning() << "cannot open" << path << ":" << m_error;
        }
    }
    emit sourceChanged();
    emit playingChanged();
    emit positionChanged();
    update();
}

void VideoItem::setPlaying(bool playing)
{
    if (playing == m_playing || !m_decoder)
        return;
    if (m_startOnFirstFrame) {
        // Nothing shown yet: the render thread starts the clock on the first frame.
    } else if (playing) {
        m_clockBaseNs = nowNs() - m_pausedNs;
        m_clockRunning = true;
    } else {
        m_pausedNs = nowNs() - m_clockBaseNs;
        m_clockRunning = false;
    }
    m_playing = playing;
    emit playingChanged();
    update();
}

void VideoItem::seek(double seconds)
{
    if (!m_decoder)
        return;
    seconds = std::clamp(seconds, 0.0, std::max(0.0, m_duration - 0.01));
    {
        std::lock_guard lock(m_queueMutex);
        m_stop = true;
    }
    m_queueCond.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    {
        std::lock_guard lock(m_queueMutex);
        for (QueuedFrame &q : m_queue)
            av_frame_free(&q.frame);
        m_queue.clear();
        m_stop = false;
    }
    m_decoder->seekTo(seconds);
    m_seekTarget = seconds;
    m_position = seconds;
    ++m_generation;
    // The clock restarts at the first frame shown, keeping play/pause as it was.
    m_clockRunning = false;
    m_startOnFirstFrame = true;
    m_thread = std::thread(&VideoItem::decodeLoop, this);
    emit positionChanged();
    update();
}

double VideoItem::clockSeconds() const
{
    const int64_t ns = m_clockRunning ? nowNs() - m_clockBaseNs : m_pausedNs.load();
    return ns * 1e-9;
}

void VideoItem::stopDecoder()
{
    {
        std::lock_guard lock(m_queueMutex);
        m_stop = true;
    }
    m_queueCond.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    std::lock_guard lock(m_queueMutex);
    for (QueuedFrame &q : m_queue)
        av_frame_free(&q.frame);
    m_queue.clear();
    m_decoder.reset();
    m_clockRunning = false;
}

void VideoItem::decodeLoop()
{
    const int generation = m_generation;
    const double frameSeconds = m_frameSeconds;
    double loopOffset = 0;
    double lastTime = 0;
    bool producedThisLoop = false;
    for (;;) {
        AVFrame *frame = m_decoder->decodeNext();
        if (!frame) {
            // End of file: start over so the preview loops without a gap.
            if (!producedThisLoop || !m_decoder->seekToStart())
                break;
            loopOffset = lastTime + frameSeconds;
            producedThisLoop = false;
            continue;
        }
        producedThisLoop = true;
        const double position = m_decoder->frameSeconds(frame);
        // After a seek the decoder restarts at the previous keyframe; skip up to the target.
        if (loopOffset == 0 && position < m_seekTarget - 0.5 * frameSeconds) {
            av_frame_free(&frame);
            continue;
        }
        lastTime = loopOffset + position;

        std::unique_lock lock(m_queueMutex);
        m_queueCond.wait(lock, [this] { return m_stop || m_queue.size() < kQueueDepth; });
        if (m_stop) {
            av_frame_free(&frame);
            return;
        }
        m_queue.push_back({frame, lastTime, position, generation});
    }
    if (!m_decoder->lastError().empty())
        qWarning() << "decoder stopped:" << m_decoder->lastError().c_str();
}

void VideoItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange) {
        if (m_window)
            disconnect(m_window, nullptr, this, nullptr);
        m_window = value.window;
        if (m_window) {
            connect(m_window, &QQuickWindow::beforeRendering, this, &VideoItem::renderFrame, Qt::DirectConnection);
            connect(m_window, &QQuickWindow::sceneGraphInvalidated, this, [this] { m_render.reset(); },
                    Qt::DirectConnection);
            // Keep the scene graph rendering at the display refresh rate while playing.
            connect(m_window, &QQuickWindow::frameSwapped, this, [this] {
                if (m_playing)
                    update();
            }, Qt::QueuedConnection);
        }
    }
    QQuickItem::itemChange(change, value);
}

QSGNode *VideoItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    if (!g_gpu || m_videoSize.isEmpty() || width() <= 0 || height() <= 0) {
        delete node;
        return nullptr;
    }
    if (!m_render)
        m_render = std::make_unique<RenderState>();
    RenderState &rs = *m_render;
    if (!rs.rendererReady) {
        std::string error;
        rs.rendererReady = rs.renderer.init(g_gpu->device(), &error);
        if (!rs.rendererReady) {
            qWarning() << "renderer init failed:" << error.c_str();
            delete node;
            return nullptr;
        }
    }

    const diva::RectF fit = diva::Nv12Renderer::fit(m_videoSize.width(), m_videoSize.height(), int(width()),
                                                    int(height()));
    const QRectF rect(fit.x, fit.y, fit.w, fit.h);
    // Render at the on-screen pixel size (never above the source) and let the shader do the scaling.
    const qreal dpr = window()->effectiveDevicePixelRatio();
    const QSize pixels = QSize(qMax(1, qRound(rect.width() * dpr)), qMax(1, qRound(rect.height() * dpr)))
                             .boundedTo(m_videoSize);
    if (pixels != rs.size) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(pixels.width());
        td.Height = UINT(pixels.height());
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        rs.texture.Reset();
        rs.target.Reset();
        if (FAILED(g_gpu->device()->CreateTexture2D(&td, nullptr, &rs.texture))
            || FAILED(g_gpu->device()->CreateRenderTargetView(rs.texture.Get(), nullptr, &rs.target))) {
            qWarning() << "cannot create preview texture" << pixels;
            delete node;
            return nullptr;
        }
        delete rs.sgTexture;
        rs.sgTexture = QNativeInterface::QSGD3D11Texture::fromNative(rs.texture.Get(), window(), pixels);
        rs.size = pixels;
        rs.targetDirty = true;
    }

    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setFiltering(QSGTexture::Linear);
        node->setOwnsTexture(false);
    }
    node->setTexture(rs.sgTexture);
    node->setRect(rect);
    return node;
}

void VideoItem::renderFrame()
{
    RenderState *rs = m_render.get();
    if (!rs || !rs->target || !g_gpu)
        return;

    const int generation = m_generation;
    if (rs->generation != generation) {
        av_frame_free(&rs->current);
        rs->generation = generation;
        rs->stalled = false;
        rs->renderer.clearCache();
    }

    bool changed = rs->targetDirty;
    {
        std::lock_guard lock(m_queueMutex);
        // First frame of a new source: show it now and start the clock from here, so
        // window creation and decoder warm-up never count as late or dropped frames.
        if (m_startOnFirstFrame && !m_queue.empty() && m_queue.front().generation == generation) {
            m_startOnFirstFrame = false;
            const int64_t start = nowNs() - int64_t(m_queue.front().time * 1e9);
            m_clockBaseNs = start;
            m_sourceStartNs = start;
            m_clockRunning = m_playing.load();
            if (!m_playing)
                m_pausedNs = nowNs() - start;
        }
    }
    const double now = clockSeconds();
    {
        std::lock_guard lock(m_queueMutex);
        while (!m_queue.empty() && (m_queue.front().generation != generation || m_queue.front().time <= now)) {
            QueuedFrame q = m_queue.front();
            m_queue.pop_front();
            if (q.generation != generation) {
                av_frame_free(&q.frame);
                continue;
            }
            // A frame replaced before it reached the screen is a dropped frame.
            if (rs->current && !rs->currentShown)
                ++m_dropped;
            av_frame_free(&rs->current);
            rs->current = q.frame;
            rs->currentTime = q.time;
            rs->currentShown = false;
            m_position = q.position;
            changed = true;
        }
        // Decoder starvation: the next frame is overdue and nothing is queued.
        const bool late = m_clockRunning && rs->current && m_queue.empty()
                          && now > rs->currentTime + 2.0 * m_frameSeconds;
        if (late && !rs->stalled)
            ++m_stalls;
        rs->stalled = late;
    }
    m_queueCond.notify_one();
    if (!changed || !rs->current)
        return;

    m_window->beginExternalCommands();
    const diva::RectF full{0, 0, float(rs->size.width()), float(rs->size.height())};
    std::string error;
    if (!rs->renderer.draw(g_gpu->context(), rs->current, rs->target.Get(), rs->size.width(), rs->size.height(),
                           full, false, &error))
        qWarning() << "draw failed:" << error.c_str();
    m_window->endExternalCommands();
    rs->targetDirty = false;
    if (!rs->currentShown) {
        rs->currentShown = true;
        ++m_presented;
    }
}

void VideoItem::updateStats()
{
    const int64_t now = nowNs();
    const qint64 presented = m_presented;
    const double dt = (now - m_statsNs) * 1e-9;
    m_displayFps = dt > 0 ? (presented - m_statsPresented) / dt : 0;
    m_statsPresented = presented;
    m_statsNs = now;
    emit statsChanged();
}

QString VideoItem::benchReport() const
{
    const double seconds = (nowNs() - m_sourceStartNs) * 1e-9;
    const qint64 presented = m_presented;
    QString report;
    report += QStringLiteral("source: %1\n").arg(m_source.toLocalFile());
    report += QStringLiteral("video: %1x%2 %3 @ %4 fps\n")
                  .arg(m_videoSize.width())
                  .arg(m_videoSize.height())
                  .arg(m_codec)
                  .arg(m_fps, 0, 'f', 2);
    report += QStringLiteral("gpu: %1\n").arg(gpuName());
    report += QStringLiteral("played: %1 s\n").arg(seconds, 0, 'f', 2);
    report += QStringLiteral("frames shown: %1 (%2 fps, expected %3)\n")
                  .arg(presented)
                  .arg(presented / seconds, 0, 'f', 2)
                  .arg(m_fps, 0, 'f', 2);
    report += QStringLiteral("dropped frames: %1\n").arg(m_dropped.load());
    report += QStringLiteral("decoder stalls: %1\n").arg(m_stalls.load());
    if (!m_error.isEmpty())
        report += QStringLiteral("error: %1\n").arg(m_error);
    return report;
}
