// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Project.h"

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

class QNetworkReply;
class QQmlEngine;
class QJSEngine;

namespace diva {
class AgentTools;
class GpuDevice;
}

// DivaAI inside the app: a Claude agent (Claude API, user's own API key) that edits the open
// project with AgentTools. The model looks at the footage through contact sheets, decides
// what to cut and which transitions fit, and applies the edits; every edit lands in the
// project as an undo step. Claude Code users can drive the same tools through divamcp.
class AgentController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY keyChanged)
    Q_PROPERTY(QString keySource READ keySource NOTIFY keyChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QVariantList log READ log NOTIFY logChanged)
    Q_PROPERTY(QString mcpCommand READ mcpCommand CONSTANT)
    Q_PROPERTY(QString model READ model CONSTANT)

public:
    explicit AgentController(QObject *parent = nullptr);
    ~AgentController() override;
    static AgentController *create(QQmlEngine *, QJSEngine *);

    bool hasApiKey() const;
    QString keySource() const;
    bool running() const { return m_running; }
    QVariantList log() const { return m_log; }
    QString mcpCommand() const;
    QString model() const;

    Q_INVOKABLE bool setApiKey(const QString &key);
    Q_INVOKABLE void clearApiKey();
    Q_INVOKABLE void run(const QString &prompt);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE void copyText(const QString &text);

signals:
    void keyChanged();
    void runningChanged();
    void logChanged();

private:
    void addLog(const QString &kind, const QString &text);
    void sendRequest();
    void handleReply(QNetworkReply *reply);
    void runTools(const QJsonArray &toolUses);
    void finish(const QString &message, bool error);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QString m_apiKey;
    QJsonArray m_messages;
    QVariantList m_log;
    bool m_running = false;
    std::atomic<bool> m_cancel{false};
    int m_turns = 0;
    int m_retries = 0;

    diva::Project m_work; // the copy DivaAI edits (tool thread only while running)
    std::shared_ptr<diva::GpuDevice> m_gpu;
    std::unique_ptr<diva::AgentTools> m_tools;
};
