// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectController.h"

#include "TimelineRenderer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonArray>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>

#include <cmath>

namespace {

ProjectController *g_instance = nullptr;
constexpr int kMaxRecent = 8;

QString previewDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/DivaStudio";
    QDir().mkpath(dir);
    return dir;
}

} // namespace

ProjectController::ProjectController(QObject *parent) : QObject(parent)
{
    g_instance = this;
    m_previewTimer.setSingleShot(true);
    connect(&m_previewTimer, &QTimer::timeout, this, &ProjectController::renderPreview);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &ProjectController::reloadFromDisk);
}

ProjectController::~ProjectController()
{
    m_cancelPreview = true;
    m_cancelExport = true;
    QThreadPool::globalInstance()->waitForDone();
    g_instance = nullptr;
}

ProjectController *ProjectController::instance()
{
    return g_instance;
}

ProjectController *ProjectController::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QVariantList ProjectController::clips() const
{
    QVariantList list;
    for (const diva::PlacedClip &p : m_project.layout()) {
        const diva::MediaInfo *m = m_project.mediaById(p.clip.mediaId);
        list.append(QVariantMap{{"index", p.index},
                                {"start", p.start},
                                {"end", p.end},
                                {"sourceIn", p.clip.in},
                                {"sourceOut", p.clip.out},
                                {"name", m ? QFileInfo(m->path).fileName() : QString()},
                                {"hasAudio", m && m->audioStreams > 0},
                                {"transition", p.clip.transitionIn.type},
                                {"transitionDuration", p.clip.transitionIn.duration}});
    }
    return list;
}

QVariantList ProjectController::transitionKinds() const
{
    QVariantList list;
    for (const diva::TransitionKind &k : diva::transitionKinds())
        list.append(QVariantMap{{"id", QString::fromLatin1(k.id)}, {"label", QString::fromUtf8(k.label)}});
    return list;
}

void ProjectController::setLocked(bool locked)
{
    if (m_locked == locked)
        return;
    m_locked = locked;
    emit lockedChanged();
}

QStringList ProjectController::recentProjects() const
{
    QStringList list;
    for (const QString &path : QSettings().value("recentProjects").toStringList())
        if (QFileInfo::exists(path))
            list.append(path);
    return list;
}

void ProjectController::addRecent(const QString &path)
{
    QSettings settings;
    QStringList list = settings.value("recentProjects").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecent)
        list.removeLast();
    settings.setValue("recentProjects", list);
    // divamcp (Claude Code) works on the project last opened or saved here.
    settings.setValue("currentProject", path);
    emit recentChanged();
}

void ProjectController::edited()
{
    emit changed();
    schedulePreview();
}

void ProjectController::newProject()
{
    m_project.clear();
    m_hasProject = true;
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    edited();
}

bool ProjectController::openProject(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QString error;
    diva::Project loaded;
    if (!loaded.load(path, &error)) {
        emit errorOccurred(QStringLiteral("Proje açılamadı: %1").arg(error));
        return false;
    }
    if (!m_project.load(path, &error))
        return false;
    m_hasProject = true;
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    m_watcher.addPath(path);
    addRecent(path);
    edited();
    return true;
}

bool ProjectController::save()
{
    if (m_project.filePath.isEmpty())
        return false;
    return saveAs(QUrl::fromLocalFile(m_project.filePath));
}

bool ProjectController::saveAs(const QUrl &file)
{
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!path.endsWith(QLatin1String(".divaproj"), Qt::CaseInsensitive))
        path += QStringLiteral(".divaproj");
    QString error;
    m_lastOwnSave = QDateTime::currentMSecsSinceEpoch();
    if (!m_project.save(path, &error)) {
        emit errorOccurred(QStringLiteral("Kaydedilemedi: %1").arg(error));
        return false;
    }
    if (!m_watcher.files().contains(path)) {
        if (!m_watcher.files().isEmpty())
            m_watcher.removePaths(m_watcher.files());
        m_watcher.addPath(path);
    }
    addRecent(path);
    emit changed();
    return true;
}

