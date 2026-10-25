#include "controller.h"
#include "appsettings.h"
#include "mainwindow.h"
#include "previewwidget.h"
#include "startup.h"
#include "timelinewidget.h"

#include <QAbstractButton>
#include <QApplication>
#include <QAudioOutput>
#include <QCryptographicHash>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTabWidget>
#include <QTimer>
#include <QVideoFrameFormat>
#include <cmath>
#include <cstring>

class PaintTracker final : public QObject {
  public:
    QRegion dirty;
  protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint)
            dirty |= static_cast<QPaintEvent *>(event)->region();
        return false;
    }
};

class UiSmoke final : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    std::unique_ptr<MediaController> controller;
    std::unique_ptr<MainWindow> window;
    QString original, ffmpeg, ffprobe, qaDirectory;
    QByteArray sourceHash;

    QString tool(const QString &name) {
#ifdef Q_OS_MACOS
        if (QFileInfo::exists("/opt/homebrew/bin/" + name)) return "/opt/homebrew/bin/" + name;
#endif
        return QStandardPaths::findExecutable(name);
    }
    QByteArray hash(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        QCryptographicHash digest(QCryptographicHash::Sha256);
        digest.addData(&file);
        return digest.result();
    }
    void screenshot(const QString &name) {
        if (!qaDirectory.isEmpty()) {
            QDir().mkpath(qaDirectory);
            window->grab().save(qaDirectory + '/' + name + ".png");
        }
    }

    void dragEdge(TimelineWidget *timeline, const QPoint &from, const QPoint &to, bool cancel = false) {
        QApplication::setActiveWindow(window.get());
        QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(timeline->mapToGlobal(to)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &move);
        if (cancel) QTest::keyClick(timeline, Qt::Key_Escape);
        QTest::mouseRelease(timeline, Qt::LeftButton, Qt::NoModifier, to);
        QCoreApplication::processEvents();
    }

