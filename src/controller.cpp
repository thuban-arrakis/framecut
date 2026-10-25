#include "controller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPromise>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

MediaController::MediaController(QString ffmpegPath, QString ffprobePath, QString cacheDir,
                                 QString samplePath, QObject *parent)
    : QObject(parent), m_engine(std::make_shared<MediaEngine>(std::move(ffmpegPath),
          std::move(ffprobePath), cacheDir)), m_samplePath(std::move(samplePath)),
      m_cacheDir(std::move(cacheDir)) {}

MediaController::~MediaController() {
    // The worker owns its engine and cancellation flag until it finishes. It
    // must never depend on a surviving controller or window.
    cancelOperation();
}

void MediaController::setWindow(QWidget *window) { m_window = window; }

const MediaInfo *MediaController::currentMedia() const {
    for (const auto &media : m_library)
        if (media.id == m_currentId) return &media;
    return nullptr;
}

double MediaController::trimStart() const {
    const auto found = m_trims.constFind(m_currentId);
    return found == m_trims.cend() ? 0 : found->range.start;
}

double MediaController::trimEnd() const {
    const auto found = m_trims.constFind(m_currentId);
    const auto *media = currentMedia();
    return found == m_trims.cend() ? (media ? media->duration : 0) : found->range.end;
}

bool MediaController::hasTrim() const {
    const auto *media = currentMedia();
    return media && (trimStart() > 0.000001 || trimEnd() < media->duration - 0.000001);
}

bool MediaController::canUndoTrim() const {
    const auto found = m_trims.constFind(m_currentId);
    return !m_busy && found != m_trims.cend() && !found->undo.isEmpty();
}

bool MediaController::canRedoTrim() const {
    const auto found = m_trims.constFind(m_currentId);
    return !m_busy && found != m_trims.cend() && !found->redo.isEmpty();
}

MediaController::ClipTrim &MediaController::currentTrim() {
    if (!m_trims.contains(m_currentId))
        m_trims.insert(m_currentId, ClipTrim{{0, currentMedia()->duration}, {}, {}});
    return m_trims[m_currentId];
}

void MediaController::setTrimRange(double start, double end) {
    const auto *media = currentMedia();
    if (m_busy || !media || !std::isfinite(start) || !std::isfinite(end)) return;
    const double frame = std::min(media->duration, 1.0 / (media->fps > 0 ? media->fps : 25.0));
    start = qBound(0.0, start, qMax(0.0, media->duration - frame));
    end = qBound(start + frame, end, media->duration);
    auto &trim = currentTrim();
    if (std::abs(trim.range.start - start) < 0.000001 && std::abs(trim.range.end - end) < 0.000001) return;
    if (m_trimGestureId != m_currentId) {
        trim.undo.append(trim.range);
        if (trim.undo.size() > 100) trim.undo.removeFirst();
        trim.redo.clear();
    }
    trim.range = {start, end};
    emit trimChanged();
}

void MediaController::resetTrim() {
    if (const auto *media = currentMedia()) setTrimRange(0, media->duration);
}

void MediaController::beginTrimGesture() {
    if (m_busy || !currentMedia() || !m_trimGestureId.isEmpty()) return;
    m_trimGestureId = m_currentId;
    m_trimGestureBefore = currentTrim().range;
}

void MediaController::endTrimGesture() {
    if (m_trimGestureId.isEmpty()) return;
    auto found = m_trims.find(m_trimGestureId);
    if (found != m_trims.end()
        && (std::abs(found->range.start - m_trimGestureBefore.start) > 0.000001
            || std::abs(found->range.end - m_trimGestureBefore.end) > 0.000001)) {
        found->undo.append(m_trimGestureBefore);
        if (found->undo.size() > 100) found->undo.removeFirst();
        found->redo.clear();
    }
    m_trimGestureId.clear();
    emit trimChanged();
}

void MediaController::undoTrim() {
    if (!canUndoTrim() || !m_trimGestureId.isEmpty()) return;
    auto &trim = currentTrim();
    trim.redo.append(trim.range);
    trim.range = trim.undo.takeLast();
    emit trimChanged();
}

void MediaController::redoTrim() {
    if (!canRedoTrim() || !m_trimGestureId.isEmpty()) return;
    auto &trim = currentTrim();
    trim.undo.append(trim.range);
    trim.range = trim.redo.takeLast();
    emit trimChanged();
}

