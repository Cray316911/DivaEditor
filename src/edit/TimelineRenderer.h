// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Project.h"

#include <QString>

#include <functional>

namespace diva {

struct RenderSettings
{
    QString outputPath;
    int width = 0;   // 0 = project size
    int height = 0;
    double fps = 0;  // 0 = project frame rate
    QString codec = QStringLiteral("hevc");
    QString backend = QStringLiteral("auto");
    QString preset = QStringLiteral("balanced");
    qint64 bitrate = 0; // 0 = picked from size and frame rate
};

struct RenderStatus
{
    double fraction = 0;
    double fps = 0;
    double etaSeconds = 0;
    QString encoder;
};

// Return false to cancel.
using RenderProgress = std::function<bool(const RenderStatus &)>;

// Renders the project timeline (video with transitions, mixed audio) to a file, all on the GPU:
// decode -> NV12->RGB -> transition blend -> RGB->NV12 -> hardware encoder (CPU fallback).
bool renderTimeline(const Project &project, const RenderSettings &settings, const RenderProgress &progress,
                    QString *error);

} // namespace diva
