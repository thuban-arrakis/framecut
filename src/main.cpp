#include "controller.h"
#include "mainwindow.h"
#include "startup.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>

static QString toolPath(const QString &name) {
    const QString bundled = QCoreApplication::applicationDirPath() + '/' + name;
    if (QFileInfo(bundled).isExecutable()) return bundled;
    const QString found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty()) return found;
#ifdef Q_OS_MACOS
    for (const auto &prefix : {QStringLiteral("/opt/homebrew/bin/"), QStringLiteral("/usr/local/bin/")})
        if (QFileInfo(prefix + name).isExecutable()) return prefix + name;
#endif
    return name;
}

int main(int argc, char **argv) {
    // UI and video are deliberately CPU-rendered for machines without a GPU.
    configureSoftwareMedia();
    QApplication app(argc, argv);
    configureApplication(app);

    QCommandLineParser parser;
    parser.setApplicationDescription("A local video editor. Trim clips and remove sound.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("video", "Local video to open.", "[video]");
    parser.process(app);

    const QString cacheRoot = qEnvironmentVariable("FRAMECUT_CACHE_DIR",
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    if (cacheRoot.isEmpty() || !QDir().mkpath(cacheRoot)) {
        qCritical("Could not create the preview cache directory.");
        return 1;
    }
    QTemporaryDir session(cacheRoot + "/session-XXXXXX");
    if (!session.isValid()) {
        qCritical("Could not create a temporary preview session.");
        return 1;
    }
    const QString samplePath = session.filePath("Coastal light.mp4");
    if (!QFile::copy(":/assets/sample.mp4", samplePath)) {
        qCritical("Could not prepare the bundled sample video.");
        return 1;
    }
    MediaController controller(toolPath("ffmpeg"), toolPath("ffprobe"), session.path(), samplePath);
    MainWindow window(&controller);
    controller.setWindow(&window);
    window.show();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, [&] {
        controller.cancelOperation();
        QThreadPool::globalInstance()->waitForDone();
    });
    if (!parser.positionalArguments().isEmpty()) {
        const QString path = QFileInfo(parser.positionalArguments().first()).absoluteFilePath();
        QTimer::singleShot(0, &controller, [&controller, path] { controller.importFile(path); });
    }
    return app.exec();
}
