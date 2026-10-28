// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Project.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <functional>
#include <map>
#include <memory>

namespace diva {

class GpuDevice;
class FrameSource;

struct ToolResult
{
    QJsonObject data;      // sent to the model as JSON text
    QList<QImage> images;  // contact sheets, sent as JPEG images
    bool isError = false;
    QString summary;       // one line for the UI log
};

// The editing tools DivaAI works with. They operate on a Project and are shared by the
// in-app agent (Claude API) and the MCP server (Claude Code), so both see the same tools.
class AgentTools
{
public:
    AgentTools(Project *project, std::shared_ptr<GpuDevice> gpu);
    ~AgentTools();

    // Tool definitions; `mcp` switches the schema key to MCP's "inputSchema".
    static QJsonArray definitions(bool mcp = false);
    static QString systemPrompt();

    ToolResult call(const QString &name, const QJsonObject &input);

    // Called after any edit so the UI can refresh / the MCP server can save.
    std::function<void()> onProjectChanged;
    // Render progress for export_video (fraction 0..1).
    std::function<void(double)> onRenderProgress;

    QJsonObject timelineState() const;

private:
    ToolResult scanVideo(const QJsonObject &in);
    ToolResult viewFrames(const QJsonObject &in);
    ToolResult audioLevels(const QJsonObject &in);
    ToolResult removeRanges(const QJsonObject &in);
    ToolResult splitAt(const QJsonObject &in);
    ToolResult setTransition(const QJsonObject &in);
    ToolResult undo();
    ToolResult exportVideo(const QJsonObject &in);
    ToolResult readReferenceProject(const QJsonObject &in);
    ToolResult applyReferenceEdits(const QJsonObject &in);

    struct Sample
    {
        double time;
        QImage image;
    };
    QList<Sample> sample(const QList<double> &times, bool precise, QString *error);
    QImage contactSheet(const QList<Sample> &samples) const;
    FrameSource *sourceFor(const QString &mediaId, QString *error);

    struct TileRenderer;

    Project *m_project;
    std::shared_ptr<GpuDevice> m_gpu;
    std::unique_ptr<TileRenderer> m_tiles;
    std::map<QString, std::unique_ptr<FrameSource>> m_sources;
};

QString formatTime(double seconds);
// JPEG (base64) for images sent to the model.
QByteArray jpegBase64(const QImage &image);

} // namespace diva
