#pragma once

#include <QObject>
#include <QList>
#include <QHash>
#include <QPointer>
#include <QString>
#include <memory>
#include "mediaengine.h"

class QWidget;

class MediaController final : public QObject {
    Q_OBJECT
public:
    explicit MediaController(QString ffmpegPath, QString ffprobePath, QString cacheDir,
                             QString samplePath, QObject *parent = nullptr);
    ~MediaController() override;
    void setWindow(QWidget *window);
    const QList<MediaInfo> &library() const { return m_library; }
    const MediaInfo *currentMedia() const;
    bool busy() const { return m_busy; }
    bool exporting() const { return m_exporting; }
    bool removeAudio() const { return m_removeAudio; }
    QString lastExportPath() const { return m_lastExport; }
    double trimStart() const;
    double trimEnd() const;
    double trimmedDuration() const { return trimEnd() - trimStart(); }
    bool hasTrim() const;
    bool canUndoTrim() const;
    bool canRedoTrim() const;

public slots:
    void importVideo();
    void importFile(const QString &path);
    void loadSample();
    void selectMedia(const QString &id);
    void removeMedia(const QString &id);
    void removeCurrentMedia();
    void exportVideo();
    void cancelOperation();
    void revealExport();
    void setRemoveAudio(bool remove);
    void setTrimRange(double start, double end);
    void resetTrim();
    void beginTrimGesture();
    void endTrimGesture();
    void undoTrim();
    void redoTrim();

signals:
    void libraryChanged();
    void mediaChanged();
    void audioChanged(bool remove);
    void trimChanged();
    void busyChanged(bool busy, const QString &message);
    void progressChanged(double percent);
    void exportStarted();
    void exportCompleted(const QString &path, qint64 size);
    void operationCancelled();
    void errorOccurred(const QString &message);

private:
    struct TrimRange { double start = 0, end = 0; };
    struct ClipTrim { TrimRange range; QList<TrimRange> undo, redo; };
    ClipTrim &currentTrim();
    QHash<QString, ClipTrim> m_trims;
    QString m_trimGestureId;
    TrimRange m_trimGestureBefore;
    void discardCachedMedia(const MediaInfo &media);
    std::shared_ptr<MediaEngine> m_engine;
    QString m_samplePath, m_cacheDir;
    QList<MediaInfo> m_library;
    QString m_currentId, m_lastExport;
    bool m_busy = false, m_exporting = false, m_removeAudio = true;
    CancelFlag m_cancel;
    QPointer<QWidget> m_window;
};
