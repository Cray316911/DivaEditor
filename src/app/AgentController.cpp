// SPDX-License-Identifier: GPL-3.0-or-later
#include "AgentController.h"

#include "AgentTools.h"
#include "ProjectController.h"
#include "gpu/GpuDevice.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThreadPool>
#include <QTimer>

#include <windows.h>
#include <wincred.h>

namespace {

AgentController *g_instance = nullptr;

constexpr const char *kModel = "claude-opus-5-5";
constexpr const char *kApiUrl = "https://api.anthropic.com/v1/messages";
constexpr int kMaxTurns = 40;
constexpr int kMaxRetries = 4;
const wchar_t *const kCredentialTarget = L"DivaStudio/AnthropicApiKey";

// The key lives in Windows Credential Manager (encrypted per user), never in settings files.
QString readStoredKey()
{
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(kCredentialTarget, CRED_TYPE_GENERIC, 0, &cred))
        return {};
    const QString key = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob),
                                          int(cred->CredentialBlobSize));
    CredFree(cred);
    return key;
}

bool storeKey(const QString &key)
{
    QByteArray blob = key.toUtf8();
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(kCredentialTarget);
    cred.UserName = const_cast<LPWSTR>(L"DivaStudio");
    cred.CredentialBlobSize = DWORD(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&cred, 0);
}

QString envKey()
{
    return qEnvironmentVariable("ANTHROPIC_API_KEY");
}

bool editsProject(const QString &tool)
{
    return tool == QLatin1String("remove_ranges") || tool == QLatin1String("split_at")
           || tool == QLatin1String("set_transition") || tool == QLatin1String("undo");
}

QString describeInput(const QString &name, const QJsonObject &input)
{
    if (name == QLatin1String("get_timeline"))
        return QStringLiteral("Timeline inceleniyor");
    if (name == QLatin1String("scan_video"))
        return QStringLiteral("Video taranıyor (%1 sn aralıkla)").arg(input.value("interval_seconds").toDouble());
    if (name == QLatin1String("view_frames"))
        return QStringLiteral("Kareler yakından inceleniyor");
    if (name == QLatin1String("audio_levels"))
        return QStringLiteral("Ses seviyeleri ölçülüyor");
    if (name == QLatin1String("remove_ranges"))
        return QStringLiteral("Aralıklar siliniyor");
    if (name == QLatin1String("set_transition"))
        return QStringLiteral("Geçiş ekleniyor: %1").arg(input.value("type").toString());
    if (name == QLatin1String("export_video"))
        return QStringLiteral("Dışa aktarılıyor");
    return name;
}

} // namespace

AgentController::AgentController(QObject *parent) : QObject(parent)
{
    g_instance = this;
}

AgentController::~AgentController()
{
    m_cancel = true;
    if (m_reply)
        m_reply->abort();
    QThreadPool::globalInstance()->waitForDone();
    g_instance = nullptr;
}

AgentController *AgentController::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

bool AgentController::hasApiKey() const
{
    return !readStoredKey().isEmpty() || !envKey().isEmpty();
}

QString AgentController::keySource() const
{
    if (!readStoredKey().isEmpty())
        return QStringLiteral("Windows Kimlik Bilgileri");
    if (!envKey().isEmpty())
        return QStringLiteral("ANTHROPIC_API_KEY ortam değişkeni");
    return {};
}

QString AgentController::mcpCommand() const
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/divamcp.exe");
    // User scope: available from every folder, not only the one the command was typed in.
    return QStringLiteral("claude mcp add -s user divastudio -- \"%1\"").arg(exe);
}

QString AgentController::model() const
{
    return QString::fromLatin1(kModel);
}

bool AgentController::setApiKey(const QString &key)
{
    const QString trimmed = key.trimmed();
    if (!trimmed.startsWith(QLatin1String("sk-ant-"))) {
        addLog(QStringLiteral("error"), QStringLiteral("Bu bir Anthropic API anahtarına benzemiyor (sk-ant- ile başlamalı)."));
        return false;
    }
    const bool ok = storeKey(trimmed);
    emit keyChanged();
    return ok;
}

void AgentController::clearApiKey()
{
    CredDeleteW(kCredentialTarget, CRED_TYPE_GENERIC, 0);
    emit keyChanged();
}

void AgentController::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void AgentController::clearLog()
{
    m_log.clear();
    emit logChanged();
}

