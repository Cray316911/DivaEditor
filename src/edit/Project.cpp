// SPDX-License-Identifier: GPL-3.0-or-later
#include "Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace diva {

namespace {

constexpr int kSchema = 1;
constexpr double kEpsilon = 1.0 / 240.0;

} // namespace

const QVector<TransitionKind> &transitionKinds()
{
    static const QVector<TransitionKind> kinds = {
        {"dissolve", "Çapraz geçiş", "Soft crossfade between the two shots. Safe default for most content."},
        {"fade_black", "Siyaha solma", "Fades out to black, then in. Marks a time skip or a new section."},
        {"flash", "Beyaz flaş", "Quick white flash. Energetic; suits gaming highlights and impacts."},
        {"wipe_left", "Sola silme", "Hard edge sweeps left to right revealing the next shot."},
        {"wipe_right", "Sağa silme", "Hard edge sweeps right to left revealing the next shot."},
        {"slide_left", "Sola kaydır", "Next shot pushes the current one out to the left. Dynamic, modern."},
        {"slide_right", "Sağa kaydır", "Next shot pushes the current one out to the right."},
        {"iris", "Daire açılış", "Next shot opens from a growing circle in the centre. Playful."},
        {"zoom", "Yakınlaşarak geçiş", "Current shot zooms in while the next settles from a zoom. Punchy."},
    };
    return kinds;
}

int transitionIndex(const QString &id)
{
    const auto &kinds = transitionKinds();
    for (int i = 0; i < kinds.size(); ++i)
        if (id == QLatin1String(kinds[i].id))
            return i;
    return -1;
}

const MediaInfo *Project::mediaById(const QString &id) const
{
    for (const MediaInfo &m : m_media)
        if (m.id == id)
            return &m;
    return nullptr;
}

QList<PlacedClip> Project::layout() const
{
    QList<PlacedClip> placed;
    double end = 0;
    for (int i = 0; i < m_clips.size(); ++i) {
        const Clip &c = m_clips[i];
        const double overlap = (i > 0 && !c.transitionIn.type.isEmpty()) ? c.transitionIn.duration : 0;
        PlacedClip p;
        p.index = i;
        p.start = std::max(0.0, end - overlap);
        p.end = p.start + c.length();
        p.clip = c;
        end = p.end;
        placed.append(p);
    }
    return placed;
}

double Project::duration() const
{
    const QList<PlacedClip> placed = layout();
    return placed.isEmpty() ? 0 : placed.last().end;
}

bool Project::locate(double t, int *clipIndex, double *sourceTime) const
{
    const QList<PlacedClip> placed = layout();
    for (int i = placed.size() - 1; i >= 0; --i) {
        const PlacedClip &p = placed[i];
        if (t >= p.start - kEpsilon && t < p.end) {
            *clipIndex = i;
            *sourceTime = p.clip.in + std::max(0.0, t - p.start);
            return true;
        }
    }
    if (!placed.isEmpty() && t >= placed.last().end - kEpsilon) {
        *clipIndex = placed.size() - 1;
        *sourceTime = placed.last().clip.out - kEpsilon;
        return true;
    }
    return false;
}

void Project::clear()
{
    m_media.clear();
    m_clips.clear();
    m_undo.clear();
    m_redo.clear();
    m_nextId = 1;
    filePath.clear();
    name = QStringLiteral("Adsız proje");
    changed();
    m_savedRevision = m_revision;
}

void Project::importMedia(const MediaInfo &info)
{
    pushUndo(QStringLiteral("İçe aktar"));
    MediaInfo m = info;
    m.id = nextId(u'm');
    m_media.append(m);
    if (m_clips.isEmpty() && m.width > 0) {
        // The first video sets the project canvas, like most editors do.
        width = m.width;
        height = m.height;
        fps = m.fps > 0 ? m.fps : fps;
    }
    Clip c;
    c.id = nextId(u'c');
    c.mediaId = m.id;
    c.in = 0;
    c.out = m.duration;
    m_clips.append(c);
    changed();
}

