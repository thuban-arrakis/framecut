#include "mediaengine.h"
#include <QCoreApplication>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <utility>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
const QStringList localInput{
    QStringLiteral("-protocol_whitelist"), QStringLiteral("file,pipe"),
    QStringLiteral("-format_whitelist"), QStringLiteral("mov,matroska,webm,avi,mpegts,mpeg,ogg,asf")
};

bool isCancelled(const CancelFlag &flag) { return flag && flag->load(std::memory_order_relaxed); }

void report(const ProgressCallback &callback, double value)
{
    if (callback) callback(std::clamp(value, 0.0, 100.0));
}

struct ProcessResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QByteArray output;
};

ProcessResult run(const QString &binary, const QStringList &arguments, const CancelFlag &cancel,
                  int timeoutMs = 0, double duration = 0, const ProgressCallback &progress = {})
{
    if (isCancelled(cancel)) return {false, true, QCoreApplication::translate("MediaEngine", "Operation cancelled."), {}};
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(binary, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000))
        return {false, false, QCoreApplication::translate("MediaEngine", "The video tools could not start. Check the FFmpeg installation."), {}};

    QElapsedTimer timer;
    timer.start();
    QByteArray output;
    QByteArray diagnostic;
    QByteArray pendingLines;
    double seconds = 0;
    bool overflow = false;
    auto read = [&] {
        diagnostic += process.readAllStandardError();
        diagnostic = diagnostic.right(8192);
        const QByteArray chunk = process.readAllStandardOutput();
        if (duration > 0) {
            pendingLines += chunk;
            qsizetype newline;
            while ((newline = pendingLines.indexOf('\n')) >= 0) {
                const QByteArray line = pendingLines.left(newline).trimmed();
                pendingLines.remove(0, newline + 1);
                if (line.startsWith("out_time_us=")) {
                    bool valid = false;
                    const double next = line.mid(12).toDouble(&valid) / 1000000.0;
                    if (valid && std::isfinite(next)) seconds = std::max(seconds, next);
                } else if (line.startsWith("progress=")) {
                    report(progress, std::min(99.0, seconds / duration * 100.0));
                }
            }
            overflow = pendingLines.size() > 1024 * 1024;
        } else {
            output += chunk;
            overflow = output.size() > 4 * 1024 * 1024;
        }
    };
    auto stop = [&] {
        process.terminate();
        if (!process.waitForFinished(1000)) {
            process.kill();
            process.waitForFinished(2000);
        }
    };
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(40);
        read();
        if (isCancelled(cancel)) {
            stop();
            return {false, true, QCoreApplication::translate("MediaEngine", "Operation cancelled."), {}};
        }
        if (timeoutMs > 0 && timer.elapsed() > timeoutMs) {
            stop();
            return {false, false, QCoreApplication::translate("MediaEngine", "The video took too long to inspect. Try another file."), {}};
        }
        if (overflow) {
            stop();
            return {false, false, QCoreApplication::translate("MediaEngine", "The video contains too much metadata to inspect safely."), {}};
        }
    }
    read();
    if (isCancelled(cancel)) return {false, true, QCoreApplication::translate("MediaEngine", "Operation cancelled."), {}};
    if (overflow) return {false, false, QCoreApplication::translate("MediaEngine", "The video contains too much metadata to inspect safely."), {}};
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString error = QCoreApplication::translate("MediaEngine", "The video could not be processed. Try another file or output format.");
        if (diagnostic.contains("No space left on device")) error = QCoreApplication::translate("MediaEngine", "The destination disk is full.");
        else if (diagnostic.contains("Permission denied") || diagnostic.contains("Operation not permitted"))
            error = QCoreApplication::translate("MediaEngine", "The file or destination cannot be accessed. Check folder permissions.");
        else if (diagnostic.contains("Invalid data found") || diagnostic.contains("moov atom not found") || diagnostic.contains("Invalid argument"))
            error = QCoreApplication::translate("MediaEngine", "This file is damaged or uses an unsupported video format.");
        else if (diagnostic.contains("whitelist"))
            error = QCoreApplication::translate("MediaEngine", "Only self-contained local video files are supported.");
        return {false, false, error, {}};
    }
    return {true, false, {}, output};
}

