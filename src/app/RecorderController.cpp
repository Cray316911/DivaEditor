// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecorderController.h"

#include "Privacy.h"
#include "ScreenRecorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJSEngine>
#include <QQuickWindow>
#include <QStandardPaths>

#include <windows.h>

#include <cmath>

namespace {

RecorderController *g_instance = nullptr;
constexpr int kHotkeyPause = 0xD1A1;
constexpr int kHotkeyStop = 0xD1A2;

} // namespace

RecorderController::RecorderController(QObject *parent) : QObject(parent)
{
    g_instance = this;
    m_statsTimer.setInterval(250);
    connect(&m_statsTimer, &QTimer::timeout, this, [this] {
        if (!m_recorder || !m_recorder->isRecording())
            return;
        const diva::RecorderStats s = m_recorder->stats();
        m_info = QStringLiteral("%1×%2 · %3 · %4 kare")
                     .arg(s.width)
                     .arg(s.height)
                     .arg(QString::fromStdString(s.encoder))
                     .arg(s.framesWritten);
        emit statsChanged();
    });
    QCoreApplication::instance()->installNativeEventFilter(this);
    refreshMonitors();
}

RecorderController::~RecorderController()
{
    if (m_recorder && m_recorder->isRecording())
        m_recorder->stop(nullptr);
    setHotkeys(false);
    g_instance = nullptr;
}

RecorderController *RecorderController::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

bool RecorderController::recording() const
{
    return m_recorder && m_recorder->isRecording();
}

bool RecorderController::paused() const
{
    return recording() && m_recorder->stats().paused;
}

double RecorderController::elapsed() const
{
    return recording() ? m_recorder->stats().seconds : 0;
}

void RecorderController::refreshMonitors()
{
    m_monitors.clear();
    int index = 0;
    for (const diva::MonitorInfo &m : diva::ScreenRecorder::monitors()) {
        m_monitors.append(QVariantMap{{"index", index++},
                                      {"name", QString::fromStdString(m.name)},
                                      {"x", m.x},
                                      {"y", m.y},
                                      {"width", m.width},
                                      {"height", m.height},
                                      {"refresh", privacyMode() ? 60 : m.refreshHz},
                                      {"primary", m.primary}});
    }
    emit monitorsChanged();
}

QString RecorderController::recordingsFolder() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/DivaStudio";
    QDir().mkpath(dir);
    return dir;
}

bool RecorderController::start(const QVariantMap &o)
{
    if (recording())
        return false;
    diva::RecorderSettings s;
    s.monitor = o.value("monitor", 0).toInt();
    s.region = {o.value("x", 0).toInt(), o.value("y", 0).toInt(), o.value("width", 0).toInt(),
                o.value("height", 0).toInt()};
    s.fps = o.value("fps", 60).toInt();
    s.codec = o.value("codec", "hevc").toString().toStdString();
    s.systemAudio = o.value("systemAudio", true).toBool();
    s.microphone = o.value("microphone", false).toBool();
    s.cursor = o.value("cursor", true).toBool();
    // Quality as bits per pixel; screen and game content needs more than camera footage.
    const QString quality = o.value("quality", "balanced").toString();
    const double bpp = quality == QLatin1String("low") ? 0.07 : quality == QLatin1String("high") ? 0.22 : 0.14;
    int w = s.region.width, h = s.region.height;
    if (w <= 0 && s.monitor < m_monitors.size()) {
        const QVariantMap m = m_monitors[s.monitor].toMap();
        w = m.value("width").toInt();
        h = m.value("height").toInt();
    }
    s.bitrate = int64_t(std::max(w, 640) * double(std::max(h, 360)) * s.fps * bpp * (s.codec == "h264" ? 1.4 : 1.0));

    m_currentFile = QDir(recordingsFolder())
                        .filePath(QStringLiteral("Kayıt %1.mkv")
                                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"))));
    s.outputPath = m_currentFile.toStdString();
    m_recorder = std::make_unique<diva::ScreenRecorder>();
    std::string error;
    if (!m_recorder->start(s, &error)) {
        m_recorder.reset();
        emit failed(QStringLiteral("Kayıt başlatılamadı: %1").arg(QString::fromStdString(error)));
        return false;
    }
    const diva::RecorderStats stats = m_recorder->stats();
    if (!stats.audioWarning.empty())
        emit failed(QStringLiteral("Ses uyarısı: %1").arg(QString::fromStdString(stats.audioWarning)));
    setHotkeys(true);
    m_statsTimer.start();
    emit stateChanged();
    return true;
}

void RecorderController::togglePause()
{
    if (!recording())
        return;
    if (m_recorder->stats().paused)
        m_recorder->resume();
    else
        m_recorder->pause();
    emit stateChanged();
}

void RecorderController::stop()
{
    if (!recording())
        return;
    std::string error;
    const bool ok = m_recorder->stop(&error);
    m_recorder.reset();
    m_statsTimer.stop();
    setHotkeys(false);
    m_lastFile = m_currentFile;
    emit stateChanged();
    if (ok)
        emit finished(m_lastFile);
    else
        emit failed(QStringLiteral("Kayıt sorunlu bitti: %1").arg(QString::fromStdString(error)));
}

void RecorderController::excludeFromCapture(QQuickWindow *window)
{
    if (window)
        SetWindowDisplayAffinity(reinterpret_cast<HWND>(window->winId()), WDA_EXCLUDEFROMCAPTURE);
}

void RecorderController::setHotkeys(bool on)
{
    if (on == m_hotkeys)
        return;
    if (on) {
        RegisterHotKey(nullptr, kHotkeyPause, MOD_NOREPEAT, VK_F9);
        RegisterHotKey(nullptr, kHotkeyStop, MOD_NOREPEAT, VK_F10);
    } else {
        UnregisterHotKey(nullptr, kHotkeyPause);
        UnregisterHotKey(nullptr, kHotkeyStop);
    }
    m_hotkeys = on;
}

bool RecorderController::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message != WM_HOTKEY)
        return false;
    if (msg->wParam == kHotkeyPause) {
        QMetaObject::invokeMethod(this, &RecorderController::togglePause, Qt::QueuedConnection);
        return true;
    }
    if (msg->wParam == kHotkeyStop) {
        QMetaObject::invokeMethod(this, &RecorderController::stop, Qt::QueuedConnection);
        return true;
    }
    return false;
}
