#include "controller.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <cmath>

class ControllerLifecycleTest final : public QObject {
    Q_OBJECT

    QString tool(const QString &name) const {
#ifdef Q_OS_MACOS
        if (QFileInfo::exists("/opt/homebrew/bin/" + name)) return "/opt/homebrew/bin/" + name;
#endif
        return QStandardPaths::findExecutable(name);
    }

    QString sample() const { return QStringLiteral(FRAMECUT_SOURCE_DIR) + "/assets/sample.mp4"; }

private slots:
    void destructionCancelsRunningInspection() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString marker = temporary.filePath("inspection-started");
        QFile probe(temporary.filePath("blocking-probe"));
        QVERIFY(probe.open(QIODevice::WriteOnly));
        // exec replaces the shell, so cancellation targets the actual sleeper.
        probe.write(("#!/bin/sh\nprintf started > '" + marker + "'\nexec /bin/sleep 5\n").toUtf8());
        probe.close();
        QVERIFY(probe.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        auto finishWorkers = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); });
        auto controller = std::make_unique<MediaController>(tool("ffmpeg"), probe.fileName(),
                                                            temporary.filePath("cache"), sample());
        controller->loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 2000);
        QVERIFY(controller->busy());
        controller.reset();
        // A forgotten cancel flag leaves this worker sleeping for five seconds.
        QVERIFY(QThreadPool::globalInstance()->waitForDone(2000));
        QVERIFY(QDir(temporary.filePath("cache")).entryList({"clip-*"}, QDir::Dirs).isEmpty());
    }

    void importProgressArrivesOnGuiThread() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        MediaController controller(tool("ffmpeg"), tool("ffprobe"), temporary.filePath("cache"), sample());
        QSignalSpy loaded(&controller, &MediaController::mediaChanged);
        QSignalSpy errors(&controller, &MediaController::errorOccurred);
        QSignalSpy progress(&controller, &MediaController::progressChanged);
        bool correctThread = true;
        connect(&controller, &MediaController::progressChanged, &controller, [&] {
            correctThread = correctThread && QThread::currentThread() == controller.thread();
        }, Qt::DirectConnection);
        controller.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 30000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        QCOMPARE(loaded.count(), 1);
        QVERIFY(correctThread);
        QVERIFY(progress.count() >= 2);
        double previous = 0;
        for (const auto &arguments : progress) {
            const double value = arguments.first().toDouble();
            QVERIFY(std::isfinite(value) && value >= previous && value <= 100.0);
            previous = value;
        }
        QCOMPARE(previous, 100.0);
    }

    void exportSuggestionUsesOverrideAfterWindowCloses() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QByteArray previousDirectory = qgetenv("FRAMECUT_EXPORT_DIR");
        const bool wasSet = qEnvironmentVariableIsSet("FRAMECUT_EXPORT_DIR");
        auto restoreDirectory = qScopeGuard([&] {
            if (wasSet) qputenv("FRAMECUT_EXPORT_DIR", previousDirectory);
            else qunsetenv("FRAMECUT_EXPORT_DIR");
        });
        const QString exports = temporary.filePath("exports");
        QVERIFY(QDir().mkpath(exports));
        qputenv("FRAMECUT_EXPORT_DIR", exports.toUtf8());
        MediaController controller(tool("ffmpeg"), tool("ffprobe"), temporary.filePath("cache"), sample());
        auto window = std::make_unique<QWidget>();
        controller.setWindow(window.get());
        window.reset();
        controller.loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 30000);
        QVERIFY(controller.currentMedia());

        QString selectedDirectory;
        bool hadParent = true;
        QTimer inspectDialog;
        connect(&inspectDialog, &QTimer::timeout, &controller, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                auto *dialog = qobject_cast<QFileDialog *>(widget);
                if (!dialog || !dialog->isVisible()) continue;
                selectedDirectory = dialog->directory().canonicalPath();
                hadParent = dialog->parentWidget() != nullptr;
                dialog->reject();
                return;
            }
        });
        inspectDialog.start(10);
        controller.exportVideo();
        inspectDialog.stop();
        QCOMPARE(selectedDirectory, QFileInfo(exports).canonicalFilePath());
        QVERIFY(!hadParent);
        QVERIFY(!controller.busy());
    }

    void cleanupTestCase() { QThreadPool::globalInstance()->waitForDone(); }
};

int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    ControllerLifecycleTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "controller_lifecycle_test.moc"