struct ProbeResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QJsonObject data;
};

ProbeResult probe(const QString &binary, const QString &path, const CancelFlag &cancel)
{
    QStringList arguments{QStringLiteral("-v"), QStringLiteral("error")};
    arguments += localInput;
    arguments += {QStringLiteral("-show_format"), QStringLiteral("-show_streams"), QStringLiteral("-show_chapters"),
                  QStringLiteral("-of"), QStringLiteral("json"), QStringLiteral("-i"), path};
    const auto result = run(binary, arguments, cancel, 45000);
    if (!result.ok) return {false, result.cancelled, result.error, {}};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(result.output, &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || !object.value(QStringLiteral("streams")).isArray()
        || !object.value(QStringLiteral("format")).isObject())
        return {false, false, QCoreApplication::translate("MediaEngine", "The video metadata could not be read."), {}};
    return {true, false, {}, object};
}

double positiveNumber(const QJsonValue &value)
{
    bool valid = false;
    const double number = value.isDouble() ? value.toDouble() : value.toString().toDouble(&valid);
    return (valid || value.isDouble()) && std::isfinite(number) && number > 0 ? number : 0;
}

double frameRate(const QJsonValue &value)
{
    const auto parts = value.toString().split('/');
    if (parts.isEmpty()) return 0;
    bool valid = false;
    const double numerator = parts.first().toDouble(&valid);
    if (!valid) return 0;
    const double denominator = parts.size() == 2 ? parts.last().toDouble(&valid) : 1;
    const double result = numerator / denominator;
    return valid && std::isfinite(result) && result > 0 ? result : 0;
}

double streamDuration(const QJsonObject &stream)
{
    const double duration = positiveNumber(stream.value(QStringLiteral("duration")));
    if (duration > 0) return duration;
    const auto tags = stream.value(QStringLiteral("tags")).toObject();
    const QString text = tags.value(QStringLiteral("DURATION")).toString(tags.value(QStringLiteral("duration")).toString());
    static const QRegularExpression expression(QStringLiteral("^(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)$"));
    const auto match = expression.match(text);
    return match.hasMatch() ? match.captured(1).toDouble() * 3600 + match.captured(2).toDouble() * 60 + match.captured(3).toDouble() : 0;
}

QString fileFingerprint(const QString &path)
{
#ifdef Q_OS_UNIX
    struct stat status{};
    if (::lstat(QFile::encodeName(path).constData(), &status) != 0 || !S_ISREG(status.st_mode)) return {};
#ifdef Q_OS_MACOS
    const auto seconds = status.st_mtimespec.tv_sec;
    const auto nanoseconds = status.st_mtimespec.tv_nsec;
#else
    const auto seconds = status.st_mtim.tv_sec;
    const auto nanoseconds = status.st_mtim.tv_nsec;
#endif
    return QStringLiteral("%1:%2:%3:%4:%5").arg(quint64(status.st_dev)).arg(quint64(status.st_ino))
        .arg(qint64(status.st_size)).arg(qint64(seconds)).arg(qint64(nanoseconds));
#else
    const QFileInfo file(path);
    return file.isFile() && !file.isSymLink() ? QStringLiteral("%1:%2").arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch()) : QString{};
#endif
}

QString validateInput(const QString &path)
{
    if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null)) return QCoreApplication::translate("MediaEngine", "Choose a local video file.");
    const QFileInfo file(path);
    if (file.isSymLink() || !file.isFile()) return QCoreApplication::translate("MediaEngine", "Choose a regular video file, rather than a folder or symbolic link.");
    if (file.size() == 0) return QCoreApplication::translate("MediaEngine", "This file is empty.");
    if (!file.isReadable()) return QCoreApplication::translate("MediaEngine", "The video file cannot be read.");
    return {};
}