void AgentController::addLog(const QString &kind, const QString &text)
{
    qInfo().noquote() << "DivaAI" << kind << ":" << text;
    m_log.append(QVariantMap{{"kind", kind},
                             {"text", text},
                             {"time", QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))}});
    emit logChanged();
}

void AgentController::run(const QString &prompt)
{
    if (m_running || prompt.trimmed().isEmpty())
        return;
    ProjectController *projects = ProjectController::instance();
    if (!projects || projects->project().clips().isEmpty()) {
        addLog(QStringLiteral("error"), QStringLiteral("Önce bir video içe aktarın veya kayıt yapın."));
        return;
    }
    m_apiKey = readStoredKey();
    if (m_apiKey.isEmpty())
        m_apiKey = envKey();
    if (m_apiKey.isEmpty()) {
        addLog(QStringLiteral("error"), QStringLiteral("API anahtarı yok. Anthropic Console'dan bir anahtar alıp buraya girin."));
        return;
    }
    if (!m_gpu) {
        std::string error;
        m_gpu = diva::GpuDevice::create(&error);
        if (!m_gpu) {
            addLog(QStringLiteral("error"), QString::fromStdString(error));
            return;
        }
    }
    m_work = diva::Project();
    m_work.fromJson(projects->project().toJson(), {}, nullptr);
    m_work.name = projects->project().name;
    m_tools = std::make_unique<diva::AgentTools>(&m_work, m_gpu);

    m_messages = QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt.trimmed()}}};
    m_turns = 0;
    m_retries = 0;
    m_cancel = false;
    m_running = true;
    projects->setLocked(true);
    emit runningChanged();
    addLog(QStringLiteral("user"), prompt.trimmed());
    sendRequest();
}

void AgentController::cancel()
{
    if (!m_running)
        return;
    m_cancel = true;
    if (m_reply)
        m_reply->abort();
}

void AgentController::sendRequest()
{
    if (m_cancel) {
        finish(QStringLiteral("Durduruldu."), false);
        return;
    }
    if (++m_turns > kMaxTurns) {
        finish(QStringLiteral("Adım sınırına ulaşıldı (%1). Yapılan düzenlemeler projede.").arg(kMaxTurns), true);
        return;
    }
    const QJsonObject body{
        {"model", kModel},
        {"max_tokens", 16000},
        {"thinking", QJsonObject{{"type", "adaptive"}, {"display", "summarized"}}},
        {"output_config", QJsonObject{{"effort", "high"}}},
        // Server-side fallback if the model declines a request (recommended for this model).
        {"fallbacks", "default"},
        // Cache tools, system prompt and history between the many turns of one run.
        {"cache_control", QJsonObject{{"type", "ephemeral"}}},
        {"system", QJsonArray{QJsonObject{{"type", "text"}, {"text", diva::AgentTools::systemPrompt()}}}},
        {"tools", diva::AgentTools::definitions(false)},
        {"messages", m_messages},
    };
    QNetworkRequest request{QUrl(QString::fromLatin1(kApiUrl))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2026-10-21");
    request.setRawHeader("anthropic-beta", "server-side-fallback-2026-07-01");
    request.setTransferTimeout(10 * 60 * 1000);
    addLog(QStringLiteral("status"), QStringLiteral("DivaAI düşünüyor…"));
    m_reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] { handleReply(reply); });
}

