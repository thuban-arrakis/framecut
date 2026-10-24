#pragma once

#include <QString>
#include <QMetaType>
#include <atomic>
#include <functional>
#include <memory>

struct MediaInfo {
    QString id;
    QString name;
    QString path;
    QString thumbnailPath;
    QString previewPath;
    QString codec;
    double duration = 0;
    double fps = 0;
    int width = 0;
    int height = 0;
    int audioTracks = 0;
    qint64 size = 0;
    bool previewTranscoded = false;

    // Original stream details used to verify a lossless export.
    int videoStreamIndex = -1;
    int codedWidth = 0;
    int codedHeight = 0;
    double videoDuration = 0;
    QString sourceFingerprint;
};

struct MediaResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    MediaInfo media;
};

struct ExportResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString outputPath;
    qint64 size = 0;
};

struct ExportOptions {
    double startSeconds = 0;
    double endSeconds = -1; // -1 selects the original end of the video.
    bool removeAudio = true;
};

using CancelFlag = std::shared_ptr<std::atomic_bool>;
using ProgressCallback = std::function<void(double)>;

class MediaEngine {
public:
    MediaEngine(QString ffmpegPath, QString ffprobePath, QString cacheDir);
    MediaResult importMedia(const QString &path, const CancelFlag &cancel,
                            const ProgressCallback &progress = {});
    ExportResult exportSilent(const MediaInfo &media, const QString &outputPath,
                              const CancelFlag &cancel, const ProgressCallback &progress = {});
    ExportResult exportVideo(const MediaInfo &media, const QString &outputPath,
                             const ExportOptions &options, const CancelFlag &cancel,
                             const ProgressCallback &progress = {});

private:
    QString m_ffmpegPath;
    QString m_ffprobePath;
    QString m_cacheDir;
};

Q_DECLARE_METATYPE(MediaInfo)
Q_DECLARE_METATYPE(MediaResult)
Q_DECLARE_METATYPE(ExportResult)
