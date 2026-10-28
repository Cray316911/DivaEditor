// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace diva {
class ScreenRecorder;
}

// Screen recording for QML: monitor list, start/pause/stop, live stats, and the F9 / F10
// global hotkeys (pause / stop, as Camtasia users expect) while a recording runs.
class RecorderController : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList monitors READ monitors NOTIFY monitorsChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(double elapsed READ elapsed NOTIFY statsChanged)
    Q_PROPERTY(QString info READ info NOTIFY statsChanged)
    Q_PROPERTY(QString lastFile READ lastFile NOTIFY stateChanged)

public:
    explicit RecorderController(QObject *parent = nullptr);
    ~RecorderController() override;
    static RecorderController *create(QQmlEngine *, QJSEngine *);

    QVariantList monitors() const { return m_monitors; }
    bool recording() const;
    bool paused() const;
    double elapsed() const;
    QString info() const { return m_info; }
    QString lastFile() const { return m_lastFile; }

    Q_INVOKABLE void refreshMonitors();
    // options: monitor, x, y, width, height (region; width 0 = full), fps, codec, quality
    // (low | balanced | high), systemAudio, microphone, cursor
    Q_INVOKABLE bool start(const QVariantMap &options);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void stop();
    // Keeps a window (the recording bar) out of the recording itself.
    Q_INVOKABLE void excludeFromCapture(QQuickWindow *window);
    Q_INVOKABLE QString recordingsFolder() const;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void monitorsChanged();
    void stateChanged();
    void statsChanged();
    void finished(const QString &path);
    void failed(const QString &message);

private:
    void setHotkeys(bool on);

    std::unique_ptr<diva::ScreenRecorder> m_recorder;
    QVariantList m_monitors;
    QTimer m_statsTimer;
    QString m_info;
    QString m_lastFile;
    QString m_currentFile;
    bool m_hotkeys = false;
};