bool Project::removeRanges(QList<QPair<double, double>> ranges, QString *error)
{
    for (auto &r : ranges) {
        if (r.second < r.first)
            std::swap(r.first, r.second);
        if (r.second - r.first < kEpsilon) {
            if (error)
                *error = QStringLiteral("empty range %1-%2").arg(r.first).arg(r.second);
            return false;
        }
    }
    // Latest first, so earlier ranges still refer to the timeline the caller saw.
    std::sort(ranges.begin(), ranges.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    pushUndo(QStringLiteral("Aralık sil"));
    for (const auto &range : ranges) {
        const QList<PlacedClip> placed = layout();
        QList<Clip> result;
        for (const PlacedClip &p : placed) {
            const double a = std::max(range.first, p.start);
            const double b = std::min(range.second, p.end);
            if (b - a < kEpsilon) {
                result.append(p.clip);
                continue;
            }
            const Clip &c = p.clip;
            const double cutIn = c.in + (a - p.start);
            const double cutOut = c.in + (b - p.start);
            if (cutIn - c.in > kEpsilon) {
                Clip head = c;
                head.out = cutIn;
                result.append(head);
            }
            if (c.out - cutOut > kEpsilon) {
                Clip tail = c;
                tail.id = nextId(u'c');
                tail.in = cutOut;
                tail.transitionIn = {}; // a new cut point starts as a hard cut
                result.append(tail);
            }
        }
        m_clips = result;
    }
    if (!m_clips.isEmpty())
        m_clips.first().transitionIn = {};
    clampTransitions();
    changed();
    return true;
}

bool Project::splitAt(double t, QString *error)
{
    int index = -1;
    double source = 0;
    if (!locate(t, &index, &source)) {
        if (error)
            *error = QStringLiteral("no clip at %1 s").arg(t);
        return false;
    }
    Clip &c = m_clips[index];
    if (source - c.in < kEpsilon || c.out - source < kEpsilon) {
        if (error)
            *error = QStringLiteral("%1 s is already a cut point").arg(t);
        return false;
    }
    pushUndo(QStringLiteral("Böl"));
    Clip tail = m_clips[index];
    tail.id = nextId(u'c');
    tail.in = source;
    tail.transitionIn = {};
    m_clips[index].out = source;
    m_clips.insert(index + 1, tail);
    changed();
    return true;
}

bool Project::setTransition(int clipIndex, const Transition &transition, QString *error)
{
    if (clipIndex < 1 || clipIndex >= m_clips.size()) {
        if (error)
            *error = QStringLiteral("clip_index must be between 1 and %1 (the cut before that clip)")
                         .arg(m_clips.size() - 1);
        return false;
    }
    if (!transition.type.isEmpty() && transitionIndex(transition.type) < 0) {
        if (error)
            *error = QStringLiteral("unknown transition type '%1'").arg(transition.type);
        return false;
    }
    pushUndo(QStringLiteral("Geçiş"));
    m_clips[clipIndex].transitionIn = transition;
    clampTransitions();
    changed();
    return true;
}

int Project::setTransitionAtAllCuts(const Transition &transition)
{
    if (m_clips.size() < 2)
        return 0;
    pushUndo(QStringLiteral("Tüm kesimlere geçiş"));
    for (int i = 1; i < m_clips.size(); ++i)
        m_clips[i].transitionIn = transition;
    clampTransitions();
    changed();
    return m_clips.size() - 1;
}

void Project::clampTransitions()
{
    // A transition can use at most half of either neighbour, so overlaps never chain.
    for (int i = 1; i < m_clips.size(); ++i) {
        Transition &t = m_clips[i].transitionIn;
        if (t.type.isEmpty())
            continue;
        const double limit = 0.5 * std::min(m_clips[i - 1].length(), m_clips[i].length());
        t.duration = std::clamp(t.duration, 0.0, limit);
        if (t.duration < kEpsilon)
            t = {};
    }
}

QJsonObject Project::snapshot() const
{
    return toJson();
}

void Project::restore(const QJsonObject &state)
{
    const QString path = filePath;
    fromJson(state, {}, nullptr);
    filePath = path;
}

void Project::pushUndo(const QString &label)
{
    m_undo.append({label, snapshot()});
    if (m_undo.size() > 200)
        m_undo.removeFirst();
    m_redo.clear();
}

void Project::applySnapshot(const QJsonObject &state, const QString &label)
{
    pushUndo(label);
    restore(state);
    changed();
}

QJsonObject Project::editRecipe() const
{
    QJsonArray media;
    for (int i = 0; i < m_media.size(); ++i)
        media.append(QJsonObject{{"index", i},
                                 {"file", QFileInfo(m_media[i].path).fileName()},
                                 {"duration", m_media[i].duration}});
    QJsonArray clips;
    QVector<QList<QPair<double, double>>> kept(m_media.size());
    for (const Clip &c : m_clips) {
        int index = 0;
        for (int i = 0; i < m_media.size(); ++i)
            if (m_media[i].id == c.mediaId)
                index = i;
        QJsonObject o{{"media", index}, {"in", c.in}, {"out", c.out}};
        if (!c.transitionIn.type.isEmpty())
            o["transition"] = QJsonObject{{"type", c.transitionIn.type}, {"duration", c.transitionIn.duration}};
        clips.append(o);
        if (index < kept.size())
            kept[index].append({c.in, c.out});
    }
    // Removed = parts of each source that no clip uses.
    QJsonArray removed;
    for (int i = 0; i < m_media.size(); ++i) {
        auto ranges = kept[i];
        std::sort(ranges.begin(), ranges.end());
        double cursor = 0;
        for (const auto &r : ranges) {
            if (r.first - cursor > kEpsilon)
                removed.append(QJsonObject{{"media", i}, {"start", cursor}, {"end", r.first}});
            cursor = std::max(cursor, r.second);
        }
        if (m_media[i].duration - cursor > kEpsilon)
            removed.append(QJsonObject{{"media", i}, {"start", cursor}, {"end", m_media[i].duration}});
    }
    return {{"media", media}, {"clips", clips}, {"removed", removed}, {"duration", duration()}};
}

bool Project::applyRecipe(const QJsonObject &recipe, const QString &mode, QString *error)
{
    if (m_media.isEmpty()) {
        if (error)
            *error = QStringLiteral("Bu projede video yok.");
        return false;
    }
    const QJsonArray refMedia = recipe.value("media").toArray();
    QList<Clip> clips;
    for (const QJsonValue &v : recipe.value("clips").toArray()) {
        const QJsonObject o = v.toObject();
        const int refIndex = o.value("media").toInt();
        const MediaInfo &target = m_media[std::min<qsizetype>(refIndex, m_media.size() - 1)];
        const double refDuration = refMedia.at(refIndex).toObject().value("duration").toDouble(target.duration);
        const double scale = mode == QLatin1String("scaled") && refDuration > 0 ? target.duration / refDuration : 1.0;
        Clip c;
        c.id = nextId(u'c');
        c.mediaId = target.id;
        c.in = std::clamp(o.value("in").toDouble() * scale, 0.0, target.duration);
        c.out = std::clamp(o.value("out").toDouble() * scale, 0.0, target.duration);
        if (c.out - c.in < kEpsilon)
            continue;
        const QJsonObject t = o.value("transition").toObject();
        if (!clips.isEmpty())
            c.transitionIn = {t.value("type").toString(), t.value("duration").toDouble()};
        clips.append(c);
    }
    if (clips.isEmpty()) {
        if (error)
            *error = QStringLiteral("Referans düzenlemeler bu videonun süresine sığmıyor.");
        return false;
    }
    pushUndo(QStringLiteral("Başka projeden düzenleme"));
    m_clips = clips;
    clampTransitions();
    changed();
    return true;
}

bool Project::undo()
{
    if (m_undo.isEmpty())
        return false;
    const auto step = m_undo.takeLast();
    m_redo.append({step.first, snapshot()});
    restore(step.second);
    changed();
    return true;
}

bool Project::redo()
{
    if (m_redo.isEmpty())
        return false;
    const auto step = m_redo.takeLast();
    m_undo.append({step.first, snapshot()});
    restore(step.second);
    changed();
    return true;
}

QJsonObject Project::toJson(const QString &baseDir) const
{
    QJsonArray media;
    for (const MediaInfo &m : m_media) {
        QJsonObject o{{"id", m.id},
                      {"path", m.path},
                      {"duration", m.duration},
                      {"width", m.width},
                      {"height", m.height},
                      {"fps", m.fps},
                      {"audioStreams", m.audioStreams},
                      {"videoCodec", m.videoCodec}};
        if (!baseDir.isEmpty())
            o["relativePath"] = QDir(baseDir).relativeFilePath(m.path);
        media.append(o);
    }
    QJsonArray clips;
    for (const Clip &c : m_clips) {
        QJsonObject o{{"id", c.id}, {"media", c.mediaId}, {"in", c.in}, {"out", c.out}};
        if (!c.transitionIn.type.isEmpty())
            o["transition"] = QJsonObject{{"type", c.transitionIn.type}, {"duration", c.transitionIn.duration}};
        clips.append(o);
    }
    return {{"app", "DivaStudio"},
            {"schema", kSchema},
            {"name", name},
            {"settings", QJsonObject{{"width", width}, {"height", height}, {"fps", fps}}},
            {"media", media},
            {"clips", clips},
            {"nextId", m_nextId}};
}

bool Project::fromJson(const QJsonObject &json, const QString &baseDir, QString *error)
{
    if (json.value("schema").toInt() > kSchema) {
        if (error)
            *error = QStringLiteral("Bu proje DivaStudio'nun daha yeni bir sürümüyle kaydedilmiş.");
        return false;
    }
    name = json.value("name").toString(name);
    const QJsonObject settings = json.value("settings").toObject();
    width = settings.value("width").toInt(width);
    height = settings.value("height").toInt(height);
    fps = settings.value("fps").toDouble(fps);
    m_media.clear();
    for (const QJsonValue &v : json.value("media").toArray()) {
        const QJsonObject o = v.toObject();
        MediaInfo m;
        m.id = o.value("id").toString();
        m.path = o.value("path").toString();
        // Prefer the path relative to the project so moved project folders keep working.
        const QString relative = o.value("relativePath").toString();
        if (!baseDir.isEmpty() && !relative.isEmpty()) {
            const QString candidate = QDir(baseDir).absoluteFilePath(relative);
            if (QFileInfo::exists(candidate))
                m.path = QDir::cleanPath(candidate);
        }
        m.duration = o.value("duration").toDouble();
        m.width = o.value("width").toInt();
        m.height = o.value("height").toInt();
        m.fps = o.value("fps").toDouble();
        m.audioStreams = o.value("audioStreams").toInt();
        m.videoCodec = o.value("videoCodec").toString();
        m_media.append(m);
    }
    m_clips.clear();
    for (const QJsonValue &v : json.value("clips").toArray()) {
        const QJsonObject o = v.toObject();
        Clip c;
        c.id = o.value("id").toString();
        c.mediaId = o.value("media").toString();
        c.in = o.value("in").toDouble();
        c.out = o.value("out").toDouble();
        const QJsonObject t = o.value("transition").toObject();
        c.transitionIn = {t.value("type").toString(), t.value("duration").toDouble()};
        m_clips.append(c);
    }
    m_nextId = std::max(json.value("nextId").toInt(1), 1);
    return true;
}

bool Project::save(const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(toJson(QFileInfo(path).absolutePath())).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    filePath = path;
    if (name == QStringLiteral("Adsız proje"))
        name = QFileInfo(path).completeBaseName();
    m_savedRevision = m_revision;
    return true;
}

bool Project::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error)
            *error = parseError.errorString();
        return false;
    }
    if (!fromJson(doc.object(), QFileInfo(path).absolutePath(), error))
        return false;
    filePath = path;
    m_undo.clear();
    m_redo.clear();
    changed();
    m_savedRevision = m_revision;
    return true;
}

