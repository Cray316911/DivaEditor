// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemInfo.h"

#include "Privacy.h"

#include "gpu/GpuDevice.h"
#include "media/HwEncoder.h"

#include <QJSEngine>
#include <QThreadPool>

#include <windows.h>

extern "C" {
#include <libavutil/log.h>
}

namespace {

SystemInfo *g_instance = nullptr;

using RtlGetVersionFn = LONG(WINAPI *)(OSVERSIONINFOW *);

} // namespace

SystemInfo::SystemInfo(QObject *parent) : QObject(parent)
{
    g_instance = this;
    const auto adapters = diva::GpuDevice::adapters();
    m_gpuName = privacyMode()    ? QStringLiteral("Ekran kartı")
                : adapters.empty() ? QStringLiteral("GPU bulunamadı")
                                   : QString::fromStdString(adapters.front().name);

    OSVERSIONINFOW version{sizeof version};
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
        fn(&version);
    m_build = version.dwBuildNumber;
    m_windowsVersion = privacyMode() ? QStringLiteral("Windows %1").arg(m_build >= 22000 ? 11 : 10)
                                     : QStringLiteral("Windows %1 (derleme %2)").arg(m_build >= 22000 ? 11 : 10).arg(m_build);

    QThreadPool::globalInstance()->start([this] {
        std::string error;
        auto gpu = diva::GpuDevice::create(&error);
        QVariantList list;
        if (gpu) {
            const int level = av_log_get_level();
            av_log_set_level(AV_LOG_QUIET);
            for (const diva::EncoderProbe &p : diva::HwEncoder::probe(gpu))
                list.append(QVariantMap{{"codec", QString::fromStdString(p.codec)},
                                        {"name", QString::fromStdString(p.name)},
                                        {"hardware", p.hardware},
                                        {"available", p.available},
                                        {"error", QString::fromStdString(p.error)}});
            av_log_set_level(level);
        }
        QMetaObject::invokeMethod(this, [this, list] {
            m_encoders = list;
            m_probing = false;
            emit encodersChanged();
        });
    });
}

SystemInfo *SystemInfo::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QString SystemInfo::encoderSummary() const
{
    if (m_probing)
        return QStringLiteral("Kodlayıcılar denetleniyor…");
    QStringList hardware, cpu;
    for (const QVariant &v : m_encoders) {
        const QVariantMap e = v.toMap();
        if (!e.value("available").toBool())
            continue;
        const QString codec = e.value("codec").toString().toUpper().replace(QLatin1String("HEVC"), QLatin1String("HEVC"));
        const QString label = privacyMode() ? codec : codec + " (" + e.value("name").toString() + ")";
        (e.value("hardware").toBool() ? hardware : cpu).append(label);
    }
    if (hardware.isEmpty())
        return QStringLiteral("Donanım kodlayıcı yok — CPU ile: %1").arg(cpu.join(", "));
    return QStringLiteral("GPU kodlama: %1").arg(hardware.join(", "));
}