QString unchanged(const MediaInfo &media)
{
    if (media.sourceFingerprint.isEmpty() || fileFingerprint(media.path) != media.sourceFingerprint)
        return QCoreApplication::translate("MediaEngine", "The original video changed or moved. Import it again before exporting.");
    return {};
}

QString outputFormat(const QString &suffix)
{
    static const QMap<QString, QString> formats{
        {QStringLiteral("mp4"), QStringLiteral("mp4")}, {QStringLiteral("m4v"), QStringLiteral("mp4")},
        {QStringLiteral("mov"), QStringLiteral("mov")}, {QStringLiteral("mkv"), QStringLiteral("matroska")},
        {QStringLiteral("webm"), QStringLiteral("webm")}, {QStringLiteral("avi"), QStringLiteral("avi")},
        {QStringLiteral("ts"), QStringLiteral("mpegts")}, {QStringLiteral("mts"), QStringLiteral("mpegts")},
        {QStringLiteral("m2ts"), QStringLiteral("mpegts")}, {QStringLiteral("mpg"), QStringLiteral("mpeg")},
        {QStringLiteral("mpeg"), QStringLiteral("mpeg")}, {QStringLiteral("ogv"), QStringLiteral("ogg")},
        {QStringLiteral("wmv"), QStringLiteral("asf")}
    };
    return formats.value(suffix.toLower());
}

QString sourceFormat(const QJsonObject &probeData, const QString &path)
{
    const QStringList names = probeData.value(QStringLiteral("format")).toObject()
        .value(QStringLiteral("format_name")).toString().split(',');
    const QString extensionFormat = outputFormat(QFileInfo(path).suffix());
    if (names.contains(QStringLiteral("mov"))) {
        return extensionFormat == QStringLiteral("mov") ? extensionFormat : QStringLiteral("mp4");
    }
    if (names.contains(QStringLiteral("matroska")) || names.contains(QStringLiteral("webm"))) {
        return extensionFormat == QStringLiteral("webm") ? extensionFormat : QStringLiteral("matroska");
    }
    for (const QString &format : {QStringLiteral("avi"), QStringLiteral("mpegts"), QStringLiteral("mpeg"), QStringLiteral("ogg"), QStringLiteral("asf")}) {
        if (names.contains(format)) return format;
    }
    return {};
}