void ProjectController::reloadFromDisk()
{
    const QString path = m_project.filePath;
    // Atomic saves replace the file, which drops it from the watcher.
    if (QFileInfo::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);
    if (QDateTime::currentMSecsSinceEpoch() - m_lastOwnSave < 1500)
        return;
    if (m_project.isDirty()) {
        emit errorOccurred(QStringLiteral("Proje dosyası dışarıda değişti (Claude Code?), ama burada kaydedilmemiş "
                                          "değişiklikler var; yeniden yüklenmedi."));
        return;
    }
    diva::Project loaded;
    QString error;
    if (!loaded.load(path, &error))
        return;
    m_project.applySnapshot(loaded.toJson(), QStringLiteral("Dış düzenleme"));
    m_lastOwnSave = QDateTime::currentMSecsSinceEpoch();
    m_project.save(path, &error); // mark clean again
    edited();
}

bool ProjectController::importMedia(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    diva::MediaInfo info;
    QString error;
    if (!diva::probeMedia(path, &info, &error)) {
        emit errorOccurred(error);
        return false;
    }
    if (!m_hasProject) {
        m_project.clear();
        m_hasProject = true;
    }
    m_project.importMedia(info);
    if (m_project.name == QStringLiteral("Adsız proje"))
        m_project.name = QFileInfo(path).completeBaseName();
    edited();
    return true;
}

void ProjectController::undo()
{
    if (!m_locked && m_project.undo())
        edited();
}

void ProjectController::redo()
{
    if (!m_locked && m_project.redo())
        edited();
}

bool ProjectController::splitAt(double t)
{
    QString error;
    if (m_locked || !m_project.splitAt(t, &error)) {
        if (!error.isEmpty())
            emit errorOccurred(error);
        return false;
    }
    edited();
    return true;
}

bool ProjectController::removeClip(int index)
{
    const QList<diva::PlacedClip> placed = m_project.layout();
    if (m_locked || index < 0 || index >= placed.size())
        return false;
    QString error;
    if (!m_project.removeRanges({{placed[index].start, placed[index].end}}, &error)) {
        emit errorOccurred(error);
        return false;
    }
    edited();
    return true;
}

bool ProjectController::setTransition(int clipIndex, const QString &type, double duration)
{
    QString error;
    if (m_locked || !m_project.setTransition(clipIndex, {type, type.isEmpty() ? 0.0 : duration}, &error)) {
        if (!error.isEmpty())
            emit errorOccurred(error);
        return false;
    }
    edited();
    return true;
}

void ProjectController::setTransitionAll(const QString &type, double duration)
{
    if (m_locked || m_project.clips().size() < 2)
        return;
    m_project.setTransitionAtAllCuts({type, type.isEmpty() ? 0.0 : duration});
    edited();
}

QVariantMap ProjectController::describeProject(const QUrl &file) const
{
    diva::Project reference;
    QString error;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!reference.load(path, &error))
        return {{"ok", false}, {"error", error}};
    const QJsonObject recipe = reference.editRecipe();
    double removedSeconds = 0;
    const QJsonArray removed = recipe.value("removed").toArray();
    for (const QJsonValue &v : removed)
        removedSeconds += v.toObject().value("end").toDouble() - v.toObject().value("start").toDouble();
    QStringList transitions;
    for (const QJsonValue &v : recipe.value("clips").toArray()) {
        const QString t = v.toObject().value("transition").toObject().value("type").toString();
        if (!t.isEmpty() && !transitions.contains(t))
            transitions.append(t);
    }
    return {{"ok", true},
            {"path", path},
            {"name", reference.name},
            {"clips", recipe.value("clips").toArray().size()},
            {"removedCount", removed.size()},
            {"removedSeconds", removedSeconds},
            {"transitions", transitions.join(", ")},
            {"duration", reference.duration()},
            {"sourceDuration", reference.media().isEmpty() ? 0.0 : reference.media().first().duration}};
}

bool ProjectController::applyEditsFrom(const QUrl &file, const QString &mode)
{
    diva::Project reference;
    QString error;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (m_locked || !reference.load(path, &error) || !m_project.applyRecipe(reference.editRecipe(), mode, &error)) {
        if (!error.isEmpty())
            emit errorOccurred(QStringLiteral("Düzenlemeler uygulanamadı: %1").arg(error));
        return false;
    }
    edited();
    return true;
}

void ProjectController::applyAgentSnapshot(const QJsonObject &state, const QString &label)
{
    m_project.applySnapshot(state, label);
    edited();
}

void ProjectController::schedulePreview()
{
    // An untouched single recording previews straight from the file.
    const QList<diva::Clip> &clips = m_project.clips();
    if (clips.size() == 1) {
        const diva::MediaInfo *m = m_project.mediaById(clips.first().mediaId);
        if (m && clips.first().in < 0.01 && std::abs(clips.first().out - m->duration) < 0.05) {
            m_cancelPreview = true;
            m_previewTimer.stop();
            m_previewSource = QUrl::fromLocalFile(m->path);
            emit previewChanged();
            return;
        }
    }
    if (clips.isEmpty()) {
        m_previewSource = {};
        emit previewChanged();
        return;
    }
    m_previewTimer.start(500);
}

