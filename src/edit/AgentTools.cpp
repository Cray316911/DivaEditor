// SPDX-License-Identifier: GPL-3.0-or-later
#include "AgentTools.h"

#include "AudioReader.h"
#include "FrameSource.h"
#include "TimelineRenderer.h"
#include "compositor/Nv12Renderer.h"
#include "gpu/GpuDevice.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
}

namespace diva {

namespace {

constexpr int kTileW = 320;
constexpr int kTileH = 180;
constexpr int kLabelH = 22;
constexpr int kColumns = 4;
constexpr int kPerSheet = 16;
constexpr int kMaxScanFrames = 96;

double round3(double v)
{
    return std::round(v * 1000.0) / 1000.0;
}

QJsonObject schema(const QJsonObject &properties, const QStringList &required = {})
{
    QJsonObject s{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
    if (!required.isEmpty())
        s["required"] = QJsonArray::fromStringList(required);
    return s;
}

QJsonObject number(const QString &description)
{
    return {{"type", "number"}, {"description", description}};
}

ToolResult errorResult(const QString &message)
{
    ToolResult r;
    r.isError = true;
    r.data = {{"error", message}};
    r.summary = message;
    return r;
}

} // namespace

QString formatTime(double seconds)
{
    if (seconds < 0)
        seconds = 0;
    const int m = int(seconds / 60);
    const double s = seconds - m * 60;
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 5, 'f', 2, QLatin1Char('0'));
}

QByteArray jpegBase64(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG", 78);
    return bytes.toBase64();
}

// Draws decoded frames into small RGBA tiles and reads them back for contact sheets.
struct AgentTools::TileRenderer
{
    Nv12Renderer renderer;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11Texture2D> staging;
    bool ready = false;

    bool init(GpuDevice &gpu, std::string *error)
    {
        if (!renderer.init(gpu.device(), error))
            return false;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = kTileW;
        td.Height = kTileH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(gpu.device()->CreateTexture2D(&td, nullptr, &target))
            || FAILED(gpu.device()->CreateRenderTargetView(target.Get(), nullptr, &rtv)))
            return false;
        td.BindFlags = 0;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ready = SUCCEEDED(gpu.device()->CreateTexture2D(&td, nullptr, &staging));
        return ready;
    }

    QImage render(GpuDevice &gpu, const AVFrame *frame)
    {
        ID3D11DeviceContext *ctx = gpu.context();
        const RectF dst = Nv12Renderer::fit(frame->width, frame->height, kTileW, kTileH);
        renderer.draw(ctx, frame, rtv.Get(), kTileW, kTileH, dst, true);
        ctx->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return {};
        QImage image(kTileW, kTileH, QImage::Format_ARGB32);
        for (int y = 0; y < kTileH; ++y)
            std::memcpy(image.scanLine(y), static_cast<const uchar *>(mapped.pData) + size_t(y) * mapped.RowPitch,
                        size_t(kTileW) * 4);
        ctx->Unmap(staging.Get(), 0);
        return image.convertToFormat(QImage::Format_RGB32);
    }
};

AgentTools::AgentTools(Project *project, std::shared_ptr<GpuDevice> gpu)
    : m_project(project), m_gpu(std::move(gpu)), m_tiles(std::make_unique<TileRenderer>())
{
}

AgentTools::~AgentTools() = default;