private slots:
    void initTestCase() {
        QVERIFY(temporary.isValid());
        original = temporary.filePath("Coastal light.mp4");
        QVERIFY(QFile::copy(QStringLiteral(FRAMECUT_SOURCE_DIR) + "/assets/sample.mp4", original));
        sourceHash = hash(original);
        ffmpeg = tool("ffmpeg"); ffprobe = tool("ffprobe");
        QVERIFY(!ffmpeg.isEmpty() && !ffprobe.isEmpty());
        qaDirectory = qEnvironmentVariable("FRAMECUT_QA_DIR");
        controller = std::make_unique<MediaController>(ffmpeg, ffprobe, temporary.filePath("cache"), original);
        window = std::make_unique<MainWindow>(controller.get());
        controller->setWindow(window.get());
        window->show();
        QVERIFY(window->findChild<QPushButton *>("sampleButton"));
        QVERIFY(!window->findChild<QPushButton *>("exportButton")->isEnabled());
        QTest::qWait(80);
        screenshot("01-empty");
    }

    void importPlaySeekAndCompareAudio() {
        QSignalSpy loaded(controller.get(), &MediaController::mediaChanged);
        QSignalSpy errors(controller.get(), &MediaController::errorOccurred);
        auto *preview = window->findChild<PreviewWidget *>("previewWidget");
        QVERIFY(preview);
        QSignalSpy frames(preview, &PreviewWidget::framePresented);
        QTest::mouseClick(window->findChild<QPushButton *>("sampleButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(loaded.count() == 1 || errors.count() > 0, 30000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        QVERIFY(controller->currentMedia());
        QCOMPARE(controller->currentMedia()->audioTracks, 1);
        QVERIFY(controller->currentMedia()->previewTranscoded);
        QVERIFY(controller->removeAudio());
        auto *audio = window->findChild<QAudioOutput *>();
        auto *player = window->findChild<QMediaPlayer *>("mediaPlayer");
        QVERIFY(audio && player);
        QVERIFY(audio->isMuted());
        QVERIFY(window->findChild<QPushButton *>("exportButton")->isEnabled());
        QTest::mouseClick(window->findChild<QPushButton *>("playButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 2, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(player->position() > 400, 5000);
        QVERIFY(!preview->currentImage().isNull());
        QVERIFY(preview->currentImage().width() <= 854);
        QVERIFY(preview->currentImage().height() <= 480);
        QTest::mouseClick(window->findChild<QPushButton *>("playButton"), Qt::LeftButton);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        auto *timeline = window->findChild<TimelineWidget *>("timelineWidget");
        QVERIFY(timeline);
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(timeline->width() / 2, 68));
        QTRY_VERIFY_WITH_TIMEOUT(player->position() > 2000, 5000);
        auto *toggle = window->findChild<QAbstractButton *>("removeAudioToggle");
        QVERIFY(toggle);
        QTest::mouseClick(toggle, Qt::LeftButton);
        QVERIFY(!audio->isMuted());
        QVERIFY(!controller->removeAudio());
        QVERIFY(!window->findChild<QPushButton *>("exportButton")->isEnabled());
        QTest::mouseClick(toggle, Qt::LeftButton);
        QVERIFY(audio->isMuted());
        QVERIFY(window->findChild<QPushButton *>("exportButton")->isEnabled());
        screenshot("02-loaded");
    }

    void timelinePlaybackRepaintsOnlyPlayhead() {
        TimelineWidget timeline;
        MediaInfo media;
        media.duration = 8;
        media.fps = 25;
        timeline.setMedia(&media);
        timeline.resize(900, 180);
        PaintTracker tracker;
        timeline.installEventFilter(&tracker);
        timeline.show();
        QTRY_VERIFY(!tracker.dirty.isEmpty());
        QTest::qWait(30);
        tracker.dirty = {};
        timeline.setPosition(2.5);
        QTRY_VERIFY(!tracker.dirty.isEmpty());
        qint64 paintedArea = 0;
        for (const auto &rect : tracker.dirty)
            paintedArea += qint64(rect.width()) * rect.height();
        QVERIFY(paintedArea < qint64(timeline.width()) * timeline.height() / 10);
        QVERIFY(!tracker.dirty.contains(QPoint(20, 70))); // Track labels are unchanged.
    }

    void interruptedTrimStopsWhenMouseButtonIsReleased() {
        TimelineWidget timeline;
        MediaInfo media;
        media.duration = 8;
        media.fps = 25;
        timeline.setMedia(&media);
        timeline.resize(900, 180);
        timeline.show();
        QSignalSpy changes(&timeline, &TimelineWidget::trimRangeChanged);
        QSignalSpy finished(&timeline, &TimelineWidget::trimGestureFinished);
        const QPoint left = timeline.leftHandleRect().center().toPoint();
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, left);
        const QPoint dragged = left + QPoint(100, 0);
        QMouseEvent drag(QEvent::MouseMove, QPointF(dragged), QPointF(timeline.mapToGlobal(dragged)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&timeline, &drag);
        QCOMPARE(changes.count(), 1);
        const QRectF trimmed = timeline.clipRect();
        const QPoint returned = dragged + QPoint(100, 0);
        QMouseEvent returnedWithoutButton(QEvent::MouseMove, QPointF(returned),
                                         QPointF(timeline.mapToGlobal(returned)),
                                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&timeline, &returnedWithoutButton);
        QCOMPARE(timeline.clipRect(), trimmed);
        QCOMPARE(changes.count(), 1);
        QCOMPARE(finished.count(), 1);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, returned);
        QCOMPARE(changes.count(), 1);
    }

    void softwarePreviewHandlesOddFrameSizes_data() {
        QTest::addColumn<int>("pixelFormat");
        QTest::addColumn<QSize>("frameSize");
        QTest::newRow("packed-rgb") << int(QVideoFrameFormat::Format_RGBX8888) << QSize(9, 7);
        // Qt-owned subsampled fixtures need an even height to allocate complete
        // chroma rows; an odd width still exercises chroma stride rounding.
        QTest::newRow("planar-yuv") << int(QVideoFrameFormat::Format_YUV420P) << QSize(9, 8);
        QTest::newRow("swapped-chroma") << int(QVideoFrameFormat::Format_YV12) << QSize(9, 8);
        QTest::newRow("interleaved-chroma") << int(QVideoFrameFormat::Format_NV12) << QSize(9, 8);
    }

    void softwarePreviewHandlesOddFrameSizes() {
        QFETCH(int, pixelFormat);
        QFETCH(QSize, frameSize);
        QVideoFrameFormat format(frameSize, QVideoFrameFormat::PixelFormat(pixelFormat));
        format.setColorRange(QVideoFrameFormat::ColorRange_Full);
        QVideoFrame frame(format);
        QVERIFY(frame.map(QVideoFrame::WriteOnly));
        for (int plane = 0; plane < frame.planeCount(); ++plane)
            std::memset(frame.bits(plane), 128, size_t(frame.mappedBytes(plane)));
        if (pixelFormat == int(QVideoFrameFormat::Format_RGBX8888)) {
            // QImage::Format_RGBX8888 requires an opaque fourth byte. Set it
            // explicitly; older Qt versions expose its value in pixelColor().
            for (int y = 0; y < frame.height(); ++y) {
                auto *row = frame.bits(0) + y * frame.bytesPerLine(0);
                for (int x = 0; x < frame.width(); ++x)
                    row[4 * x + 3] = 255;
            }
        }
        frame.unmap();
        PreviewWidget preview;
        QSignalSpy presented(&preview, &PreviewWidget::framePresented);
        preview.setFrame(frame);
        QCOMPARE(presented.count(), 1);
        const QImage image = preview.currentImage();
        QCOMPARE(image.size(), frameSize);
        const QColor lastPixel = image.pixelColor(frameSize.width() - 1, frameSize.height() - 1);
        QVERIFY2(std::abs(lastPixel.red() - 128) <= 2, qPrintable(lastPixel.name(QColor::HexArgb)));
        QVERIFY(std::abs(lastPixel.green() - 128) <= 2);
        QVERIFY(std::abs(lastPixel.blue() - 128) <= 2);
        QCOMPARE(lastPixel.alpha(), 255);
    }

    void exportThroughSaveDialog() {
        QVERIFY(controller->currentMedia());
        const QString output = temporary.filePath("Silent export.mp4");
        QSignalSpy completed(controller.get(), &MediaController::exportCompleted);
        QSignalSpy errors(controller.get(), &MediaController::errorOccurred);
        auto selected = std::make_shared<bool>(false);
        QTimer::singleShot(100, this, [output, selected] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible()) {
                    dialog->setDirectory(QFileInfo(output).absolutePath());
                    auto *filename = dialog->findChild<QLineEdit *>("fileNameEdit");
                    if (!filename) { dialog->reject(); return; }
                    filename->setText(output);
                    const QFileInfo picked(dialog->selectedFiles().value(0));
                    if (picked.absoluteDir().canonicalPath() != QFileInfo(output).absoluteDir().canonicalPath()
                        || picked.fileName() != QFileInfo(output).fileName()) {
                        qWarning() << "Save dialog automation selected" << dialog->selectedFiles();
                        dialog->reject();
                        return;
                    }
                    *selected = true;
                    static_cast<QDialog *>(dialog)->accept();
                    return;
                }
            }
        });
        QTimer::singleShot(10000, this, [this] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible()) dialog->reject();
        });
        QTest::mouseClick(window->findChild<QPushButton *>("exportButton"), Qt::LeftButton);
        QVERIFY(*selected);
        QTRY_VERIFY_WITH_TIMEOUT(completed.count() == 1 || errors.count() > 0, 15000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        QCOMPARE(QFileInfo(completed.first().first().toString()).canonicalFilePath(), QFileInfo(output).canonicalFilePath());
        QVERIFY(QFileInfo(output).size() > 0);
        QCOMPARE(hash(original), sourceHash);
        QProcess probe;
        probe.start(ffprobe, {"-v", "error", "-show_streams", "-of", "json", output});
        QVERIFY(probe.waitForFinished(10000));
        QCOMPARE(probe.exitCode(), 0);
        const auto streams = QJsonDocument::fromJson(probe.readAllStandardOutput()).object()["streams"].toArray();
        int videos = 0;
        for (const auto &stream : streams) {
            QVERIFY(stream.toObject()["codec_type"] != "audio");
            if (stream.toObject()["codec_type"] == "video") {
                ++videos;
                QCOMPARE(stream.toObject()["width"].toInt(), 960);
                QCOMPARE(stream.toObject()["height"].toInt(), 540);
            }
        }
        QCOMPARE(videos, 1);
        QCOMPARE(QFileInfo(controller->lastExportPath()).canonicalFilePath(), QFileInfo(output).canonicalFilePath());
        if (auto *dialog = window->findChild<QDialog *>("exportSuccessDialog")) {
            if (!qaDirectory.isEmpty()) dialog->grab().save(qaDirectory + "/03-export-complete.png");
            dialog->accept();
        }
    }

    void invalidImportPreservesCurrentVideo() {
        QVERIFY(controller->currentMedia());
        const QString currentId = controller->currentMedia()->id;
        const QString bad = temporary.filePath("not-a-video.mp4");
        QFile file(bad); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not a video"); file.close();
        QSignalSpy errors(controller.get(), &MediaController::errorOccurred);
        controller->importFile(bad);
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() == 1, 10000);
        QCOMPARE(controller->currentMedia()->id, currentId);
        QVERIFY(!controller->busy());
        QCOMPARE(hash(original), sourceHash);
    }

    void livePreferencesAndPersistence() {
        auto *settings = AppSettings::instance();
        auto restore = qScopeGuard([settings] { settings->setTheme("dark"); settings->setLanguage("en"); });
        QVERIFY(controller->currentMedia());
        const QString mediaId = controller->currentMedia()->id;
        auto *player = window->findChild<QMediaPlayer *>("mediaPlayer");
        player->pause();
        const qint64 position = player->position();
        const QUrl source = player->source();
        auto *settingsButton = window->findChild<QPushButton *>("settingsButton");
        QVERIFY(settingsButton);
        QTest::mouseClick(settingsButton, Qt::LeftButton);
        auto *theme = window->findChild<QComboBox *>("themeCombo");
        auto *language = window->findChild<QComboBox *>("languageCombo");
        QVERIFY(theme && language);
        QVERIFY(theme->findData("light") >= 0);
        QVERIFY(language->findData("ru") >= 0);
        theme->setCurrentIndex(theme->findData("light"));
        QCOMPARE(settings->theme(), QString("light"));
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() > 200);
        language->setCurrentIndex(language->findData("ru"));
        QCOMPARE(settings->language(), QString("ru"));
        QTRY_COMPARE(window->findChild<QPushButton *>("exportButton")->text(), QString::fromUtf8("Экспорт"));
        QCOMPARE(controller->currentMedia()->id, mediaId);
        QCOMPARE(player->source(), source);
        QCOMPARE(player->position(), position);
        QCOMPARE(controller->currentMedia()->name, QString("Coastal light.mp4"));
        if (auto *dialog = qobject_cast<QDialog *>(theme->window())) {
            if (!qaDirectory.isEmpty()) dialog->grab().save(qaDirectory + "/04-settings-ru-light.png");
            dialog->accept();
        }
        QTest::qWait(50);
        screenshot("05-light-russian");

        QProcess probe;
        probe.start(QCoreApplication::applicationFilePath(), {"--preferences-probe"});
        QVERIFY(probe.waitForFinished(10000));
        QCOMPARE(probe.exitCode(), 0);
        const auto persisted = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        QCOMPARE(persisted.value("theme").toString(), QString("light"));
        QCOMPARE(persisted.value("language").toString(), QString("ru"));
        QCOMPARE(persisted.value("export").toString(), QString::fromUtf8("Экспорт"));

        QSignalSpy errors(controller.get(), &MediaController::errorOccurred);
        controller->importFile(temporary.filePath("missing-video.mp4"));
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() == 1, 10000);
        QVERIFY(errors.first().first().toString().contains(QRegularExpression(QStringLiteral("[А-Яа-я]"))));
        settings->setTheme("dark");
        screenshot("06-dark-russian");
        settings->setLanguage("en");
        settings->setTheme("light");
        screenshot("07-light-english");
        QCOMPARE(hash(original), sourceHash);
    }

    void trimHandlesUndoPreviewAndLocalization() {
        QVERIFY(controller->currentMedia());
        auto *timeline = window->findChild<TimelineWidget *>("timelineWidget");
        auto *player = window->findChild<QMediaPlayer *>("mediaPlayer");
        auto *start = window->findChild<QDoubleSpinBox *>("trimStartSpin");
        auto *end = window->findChild<QDoubleSpinBox *>("trimEndSpin");
        QVERIFY(timeline && player && start && end);
        const double duration = controller->currentMedia()->duration;
        const QString mediaId = controller->currentMedia()->id;
        const QUrl source = player->source();
        auto *settings = AppSettings::instance();
        auto restore = qScopeGuard([&] {
            player->pause(); controller->resetTrim(); controller->setRemoveAudio(true);
            settings->setTheme("dark"); settings->setLanguage("en");
            window->resize(1440, 900);
        });
        controller->resetTrim();
        player->pause();
        const QRectF full = timeline->clipRect();
        const QPoint left = timeline->leftHandleRect().center().toPoint();
        dragEdge(timeline, left, left + QPoint(qRound(full.width() * .2), 0));
        QVERIFY(controller->trimStart() > 1.0);
        QCOMPARE(controller->trimEnd(), duration);
        QVERIFY(timeline->clipRect().width() < full.width() * .85);
        QVERIFY(controller->canUndoTrim());
        const double firstStart = controller->trimStart();
        QCOMPARE(start->value(), std::round(firstStart * 1000) / 1000);
        controller->undoTrim();
        QCOMPARE(controller->trimStart(), 0.0);
        QCOMPARE(controller->trimEnd(), duration);
        controller->redoTrim();
        QCOMPARE(controller->trimStart(), firstStart);

        const QPoint right = timeline->rightHandleRect().center().toPoint();
        dragEdge(timeline, right, right - QPoint(qRound(full.width() * .2), 0));
        QVERIFY(controller->trimEnd() < duration - 1);
        QCOMPARE(controller->trimStart(), firstStart);
        const double selectedEnd = controller->trimEnd();
        const QPoint cancelEdge = timeline->leftHandleRect().center().toPoint();
        dragEdge(timeline, cancelEdge, cancelEdge + QPoint(50, 0), true);
        QCOMPARE(controller->trimStart(), firstStart);
        QCOMPARE(controller->trimEnd(), selectedEnd);
        controller->undoTrim();
        QCOMPARE(controller->trimEnd(), duration);
        QCOMPARE(controller->trimStart(), firstStart);
        controller->redoTrim();
        QCOMPARE(controller->trimEnd(), selectedEnd);

        dragEdge(timeline, timeline->leftHandleRect().center().toPoint(), QPoint(110, 70));
        QCOMPARE(controller->trimStart(), 0.0);
        dragEdge(timeline, timeline->rightHandleRect().center().toPoint(), QPoint(timeline->width() - 4, 70));
        QCOMPARE(controller->trimEnd(), duration);
        QVERIFY(!controller->hasTrim());

        QVERIFY(start->isVisible());
        start->setValue(1.25);
        QMetaObject::invokeMethod(start, "editingFinished", Qt::DirectConnection);
        end->setValue(3.75);
        QMetaObject::invokeMethod(end, "editingFinished", Qt::DirectConnection);
        QCOMPARE(controller->trimStart(), 1.25);
        QCOMPARE(controller->trimEnd(), 3.75);
        QCOMPARE(controller->trimmedDuration(), 2.5);
        QCOMPARE(player->source(), source);
        QCOMPARE(controller->currentMedia()->id, mediaId);
        // The right-edge preview shows the last included frame; Play should
        // restart the selected moment instead of immediately stopping again.
        QTest::mouseClick(window->findChild<QPushButton *>("playButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(player->position() < 2500, 2000);
        QTest::mouseClick(window->findChild<QPushButton *>("playButton"), Qt::LeftButton);
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(135, 12));
        QTRY_VERIFY(player->position() >= 1249 && player->position() <= 1251);
        settings->setTheme("light"); settings->setLanguage("ru");
        QTest::qWait(40);
        screenshot("10-trim-light-russian");
        window->resize(1100, 720);
        QTest::qWait(40);
        screenshot("11-trim-minimum-russian");
        settings->setTheme("dark"); settings->setLanguage("en");
        window->resize(1440, 900);
        QTest::qWait(40);
        screenshot("12-trim-dark-english");
        QCOMPARE(controller->trimStart(), 1.25);
        QCOMPARE(controller->trimEnd(), 3.75);

        controller->setTrimRange(1.0, 1.5);
        player->setPosition(1000);
        QTest::mouseClick(window->findChild<QPushButton *>("playButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(player->position() >= 1400, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(player->playbackState() != QMediaPlayer::PlayingState, 5000);
        QVERIFY(player->position() <= 1550);
        QCOMPARE(hash(original), sourceHash);
    }

    void trimRangesStayWithTheirClips() {
        const QString firstId = controller->currentMedia()->id;
        const double duration = controller->currentMedia()->duration;
        controller->setTrimRange(1, 4);
        const QString extraPath = temporary.filePath("Independent trim.mp4");
        QVERIFY(QFile::copy(original, extraPath));
        controller->importFile(extraPath);
        controller->setTrimRange(0, duration);
        QCOMPARE(controller->trimStart(), 1.0); // Busy import cannot modify existing edit.
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 30000);
        const QString secondId = controller->currentMedia()->id;
        QVERIFY(secondId != firstId);
        QCOMPARE(controller->trimStart(), 0.0);
        QCOMPARE(controller->trimEnd(), duration);
        controller->setTrimRange(2, 5);
        auto *library = window->findChild<QListWidget *>("mediaList");
        QVERIFY(library);
        QSignalSpy libraryResets(library->model(), &QAbstractItemModel::modelReset);
        auto *firstItem = library->item(0);
        auto *secondItem = library->item(1);
        controller->selectMedia(firstId);
        QCOMPARE(controller->trimStart(), 1.0);
        QCOMPARE(controller->trimEnd(), 4.0);
        controller->selectMedia(secondId);
        QCOMPARE(controller->trimStart(), 2.0);
        QCOMPARE(controller->trimEnd(), 5.0);
        QCOMPARE(libraryResets.count(), 0);
        QCOMPARE(library->item(0), firstItem);
        QCOMPARE(library->item(1), secondItem);
        controller->removeCurrentMedia();
        QCOMPARE(controller->currentMedia()->id, firstId);
        QCOMPARE(controller->trimStart(), 1.0);
        controller->resetTrim();
        QCOMPARE(controller->trimEnd(), duration);
        QCOMPARE(hash(original), sourceHash);
        QCOMPARE(hash(extraPath), sourceHash);
    }

    void exportTrimWithOriginalAudio() {
        controller->setTrimRange(1.25, 3.75);
        controller->setRemoveAudio(false);
        auto restore = qScopeGuard([&] { controller->resetTrim(); controller->setRemoveAudio(true); });
        auto *button = window->findChild<QPushButton *>("exportButton");
        QVERIFY(button->isEnabled());
        const QString output = temporary.filePath("Trimmed with sound.mp4");
        QSignalSpy completed(controller.get(), &MediaController::exportCompleted);
        QSignalSpy errors(controller.get(), &MediaController::errorOccurred);
        auto selected = std::make_shared<bool>(false);
        QTimer::singleShot(100, this, [output, selected] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                auto *dialog = qobject_cast<QFileDialog *>(widget);
                if (!dialog || !dialog->isVisible()) continue;
                dialog->setDirectory(QFileInfo(output).absolutePath());
                auto *filename = dialog->findChild<QLineEdit *>("fileNameEdit");
                if (!filename) { dialog->reject(); return; }
                filename->setText(output);
                *selected = true;
                static_cast<QDialog *>(dialog)->accept();
                return;
            }
        });
        QTimer::singleShot(10000, this, [] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible()) dialog->reject();
        });
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(*selected);
        QVERIFY(controller->busy());
        controller->setTrimRange(0, 8);
        QCOMPARE(controller->trimStart(), 1.25);
        QCOMPARE(controller->trimEnd(), 3.75);
        QTRY_VERIFY_WITH_TIMEOUT(completed.count() == 1 || errors.count() > 0, 30000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        QProcess probe;
        probe.start(ffprobe, {"-v", "error", "-show_streams", "-show_format", "-of", "json", output});
        QVERIFY(probe.waitForFinished(10000));
        QCOMPARE(probe.exitCode(), 0);
        const auto info = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        QVERIFY(std::abs(info["format"].toObject()["duration"].toString().toDouble() - 2.5) < .085);
        int videoCount = 0, audioCount = 0;
        for (const auto &entry : info["streams"].toArray()) {
            const auto stream = entry.toObject();
            if (stream["codec_type"] == "video") {
                ++videoCount;
                QCOMPARE(stream["width"].toInt(), 960);
                QCOMPARE(stream["height"].toInt(), 540);
            }
            if (stream["codec_type"] == "audio") ++audioCount;
        }
        QCOMPARE(videoCount, 1); QCOMPARE(audioCount, 1);
        if (auto *dialog = window->findChild<QDialog *>("exportSuccessDialog")) {
            if (!qaDirectory.isEmpty()) dialog->grab().save(qaDirectory + "/13-trim-export-complete.png");
            dialog->accept();
        }
        QCOMPARE(hash(original), sourceHash);
    }

    void removeImportedMediaWithoutDeletingOriginals() {
        auto *removeButton = window->findChild<QPushButton *>("removeMediaButton");
        auto *library = window->findChild<QListWidget *>("mediaList");
        auto *player = window->findChild<QMediaPlayer *>("mediaPlayer");
        QVERIFY(removeButton && library && player);
        const MediaInfo first = *controller->currentMedia();
        const QString secondPath = temporary.filePath("Second clip.mp4");
        const QString thirdPath = temporary.filePath("Third clip.mp4");
        QVERIFY(QFile::copy(original, secondPath));
        QVERIFY(QFile::copy(original, thirdPath));
        controller->importFile(secondPath);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 30000);
        QCOMPARE(controller->library().size(), 2);
        controller->importFile(thirdPath);
        const QString activeBeforeBusy = controller->currentMedia()->id;
        controller->removeCurrentMedia();
        QCOMPARE(controller->currentMedia()->id, activeBeforeBusy);
        QCOMPARE(controller->library().size(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 30000);
        QCOMPARE(controller->library().size(), 3);
        const QString thirdId = controller->currentMedia()->id;
        const QUrl thirdSource = player->source();
        controller->removeMedia(first.id);
        QCOMPARE(controller->library().size(), 2);
        QCOMPARE(controller->currentMedia()->id, thirdId);
        QCOMPARE(player->source(), thirdSource);
        QVERIFY(!QFileInfo::exists(first.thumbnailPath));
        QCOMPARE(hash(original), sourceHash);

        QTest::mouseClick(removeButton, Qt::LeftButton);
        QCOMPARE(controller->library().size(), 1);
        QCOMPARE(controller->currentMedia()->path, QFileInfo(secondPath).canonicalFilePath());
        QCOMPARE(hash(thirdPath), sourceHash);
        QApplication::setActiveWindow(window.get());
        library->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(library->hasFocus());
        QTest::keyClick(library, Qt::Key_Delete);
        QTRY_VERIFY(controller->library().isEmpty());
        QVERIFY(controller->currentMedia() == nullptr);
        QVERIFY(player->source().isEmpty());
        QVERIFY(!removeButton->isEnabled());
        QVERIFY(!window->findChild<QPushButton *>("exportButton")->isEnabled());
        QCOMPARE(hash(secondPath), sourceHash);
        QCOMPARE(hash(original), sourceHash);
        auto *settings = AppSettings::instance();
        settings->setTheme("light"); settings->setLanguage("ru");
        QTest::qWait(40);
        screenshot("08-empty-light-russian");
        window->resize(1100, 720);
        QTest::qWait(40);
        screenshot("09-minimum-light-russian");
        settings->setTheme("dark"); settings->setLanguage("en");
        controller->loadSample();
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 30000);
        QVERIFY(controller->currentMedia());
        QCOMPARE(controller->library().size(), 1);
        QCOMPARE(hash(original), sourceHash);
    }

    void removingMediaPreservesAnotherImportedPreview() {
        QVERIFY(controller->currentMedia());
        const MediaInfo source = *controller->currentMedia();
        QVERIFY(source.previewTranscoded);
        controller->importFile(source.previewPath);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 30000);
        QCOMPARE(controller->library().size(), 2);
        const MediaInfo importedPreview = *controller->currentMedia();
        QCOMPARE(importedPreview.path, QFileInfo(source.previewPath).canonicalFilePath());
        controller->removeMedia(source.id);
        QCOMPARE(controller->library().size(), 1);
        QVERIFY(QFileInfo::exists(importedPreview.path));
        MediaEngine verifier(ffmpeg, ffprobe, temporary.filePath("verify-cache"));
        const auto result = verifier.exportSilent(importedPreview, temporary.filePath("preview-silent.mp4"),
                                                 std::make_shared<std::atomic_bool>(false));
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(hash(original), sourceHash);
    }

    void cleanupTestCase() {
        if (controller) controller->cancelOperation();
        QThreadPool::globalInstance()->waitForDone();
        window.reset(); controller.reset();
    }
};

int main(int argc, char **argv) {
    configureSoftwareMedia();
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    if (app.arguments().contains("--preferences-probe")) {
        configureApplication(app);
        QJsonObject result{{"theme", AppSettings::instance()->theme()},
                           {"language", AppSettings::instance()->language()},
                           {"export", QCoreApplication::translate("MainWindow", "Export")}};
        printf("%s\n", QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
        return 0;
    }
    QTemporaryDir settingsDirectory;
    qputenv("FRAMECUT_SETTINGS_FILE", settingsDirectory.filePath("settings.ini").toUtf8());
    configureApplication(app);
    UiSmoke test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_smoke.moc"
