// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QQuickItem>
#include <QSize>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

struct AVFrame;

namespace diva {
class GpuDevice;
class HwDecoder;
}

// Canvas preview: a decode thread keeps a few GPU frames queued, and the render
// thread draws the one that is due straight from the decoder surface into a
// texture the scene graph shows. Frames never leave VRAM.
class VideoItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY sourceChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY sourceChanged)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY sourceChanged)
    Q_PROPERTY(QString codec READ codec NOTIFY sourceChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY sourceChanged)
    Q_PROPERTY(QString gpuName READ gpuName CONSTANT)
    Q_PROPERTY(double displayFps READ displayFps NOTIFY statsChanged)
    Q_PROPERTY(qint64 droppedFrames READ droppedFrames NOTIFY statsChanged)
    Q_PROPERTY(qint64 stalls READ stalls NOTIFY statsChanged)

public:
    explicit VideoItem(QQuickItem *parent = nullptr);
    ~VideoItem() override;

    static void setSharedGpu(std::shared_ptr<diva::GpuDevice> gpu);

    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    bool playing() const { return m_playing; }
    void setPlaying(bool playing);
    double position() const { return m_position.load(); }
    double duration() const { return m_duration; }
    QSize videoSize() const { return m_videoSize; }
    double frameRate() const { return m_fps; }
    QString codec() const { return m_codec; }
    QString errorString() const { return m_error; }
    QString gpuName() const;
    double displayFps() const { return m_displayFps; }
    qint64 droppedFrames() const { return m_dropped.load(); }
    qint64 stalls() const { return m_stalls.load(); }

    Q_INVOKABLE void togglePlaying() { setPlaying(!m_playing); }
    Q_INVOKABLE void seek(double seconds);
    // Plain-text playback report for --bench.
    QString benchReport() const;

signals:
    void sourceChanged();
    void playingChanged();
    void positionChanged();
    void statsChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    struct RenderState;
    struct QueuedFrame
    {
        AVFrame *frame;
        double time;     // on the playback clock (keeps growing across loops)
        double position; // within the file
        int generation;
    };

    void decodeLoop();
    void stopDecoder();
    void renderFrame();
    void updateStats();
    double clockSeconds() const;

    QUrl m_source;
    std::atomic<bool> m_playing{false};
    double m_duration = 0;
    double m_fps = 0;
    QSize m_videoSize;
    QString m_codec;
    QString m_error;

    std::unique_ptr<diva::HwDecoder> m_decoder;
    std::thread m_thread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCond;
    std::deque<QueuedFrame> m_queue;
    bool m_stop = false;
    std::atomic<int> m_generation{0};
    double m_seekTarget = 0; // decode thread drops frames before this after a seek

    // Playback clock: media time = now - base while running, else the paused time.
    // After a source change it starts when the first frame reaches the screen.
    std::atomic<bool> m_clockRunning{false};
    std::atomic<bool> m_startOnFirstFrame{false};
    std::atomic<int64_t> m_clockBaseNs{0};
    std::atomic<int64_t> m_pausedNs{0};
    std::atomic<double> m_frameSeconds{1.0 / 60};

    std::atomic<double> m_position{0};
    std::atomic<qint64> m_presented{0};
    std::atomic<qint64> m_dropped{0};
    std::atomic<qint64> m_stalls{0};
    double m_displayFps = 0;
    qint64 m_statsPresented = 0;
    int64_t m_statsNs = 0;
    std::atomic<int64_t> m_sourceStartNs{0};
    int m_tick = 0;
    QTimer m_uiTimer;

    QQuickWindow *m_window = nullptr;
    std::unique_ptr<RenderState> m_render; // render thread only
};