QJsonArray AgentTools::definitions(bool mcp)
{
    QJsonArray transitionIds{"none"};
    QString transitionHelp;
    for (const TransitionKind &k : transitionKinds()) {
        transitionIds.append(QString::fromLatin1(k.id));
        transitionHelp += QStringLiteral("\n- %1: %2").arg(QString::fromLatin1(k.id), QString::fromUtf8(k.description));
    }
    const QString key = mcp ? QStringLiteral("inputSchema") : QStringLiteral("input_schema");
    auto tool = [&](const QString &name, const QString &description, const QJsonObject &inputSchema) {
        return QJsonObject{{"name", name}, {"description", description}, {key, inputSchema}};
    };
    const QJsonObject range{{"type", "object"},
                            {"properties", QJsonObject{{"start", number("Start, timeline seconds.")},
                                                       {"end", number("End, timeline seconds.")}}},
                            {"required", QJsonArray{"start", "end"}},
                            {"additionalProperties", false}};
    return {
        tool("get_timeline",
             "Returns the project: duration, canvas, every clip with its timeline position and source range, "
             "and the transition at each cut. Call it first, and again after edits since times shift.",
             schema({})),
        tool("scan_video",
             "Overview of the timeline as contact sheets (4x4 thumbnails, each labelled with its timeline time). "
             "Use it to find where things happen before deciding what to cut. Up to 96 frames per call; for a long "
             "video scan it in parts. Intervals of 2 s or more show the nearest keyframe (fast), so labels are the "
             "actual frame times.",
             schema({{"start", number("Timeline seconds to start from (default 0).")},
                     {"end", number("Timeline seconds to stop at (default: end).")},
                     {"interval_seconds", number("Seconds between frames, 0.25 to 30.")}},
                    {"interval_seconds"})),
        tool("view_frames",
             "Frame-accurate close-up of up to 16 moments as one contact sheet. Use it to pin down where a moment "
             "starts and ends (give times, or start/end/count for an even spread), and to check cuts after editing.",
             schema({{"times", QJsonObject{{"type", "array"},
                                           {"items", QJsonObject{{"type", "number"}}},
                                           {"description", "Timeline seconds, at most 16."}}},
                     {"start", number("With end and count: first time.")},
                     {"end", number("With start and count: last time.")},
                     {"count", QJsonObject{{"type", "integer"}, {"description", "2 to 16."}}}})),
        tool("audio_levels",
             "Loudness (RMS dBFS, -90 = silence) of the mixed audio in steps. Helps find silences, loud moments, "
             "explosions or death sounds.",
             schema({{"start", number("Timeline seconds (default 0).")},
                     {"end", number("Timeline seconds (default: end).")},
                     {"step_seconds", number("Window length, 0.1 to 10.")}},
                    {"step_seconds"})),
        tool("remove_ranges",
             "Ripple-deletes timeline ranges (later material moves up). Give every range in current timeline "
             "times in ONE call; order does not matter. Afterwards all later times shift, so call get_timeline "
             "before further edits.",
             schema({{"ranges", QJsonObject{{"type", "array"}, {"items", range}}},
                     {"reason", QJsonObject{{"type", "string"}, {"description", "Short note shown to the user."}}}},
                    {"ranges"})),
        tool("split_at", "Cuts the clip under the given timeline time into two clips.",
             schema({{"time", number("Timeline seconds.")}}, {"time"})),
        tool("set_transition",
             "Sets the transition at one cut (the cut before clip clip_index, which is >= 1) or at every cut "
             "(all_cuts true). A transition overlaps the two clips by its duration. Types:" + transitionHelp
                 + "\n- none: hard cut",
             schema({{"clip_index", QJsonObject{{"type", "integer"}, {"description", "Incoming clip index (>= 1)."}}},
                     {"all_cuts", QJsonObject{{"type", "boolean"}}},
                     {"type", QJsonObject{{"type", "string"}, {"enum", transitionIds}}},
                     {"duration", number("Seconds, 0.1 to 3. Typical 0.3-0.8.")}},
                    {"type"})),
        tool("undo", "Undoes the last edit.", schema({})),
        tool("read_reference_project",
             "Reads another DivaStudio project (.divaproj) the user wants to copy edits from: its kept and removed "
             "source ranges, the transitions it used, and contact sheets showing what was cut. Call it when the "
             "user refers to edits made in another project or video.",
             schema({{"path", QJsonObject{{"type", "string"}, {"description", "Path of the .divaproj file."}}}},
                    {"path"})),
        tool("apply_reference_edits",
             "Rebuilds this timeline with exactly the reference project's edits: the same kept source ranges and "
             "transitions. mode 'exact' uses the same timestamps (same footage or same structure, e.g. intro and "
             "outro trims); 'scaled' stretches them to this video's length. When the content is different (the "
             "moments to cut happen at other times here), do NOT use this: find the equivalent moments yourself "
             "and recreate the same kind of edit with the same transitions.",
             schema({{"path", QJsonObject{{"type", "string"}}},
                     {"mode", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"exact", "scaled"}}}}},
                    {"path", "mode"})),
        tool("export_video",
             "Renders the edited timeline to a video file. Only call this when the user asked for an export.",
             schema({{"path", QJsonObject{{"type", "string"}, {"description", "Output .mp4 path (optional)."}}},
                     {"resolution", QJsonObject{{"type", "string"},
                                                {"enum", QJsonArray{"project", "720p", "1080p", "1440p", "2160p"}}}},
                     {"fps", number("30, 60 or 120 (default: project).")},
                     {"codec", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"h264", "hevc", "av1"}}}}})),
    };
}