QList<QByteArray> streamSignatures(const QJsonArray &streams, bool excludeAudio)
{
    QList<QByteArray> result;
    for (const auto &value : streams) {
        const auto stream = value.toObject();
        if (excludeAudio && stream.value(QStringLiteral("codec_type")).toString() == QStringLiteral("audio")) continue;
        QJsonObject signature;
        for (const QString &key : {QStringLiteral("codec_type"), QStringLiteral("codec_name"), QStringLiteral("width"), QStringLiteral("height"),
                                  QStringLiteral("sample_rate"), QStringLiteral("channels")}) {
            if (stream.contains(key)) signature.insert(key, stream.value(key));
        }
        // Attachments are identified by their MIME type and filename as well as codec.
        if (stream.value(QStringLiteral("codec_type")).toString() == QStringLiteral("attachment")) {
            const auto tags = stream.value(QStringLiteral("tags")).toObject();
            signature.insert(QStringLiteral("filename"), tags.value(QStringLiteral("filename")));
            signature.insert(QStringLiteral("mimetype"), tags.value(QStringLiteral("mimetype")));
        }
        result.append(QJsonDocument(signature).toJson(QJsonDocument::Compact));
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool chaptersMatch(const QJsonArray &expected, const QJsonArray &actual)
{
    if (expected.size() != actual.size()) return false;
    for (qsizetype index = 0; index < expected.size(); ++index) {
        const auto before = expected.at(index).toObject();
        const auto after = actual.at(index).toObject();
        if (before.value(QStringLiteral("tags")).toObject() != after.value(QStringLiteral("tags")).toObject()) return false;
        for (const QString &key : {QStringLiteral("start_time"), QStringLiteral("end_time")}) {
            if (std::abs(before.value(key).toVariant().toDouble() - after.value(key).toVariant().toDouble()) > 0.002) return false;
        }
    }
    return true;
}

QStringList ffmpegBase()
{
    return {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-nostdin"), QStringLiteral("-n")};
}
} // namespace

MediaEngine::MediaEngine(QString ffmpegPath, QString ffprobePath, QString cacheDir)
    : m_ffmpegPath(std::move(ffmpegPath)), m_ffprobePath(std::move(ffprobePath)), m_cacheDir(std::move(cacheDir))
{}

MediaResult MediaEngine::importMedia(const QString &path, const CancelFlag &cancel, const ProgressCallback &progress)
{
    if (isCancelled(cancel)) return {false, true, QCoreApplication::translate("MediaEngine", "Operation cancelled."), {}};
    const QString inputError = validateInput(path);
    if (!inputError.isEmpty()) return {false, false, inputError, {}};
    const QFileInfo input(path);
    MediaInfo media;
    media.path = input.canonicalFilePath();
    media.sourceFingerprint = fileFingerprint(media.path);
    const auto inspected = probe(m_ffprobePath, media.path, cancel);
    if (!inspected.ok) return {false, inspected.cancelled, inspected.error, {}};
    const auto streams = inspected.data.value(QStringLiteral("streams")).toArray();
    QJsonObject video;
    QString firstAudioCodec;
    for (const auto &streamValue : streams) {
        const auto stream = streamValue.toObject();
        const auto type = stream.value(QStringLiteral("codec_type")).toString();
        if (type == QStringLiteral("video") && video.isEmpty()
            && stream.value(QStringLiteral("disposition")).toObject().value(QStringLiteral("attached_pic")).toInt() == 0)
            video = stream;
        else if (type == QStringLiteral("audio")) {
            if (media.audioTracks == 0) firstAudioCodec = stream.value(QStringLiteral("codec_name")).toString();
            ++media.audioTracks;
        }
    }
    media.codedWidth = video.value(QStringLiteral("width")).toInt();
    media.codedHeight = video.value(QStringLiteral("height")).toInt();
    media.videoStreamIndex = video.value(QStringLiteral("index")).toInt(-1);
    if (video.isEmpty() || media.codedWidth <= 0 || media.codedHeight <= 0 || media.videoStreamIndex < 0)
        return {false, false, QCoreApplication::translate("MediaEngine", "This file has no video track. Choose a video to remove its sound."), {}};
    media.videoDuration = streamDuration(video);
    media.duration = media.videoDuration > 0 ? media.videoDuration : positiveNumber(inspected.data.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")));
    if (!(media.duration > 0)) return {false, false, QCoreApplication::translate("MediaEngine", "The video duration could not be determined."), {}};
    media.fps = frameRate(video.value(QStringLiteral("avg_frame_rate")));
    if (media.fps <= 0) media.fps = frameRate(video.value(QStringLiteral("r_frame_rate")));
    media.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    media.name = input.fileName();
    media.size = input.size();
    media.codec = video.value(QStringLiteral("codec_name")).toString(QStringLiteral("unknown"));
    double rotation = video.value(QStringLiteral("tags")).toObject().value(QStringLiteral("rotate")).toString().toDouble();
    for (const auto &item : video.value(QStringLiteral("side_data_list")).toArray()) {
        if (item.toObject().contains(QStringLiteral("rotation"))) {
            rotation = item.toObject().value(QStringLiteral("rotation")).toDouble();
            break;
        }
    }
    const bool swapAxes = std::abs(qRound(rotation / 90)) % 2 == 1;
    media.width = swapAxes ? media.codedHeight : media.codedWidth;
    media.height = swapAxes ? media.codedWidth : media.codedHeight;
    if (!QDir::isAbsolutePath(m_cacheDir) || !QDir().mkpath(m_cacheDir))
        return {false, false, QCoreApplication::translate("MediaEngine", "The preview cache could not be created."), {}};
    QTemporaryDir directory(QDir(m_cacheDir).filePath(QStringLiteral("clip-XXXXXX")));
    if (!directory.isValid()) return {false, false, QCoreApplication::translate("MediaEngine", "The preview cache could not be created."), {}};
    const QString pixelFormat = video.value(QStringLiteral("pix_fmt")).toString();
    media.previewTranscoded = media.width > 854 || media.height > 480 || media.fps > 30
        || media.codec != QStringLiteral("h264")
        || !QStringList{QStringLiteral("mp4"), QStringLiteral("m4v")}.contains(input.suffix().toLower())
        || !QStringList{QStringLiteral("yuv420p"), QStringLiteral("yuvj420p")}.contains(pixelFormat)
        || (media.audioTracks > 0 && !QStringList{QStringLiteral("aac"), QStringLiteral("mp3")}.contains(firstAudioCodec));
    media.previewPath = media.path;
    report(progress, 0);
    if (media.previewTranscoded) {
        media.previewPath = directory.filePath(QStringLiteral("preview.mp4"));
        QString filter = QStringLiteral("scale=w='min(854,iw)':h='min(480,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2");
        if (media.fps > 30) filter += QStringLiteral(",fps=30");
        QStringList arguments = ffmpegBase();
        arguments += localInput;
        arguments += {QStringLiteral("-hwaccel"), QStringLiteral("none"), QStringLiteral("-threads"), QStringLiteral("2"),
            QStringLiteral("-i"), media.path, QStringLiteral("-map"), QStringLiteral("0:%1").arg(media.videoStreamIndex),
            QStringLiteral("-map"), QStringLiteral("0:a:0?"), QStringLiteral("-sn"), QStringLiteral("-dn"),
            QStringLiteral("-vf"), filter, QStringLiteral("-c:v"), QStringLiteral("libx264"),
            QStringLiteral("-preset"), QStringLiteral("veryfast"), QStringLiteral("-crf"), QStringLiteral("25"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-threads"), QStringLiteral("2"),
            QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("128k"),
            QStringLiteral("-ac"), QStringLiteral("2"), QStringLiteral("-movflags"), QStringLiteral("+faststart"),
            QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats"), media.previewPath};
        const auto converted = run(m_ffmpegPath, arguments, cancel, 0, media.duration,
            [&](double value) { report(progress, value * 0.95); });
        if (!converted.ok) return {false, converted.cancelled, converted.error, {}};
    }
    media.thumbnailPath = directory.filePath(QStringLiteral("thumbnail.jpg"));
    QStringList thumbnailArguments = ffmpegBase();
    thumbnailArguments += localInput;
    thumbnailArguments += {QStringLiteral("-hwaccel"), QStringLiteral("none"), QStringLiteral("-threads"), QStringLiteral("2"),
        QStringLiteral("-ss"), QString::number(std::min(0.25, media.duration / 4.0), 'f', 5),
        QStringLiteral("-i"), media.previewPath, QStringLiteral("-map"),
        media.previewTranscoded ? QStringLiteral("0:v:0") : QStringLiteral("0:%1").arg(media.videoStreamIndex),
        QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"), QStringLiteral("scale=640:360:force_original_aspect_ratio=decrease"),
        QStringLiteral("-q:v"), QStringLiteral("3"), QStringLiteral("-threads"), QStringLiteral("2"), media.thumbnailPath};
    const auto thumbnail = run(m_ffmpegPath, thumbnailArguments, cancel, 45000);
    if (!thumbnail.ok) return {false, thumbnail.cancelled, thumbnail.error, {}};
    if (QFileInfo(media.thumbnailPath).size() <= 0)
        return {false, false, QCoreApplication::translate("MediaEngine", "The video contains no readable preview frame."), {}};
    const QString changed = unchanged(media);
    if (!changed.isEmpty()) return {false, false, changed, {}};
    if (isCancelled(cancel)) return {false, true, QCoreApplication::translate("MediaEngine", "Operation cancelled."), {}};
    directory.setAutoRemove(false);
    report(progress, 100);
    return {true, false, {}, media};
}

ExportResult MediaEngine::exportSilent(const MediaInfo &media, const QString &outputPath,
                                     const CancelFlag &cancel, const ProgressCallback &progress)
{
    return exportVideo(media, outputPath, ExportOptions{}, cancel, progress);
}

ExportResult MediaEngine::exportVideo(const MediaInfo &media, const QString &outputPath,
                                    const ExportOptions &options, const CancelFlag &cancel,
                                    const ProgressCallback &progress)
{
    if (isCancelled(cancel)) return {false, true, QCoreApplication::translate("MediaEngine", "Operation cancelled."), {}, 0};
    const QString changed = unchanged(media);
    if (!changed.isEmpty()) return {false, false, changed, {}, 0};
    if (media.videoStreamIndex < 0 || !std::isfinite(media.duration) || media.duration <= 0)
        return {false, false, QCoreApplication::translate("MediaEngine", "Import a video before exporting."), {}, 0};
    const double end = options.endSeconds == -1 ? media.duration : options.endSeconds;
    if (!std::isfinite(options.startSeconds) || !std::isfinite(end)
        || options.startSeconds < 0 || end > media.duration + 0.000001
        || end <= options.startSeconds)
        return {false, false, QCoreApplication::translate("MediaEngine", "Choose a trim range inside the original video, with the end after the start."), {}, 0};
    const double start = options.startSeconds;
    const double duration = std::min(end, media.duration) - start;
    const bool trimmed = start > 0.000001 || end < media.duration - 0.000001;
    if (!QDir::isAbsolutePath(outputPath) || outputPath.contains(QChar::Null))
        return {false, false, QCoreApplication::translate("MediaEngine", "Choose a local destination for the exported video."), {}, 0};
    const QFileInfo output(outputPath);
    const QFileInfo parent(output.absolutePath());
    if (!parent.isDir() || parent.canonicalFilePath().isEmpty())
        return {false, false, QCoreApplication::translate("MediaEngine", "The destination folder is unavailable."), {}, 0};
    const QString destination = QDir(parent.canonicalFilePath()).filePath(output.fileName());
    if (destination == media.path)
        return {false, false, QCoreApplication::translate("MediaEngine", "Choose a different filename. Your original video is always preserved."), {}, 0};
    if (output.exists() || output.isSymLink())
        return {false, false, QCoreApplication::translate("MediaEngine", "A file already exists at this destination. Choose a new filename."), {}, 0};
    const QString format = outputFormat(output.suffix());
    if (format.isEmpty()) return {false, false, QCoreApplication::translate("MediaEngine", "Use a supported video extension, such as MP4, MOV, MKV, or WebM."), {}, 0};
    if (trimmed && output.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0)
        return {false, false, QCoreApplication::translate("MediaEngine", "Save trimmed video as an MP4 file."), {}, 0};
    const auto originalProbe = probe(m_ffprobePath, media.path, cancel);
    if (!originalProbe.ok) return {false, originalProbe.cancelled, originalProbe.error, {}, 0};
    if (!trimmed && format != sourceFormat(originalProbe.data, media.path))
        return {false, false, QCoreApplication::translate("MediaEngine", "Keep the original video container when removing audio so all other tracks can be preserved."), {}, 0};
    QTemporaryDir directory(QDir(parent.canonicalFilePath()).filePath(QStringLiteral(".framecut-export-XXXXXX")));
    if (!directory.isValid()) return {false, false, QCoreApplication::translate("MediaEngine", "The destination folder cannot be written to."), {}, 0};
    const QString staging = directory.filePath(QStringLiteral("video.%1").arg(output.suffix()));
    report(progress, 0);
    QStringList arguments = ffmpegBase();
    arguments += localInput;
    if (trimmed) {
        // Input seeking decodes and discards frames up to the cut instead of
        // keeping the preceding keyframe. Both decode and encode are CPU-only.
        arguments += {QStringLiteral("-hwaccel"), QStringLiteral("none"), QStringLiteral("-threads"), QStringLiteral("2"),
            QStringLiteral("-ss"), QString::number(start, 'f', 9), QStringLiteral("-i"), media.path,
            QStringLiteral("-t"), QString::number(duration, 'f', 9),
            QStringLiteral("-map"), QStringLiteral("0:%1").arg(media.videoStreamIndex)};
        if (!options.removeAudio)
            arguments += {QStringLiteral("-map"), QStringLiteral("0:a:0?")};
        else
            arguments += QStringLiteral("-an");
        arguments += {QStringLiteral("-sn"), QStringLiteral("-dn"),
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-crf"), QStringLiteral("18"),
            QStringLiteral("-preset"), QStringLiteral("fast"), QStringLiteral("-threads"), QStringLiteral("2"),
            QStringLiteral("-pix_fmt"), media.width % 2 == 0 && media.height % 2 == 0 ? QStringLiteral("yuv420p") : QStringLiteral("yuv444p"),
            QStringLiteral("-fps_mode"), QStringLiteral("vfr"),
            QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("192k"),
            QStringLiteral("-map_metadata"), QStringLiteral("-1"), QStringLiteral("-map_chapters"), QStringLiteral("-1")};
    } else {
        arguments += {QStringLiteral("-i"), media.path, QStringLiteral("-map"), QStringLiteral("0")};
        if (options.removeAudio)
            arguments += {QStringLiteral("-map"), QStringLiteral("-0:a?"), QStringLiteral("-an")};
        arguments += {QStringLiteral("-c"), QStringLiteral("copy"),
            QStringLiteral("-map_metadata"), QStringLiteral("0"), QStringLiteral("-map_chapters"), QStringLiteral("0")};
    }
    if (format == QStringLiteral("mp4") || format == QStringLiteral("mov"))
        arguments += {QStringLiteral("-movflags"), QStringLiteral("+faststart")};
    arguments += {QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats"), QStringLiteral("-f"), format, staging};
    const auto result = run(m_ffmpegPath, arguments, cancel, 0, duration, progress);
    if (!result.ok) return {false, result.cancelled, result.error, {}, 0};
    const auto verification = probe(m_ffprobePath, staging, cancel);
    if (!verification.ok) return {false, verification.cancelled, verification.error, {}, 0};
    const auto streams = verification.data.value(QStringLiteral("streams")).toArray();
    QJsonObject video;
    bool hasAudio = false;
    for (const auto &value : streams) {
        const auto stream = value.toObject();
        const QString type = stream.value(QStringLiteral("codec_type")).toString();
        if (type == QStringLiteral("audio")) hasAudio = true;
        if (type == QStringLiteral("video") && video.isEmpty()
            && stream.value(QStringLiteral("disposition")).toObject().value(QStringLiteral("attached_pic")).toInt() == 0) video = stream;
    }
    const double actualDuration = streamDuration(video);
    bool invalid = video.isEmpty();
    if (trimmed) {
        bool expectedAudio = !options.removeAudio && media.audioTracks > 0;
        if (expectedAudio) {
            const double inputStart = originalProbe.data.value(QStringLiteral("format")).toObject()
                .value(QStringLiteral("start_time")).toVariant().toDouble();
            for (const auto &value : originalProbe.data.value(QStringLiteral("streams")).toArray()) {
                const auto originalStream = value.toObject();
                if (originalStream.value(QStringLiteral("codec_type")).toString() != QStringLiteral("audio")) continue;
                const double audioStart = originalStream.value(QStringLiteral("start_time")).toVariant().toDouble() - inputStart;
                const double audioDuration = streamDuration(originalStream);
                // A video may outlast its sound. The selected region can then be
                // silent even when the source contains an audio stream.
                if (audioDuration > 0 && (audioStart + audioDuration <= start || audioStart >= end))
                    expectedAudio = false;
                break;
            }
        }
        const double frameTolerance = std::max(0.002, 1.01 / std::max(media.fps, 1.0));
        invalid |= hasAudio != expectedAudio || streams.size() != (expectedAudio ? 2 : 1)
            || video.value(QStringLiteral("codec_name")).toString() != QStringLiteral("h264")
            || video.value(QStringLiteral("width")).toInt() != media.width
            || video.value(QStringLiteral("height")).toInt() != media.height
            || actualDuration <= 0 || std::abs(duration - actualDuration) > frameTolerance;
        // Transcoding applies the display rotation to pixels. It must not leave
        // a second rotation behind for players to apply again.
        for (const auto &sideData : video.value(QStringLiteral("side_data_list")).toArray())
            invalid |= std::abs(sideData.toObject().value(QStringLiteral("rotation")).toDouble()) > 0.01;
    } else {
        const bool durationMismatch = media.videoDuration > 0 && actualDuration > 0
            && std::abs(media.videoDuration - actualDuration) > std::max({0.5, 3.0 / std::max(media.fps, 1.0), media.videoDuration * 0.02});
        invalid |= (options.removeAudio && hasAudio)
            || streamSignatures(streams, options.removeAudio) != streamSignatures(originalProbe.data.value(QStringLiteral("streams")).toArray(), options.removeAudio)
            || !chaptersMatch(originalProbe.data.value(QStringLiteral("chapters")).toArray(), verification.data.value(QStringLiteral("chapters")).toArray())
            || video.value(QStringLiteral("codec_name")).toString() != media.codec
            || video.value(QStringLiteral("width")).toInt() != media.codedWidth
            || video.value(QStringLiteral("height")).toInt() != media.codedHeight || durationMismatch;
    }
    if (invalid)
        return {false, false, QCoreApplication::translate("MediaEngine", "The exported video did not pass verification. No output file was saved."), {}, 0};
    const QString modified = unchanged(media);
    if (!modified.isEmpty()) return {false, false, modified, {}, 0};
    if (isCancelled(cancel)) return {false, true, QCoreApplication::translate("MediaEngine", "Operation cancelled."), {}, 0};
    QFile file(staging);
    if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) || !file.open(QIODevice::ReadOnly))
        return {false, false, QCoreApplication::translate("MediaEngine", "The completed video could not be prepared for saving."), {}, 0};
#ifdef Q_OS_UNIX
    if (::fsync(file.handle()) != 0)
        return {false, false, QCoreApplication::translate("MediaEngine", "The completed video could not be written to disk."), {}, 0};
#endif
    const qint64 size = file.size();
    file.close();
    if (isCancelled(cancel)) return {false, true, QCoreApplication::translate("MediaEngine", "Operation cancelled."), {}, 0};
#ifdef Q_OS_UNIX
    // link publishes an already verified file atomically and never replaces another file.
    if (::link(QFile::encodeName(staging).constData(), QFile::encodeName(destination).constData()) != 0) {
        if (errno == EEXIST)
            return {false, false, QCoreApplication::translate("MediaEngine", "A file appeared at this destination. Choose a new filename."), {}, 0};
        return {false, false, QCoreApplication::translate("MediaEngine", "This destination does not support safe export. Choose a folder on your local disk."), {}, 0};
    }
#else
    return {false, false, QCoreApplication::translate("MediaEngine", "Safe export is currently supported on macOS and Linux."), {}, 0};
#endif
    report(progress, 100);
    return {true, false, {}, destination, size};
}
