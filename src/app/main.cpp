// SPDX-License-Identifier: GPL-3.0-or-later
#include "AgentController.h"
#include "ProjectController.h"
#include "RecorderController.h"
#include "SystemInfo.h"
#include "VideoItem.h"

#include "gpu/GpuDevice.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQuickGraphicsDevice>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

// Ask hybrid-graphics laptops (NVIDIA Optimus, AMD PowerXpress) to run us on the dedicated GPU.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace {

QFile *g_log = nullptr;

void writeLog(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    if (!g_log)
        return;
    static const char *const names[] = {"debug", "warning", "critical", "fatal", "info"};
    QTextStream(g_log) << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << ' ' << names[type] << ' '
                       << message << '\n';
    g_log->flush();
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("DivaStudio"));
    QGuiApplication::setOrganizationName(QStringLiteral("DivaStudio"));
    QGuiApplication::setApplicationVersion(QStringLiteral(DIVA_VERSION));

    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(logDir);
    QFile logFile(logDir + QStringLiteral("/divastudio.log"));
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        g_log = &logFile;
        qInstallMessageHandler(writeLog);
    }

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Video to open."));
    const QCommandLineOption benchOption(QStringLiteral("bench"),
                                         QStringLiteral("Play for <seconds>, write a report and quit."),
                                         QStringLiteral("seconds"));
    const QCommandLineOption reportOption(QStringLiteral("report"), QStringLiteral("File for the --bench report."),
                                          QStringLiteral("path"));
    const QCommandLineOption shotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Save the window to <png> after --screenshot-delay."),
                                        QStringLiteral("png"));
    const QCommandLineOption shotDelayOption(QStringLiteral("screenshot-delay"),
                                             QStringLiteral("Milliseconds before --screenshot."),
                                             QStringLiteral("ms"), QStringLiteral("4000"));
    const QCommandLineOption agentOption(QStringLiteral("agent-prompt"),
                                         QStringLiteral("Run DivaAI with <prompt> after start (testing)."),
                                         QStringLiteral("prompt"));
    const QCommandLineOption panelOption(QStringLiteral("show"),
                                         QStringLiteral("Open a panel at start: record, ai, export, apply (testing)."),
                                         QStringLiteral("panel"));
    const QCommandLineOption privacyOption(QStringLiteral("privacy"),
                                           QStringLiteral("Documentation screenshots: temporary settings, no machine-specific details."));
    parser.addOptions({benchOption, reportOption, shotOption, shotDelayOption, agentOption, panelOption,
                       privacyOption});
    parser.process(app);

    if (parser.isSet(privacyOption)) {
        app.setProperty("divaPrivacy", true);
        // Must happen before the first QSettings is created.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                               + QStringLiteral("/DivaStudioPrivacy"));
    }

    std::string error;
    auto gpu = diva::GpuDevice::create(&error);
    if (!gpu) {
        qCritical() << "GPU init failed:" << error.c_str();
        return 1;
    }
    qInfo() << "GPU:" << gpu->adapterName().c_str();
    VideoItem::setSharedGpu(gpu);

    // Qt Quick renders through our D3D11 device, so decoded frames reach the screen without copies.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);

    // Poppins (SIL OFL) is bundled; it is the face of the DivaStudio look.
    for (const char *weight : {"Regular", "Medium", "SemiBold", "Bold"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/DivaStudio/fonts/Poppins-%1.ttf").arg(QLatin1String(weight)));
    QGuiApplication::setFont(QFont(QStringLiteral("Poppins"), 9));

    // QML singletons; created first so they outlive the engine.
    SystemInfo systemInfo;
    ProjectController projects;
    RecorderController recorder;
    AgentController agent;

    QQmlApplicationEngine engine;
    QUrl initialSource;
    if (!parser.positionalArguments().isEmpty())
        initialSource = QUrl::fromLocalFile(QFileInfo(parser.positionalArguments().constFirst()).absoluteFilePath());
    engine.setInitialProperties({{QStringLiteral("initialSource"), initialSource},
                                 {QStringLiteral("testPanel"), parser.value(panelOption)}});
    engine.loadFromModule("DivaStudio", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window)
        return 1;
    window->setGraphicsDevice(QQuickGraphicsDevice::fromDeviceAndContext(gpu->device(), gpu->context()));
    window->show();

    if (parser.isSet(agentOption)) {
        QTimer::singleShot(2000, &agent, [&agent, prompt = parser.value(agentOption)] { agent.run(prompt); });
    }
    if (parser.isSet(shotOption)) {
        QTimer::singleShot(parser.value(shotDelayOption).toInt(), window, [window, path = parser.value(shotOption)] {
            qInfo() << "screenshot" << path << window->grabWindow().save(path);
        });
    }
    if (parser.isSet(benchOption)) {
        const int ms = int(parser.value(benchOption).toDouble() * 1000);
        QTimer::singleShot(ms, window, [window, path = parser.value(reportOption)] {
            auto *video = window->findChild<VideoItem *>(QStringLiteral("canvasVideo"));
            const QString report = video ? video->benchReport() : QStringLiteral("no canvas video item\n");
            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text))
                file.write(report.toUtf8());
            // exit(), not quit(): a benchmark run must not stop at the "unsaved changes" question.
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}