QString AgentTools::systemPrompt()
{
    return QStringLiteral(R"(You are DivaAI, the built-in video editor of DivaStudio, a screen recording and video editing app. You edit like an experienced professional: you look at the footage before you cut, you choose transitions with taste, and you never remove more than the request calls for.

How this editor works
- One main track: clips play back to back; each clip's audio follows its picture.
- remove_ranges ripple-deletes, so everything after a removed range moves earlier.
- A transition sits on a cut and overlaps the two clips by its duration (the video gets slightly shorter).

How to work
1. Call get_timeline.
2. Find the moments with your eyes: scan_video across the whole timeline (about 2 s apart for videos under 10 minutes, 4-6 s for longer ones), then view_frames around each candidate to find where it really starts and ends, to about 0.25 s. audio_levels helps with loud events and silences.
3. Remove all ranges in one remove_ranges call, using the times you saw. Then call get_timeline again, because times have shifted.
4. Transitions: if the user wants them but did not say which, pick what suits the content and pacing, and keep one style throughout. Gaming: flash, zoom or dissolve at 0.3-0.6 s. Tutorials and talks: dissolve at 0.4-0.8 s. A clear jump in time: fade_black.
5. Check your work: view_frames across each new cut to confirm nothing wanted was lost and nothing unwanted remains.
6. Do not export unless asked.

Copying edits from another project
- When the user wants this video edited like another project, call read_reference_project first to see what was removed there (look at the contact sheets) and which transitions were used.
- Same footage or the same structure (e.g. the same intro and outro trims): apply_reference_edits with mode "exact" (or "scaled" for a different length).
- Different footage (the moments happen at other times here, e.g. deaths in another match): find the equivalent moments in this video with scan_video and view_frames, remove them, and use the same transition type and duration as the reference.

Judgement
- When unsure whether a moment matches the request, look at more frames. If it is still unclear, keep it and say so.
- Deaths in games: look for death or elimination screens ("YOU DIED", "ÖLDÜN", "Eliminated", "Wasted"), grey or red tint, kill-cams, spectator view, respawn countdowns. A death segment runs from the moment of death until the player is back in control.

Finish with a short summary in the user's language (they usually write Turkish): what you removed with the original times, which transition you chose and why, and the new duration.)");
}

QJsonObject AgentTools::timelineState() const
{
    QJsonArray clips;
    for (const PlacedClip &p : m_project->layout()) {
        QJsonObject o{{"index", p.index},
                      {"timeline_start", round3(p.start)},
                      {"timeline_end", round3(p.end)},
                      {"source_in", round3(p.clip.in)},
                      {"source_out", round3(p.clip.out)},
                      {"media", p.clip.mediaId}};
        if (!p.clip.transitionIn.type.isEmpty())
            o["transition_in"] =
                QJsonObject{{"type", p.clip.transitionIn.type}, {"duration", round3(p.clip.transitionIn.duration)}};
        clips.append(o);
    }
    QJsonArray media;
    for (const MediaInfo &m : m_project->media())
        media.append(QJsonObject{{"id", m.id},
                                 {"file", QFileInfo(m.path).fileName()},
                                 {"duration", round3(m.duration)},
                                 {"size", QStringLiteral("%1x%2").arg(m.width).arg(m.height)},
                                 {"fps", round3(m.fps)},
                                 {"audio_streams", m.audioStreams}});
    return {{"duration", round3(m_project->duration())},
            {"canvas", QStringLiteral("%1x%2 @ %3 fps").arg(m_project->width).arg(m_project->height).arg(m_project->fps)},
            {"clips", clips},
            {"media", media}};
}

ToolResult AgentTools::call(const QString &name, const QJsonObject &input)
{
    if (!m_gpu)
        return errorResult(QStringLiteral("GPU is not available"));
    if (!m_tiles->ready) {
        std::string error;
        if (!m_tiles->init(*m_gpu, &error))
            return errorResult(QStringLiteral("cannot prepare thumbnails: %1").arg(QString::fromStdString(error)));
    }
    if (name == QLatin1String("get_timeline")) {
        ToolResult r;
        r.data = timelineState();
        r.summary = QStringLiteral("Timeline: %1 klip, %2").arg(m_project->clips().size()).arg(formatTime(m_project->duration()));
        return r;
    }
    if (name == QLatin1String("scan_video"))
        return scanVideo(input);
    if (name == QLatin1String("view_frames"))
        return viewFrames(input);
    if (name == QLatin1String("audio_levels"))
        return audioLevels(input);
    if (name == QLatin1String("remove_ranges"))
        return removeRanges(input);
    if (name == QLatin1String("split_at"))
        return splitAt(input);
    if (name == QLatin1String("set_transition"))
        return setTransition(input);
    if (name == QLatin1String("undo"))
        return undo();
    if (name == QLatin1String("export_video"))
        return exportVideo(input);
    if (name == QLatin1String("read_reference_project"))
        return readReferenceProject(input);
    if (name == QLatin1String("apply_reference_edits"))
        return applyReferenceEdits(input);
    return errorResult(QStringLiteral("unknown tool %1").arg(name));
}

FrameSource *AgentTools::sourceFor(const QString &mediaId, QString *error)
{
    auto &slot = m_sources[mediaId];
    if (!slot) {
        const MediaInfo *m = m_project->mediaById(mediaId);
        auto source = std::make_unique<FrameSource>();
        if (!m || !source->open(m->path, m_gpu, error)) {
            m_sources.erase(mediaId);
            return nullptr;
        }
        slot = std::move(source);
    }
    return slot.get();
}

QList<AgentTools::Sample> AgentTools::sample(const QList<double> &times, bool precise, QString *error)
{
    QList<Sample> samples;
    const QList<PlacedClip> placed = m_project->layout();
    QList<double> sorted = times;
    std::sort(sorted.begin(), sorted.end());
    for (double t : sorted) {
        int index = 0;
        double source = 0;
        if (!m_project->locate(t, &index, &source))
            continue;
        const PlacedClip &pc = placed[index];
        FrameSource *fs = sourceFor(pc.clip.mediaId, error);
        if (!fs)
            return samples;
        const AVFrame *frame = precise ? fs->frameAt(source) : fs->keyframeAt(source);
        if (!frame)
            continue;
        const double label = pc.start + std::clamp(fs->timeOf(frame) - pc.clip.in, 0.0, pc.clip.length());
        if (!samples.isEmpty() && std::abs(samples.last().time - label) < 0.01)
            continue; // same keyframe again
        samples.append({label, m_tiles->render(*m_gpu, frame)});
    }
    return samples;
}

QImage AgentTools::contactSheet(const QList<Sample> &samples) const
{
    const int rows = (int(samples.size()) + kColumns - 1) / kColumns;
    QImage sheet(kColumns * kTileW, rows * (kTileH + kLabelH), QImage::Format_RGB32);
    sheet.fill(QColor(16, 16, 20));
    QPainter painter(&sheet);
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(15);
    font.setBold(true);
    painter.setFont(font);
    for (int i = 0; i < samples.size(); ++i) {
        const int x = (i % kColumns) * kTileW;
        const int y = (i / kColumns) * (kTileH + kLabelH);
        painter.drawImage(x, y, samples[i].image);
        painter.setPen(QColor(255, 220, 90));
        painter.drawText(QRect(x + 6, y + kTileH, kTileW - 12, kLabelH), Qt::AlignVCenter | Qt::AlignLeft,
                         QStringLiteral("t=%1  (%2 s)").arg(formatTime(samples[i].time)).arg(samples[i].time, 0, 'f', 2));
        painter.setPen(QColor(60, 60, 70));
        painter.drawRect(x, y, kTileW - 1, kTileH + kLabelH - 1);
    }
    painter.end();
    return sheet;
}

ToolResult AgentTools::scanVideo(const QJsonObject &in)
{
    const double duration = m_project->duration();
    const double interval = in.value("interval_seconds").toDouble(0);
    if (interval < 0.25 || interval > 30)
        return errorResult(QStringLiteral("interval_seconds must be between 0.25 and 30"));
    const double start = std::clamp(in.value("start").toDouble(0), 0.0, duration);
    double end = std::clamp(in.value("end").toDouble(duration), start, duration);
    QList<double> times;
    for (double t = start; t < end - 1e-6 && times.size() < kMaxScanFrames; t += interval)
        times.append(t);
    const double covered = times.isEmpty() ? start : times.last() + interval;
    QString error;
    const QList<Sample> samples = sample(times, interval < 2.0, &error);
    if (samples.isEmpty())
        return errorResult(error.isEmpty() ? QStringLiteral("no frames in that range") : error);
    ToolResult r;
    QJsonArray labels;
    for (int i = 0; i < samples.size(); i += kPerSheet) {
        const QList<Sample> page = samples.mid(i, kPerSheet);
        r.images.append(contactSheet(page));
        labels.append(QStringLiteral("sheet %1: %2 - %3").arg(i / kPerSheet + 1).arg(formatTime(page.first().time),
                                                                                      formatTime(page.last().time)));
    }
    r.data = {{"frames", samples.size()}, {"sheets", labels}, {"timeline_duration", round3(duration)}};
    if (covered < end - 1e-6)
        r.data["note"] = QStringLiteral("Stopped at %1 (96-frame limit); scan again from there.").arg(round3(covered));
    r.summary = QStringLiteral("Video tarandı: %1 - %2, %3 kare").arg(formatTime(start), formatTime(std::min(covered, end))).arg(samples.size());
    return r;
}

ToolResult AgentTools::viewFrames(const QJsonObject &in)
{
    QList<double> times;
    for (const QJsonValue &v : in.value("times").toArray())
        times.append(v.toDouble());
    if (times.isEmpty() && in.contains("start") && in.contains("end")) {
        const int count = std::clamp(in.value("count").toInt(8), 2, kPerSheet);
        const double a = in.value("start").toDouble(), b = in.value("end").toDouble();
        for (int i = 0; i < count; ++i)
            times.append(a + (b - a) * i / (count - 1));
    }
    if (times.isEmpty())
        return errorResult(QStringLiteral("give times[] or start/end/count"));
    if (times.size() > kPerSheet)
        times = times.mid(0, kPerSheet);
    QString error;
    const QList<Sample> samples = sample(times, true, &error);
    if (samples.isEmpty())
        return errorResult(error.isEmpty() ? QStringLiteral("no frames at those times") : error);
    ToolResult r;
    r.images.append(contactSheet(samples));
    r.data = {{"frames", samples.size()}};
    r.summary = QStringLiteral("%1 kare yakından incelendi (%2 - %3)")
                    .arg(samples.size())
                    .arg(formatTime(samples.first().time), formatTime(samples.last().time));
    return r;
}

ToolResult AgentTools::audioLevels(const QJsonObject &in)
{
    const double duration = m_project->duration();
    const double step = in.value("step_seconds").toDouble(0);
    if (step < 0.1 || step > 10)
        return errorResult(QStringLiteral("step_seconds must be between 0.1 and 10"));
    const double start = std::clamp(in.value("start").toDouble(0), 0.0, duration);
    double end = std::clamp(in.value("end").toDouble(duration), start, duration);
    end = std::min(end, start + step * 600);
    const QList<PlacedClip> placed = m_project->layout();
    std::map<QString, std::unique_ptr<AudioReader>> readers;
    std::map<QString, double> nextTime;
    QJsonArray levels;
    const int n = std::max(1, int(step * AudioReader::kRate));
    std::vector<float> l(n), r(n);
    for (double t = start; t < end - 1e-6; t += step) {
        int index = 0;
        double source = 0;
        double db = -90;
        if (m_project->locate(t, &index, &source)) {
            const QString mediaId = placed[index].clip.mediaId;
            auto &reader = readers[mediaId];
            if (!reader) {
                reader = std::make_unique<AudioReader>();
                const MediaInfo *m = m_project->mediaById(mediaId);
                if (m)
                    reader->open(m->path, nullptr);
                nextTime[mediaId] = -1;
            }
            if (std::abs(nextTime[mediaId] - source) > 0.05)
                reader->seek(source);
            reader->read(l.data(), r.data(), n);
            nextTime[mediaId] = source + step;
            double sum = 0;
            for (int i = 0; i < n; ++i)
                sum += 0.5 * (double(l[i]) * l[i] + double(r[i]) * r[i]);
            const double rms = std::sqrt(sum / n);
            db = rms > 1e-5 ? 20 * std::log10(rms) : -90;
        }
        levels.append(QJsonArray{round3(t), std::round(db * 10) / 10});
    }
    ToolResult res;
    res.data = {{"step_seconds", step}, {"levels_time_db", levels}};
    res.summary = QStringLiteral("Ses seviyeleri ölçüldü (%1 - %2)").arg(formatTime(start), formatTime(end));
    return res;
}

ToolResult AgentTools::removeRanges(const QJsonObject &in)
{
    QList<QPair<double, double>> ranges;
    QStringList listed;
    for (const QJsonValue &v : in.value("ranges").toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.contains("start") || !o.contains("end"))
            return errorResult(QStringLiteral("each range needs start and end"));
        ranges.append({o.value("start").toDouble(), o.value("end").toDouble()});
        listed << QStringLiteral("%1-%2").arg(formatTime(ranges.last().first), formatTime(ranges.last().second));
    }
    if (ranges.isEmpty())
        return errorResult(QStringLiteral("ranges is empty"));
    const double before = m_project->duration();
    QString error;
    if (!m_project->removeRanges(ranges, &error))
        return errorResult(error);
    if (onProjectChanged)
        onProjectChanged();
    ToolResult r;
    r.data = timelineState();
    r.data["removed_seconds"] = round3(before - m_project->duration());
    r.summary = QStringLiteral("%1 aralık silindi: %2").arg(ranges.size()).arg(listed.join(QStringLiteral(", ")));
    const QString reason = in.value("reason").toString();
    if (!reason.isEmpty())
        r.summary += QStringLiteral(" — ") + reason;
    return r;
}

