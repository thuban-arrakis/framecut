#pragma once

#include <QHash>
#include <QMainWindow>

class MediaController;
class PreviewWidget;
class TimelineWidget;
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;
class QPushButton;
class QAbstractButton;
class QLabel;
class QListWidget;
class QSlider;
class QProgressBar;
class QTabWidget;
class QFrame;
class QAction;
class QDialog;
class QDoubleSpinBox;
class QTimer;

class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(MediaController *controller, QWidget *parent = nullptr);
    ~MainWindow() override;

  protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    void rebuildLibrary();
    void changeMedia();
    void updateUi();
    void updatePosition(qint64 position);
    void updateTrim();
    double lastTrimFrame() const;
    void refreshAudioOutput();
    void togglePlayback();
    void seek(double seconds);
    void showNotice(const QString &text, bool error = true);
    void showExportResult(const QString &path, qint64 size);
    void showShortcuts();
    void showSettings();
    void applyTheme();
    void retranslateUi();
    void removeSelectedMedia();
    MediaController *m_controller;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QVideoSink *m_sink;
    PreviewWidget *m_preview;
    TimelineWidget *m_timeline;
    QListWidget *m_library;
    QWidget *m_emptyLibrary;
    QTabWidget *m_inspector;
    QPushButton *m_import, *m_sample, *m_export, *m_play, *m_audioAction, *m_cancel, *m_remove;
    QAction *m_removeAction = nullptr;
    QAction *m_undoTrimAction = nullptr, *m_redoTrimAction = nullptr;
    QPushButton *m_trimButton = nullptr, *m_resetTrim = nullptr;
    QDoubleSpinBox *m_trimStart = nullptr, *m_trimEnd = nullptr;
    QLabel *m_trimDuration = nullptr, *m_trimExportHint = nullptr;
    QLabel *m_qualityHeading = nullptr, *m_qualityDescription = nullptr;
    QTimer *m_rangeTimer = nullptr;
    QDialog *m_settingsDialog = nullptr;
    QAbstractButton *m_audioToggle;
    QLabel *m_projectTitle, *m_previewInfo, *m_time, *m_status, *m_count, *m_audioDescription,
        *m_audioState, *m_timelineInfo, *m_busyMessage, *m_noticeText;
    QSlider *m_seek;
    QProgressBar *m_progress;
    QFrame *m_busyBanner, *m_notice;
    QHash<QString, QLabel *> m_properties;
    QList<QAbstractButton *> m_playbackButtons;
    bool m_uiReady = false;
    bool m_noticeError = true;
    bool m_cancelling = false;
    bool m_adjustingBoundary = false;
    bool m_atTrimEnd = false;
    bool m_pendingInitialSeek = false;
};