void MediaController::importVideo() {
    if (m_busy) return;
    const QString file = QFileDialog::getOpenFileName(m_window, tr("Import video"), {},
        tr("Video files (*.mp4 *.mov *.m4v *.mkv *.webm *.avi *.ts *.mts *.m2ts *.mpg *.mpeg *.ogv *.wmv);;All files (*)"));
    if (!file.isEmpty()) importFile(file);
}

void MediaController::loadSample() { importFile(m_samplePath); }

void MediaController::importFile(const QString &path) {
    if (m_busy) return;
    endTrimGesture();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    m_busy = true;
    m_exporting = false;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    emit busyChanged(true, tr("Preparing video preview…"));
    emit progressChanged(0);
    auto *watcher = new QFutureWatcher<MediaResult>(this);
    connect(watcher, &QFutureWatcher<MediaResult>::progressValueChanged,
            this, [this](int value) { emit progressChanged(value / 100.0); });
    connect(watcher, &QFutureWatcher<MediaResult>::finished, this, [this, watcher] {
        const MediaResult result = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        m_cancel.reset();
        emit busyChanged(false, {});
        if (result.cancelled) { emit operationCancelled(); return; }
        if (!result.ok) { emit errorOccurred(result.error); return; }
        bool replaced = false;
        MediaInfo previous;
        for (auto &media : m_library) {
            if (media.path == result.media.path) { previous = media; media = result.media; replaced = true; break; }
        }
        if (!replaced) m_library.append(result.media);
        m_currentId = result.media.id;
        if (replaced) m_trims.remove(previous.id);
        m_removeAudio = true;
        emit libraryChanged();
        emit mediaChanged();
        emit audioChanged(m_removeAudio);
        if (replaced) discardCachedMedia(previous);
    });
    const auto engine = m_engine;
    const auto cancel = m_cancel;
    watcher->setFuture(QtConcurrent::run([engine, absolute, cancel](QPromise<MediaResult> &promise) {
        promise.setProgressRange(0, 10000);
        promise.addResult(engine->importMedia(absolute, cancel, [&promise](double percent) {
            if (std::isfinite(percent)) promise.setProgressValue(qRound(qBound(0.0, percent, 100.0) * 100));
        }));
    }));
}

void MediaController::selectMedia(const QString &id) {
    if (m_busy || id == m_currentId) return;
    for (const auto &media : m_library) {
        if (media.id == id) {
            endTrimGesture();
            m_currentId = id;
            emit mediaChanged();
            return;
        }
    }
}

void MediaController::setRemoveAudio(bool remove) {
    if (m_exporting || m_removeAudio == remove) return;
    m_removeAudio = remove;
    emit audioChanged(remove);
}

void MediaController::removeMedia(const QString &id) {
    if (m_busy || id.isEmpty()) return;
    for (qsizetype index = 0; index < m_library.size(); ++index) {
        if (m_library.at(index).id != id) continue;
        const bool wasCurrent = id == m_currentId;
        endTrimGesture();
        const MediaInfo removed = m_library.takeAt(index);
        m_trims.remove(id);
        if (wasCurrent) {
            m_currentId = m_library.isEmpty() ? QString{} : m_library.at(qMin(index, m_library.size() - 1)).id;
            // Detach the player before cleaning up app-owned preview files.
            emit mediaChanged();
        }
        emit libraryChanged();
        discardCachedMedia(removed);
        return;
    }
}

void MediaController::removeCurrentMedia() { removeMedia(m_currentId); }

void MediaController::discardCachedMedia(const MediaInfo &media) {
    if (media.thumbnailPath.isEmpty()) return;
    const QFileInfo directory(QFileInfo(media.thumbnailPath).absolutePath());
    const QString root = QFileInfo(m_cacheDir).canonicalFilePath();
    const QString cached = directory.canonicalFilePath();
    const QString original = QFileInfo(media.path).canonicalFilePath();
    // Only a direct app-created clip directory may be removed; never the original.
    if (root.isEmpty() || cached.isEmpty() || directory.isSymLink()
        || directory.dir().canonicalPath() != root || !directory.fileName().startsWith("clip-")
        || original == cached || original.startsWith(cached + '/')) return;
    for (const auto &entry : m_library) {
        const QString source = QFileInfo(entry.path).canonicalFilePath();
        if (source == cached || source.startsWith(cached + '/')) return;
    }
    const QString exported = QFileInfo(m_lastExport).canonicalFilePath();
    if (exported == cached || exported.startsWith(cached + '/')) return;
    QDir(cached).removeRecursively();
}