ToolResult AgentTools::splitAt(const QJsonObject &in)
{
    QString error;
    if (!m_project->splitAt(in.value("time").toDouble(), &error))
        return errorResult(error);
    if (onProjectChanged)
        onProjectChanged();
    ToolResult r;
    r.data = timelineState();
    r.summary = QStringLiteral("%1 noktasında bölündü").arg(formatTime(in.value("time").toDouble()));
    return r;
}

ToolResult AgentTools::setTransition(const QJsonObject &in)
{
    QString type = in.value("type").toString();
    if (type == QLatin1String("none"))
        type.clear();
    else if (transitionIndex(type) < 0)
        return errorResult(QStringLiteral("unknown transition type '%1'").arg(type));
    const double duration = std::clamp(in.value("duration").toDouble(0.5), 0.1, 3.0);
    const Transition transition{type, type.isEmpty() ? 0.0 : duration};
    QString error;
    int count = 0;
    if (in.value("all_cuts").toBool()) {
        count = m_project->setTransitionAtAllCuts(transition);
    } else {
        if (!in.contains("clip_index"))
            return errorResult(QStringLiteral("give clip_index or all_cuts"));
        if (!m_project->setTransition(in.value("clip_index").toInt(), transition, &error))
            return errorResult(error);
        count = 1;
    }
    if (onProjectChanged)
        onProjectChanged();
    ToolResult r;
    r.data = timelineState();
    r.data["cuts_changed"] = count;
    r.summary = type.isEmpty() ? QStringLiteral("%1 kesimdeki geçiş kaldırıldı").arg(count)
                               : QStringLiteral("%1 kesime '%2' geçişi (%3 sn)").arg(count).arg(type).arg(duration);
    return r;
}