bool probeMedia(const QString &path, MediaInfo *info, QString *error)
{
    AVFormatContext *format = nullptr;
    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&format, utf8.constData(), nullptr, nullptr) < 0) {
        if (error)
            *error = QStringLiteral("Dosya açılamadı: %1").arg(path);
        return false;
    }
    avformat_find_stream_info(format, nullptr);
    const int video = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video < 0) {
        avformat_close_input(&format);
        if (error)
            *error = QStringLiteral("Video akışı yok: %1").arg(path);
        return false;
    }
    const AVStream *stream = format->streams[video];
    info->path = QFileInfo(path).absoluteFilePath();
    info->width = stream->codecpar->width;
    info->height = stream->codecpar->height;
    info->fps = av_q2d(av_guess_frame_rate(format, const_cast<AVStream *>(stream), nullptr));
    info->videoCodec = QString::fromLatin1(avcodec_get_name(stream->codecpar->codec_id));
    info->duration = stream->duration != AV_NOPTS_VALUE ? stream->duration * av_q2d(stream->time_base)
                     : format->duration != AV_NOPTS_VALUE ? format->duration / double(AV_TIME_BASE)
                                                          : 0;
    info->audioStreams = 0;
    for (unsigned i = 0; i < format->nb_streams; ++i)
        if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            ++info->audioStreams;
    avformat_close_input(&format);
    return true;
}

} // namespace diva