void MediaController::exportVideo() {
    const MediaInfo *current = currentMedia();
    if (m_busy || !current || (!m_removeAudio && !hasTrim())) return;
    endTrimGesture();
    const MediaInfo media = *current;
    const bool trimmed = hasTrim();
    const ExportOptions options{trimStart(), trimEnd(), m_removeAudio};
    const QFileInfo source(media.path);
    const QString extension = trimmed ? QStringLiteral("mp4") : source.suffix().toLower();
    const QString stem = source.completeBaseName() + (trimmed ? "-trimmed" : "") + (m_removeAudio ? "-silent" : "");
    QDir outputDirectory = source.dir();
    const QString exportDirectory = qEnvironmentVariable("FRAMECUT_EXPORT_DIR");
    if (!exportDirectory.isEmpty()) {
        outputDirectory = QDir(exportDirectory);
    } else if (source.canonicalFilePath() == QFileInfo(m_samplePath).canonicalFilePath()) {
        QString destination = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (!QDir(destination).exists()) destination = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (!QDir(destination).exists()) destination = QDir::homePath();
        outputDirectory = QDir(destination);
    }
    QString defaultPath = outputDirectory.filePath(stem + '.' + extension);
    int copy = 2;
    while (QFileInfo::exists(defaultPath) || QFileInfo(defaultPath).isSymLink())
        defaultPath = outputDirectory.filePath(stem + '-' + QString::number(copy++) + '.' + extension);
    QString output = QFileDialog::getSaveFileName(m_window, trimmed ? tr("Export trimmed video") : tr("Export silent video"), defaultPath,
        trimmed ? tr("MP4 video (*.mp4)") : tr("Original video format (*.%1)").arg(extension), nullptr, QFileDialog::DontConfirmOverwrite);
    if (output.isEmpty()) return;
    const QString cacheRoot = QFileInfo(m_cacheDir).canonicalFilePath();
    const QString outputFolder = QFileInfo(QFileInfo(output).absolutePath()).canonicalFilePath();
    if (!cacheRoot.isEmpty() && (outputFolder == cacheRoot || outputFolder.startsWith(cacheRoot + '/'))) {
        emit errorOccurred(tr("Choose a folder outside the temporary preview cache."));
        return;
    }
    if (QFileInfo(output).suffix().isEmpty()) output += '.' + extension;
    if (QFileInfo(output).suffix().compare(extension, Qt::CaseInsensitive) != 0) {
        emit errorOccurred(trimmed ? tr("Use the .mp4 extension for a trimmed video.")
                                  : tr("Keep the .%1 extension to preserve the original video format.").arg(extension));
        return;
    }
    if (QFileInfo::exists(output) || QFileInfo(output).isSymLink()) {
        emit errorOccurred(tr("That file already exists. Choose a new name to keep existing files safe."));
        return;
    }
    m_busy = true;
    m_exporting = true;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    emit exportStarted();
    emit busyChanged(true, trimmed ? tr("Exporting trimmed video…") : tr("Exporting silent video…"));
    emit progressChanged(0);
    auto *watcher = new QFutureWatcher<ExportResult>(this);
    connect(watcher, &QFutureWatcher<ExportResult>::progressValueChanged,
            this, [this](int value) { emit progressChanged(value / 100.0); });
    connect(watcher, &QFutureWatcher<ExportResult>::finished, this, [this, watcher] {
        const ExportResult result = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        m_exporting = false;
        m_cancel.reset();
        emit busyChanged(false, {});
        if (result.cancelled) { emit operationCancelled(); return; }
        if (!result.ok) { emit errorOccurred(result.error); return; }
        m_lastExport = result.outputPath;
        emit exportCompleted(result.outputPath, result.size);
    });
    const auto engine = m_engine;
    const auto cancel = m_cancel;
    watcher->setFuture(QtConcurrent::run([engine, media, output, options, cancel](QPromise<ExportResult> &promise) {
        promise.setProgressRange(0, 10000);
        promise.addResult(engine->exportVideo(media, output, options, cancel, [&promise](double percent) {
            if (std::isfinite(percent)) promise.setProgressValue(qRound(qBound(0.0, percent, 100.0) * 100));
        }));
    }));
}

void MediaController::cancelOperation() {
    if (m_cancel) m_cancel->store(true);
}

void MediaController::revealExport() {
    if (m_lastExport.isEmpty() || !QFileInfo::exists(m_lastExport)) return;
#ifdef Q_OS_MACOS
    QProcess::startDetached("/usr/bin/open", {"-R", m_lastExport});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastExport).absolutePath()));
#endif
}