void ProjectController::renderPreview()
{
    if (m_previewBusy) {
        // Finish (or cancel) the running render first, then start over with the latest edit.
        m_cancelPreview = true;
        m_previewTimer.start(300);
        return;
    }
    m_previewBusy = true;
    m_cancelPreview = false;
    m_previewProgress = 0;
    emit previewChanged();

    const quint64 revision = m_project.revision();
    const QJsonObject state = m_project.toJson();
    const QString out = previewDir() + QStringLiteral("/preview_%1_%2.mp4")
                                           .arg(QCoreApplication::applicationPid())
                                           .arg(revision);
    QThreadPool::globalInstance()->start([this, state, out, revision] {
        diva::Project copy;
        copy.fromJson(state, {}, nullptr);
        diva::RenderSettings s;
        s.outputPath = out;
        s.height = 720;
        s.width = int(std::lround(720.0 * copy.width / std::max(1, copy.height))) & ~1;
        s.fps = std::min(copy.fps > 0 ? copy.fps : 60.0, 60.0);
        s.codec = QStringLiteral("h264");
        s.preset = QStringLiteral("speed");
        s.bitrate = 8'000'000;
        QString error;
        const bool ok = diva::renderTimeline(copy, s, [this](const diva::RenderStatus &status) {
            QMetaObject::invokeMethod(this, [this, f = status.fraction] {
                m_previewProgress = f;
                emit previewChanged();
            });
            return !m_cancelPreview.load();
        }, &error);
        QMetaObject::invokeMethod(this, [this, ok, out, revision, error] {
            m_previewBusy = false;
            if (ok && revision == m_project.revision()) {
                const QString old = m_previewSource.toLocalFile();
                m_previewSource = QUrl::fromLocalFile(out);
                if (old.startsWith(previewDir()))
                    QFile::remove(old);
            } else {
                QFile::remove(out);
                if (!ok && !m_cancelPreview)
                    emit errorOccurred(QStringLiteral("Önizleme oluşturulamadı: %1").arg(error));
            }
            emit previewChanged();
            if (revision != m_project.revision())
                schedulePreview();
        });
    });
}

QString ProjectController::defaultExportPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/DivaStudio";
    QDir().mkpath(dir);
    return QDir(dir).filePath(m_project.name + QStringLiteral(".mp4"));
}

void ProjectController::exportVideo(const QUrl &file, int height, double fps, const QString &codec,
                                    const QString &preset)
{
    if (m_exportBusy || m_project.clips().isEmpty())
        return;
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".mp4");
    m_exportBusy = true;
    m_cancelExport = false;
    m_exportProgress = 0;
    m_exportStatus = QStringLiteral("Başlıyor…");
    emit exportChanged();

    const QJsonObject state = m_project.toJson();
    QThreadPool::globalInstance()->start([this, state, path, height, fps, codec, preset] {
        diva::Project copy;
        copy.fromJson(state, {}, nullptr);
        diva::RenderSettings s;
        s.outputPath = path;
        if (height > 0) {
            s.height = height;
            s.width = int(std::lround(double(height) * copy.width / std::max(1, copy.height))) & ~1;
        }
        s.fps = fps;
        s.codec = codec;
        s.preset = preset;
        QString error;
        const bool ok = diva::renderTimeline(copy, s, [this](const diva::RenderStatus &status) {
            QMetaObject::invokeMethod(this, [this, status] {
                m_exportProgress = status.fraction;
                m_exportStatus = QStringLiteral("%1 fps · kalan %2 sn · %3")
                                     .arg(status.fps, 0, 'f', 0)
                                     .arg(status.etaSeconds, 0, 'f', 0)
                                     .arg(status.encoder);
                emit exportChanged();
            });
            return !m_cancelExport.load();
        }, &error);
        QMetaObject::invokeMethod(this, [this, ok, path, error] {
            m_exportBusy = false;
            m_exportStatus = ok ? QStringLiteral("Bitti: %1").arg(path) : error;
            emit exportChanged();
            if (ok)
                emit exportFinished(path);
            else if (!m_cancelExport)
                emit errorOccurred(QStringLiteral("Dışa aktarılamadı: %1").arg(error));
        });
    });
}
