// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Project.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>

class QQmlEngine;
class QJSEngine;

// The open project for QML: file handling, edits with undo, preview and export rendering.
class ProjectController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString filePath READ filePath NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
    Q_PROPERTY(QStringList recentProjects READ recentProjects NOTIFY recentChanged)
    Q_PROPERTY(QUrl previewSource READ previewSource NOTIFY previewChanged)
    Q_PROPERTY(bool previewBusy READ previewBusy NOTIFY previewChanged)
    Q_PROPERTY(double previewProgress READ previewProgress NOTIFY previewChanged)
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY exportChanged)
    Q_PROPERTY(QVariantList transitionKinds READ transitionKinds CONSTANT)

public:
    explicit ProjectController(QObject *parent = nullptr);
    ~ProjectController() override;
    static ProjectController *instance();
    static ProjectController *create(QQmlEngine *, QJSEngine *);

    diva::Project &project() { return m_project; }

    bool hasProject() const { return m_hasProject; }
    QString name() const { return m_project.name; }
    QString filePath() const { return m_project.filePath; }
    bool dirty() const { return m_project.isDirty(); }
    double duration() const { return m_project.duration(); }
    QVariantList clips() const;
    bool canUndo() const { return m_project.canUndo(); }
    bool canRedo() const { return m_project.canRedo(); }
    bool locked() const { return m_locked; }
    void setLocked(bool locked);
    QStringList recentProjects() const;
    QUrl previewSource() const { return m_previewSource; }
    bool previewBusy() const { return m_previewBusy; }
    double previewProgress() const { return m_previewProgress; }
    bool exportBusy() const { return m_exportBusy; }
    double exportProgress() const { return m_exportProgress; }
    QString exportStatus() const { return m_exportStatus; }
    QVariantList transitionKinds() const;

    Q_INVOKABLE void newProject();
    Q_INVOKABLE bool openProject(const QUrl &file);
    // False when the project has never been saved: ask for a file and call saveAs.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl &file);
    Q_INVOKABLE bool importMedia(const QUrl &file);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool splitAt(double t);
    Q_INVOKABLE bool removeClip(int index);
    Q_INVOKABLE bool setTransition(int clipIndex, const QString &type, double duration);
    Q_INVOKABLE void setTransitionAll(const QString &type, double duration);
    // Summary of another project's edits (clips, removed time, transitions) for the copy dialog.
    Q_INVOKABLE QVariantMap describeProject(const QUrl &file) const;
    // Repeats another project's edits on this project's video: "exact" or "scaled".
    Q_INVOKABLE bool applyEditsFrom(const QUrl &file, const QString &mode);
    Q_INVOKABLE QString defaultExportPath() const;
    Q_INVOKABLE void exportVideo(const QUrl &file, int height, double fps, const QString &codec,
                                 const QString &preset);
    Q_INVOKABLE void cancelExport() { m_cancelExport = true; }

    // DivaAI's edits arrive as a whole project state, applied as one undo step.
    void applyAgentSnapshot(const QJsonObject &state, const QString &label);

signals:
    void changed();
    void lockedChanged();
    void recentChanged();
    void previewChanged();
    void exportChanged();
    void errorOccurred(const QString &message);
    void exportFinished(const QString &path);

private:
    void edited();
    void addRecent(const QString &path);
    void schedulePreview();
    void renderPreview();
    void reloadFromDisk();

    diva::Project m_project;
    bool m_hasProject = false;
    bool m_locked = false;
    QFileSystemWatcher m_watcher;
    qint64 m_lastOwnSave = 0;

    QUrl m_previewSource;
    QTimer m_previewTimer;
    bool m_previewBusy = false;
    double m_previewProgress = 0;
    std::atomic<bool> m_cancelPreview{false};
    quint64 m_previewRevision = 0;

    bool m_exportBusy = false;
    double m_exportProgress = 0;
    QString m_exportStatus;
    std::atomic<bool> m_cancelExport{false};
};
