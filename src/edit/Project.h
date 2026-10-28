// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QVector>

namespace diva {

struct MediaInfo
{
    QString id;
    QString path;
    double duration = 0;
    int width = 0;
    int height = 0;
    double fps = 0;
    int audioStreams = 0;
    QString videoCodec;
};

struct Transition
{
    QString type;        // empty = hard cut
    double duration = 0; // seconds; the incoming clip overlaps the outgoing one by this much
};

struct Clip
{
    QString id;
    QString mediaId;
    double in = 0;  // source seconds
    double out = 0;
    Transition transitionIn; // from the previous clip into this one
    double length() const { return out - in; }
};

// A clip placed on the timeline. With a transition the incoming clip starts before the
// outgoing one ends, so neighbours overlap by the transition duration.
struct PlacedClip
{
    int index = 0;
    double start = 0;
    double end = 0;
    Clip clip;
};

struct TransitionKind
{
    const char *id;
    const char *label;       // UI (Turkish)
    const char *description; // for DivaAI: what it looks like and when it fits
};

const QVector<TransitionKind> &transitionKinds();
int transitionIndex(const QString &id); // -1 when unknown

// Single main audio/video track (clips play back to back, audio follows video).
// Every edit is one undo step; undo/redo restore JSON snapshots.
class Project
{
public:
    QString name = QStringLiteral("Adsız proje");
    QString filePath;
    int width = 1920;
    int height = 1080;
    double fps = 60;

    const QList<MediaInfo> &media() const { return m_media; }
    const QList<Clip> &clips() const { return m_clips; }
    const MediaInfo *mediaById(const QString &id) const;

    QList<PlacedClip> layout() const;
    double duration() const;
    // Clip on top at timeline time t (the incoming one inside a transition) and its source time.
    bool locate(double t, int *clipIndex, double *sourceTime) const;

    void clear();
    // Adds a file to the media bin and appends it to the timeline.
    void importMedia(const MediaInfo &info);
    // Ripple-deletes timeline ranges [start, end) given in current timeline seconds.
    bool removeRanges(QList<QPair<double, double>> ranges, QString *error);
    bool splitAt(double t, QString *error);
    // Sets the transition into clip `clipIndex` (>= 1). An empty type makes it a hard cut.
    bool setTransition(int clipIndex, const Transition &transition, QString *error);
    int setTransitionAtAllCuts(const Transition &transition);

    // Replaces the edit state with a snapshot (e.g. DivaAI's result) as one undo step.
    void applySnapshot(const QJsonObject &state, const QString &label);

    // The edit decisions relative to the source media: kept source ranges in order, the
    // transitions between them, and the removed ranges (what was cut).
    QJsonObject editRecipe() const;
    // Rebuilds this timeline from another project's recipe on this project's own media.
    // "exact": the same source times; "scaled": times scaled to this media's length.
    bool applyRecipe(const QJsonObject &recipe, const QString &mode, QString *error);

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString undoLabel() const { return m_undo.isEmpty() ? QString() : m_undo.last().first; }
    bool undo();
    bool redo();

    QJsonObject toJson(const QString &baseDir = {}) const;
    bool fromJson(const QJsonObject &json, const QString &baseDir, QString *error);
    bool save(const QString &path, QString *error);
    bool load(const QString &path, QString *error);

    quint64 revision() const { return m_revision; }
    bool isDirty() const { return m_revision != m_savedRevision; }

private:
    QJsonObject snapshot() const;
    void restore(const QJsonObject &state);
    void pushUndo(const QString &label);
    void changed() { ++m_revision; }
    QString nextId(QChar prefix) { return QString(prefix) + QString::number(m_nextId++); }
    void clampTransitions();

    QList<MediaInfo> m_media;
    QList<Clip> m_clips;
    int m_nextId = 1;
    QList<QPair<QString, QJsonObject>> m_undo;
    QList<QPair<QString, QJsonObject>> m_redo;
    quint64 m_revision = 0;
    quint64 m_savedRevision = 0;
};

// Reads duration, size, frame rate and audio streams with FFmpeg (no decoding).
bool probeMedia(const QString &path, MediaInfo *info, QString *error);

} // namespace diva