ToolResult AgentTools::undo()
{
    if (!m_project->undo())
        return errorResult(QStringLiteral("nothing to undo"));
    if (onProjectChanged)
        onProjectChanged();
    ToolResult r;
    r.data = timelineState();
    r.summary = QStringLiteral("Son düzenleme geri alındı");
    return r;
}

ToolResult AgentTools::readReferenceProject(const QJsonObject &in)
{
    Project reference;
    QString error;
    const QString path = in.value("path").toString();
    if (!reference.load(path, &error))
        return errorResult(QStringLiteral("cannot open %1: %2").arg(path, error));
    const QJsonObject recipe = reference.editRecipe();

    // Show what was cut: a few frames from each removed range of the reference footage.
    QList<Sample> samples;
    const QJsonArray removed = recipe.value("removed").toArray();
    for (int i = 0; i < removed.size() && samples.size() < 2 * kPerSheet; ++i) {
        const QJsonObject r = removed[i].toObject();
        const MediaInfo &m = reference.media().at(r.value("media").toInt());
        FrameSource source;
        if (!source.open(m.path, m_gpu, &error))
            continue;
        const double a = r.value("start").toDouble(), b = r.value("end").toDouble();
        for (int k = 0; k < 4; ++k) {
            const double t = a + (b - a) * (k + 0.5) / 4;
            if (const AVFrame *frame = source.frameAt(t))
                samples.append({t, m_tiles->render(*m_gpu, frame)});
        }
    }
    ToolResult r;
    r.data = recipe;
    r.data["note"] = QStringLiteral("Times are source seconds of the reference footage. The contact sheets show 4 "
                                    "frames from each removed range, in order, labelled with reference source time.");
    for (int i = 0; i < samples.size(); i += kPerSheet)
        r.images.append(contactSheet(samples.mid(i, kPerSheet)));
    r.summary = QStringLiteral("Referans proje okundu: %1 klip, %2 kesilen bölüm")
                    .arg(recipe.value("clips").toArray().size())
                    .arg(removed.size());
    return r;
}

