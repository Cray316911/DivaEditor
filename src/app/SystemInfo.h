// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// GPU, encoder and Windows capabilities for the start screen and the export dialog.
class SystemInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString gpuName READ gpuName CONSTANT)
    Q_PROPERTY(QString windowsVersion READ windowsVersion CONSTANT)
    Q_PROPERTY(bool captureRateCap READ captureRateCap CONSTANT)
    Q_PROPERTY(bool probing READ probing NOTIFY encodersChanged)
    Q_PROPERTY(QVariantList encoders READ encoders NOTIFY encodersChanged)
    Q_PROPERTY(QString encoderSummary READ encoderSummary NOTIFY encodersChanged)

public:
    explicit SystemInfo(QObject *parent = nullptr);
    static SystemInfo *create(QQmlEngine *, QJSEngine *);

    QString gpuName() const { return m_gpuName; }
    QString windowsVersion() const { return m_windowsVersion; }
    bool captureRateCap() const { return m_build >= 26100; }
    bool probing() const { return m_probing; }
    QVariantList encoders() const { return m_encoders; }
    QString encoderSummary() const;

signals:
    void encodersChanged();

private:
    QString m_gpuName;
    QString m_windowsVersion;
    unsigned m_build = 0;
    bool m_probing = true;
    QVariantList m_encoders;
};
