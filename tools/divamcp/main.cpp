// SPDX-License-Identifier: GPL-3.0-or-later
//
// divamcp: MCP server (stdio) that lets Claude Code edit a DivaStudio project with the same
// tools DivaAI uses inside the app. Register it once:
//
//   claude mcp add -s user divastudio -- "C:\path\to\divamcp.exe"
//
// It works on the project last saved in DivaStudio, or on --project <file.divaproj>
// (add --import <video> to create that project from a recording). Edits are saved to the
// project file right away, and DivaStudio reloads it.

#include "AgentTools.h"
#include "Project.h"
#include "gpu/GpuDevice.h"

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSettings>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <iostream>
#include <string>

using namespace diva;

namespace {

void send(const QJsonObject &message)
{
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
}

void reply(const QJsonValue &id, const QJsonObject &result)
{
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

void replyError(const QJsonValue &id, int code, const QString &message)
{
    send({{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}});
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("DivaStudio"));
    QGuiApplication::setOrganizationName(QStringLiteral("DivaStudio"));
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    QCommandLineParser parser;
    const QCommandLineOption projectOption(QStringLiteral("project"), QStringLiteral("Project file."),
                                           QStringLiteral("file"));
    const QCommandLineOption importOption(QStringLiteral("import"), QStringLiteral("Create the project from a video."),
                                          QStringLiteral("video"));
    parser.addOptions({projectOption, importOption});
    parser.process(app);

    QString projectPath = parser.value(projectOption);
    if (projectPath.isEmpty())
        projectPath = QSettings().value(QStringLiteral("currentProject")).toString();

    Project project;
    QString startupError;
    if (parser.isSet(importOption)) {
        MediaInfo info;
        if (probeMedia(parser.value(importOption), &info, &startupError)) {
            project.importMedia(info);
            if (!projectPath.isEmpty())
                project.save(projectPath, &startupError);
        }
    } else if (projectPath.isEmpty()) {
        startupError = QStringLiteral("No project: save a project in DivaStudio first, or pass --project.");
    } else if (!project.load(projectPath, &startupError)) {
        startupError = QStringLiteral("Cannot open %1: %2").arg(projectPath, startupError);
    }
    if (!startupError.isEmpty())
        std::fprintf(stderr, "divamcp: %s\n", startupError.toUtf8().constData());

    std::string gpuError;
    auto gpu = GpuDevice::create(&gpuError);
    AgentTools tools(&project, gpu);
    tools.onProjectChanged = [&] {
        QString error;
        if (!projectPath.isEmpty() && !project.save(projectPath, &error))
            std::fprintf(stderr, "divamcp: save failed: %s\n", error.toUtf8().constData());
    };

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty() || line == "\r")
            continue;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(line), &parseError);
        if (!doc.isObject()) {
            replyError(QJsonValue::Null, -32700, parseError.errorString());
            continue;
        }
        const QJsonObject msg = doc.object();
        const QString method = msg.value("method").toString();
        const QJsonValue id = msg.value("id");
        const QJsonObject params = msg.value("params").toObject();
        const bool isRequest = msg.contains("id");

        if (method == QLatin1String("initialize")) {
            const QString version = params.value("protocolVersion").toString(QStringLiteral("2025-06-18"));
            reply(id, {{"protocolVersion", version},
                       {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
                       {"serverInfo", QJsonObject{{"name", "divastudio"}, {"version", DIVA_VERSION}}},
                       {"instructions", AgentTools::systemPrompt()}});
        } else if (method == QLatin1String("tools/list")) {
            reply(id, {{"tools", AgentTools::definitions(true)}});
        } else if (method == QLatin1String("tools/call")) {
            if (!startupError.isEmpty() && project.clips().isEmpty()) {
                reply(id, {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", startupError}}}},
                           {"isError", true}});
                continue;
            }
            const ToolResult r = tools.call(params.value("name").toString(), params.value("arguments").toObject());
            QJsonArray content{QJsonObject{
                {"type", "text"},
                {"text", QString::fromUtf8(QJsonDocument(r.data).toJson(QJsonDocument::Compact))}}};
            for (const QImage &image : r.images)
                content.append(QJsonObject{{"type", "image"},
                                           {"data", QString::fromLatin1(jpegBase64(image))},
                                           {"mimeType", "image/jpeg"}});
            reply(id, {{"content", content}, {"isError", r.isError}});
        } else if (method == QLatin1String("ping")) {
            reply(id, {});
        } else if (isRequest) {
            replyError(id, -32601, QStringLiteral("Method not found: %1").arg(method));
        }
        // Notifications (no id) need no answer.
    }
    return 0;
}
