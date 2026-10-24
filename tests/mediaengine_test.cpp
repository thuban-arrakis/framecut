#include "mediaengine.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <limits>
#include <memory>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
QString tool(const char *environment, const QString &name)
{
    const QString configured = qEnvironmentVariable(environment);
    if (!configured.isEmpty()) return configured;
#ifdef Q_OS_MACOS
    const QString homebrew = QStringLiteral("/opt/homebrew/bin/") + name;
    if (QFileInfo::exists(homebrew)) return homebrew;
#endif
    return QStandardPaths::findExecutable(name);
}

struct CommandResult { bool ok; QByteArray output; QByteArray error; };

CommandResult command(const QString &binary, const QStringList &arguments)
{
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(binary, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) return {false, {}, process.errorString().toUtf8()};
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished();
        return {false, {}, QByteArrayLiteral("Test command timed out")};
    }
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            process.readAllStandardOutput(), process.readAllStandardError()};
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

CancelFlag flag() { return std::make_shared<std::atomic_bool>(false); }
}

class MediaEngineTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir directory;
    std::unique_ptr<MediaEngine> engine;
    QString ffmpeg;
    QString ffprobe;
    QString source;
    MediaInfo media;

    QString path(const QString &name) const { return directory.filePath(name); }
    QJsonObject inspect(const QString &file) const
    {
        const auto result = command(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-show_streams"), QStringLiteral("-show_format"), QStringLiteral("-show_chapters"), QStringLiteral("-of"), QStringLiteral("json"), file});
        return result.ok ? QJsonDocument::fromJson(result.output).object() : QJsonObject{};
    }
    QByteArray frameHashes(const QString &file, int videoIndex = 0) const
    {
        const auto result = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-hwaccel"), QStringLiteral("none"), QStringLiteral("-i"), file,
            QStringLiteral("-map"), QStringLiteral("0:v:%1").arg(videoIndex), QStringLiteral("-f"), QStringLiteral("framemd5"), QStringLiteral("-")});
        if (!result.ok) return {};
        QByteArray hashes;
        for (const auto &line : result.output.split('\n')) {
            if (line.isEmpty() || line.startsWith('#')) continue;
            hashes += line.split(',').last().trimmed() + '\n';
        }
        return hashes;
    }
    QByteArray frameLuma(const QString &file) const
    {
        const auto result = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-hwaccel"), QStringLiteral("none"), QStringLiteral("-i"), file,
            QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-vf"), QStringLiteral("scale=1:1:flags=area,format=gray"),
            QStringLiteral("-fps_mode"), QStringLiteral("passthrough"), QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-")});
        return result.ok ? result.output : QByteArray{};
    }
    bool noStagingFiles() const
    {
        return QDir(directory.path()).entryList({QStringLiteral(".framecut-export-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(directory.isValid());
        ffmpeg = tool("FRAMECUT_TEST_FFMPEG", QStringLiteral("ffmpeg"));
        ffprobe = tool("FRAMECUT_TEST_FFPROBE", QStringLiteral("ffprobe"));
        QVERIFY2(!ffmpeg.isEmpty() && !ffprobe.isEmpty(), "FFmpeg and FFprobe are required for the integration tests.");
        source = path(QStringLiteral("original; literal $ video.mp4"));
        const auto created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc2=size=160x90:rate=24:duration=1.25"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=1.25"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=880:duration=1.25"),
            QStringLiteral("-map"), QStringLiteral("0:v"), QStringLiteral("-map"), QStringLiteral("1:a"), QStringLiteral("-map"), QStringLiteral("2:a"),
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-c:a"), QStringLiteral("aac"), source});
        QVERIFY2(created.ok, created.error.constData());
        engine = std::make_unique<MediaEngine>(ffmpeg, ffprobe, path(QStringLiteral("cache")));
        const auto imported = engine->importMedia(source, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        media = imported.media;
    }

    void importsMetadataAndThumbnail()
    {
        QCOMPARE(media.name, QFileInfo(source).fileName());
        QCOMPARE(media.width, 160);
        QCOMPARE(media.height, 90);
        QCOMPARE(media.audioTracks, 2);
        QCOMPARE(media.fps, 24.0);
        QCOMPARE(media.codec, QStringLiteral("h264"));
        QVERIFY(media.duration >= 1.2 && media.duration < 1.4);
        QVERIFY(!media.previewTranscoded);
        QCOMPARE(media.previewPath, QFileInfo(source).canonicalFilePath());
        QVERIFY(QFileInfo(media.thumbnailPath).size() > 0);
    }

    void singleFrameVideoCanBeImported()
    {
        const QString original = path(QStringLiteral("single-frame.mp4"));
        const auto created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=c=blue:size=160x90:rate=24"),
            QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), original});
        QVERIFY2(created.ok, created.error.constData());
        const auto imported = engine->importMedia(original, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QVERIFY(imported.media.duration > 0 && imported.media.duration < 0.05);
        QVERIFY(QFileInfo(imported.media.thumbnailPath).size() > 0);
    }

    void previewStopsAtVideoEndWhenAudioIsLonger()
    {
        const QString original = path(QStringLiteral("long-audio.mp4"));
        const auto created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-i"), source, QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("sine=frequency=440:duration=5"), QStringLiteral("-map"), QStringLiteral("0:v:0"),
            QStringLiteral("-map"), QStringLiteral("1:a:0"), QStringLiteral("-vf"), QStringLiteral("scale=960:540"),
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
            QStringLiteral("-c:a"), QStringLiteral("aac"), original});
        QVERIFY2(created.ok, created.error.constData());
        const auto originalHash = QCryptographicHash::hash(fileBytes(original), QCryptographicHash::Sha256);
        const auto imported = engine->importMedia(original, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QVERIFY(imported.media.previewTranscoded);
        const auto preview = inspect(imported.media.previewPath);
        QCOMPARE(preview.value(QStringLiteral("streams")).toArray().size(), 2);
        const double previewDuration = preview.value(QStringLiteral("format")).toObject()
            .value(QStringLiteral("duration")).toString().toDouble();
        QVERIFY(previewDuration > 1.2 && previewDuration < 1.35);
        QCOMPARE(QCryptographicHash::hash(fileBytes(original), QCryptographicHash::Sha256), originalHash);
    }

    void exportRemovesAllAudioPreservesFramesAndOriginal()
    {
        const auto original = fileBytes(source);
        QVERIFY(!original.isEmpty());
        QList<double> progress;
        const QString target = path(QStringLiteral("silent video.mp4"));
        const auto result = engine->exportSilent(media, target, flag(), [&](double percent) { progress.append(percent); });
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto streams = inspect(target).value(QStringLiteral("streams")).toArray();
        QCOMPARE(streams.size(), 1);
        const auto video = streams.first().toObject();
        QCOMPARE(video.value(QStringLiteral("codec_type")).toString(), QStringLiteral("video"));
        QCOMPARE(video.value(QStringLiteral("codec_name")).toString(), QStringLiteral("h264"));
        QCOMPARE(video.value(QStringLiteral("width")).toInt(), 160);
        QCOMPARE(video.value(QStringLiteral("height")).toInt(), 90);
        const auto hashes = frameHashes(source);
        QVERIFY(!hashes.isEmpty());
        QCOMPARE(frameHashes(target), hashes);
        QCOMPARE(fileBytes(source), original);
        QCOMPARE(result.size, QFileInfo(target).size());
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.first(), 0.0);
        QCOMPARE(progress.last(), 100.0);
        for (double percent : progress) QVERIFY(percent >= 0 && percent <= 100);
        QVERIFY(noStagingFiles());
    }

    void existingDestinationAndOriginalAreProtected()
    {
        const QString target = path(QStringLiteral("existing.mp4"));
        QVERIFY(write(target, QByteArrayLiteral("preserve existing data")));
        const auto exists = engine->exportSilent(media, target, flag());
        QVERIFY(!exists.ok);
        QVERIFY(exists.error.contains(QStringLiteral("already exists")));
        QCOMPARE(fileBytes(target), QByteArrayLiteral("preserve existing data"));
        const auto same = engine->exportSilent(media, source, flag());
        QVERIFY(!same.ok);
        QVERIFY(same.error.contains(QStringLiteral("original")));
#ifdef Q_OS_UNIX
        const QString symlinkPath = path(QStringLiteral("output-link.mp4"));
        QCOMPARE(::symlink(QFile::encodeName(target).constData(), QFile::encodeName(symlinkPath).constData()), 0);
        QVERIFY(!engine->exportSilent(media, symlinkPath, flag()).ok);
#endif
    }

    void trimCutsDecodedFramesInsteadOfKeyframesAndKeepsAudioWhenRequested()
    {
        const QString original = path(QStringLiteral("long-gop.mp4"));
        const auto created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("nullsrc=size=160x90:rate=24:duration=4,geq=lum='16+N*2':cb=128:cr=128"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=660:duration=4"),
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-crf"), QStringLiteral("0"),
            QStringLiteral("-g"), QStringLiteral("240"), QStringLiteral("-keyint_min"), QStringLiteral("240"),
            QStringLiteral("-sc_threshold"), QStringLiteral("0"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-c:a"), QStringLiteral("aac"), original});
        QVERIFY2(created.ok, created.error.constData());
        const auto imported = engine->importMedia(original, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        const auto sourceHash = QCryptographicHash::hash(fileBytes(original), QCryptographicHash::Sha256);
        const auto originalLuma = frameLuma(original);
        QCOMPARE(originalLuma.size(), 96);
        for (const bool removeAudio : {true, false}) {
            const QString target = path(removeAudio ? QStringLiteral("trimmed-muted.mp4") : QStringLiteral("trimmed-audio.mp4"));
            QList<double> progress;
            const auto result = engine->exportVideo(imported.media, target, {15.0 / 24.0, 51.0 / 24.0, removeAudio}, flag(),
                [&](double percent) { progress.append(percent); });
            QVERIFY2(result.ok, qPrintable(result.error));
            const auto output = inspect(target);
            const auto streams = output.value(QStringLiteral("streams")).toArray();
            QCOMPARE(streams.size(), removeAudio ? 1 : 2);
            const auto video = streams.first().toObject();
            QCOMPARE(video.value(QStringLiteral("codec_name")).toString(), QStringLiteral("h264"));
            QCOMPARE(video.value(QStringLiteral("width")).toInt(), 160);
            QCOMPARE(video.value(QStringLiteral("height")).toInt(), 90);
            QVERIFY(std::abs(video.value(QStringLiteral("duration")).toString().toDouble() - 1.5) < 0.002);
            if (!removeAudio) {
                QCOMPARE(streams.last().toObject().value(QStringLiteral("codec_name")).toString(), QStringLiteral("aac"));
                QVERIFY(std::abs(streams.last().toObject().value(QStringLiteral("duration")).toString().toDouble() - 1.5) < 0.03);
            }
            const auto selectedFrames = frameLuma(target);
            QCOMPARE(selectedFrames.size(), 36);
            // Every source frame has a unique shade. Checking all decoded shades
            // proves the boundary content, not merely a container duration.
            for (int index = 0; index < selectedFrames.size(); ++index)
                QVERIFY(qAbs(int(uchar(selectedFrames[index])) - int(uchar(originalLuma[index + 15]))) <= 3);
            QCOMPARE(progress.first(), 0.0);
            QCOMPARE(progress.last(), 100.0);
        }
        QCOMPARE(QCryptographicHash::hash(fileBytes(original), QCryptographicHash::Sha256), sourceHash);
        QVERIFY(noStagingFiles());
    }

    void fullRangeExportCanKeepAllAudio()
    {
        const QString target = path(QStringLiteral("untrimmed-kept-audio.mp4"));
        const auto result = engine->exportVideo(media, target, {0, -1, false}, flag());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(inspect(target).value(QStringLiteral("streams")).toArray().size(), 3);
        QCOMPARE(frameHashes(target), frameHashes(source));
    }

    void trimBeyondShorterAudioTrackCanExportSilence()
    {
        const QString original = path(QStringLiteral("short-audio.mp4"));
        const auto created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-i"), source, QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("0:a:0"),
            QStringLiteral("-vf"), QStringLiteral("tpad=stop_mode=clone:stop_duration=2"),
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
            QStringLiteral("-c:a"), QStringLiteral("copy"), original});
        QVERIFY2(created.ok, created.error.constData());
        const auto imported = engine->importMedia(original, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QCOMPARE(imported.media.audioTracks, 1);
        const QString target = path(QStringLiteral("short-audio-trim.mp4"));
        const auto result = engine->exportVideo(imported.media, target, {2, 3, false}, flag());
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto streams = inspect(target).value(QStringLiteral("streams")).toArray();
        QCOMPARE(streams.size(), 1);
        QCOMPARE(streams.first().toObject().value(QStringLiteral("codec_type")).toString(), QStringLiteral("video"));
        QVERIFY(std::abs(streams.first().toObject().value(QStringLiteral("duration")).toString().toDouble() - 1) < 0.002);
    }

    void trimRangeValidationAndCancellationProtectFiles()
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double infinity = std::numeric_limits<double>::infinity();
        const QList<ExportOptions> invalid{{-0.1, 1, true}, {1, 1, true}, {1.1, 0.5, true},
            {0, media.duration + 1, true}, {0, -2, true}, {nan, 1, true}, {0, nan, true},
            {infinity, 1, true}, {0, infinity, true}, {media.duration + 1, -1, true}};
        const QString target = path(QStringLiteral("invalid-trim.mp4"));
        for (const auto &options : invalid) {
            const auto result = engine->exportVideo(media, target, options, flag());
            QVERIFY(!result.ok && !result.cancelled);
            QVERIFY(!QFileInfo::exists(target));
        }
        QVERIFY(!engine->exportVideo(media, path(QStringLiteral("trim-wrong-format.mkv")), {0.25, 1, true}, flag()).ok);
        QVERIFY(!engine->exportVideo(media, source, {0.25, 1, true}, flag()).ok);
        auto before = flag();
        before->store(true);
        const auto immediate = engine->exportVideo(media, target, {0.25, 1, true}, before);
        QVERIFY(!immediate.ok && immediate.cancelled);
        auto during = flag();
        int progressEvents = 0;
        const auto cancelled = engine->exportVideo(media, target, {0.25, 1, false}, during,
            [&](double) { if (++progressEvents > 1) during->store(true); });
        QVERIFY(progressEvents > 1);
        QVERIFY(!cancelled.ok && cancelled.cancelled);
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(noStagingFiles());
    }

    void competingWriterIsNeverOverwritten()
    {
        const QString target = path(QStringLiteral("race.mp4"));
        bool created = false;
        const auto result = engine->exportSilent(media, target, flag(), [&](double) {
            if (!created) created = write(target, QByteArrayLiteral("concurrent writer"));
        });
        QVERIFY(created);
        QVERIFY(!result.ok);
        QCOMPARE(fileBytes(target), QByteArrayLiteral("concurrent writer"));
        QVERIFY(noStagingFiles());
    }

    void cancellationCleansPartialExports()
    {
        auto before = flag();
        before->store(true);
        const QString beforeTarget = path(QStringLiteral("cancel-before.mp4"));
        const auto immediate = engine->exportSilent(media, beforeTarget, before);
        QVERIFY(!immediate.ok && immediate.cancelled);
        QVERIFY(!QFileInfo::exists(beforeTarget));
        auto during = flag();
        int events = 0;
        const QString duringTarget = path(QStringLiteral("cancel-during.mp4"));
        const auto result = engine->exportSilent(media, duringTarget, during, [&](double) {
            if (++events > 1) during->store(true);
        });
        QVERIFY(events > 1);
        QVERIFY(!result.ok && result.cancelled);
        QVERIFY(!QFileInfo::exists(duringTarget));
        QVERIFY(noStagingFiles());
    }

    void invalidAndAudioOnlyInputsFail()
    {
        const QString corrupt = path(QStringLiteral("corrupt.mp4"));
        QVERIFY(write(corrupt, QByteArrayLiteral("not video")));
        QVERIFY(!engine->importMedia(corrupt, flag()).ok);
        const QString empty = path(QStringLiteral("empty.mp4"));
        QVERIFY(write(empty, {}));
        QVERIFY(!engine->importMedia(empty, flag()).ok);
        const QString audio = path(QStringLiteral("audio.m4a"));
        const auto created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-i"), source, QStringLiteral("-vn"), QStringLiteral("-c:a"), QStringLiteral("copy"), audio});
        QVERIFY2(created.ok, created.error.constData());
        const auto result = engine->importMedia(audio, flag());
        QVERIFY(!result.ok && result.error.contains(QStringLiteral("no video")));
        QVERIFY(!engine->importMedia(QStringLiteral("https://example.com/video.mp4"), flag()).ok);
#ifdef Q_OS_UNIX
        const QString symlinkPath = path(QStringLiteral("input-link.mp4"));
        QCOMPARE(::symlink(QFile::encodeName(source).constData(), QFile::encodeName(symlinkPath).constData()), 0);
        QVERIFY(!engine->importMedia(symlinkPath, flag()).ok);
#endif
    }

    void legacyPreviewUsesCpuProxyAndExportKeepsCodec()
    {
        const QString avi = path(QStringLiteral("legacy.avi"));
        const auto created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-i"), source, QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("0:a:0"),
            QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), avi});
        QVERIFY2(created.ok, created.error.constData());
        const auto imported = engine->importMedia(avi, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QVERIFY(imported.media.previewTranscoded);
        const auto preview = inspect(imported.media.previewPath).value(QStringLiteral("streams")).toArray();
        QCOMPARE(preview.size(), 2);
        QCOMPARE(preview.first().toObject().value(QStringLiteral("codec_name")).toString(), QStringLiteral("h264"));
        QCOMPARE(preview.last().toObject().value(QStringLiteral("codec_name")).toString(), QStringLiteral("aac"));
        const QString target = path(QStringLiteral("legacy-silent.avi"));
        const auto result = engine->exportSilent(imported.media, target, flag());
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto output = inspect(target).value(QStringLiteral("streams")).toArray();
        QCOMPARE(output.size(), 1);
        QCOMPARE(output.first().toObject().value(QStringLiteral("codec_name")).toString(), QStringLiteral("mpeg4"));
        const auto hashes = frameHashes(avi);
        QVERIFY(!hashes.isEmpty());
        QCOMPARE(frameHashes(target), hashes);
    }

    void highResolutionPreviewIsSmallAndOriginalExportUnchanged()
    {
        const QString large = path(QStringLiteral("large.mp4"));
        const auto created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc2=size=1280x720:rate=60:duration=0.5"),
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"), large});
        QVERIFY2(created.ok, created.error.constData());
        const auto imported = engine->importMedia(large, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QVERIFY(imported.media.previewTranscoded);
        const auto preview = inspect(imported.media.previewPath).value(QStringLiteral("streams")).toArray().first().toObject();
        QVERIFY(preview.value(QStringLiteral("width")).toInt() <= 854);
        QVERIFY(preview.value(QStringLiteral("height")).toInt() <= 480);
        QCOMPARE(preview.value(QStringLiteral("avg_frame_rate")).toString(), QStringLiteral("30/1"));
        const QString target = path(QStringLiteral("large-silent.mp4"));
        const auto result = engine->exportSilent(imported.media, target, flag());
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto output = inspect(target).value(QStringLiteral("streams")).toArray();
        QCOMPARE(output.size(), 1);
        QCOMPARE(output.first().toObject().value(QStringLiteral("width")).toInt(), 1280);
        QCOMPARE(output.first().toObject().value(QStringLiteral("height")).toInt(), 720);
        QCOMPARE(output.first().toObject().value(QStringLiteral("avg_frame_rate")).toString(), QStringLiteral("60/1"));
    }

    void rotationIsDisplayedAndPreserved()
    {
        const QString rotated = path(QStringLiteral("rotated.mp4"));
        auto created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-i"), source, QStringLiteral("-map"), QStringLiteral("0"), QStringLiteral("-c"), QStringLiteral("copy"),
            QStringLiteral("-metadata:s:v:0"), QStringLiteral("rotate=90"), rotated});
        QVERIFY2(created.ok, created.error.constData());
        bool fixtureHasRotation = false;
        const auto fixtureVideo = inspect(rotated).value(QStringLiteral("streams")).toArray().first().toObject();
        for (const auto &item : fixtureVideo.value(QStringLiteral("side_data_list")).toArray()) {
            if (qAbs(item.toObject().value(QStringLiteral("rotation")).toDouble()) == 90) fixtureHasRotation = true;
        }
        if (!fixtureHasRotation) {
            // New FFmpeg versions use an input display transform; Debian's older builds use the metadata form.
            created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                QStringLiteral("-display_rotation"), QStringLiteral("90"), QStringLiteral("-i"), source,
                QStringLiteral("-map"), QStringLiteral("0"), QStringLiteral("-c"), QStringLiteral("copy"), rotated});
            QVERIFY2(created.ok, created.error.constData());
        }
        const auto imported = engine->importMedia(rotated, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QCOMPARE(imported.media.width, 90);
        QCOMPARE(imported.media.height, 160);
        const QString target = path(QStringLiteral("rotated-silent.mp4"));
        QVERIFY(engine->exportSilent(imported.media, target, flag()).ok);
        const auto video = inspect(target).value(QStringLiteral("streams")).toArray().first().toObject();
        bool rotatedCorrectly = false;
        for (const auto &item : video.value(QStringLiteral("side_data_list")).toArray()) {
            if (qAbs(item.toObject().value(QStringLiteral("rotation")).toDouble()) == 90) rotatedCorrectly = true;
        }
        QVERIFY(rotatedCorrectly);
        const QString trimmed = path(QStringLiteral("rotated-trim.mp4"));
        const auto trimResult = engine->exportVideo(imported.media, trimmed, {0.25, 1, true}, flag());
        QVERIFY2(trimResult.ok, qPrintable(trimResult.error));
        const auto trimmedVideo = inspect(trimmed).value(QStringLiteral("streams")).toArray().first().toObject();
        QCOMPARE(trimmedVideo.value(QStringLiteral("width")).toInt(), 90);
        QCOMPARE(trimmedVideo.value(QStringLiteral("height")).toInt(), 160);
        for (const auto &item : trimmedVideo.value(QStringLiteral("side_data_list")).toArray())
            QCOMPARE(item.toObject().value(QStringLiteral("rotation")).toDouble(), 0.0);
    }

    void sourceModificationIsDetected()
    {
        const QString changed = path(QStringLiteral("changed.mp4"));
        QVERIFY(QFile::copy(source, changed));
        const auto imported = engine->importMedia(changed, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QFile file(changed);
        QVERIFY(file.open(QIODevice::Append));
        QCOMPARE(file.write("changed", 7), qint64(7));
        file.close();
        const auto result = engine->exportSilent(imported.media, path(QStringLiteral("changed-silent.mp4")), flag());
        QVERIFY(!result.ok && result.error.contains(QStringLiteral("changed")));
    }

    void sourceRewriteWithRestoredModificationTimeIsDetected()
    {
#ifdef Q_OS_UNIX
        const QString original = path(QStringLiteral("restored-timestamp.mp4"));
        QVERIFY(QFile::copy(source, original));
        QFile file(original);
        QVERIFY(file.open(QIODevice::ReadWrite));
        const QDateTime modified = QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch() - 3600);
        QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
        file.close();
        const auto imported = engine->importMedia(original, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        const QByteArray contents = fileBytes(original);
        QVERIFY(write(original, contents));
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
        file.close();
        QCOMPARE(QFileInfo(original).size(), imported.media.size);
        QCOMPARE(QFileInfo(original).lastModified(), modified);
        const QString target = path(QStringLiteral("restored-timestamp-export.mp4"));
        const auto result = engine->exportSilent(imported.media, target, flag());
        QVERIFY(!result.ok && result.error.contains(QStringLiteral("changed")));
        QVERIFY(!QFileInfo::exists(target));
#else
        QSKIP("Filesystem change-time detection is supported on macOS and Linux.");
#endif
    }

    void subtitlesChaptersAttachmentsAndSecondaryVideoArePreserved()
    {
        const QString subtitles = path(QStringLiteral("captions.srt"));
        QVERIFY(write(subtitles, QByteArrayLiteral("1\n00:00:00,000 --> 00:00:01,000\nKeep these captions.\n")));
        const QString chapters = path(QStringLiteral("chapters.ffmetadata"));
        QVERIFY(write(chapters, QByteArrayLiteral(";FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=1250\ntitle=Chapter one\n")));
        const QString attachment = path(QStringLiteral("notes.txt"));
        const QByteArray attachmentContents("Keep this attachment unchanged.\n");
        QVERIFY(write(attachment, attachmentContents));
        const QString original = path(QStringLiteral("all-tracks.mkv"));
        const auto created = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-i"), source, QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("color=c=blue:size=80x46:rate=24:duration=1.25"), QStringLiteral("-i"), subtitles,
            QStringLiteral("-f"), QStringLiteral("ffmetadata"), QStringLiteral("-i"), chapters,
            QStringLiteral("-map"), QStringLiteral("0"), QStringLiteral("-map"), QStringLiteral("1:v"),
            QStringLiteral("-map"), QStringLiteral("2:s"), QStringLiteral("-map_metadata"), QStringLiteral("3"),
            QStringLiteral("-map_chapters"), QStringLiteral("3"), QStringLiteral("-c"), QStringLiteral("copy"),
            QStringLiteral("-c:v:1"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
            QStringLiteral("-attach"), attachment, QStringLiteral("-metadata:s:t"), QStringLiteral("mimetype=text/plain"),
            QStringLiteral("-metadata:s:t"), QStringLiteral("filename=notes.txt"), original});
        QVERIFY2(created.ok, created.error.constData());
        const QByteArray originalBytes = fileBytes(original);
        const auto imported = engine->importMedia(original, flag());
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QCOMPARE(imported.media.audioTracks, 2);
        const QString target = path(QStringLiteral("all-tracks-silent.mkv"));
        const auto result = engine->exportSilent(imported.media, target, flag());
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto exported = inspect(target);
        const auto streams = exported.value(QStringLiteral("streams")).toArray();
        int videos = 0;
        int subtitleTracks = 0;
        int attachments = 0;
        for (const auto &value : streams) {
            const auto stream = value.toObject();
            const QString type = stream.value(QStringLiteral("codec_type")).toString();
            QVERIFY(type != QStringLiteral("audio"));
            if (type == QStringLiteral("video")) ++videos;
            if (type == QStringLiteral("subtitle")) {
                ++subtitleTracks;
                QCOMPARE(stream.value(QStringLiteral("codec_name")).toString(), QStringLiteral("subrip"));
            }
            if (type == QStringLiteral("attachment")) {
                ++attachments;
                QCOMPARE(stream.value(QStringLiteral("tags")).toObject().value(QStringLiteral("filename")).toString(), QStringLiteral("notes.txt"));
            }
        }
        QCOMPARE(videos, 2);
        QCOMPARE(subtitleTracks, 1);
        QCOMPARE(attachments, 1);
        const auto outputChapters = exported.value(QStringLiteral("chapters")).toArray();
        QCOMPARE(outputChapters.size(), 1);
        QCOMPARE(outputChapters.first().toObject().value(QStringLiteral("tags")).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Chapter one"));
        QCOMPARE(outputChapters.first().toObject().value(QStringLiteral("start_time")).toString().toDouble(), 0.0);
        QCOMPARE(outputChapters.first().toObject().value(QStringLiteral("end_time")).toString().toDouble(), 1.25);
        for (int index = 0; index < 2; ++index) {
            const auto hashes = frameHashes(original, index);
            QVERIFY(!hashes.isEmpty());
            QCOMPARE(frameHashes(target, index), hashes);
        }
        const auto extractedSubtitles = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-i"), target, QStringLiteral("-map"), QStringLiteral("0:s:0"), QStringLiteral("-c"), QStringLiteral("copy"),
            QStringLiteral("-f"), QStringLiteral("srt"), QStringLiteral("-")});
        QVERIFY2(extractedSubtitles.ok, extractedSubtitles.error.constData());
        QVERIFY(extractedSubtitles.output.contains("Keep these captions."));
        const QString extractedAttachment = path(QStringLiteral("exported-notes.txt"));
        const auto extracted = command(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
            QStringLiteral("-dump_attachment:t:0"), extractedAttachment, QStringLiteral("-i"), target,
            QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-c"), QStringLiteral("copy"),
            QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
        QVERIFY2(extracted.ok, extracted.error.constData());
        QCOMPARE(fileBytes(extractedAttachment), attachmentContents);
        QCOMPARE(fileBytes(original), originalBytes);
        const auto differentContainer = engine->exportSilent(imported.media, path(QStringLiteral("all-tracks-silent.mp4")), flag());
        QVERIFY(!differentContainer.ok);
        QVERIFY(differentContainer.error.contains(QStringLiteral("original video container")));
        QVERIFY(!QFileInfo::exists(path(QStringLiteral("all-tracks-silent.mp4"))));
    }
};

QTEST_GUILESS_MAIN(MediaEngineTest)
#include "mediaengine_test.moc"