ToolResult AgentTools::applyReferenceEdits(const QJsonObject &in)
{
    Project reference;
    QString error;
    if (!reference.load(in.value("path").toString(), &error))
        return errorResult(error);
    const QString mode = in.value("mode").toString(QStringLiteral("exact"));
    if (!m_project->applyRecipe(reference.editRecipe(), mode, &error))
        return errorResult(error);
    if (onProjectChanged)
        onProjectChanged();
    ToolResult r;
    r.data = timelineState();
    r.summary = QStringLiteral("Referans düzenlemeler uygulandı (%1)").arg(mode == QLatin1String("scaled") ? "orantılı" : "birebir");
    return r;
}

ToolResult AgentTools::exportVideo(const QJsonObject &in)
{
    RenderSettings settings;
    settings.outputPath = in.value("path").toString();
    if (settings.outputPath.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/DivaStudio";
        QDir().mkpath(dir);
        settings.outputPath = dir + "/" + m_project->name + "_DivaAI.mp4";
    }
    const QString res = in.value("resolution").toString(QStringLiteral("project"));
    const int heights[] = {720, 1080, 1440, 2160};
    const char *names[] = {"720p", "1080p", "1440p", "2160p"};
    for (int i = 0; i < 4; ++i) {
        if (res == QLatin1String(names[i])) {
            settings.height = heights[i];
            settings.width = int(std::lround(heights[i] * double(m_project->width) / m_project->height)) & ~1;
        }
    }
    settings.fps = in.value("fps").toDouble(0);
    settings.codec = in.value("codec").toString(QStringLiteral("hevc"));
    QString error;
    const bool ok = renderTimeline(*m_project, settings, [this](const RenderStatus &s) {
        if (onRenderProgress)
            onRenderProgress(s.fraction);
        return true;
    }, &error);
    if (!ok)
        return errorResult(error);
    ToolResult r;
    r.data = {{"path", settings.outputPath}, {"duration", round3(m_project->duration())}};
    r.summary = QStringLiteral("Dışa aktarıldı: %1").arg(settings.outputPath);
    return r;
}

} // namespace diva