void AgentController::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (m_cancel) {
        finish(QStringLiteral("Durduruldu."), false);
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    const QJsonObject json = QJsonDocument::fromJson(data).object();
    if (status != 200) {
        const QString apiMessage = json.value("error").toObject().value("message").toString();
        const bool retryable = status == 429 || status >= 500 || status == 0;
        if (retryable && m_retries < kMaxRetries) {
            ++m_retries;
            const int retryAfter = reply->rawHeader("retry-after").toInt();
            const int delayMs = retryAfter > 0 ? retryAfter * 1000 : 2000 * (1 << m_retries);
            addLog(QStringLiteral("info"), QStringLiteral("Sunucu meşgul (%1), %2 sn sonra yeniden denenecek.")
                                               .arg(status)
                                               .arg(delayMs / 1000));
            --m_turns;
            QTimer::singleShot(delayMs, this, &AgentController::sendRequest);
            return;
        }
        QString message = apiMessage.isEmpty() ? reply->errorString() : apiMessage;
        if (status == 401)
            message = QStringLiteral("API anahtarı geçersiz (401). Anahtarı kontrol edin.");
        else if (status == 403)
            message = QStringLiteral("Erişim reddedildi (403): %1").arg(apiMessage);
        finish(QStringLiteral("Claude API hatası: %1").arg(message), true);
        return;
    }
    m_retries = 0;

    const QJsonArray content = json.value("content").toArray();
    QJsonArray toolUses;
    QString lastText;
    for (const QJsonValue &v : content) {
        const QJsonObject block = v.toObject();
        const QString type = block.value("type").toString();
        if (type == QLatin1String("thinking")) {
            const QString summary = block.value("thinking").toString().trimmed();
            if (!summary.isEmpty())
                addLog(QStringLiteral("thinking"), summary);
        } else if (type == QLatin1String("text")) {
            lastText = block.value("text").toString().trimmed();
            if (!lastText.isEmpty())
                addLog(QStringLiteral("text"), lastText);
        } else if (type == QLatin1String("tool_use")) {
            toolUses.append(block);
        } else if (type == QLatin1String("fallback")) {
            addLog(QStringLiteral("info"), QStringLiteral("Yedek modele geçildi."));
        }
    }
    // Send the assistant turn back exactly as received (thinking blocks included).
    m_messages.append(QJsonObject{{"role", "assistant"}, {"content", content}});

    const QString stop = json.value("stop_reason").toString();
    if (stop == QLatin1String("tool_use") && !toolUses.isEmpty()) {
        runTools(toolUses);
    } else if (stop == QLatin1String("end_turn")) {
        finish(lastText.isEmpty() ? QStringLiteral("Bitti.") : QString(), false);
    } else if (stop == QLatin1String("refusal")) {
        finish(QStringLiteral("İstek model tarafından reddedildi."), true);
    } else if (stop == QLatin1String("max_tokens")) {
        finish(QStringLiteral("Yanıt uzunluk sınırına ulaştı; isteği daha küçük parçalara bölün."), true);
    } else {
        sendRequest();
    }
}

void AgentController::runTools(const QJsonArray &toolUses)
{
    for (const QJsonValue &v : toolUses) {
        const QJsonObject use = v.toObject();
        addLog(QStringLiteral("tool"), describeInput(use.value("name").toString(), use.value("input").toObject()));
    }
    QThreadPool::globalInstance()->start([this, toolUses] {
        QJsonArray results;
        for (const QJsonValue &v : toolUses) {
            const QJsonObject use = v.toObject();
            const QString name = use.value("name").toString();
            diva::ToolResult r;
            if (m_cancel) {
                r.isError = true;
                r.data = QJsonObject{{"error", "cancelled by the user"}};
                r.summary = QStringLiteral("İptal edildi");
            } else {
                r = m_tools->call(name, use.value("input").toObject());
            }
            QJsonArray parts{QJsonObject{
                {"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(r.data).toJson(QJsonDocument::Compact))}}};
            for (const QImage &image : r.images)
                parts.append(QJsonObject{{"type", "image"},
                                         {"source", QJsonObject{{"type", "base64"},
                                                                {"media_type", "image/jpeg"},
                                                                {"data", QString::fromLatin1(diva::jpegBase64(image))}}}});
            QJsonObject result{{"type", "tool_result"}, {"tool_use_id", use.value("id")}, {"content", parts}};
            if (r.isError)
                result["is_error"] = true;
            results.append(result);

            const QJsonObject snapshot = (!r.isError && editsProject(name)) ? m_work.toJson() : QJsonObject();
            QMetaObject::invokeMethod(this, [this, r, snapshot] {
                addLog(r.isError ? QStringLiteral("error") : QStringLiteral("result"), r.summary);
                if (!snapshot.isEmpty() && ProjectController::instance())
                    ProjectController::instance()->applyAgentSnapshot(snapshot, QStringLiteral("DivaAI düzenlemesi"));
            });
        }
        QMetaObject::invokeMethod(this, [this, results] {
            // All results of one turn go back in a single user message.
            m_messages.append(QJsonObject{{"role", "user"}, {"content", results}});
            sendRequest();
        });
    });
}

void AgentController::finish(const QString &message, bool error)
{
    if (!message.isEmpty())
        addLog(error ? QStringLiteral("error") : QStringLiteral("done"), message);
    else
        addLog(QStringLiteral("done"), QStringLiteral("DivaAI işini bitirdi. Tüm düzenlemeler Geri Al (Ctrl+Z) ile geri alınabilir."));
    m_running = false;
    m_apiKey.clear();
    if (ProjectController::instance())
        ProjectController::instance()->setLocked(false);
    emit runningChanged();
}
