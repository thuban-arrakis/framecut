#include "timelinewidget.h"
#include "appsettings.h"
#include "theme.h"
#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace {
QString timeLabel(double seconds) {
    int s = qMax(0, int(std::round(seconds)));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}
QString preciseTime(double seconds) {
    const qint64 milliseconds = qMax<qint64>(0, qRound64(seconds * 1000));
    return QString("%1:%2.%3")
        .arg(milliseconds / 60000)
        .arg(milliseconds / 1000 % 60, 2, 10, QChar('0'))
        .arg(milliseconds % 1000, 3, 10, QChar('0'));
}
} // namespace
TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent) {
    setObjectName("timelineWidget");
    setAccessibleName(tr("Video timeline. Drag either clip edge to trim. Use left and right arrows to seek."));
    setToolTip(tr("Drag either edge to trim. Drag outward to restore footage. Press Escape to cancel a drag."));
    setMinimumHeight(158);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(AppSettings::instance(), &AppSettings::themeChanged, this, [this] { update(); });
}
void TimelineWidget::setMedia(const MediaInfo *media) {
    finishTrim(false);
    m_scrubbing = false;
    m_hoverEdge = Edge::None;
    unsetCursor();
    m_hasMedia = media;
    m_duration = media ? media->duration : 0;
    m_fps = media ? media->fps : 0;
    m_trimStart = 0;
    m_trimEnd = m_duration;
    m_name = media ? media->name : QString();
    m_audioTracks = media ? media->audioTracks : 0;
    m_position = 0;
    m_thumb = media ? QImage(media->thumbnailPath) : QImage();
    if (!m_thumb.isNull())
        m_thumb = m_thumb.scaledToHeight(43, Qt::SmoothTransformation);
    update();
}
void TimelineWidget::setPosition(double seconds) {
    if (!std::isfinite(seconds))
        return;
    const double previous = m_position;
    m_position = qBound(0., seconds, m_duration);
    if (previous != m_position) {
        // Only the old and new playhead columns change during playback. Avoid
        // repainting the entire thumbnail strip at every playback tick.
        const auto headArea = [this](double time) {
            return QRectF(pixelForSeconds(time) - 6, 19, 12, height() - 19).toAlignedRect();
        };
        update(QRegion(headArea(previous)) | QRegion(headArea(m_position)));
    }
}
void TimelineWidget::setAudioRemoved(bool removed) {
    m_removed = removed;
    update();
}
void TimelineWidget::setTrimRange(double start, double end) {
    if (!m_hasMedia || !std::isfinite(start) || !std::isfinite(end))
        return;
    start = qBound(0., start, m_duration);
    end = qBound(0., end, m_duration);
    if (end - start < minimumDuration()) {
        end = qMin(m_duration, start + minimumDuration());
        start = qMax(0., end - minimumDuration());
    }
    if (m_trimStart == start && m_trimEnd == end)
        return;
    m_trimStart = start;
    m_trimEnd = end;
    update();
}
double TimelineWidget::timelineSpan() const {
    return m_hasMedia ? qMax(1., m_duration * 1.15) : 30.;
}
double TimelineWidget::pixelForSeconds(double seconds) const {
    return 132 + qMax(1, width() - 132 - 24) * seconds / timelineSpan();
}
double TimelineWidget::secondsForPixel(double x) const {
    return (x - 132) / qMax(1, width() - 132 - 24) * timelineSpan();
}
double TimelineWidget::minimumDuration() const {
    const double frame = std::isfinite(m_fps) && m_fps > 0 ? 1. / m_fps : .04;
    return qMin(m_duration, frame);
}
double TimelineWidget::lastIncludedTime() const {
    return qMax(m_trimStart, m_trimEnd - minimumDuration());
}
double TimelineWidget::snapTime(double seconds) const {
    if (std::isfinite(m_fps) && m_fps > 0)
        seconds = std::round(seconds * m_fps) / m_fps;
    return qBound(0., seconds, m_duration);
}
QRectF TimelineWidget::clipRect() const {
    if (!m_hasMedia)
        return {};
    return QRectF(pixelForSeconds(m_trimStart), 41,
                  pixelForSeconds(m_trimEnd) - pixelForSeconds(m_trimStart), 64);
}
QRectF TimelineWidget::leftHandleRect() const {
    return m_hasMedia ? QRectF(clipRect().left() - 8, 36, 16, 74) : QRectF();
}
QRectF TimelineWidget::rightHandleRect() const {
    return m_hasMedia ? QRectF(clipRect().right() - 8, 36, 16, 74) : QRectF();
}
TimelineWidget::Edge TimelineWidget::edgeAt(const QPointF &position) const {
    const bool left = leftHandleRect().contains(position);
    const bool right = rightHandleRect().contains(position);
    if (left && right)
        return std::abs(position.x() - clipRect().left()) <= std::abs(position.x() - clipRect().right())
                   ? Edge::Start : Edge::End;
    return left ? Edge::Start : right ? Edge::End : Edge::None;
}
void TimelineWidget::updateHover(const QPointF &position) {
    const Edge edge = isEnabled() ? edgeAt(position) : Edge::None;
    if (edge != m_hoverEdge) {
        m_hoverEdge = edge;
        update();
    }
    if (edge != Edge::None)
        setCursor(Qt::SizeHorCursor);
    else if (m_hasMedia && position.x() >= 132)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
}
void TimelineWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Theme::color("#1a1d1f", "#f4f7f5"));
    const int labels = 132;
    const double available = qMax(1, width() - labels - 24), span = timelineSpan();
    p.fillRect(QRect(0, 0, labels, height()), Theme::color("#202326", "#eaf0ec"));
    p.setPen(Theme::color("#30363a", "#cbd8d0"));
    p.drawLine(labels, 0, labels, height());
    p.drawLine(labels, 32, width(), 32);
    p.drawLine(0, 113, width(), 113);
    p.setPen(Theme::color("#293034", "#d9e2dc"));
    p.drawLine(0, 163, width(), 163);
    QFont f = font();
    f.setPixelSize(9);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    p.setFont(f);
    p.setPen(Theme::color("#9daba3", "#52695c"));
    p.drawText(20, 20, tr("TRACKS"));
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(Theme::color("#9eada5", "#304c3c"));
    p.drawText(22, 77, tr("Video"));
    p.setPen(Theme::color("#778d80", "#486a56"));
    p.drawText(22, 142, tr("Audio"));
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(Theme::color("#92a899", "#526a5b"));
    p.drawText(106, 77, "01");
    for (int i = 0; i <= 40; ++i) {
        const double x = labels + available * i / 40.;
        p.setPen((i % 5 == 0 ? Theme::color("#525f57", "#96aa9d") : Theme::color("#353f39", "#c5d3ca")));
        p.drawLine(QPointF(x, 27), QPointF(x, 32));
        if (i % 5 == 0) {
            p.setPen(Theme::color("#84928a", "#4c6556"));
            QRectF label(x - 25, 5, 50, 16);
            if (i == 0)
                label = QRectF(x + 5, 5, 50, 16);
            if (i == 40)
                label = QRectF(x - 48, 5, 48, 16);
            p.drawText(label, i == 0 ? Qt::AlignLeft : Qt::AlignCenter, timeLabel(span * i / 40.));
        }
    }
    if (!m_hasMedia) {
        QRectF empty(labels + 18, 48, available - 18, 100);
        p.setPen(QPen(Theme::color("#405448", "#a3bcae"), 1, Qt::DashLine));
        p.setBrush(Theme::color("#1e2822", "#edf5ef"));
        p.drawRoundedRect(empty, 5, 5);
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(Theme::color("#a9bdb1", "#345943"));
        p.drawText(empty.adjusted(0, -9, 0, -9), Qt::AlignCenter, tr("+   Add a video to start editing"));
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(Theme::color("#6c8576", "#577662"));
        p.drawText(empty.adjusted(0, 29, 0, 29), Qt::AlignCenter, tr("Your clip and audio will appear here"));
        return;
    }
    const QRectF clip = clipRect();
    const QRectF source(pixelForSeconds(0), clip.top(), pixelForSeconds(m_duration) - pixelForSeconds(0), clip.height());
    if (m_trimStart > 0 || m_trimEnd < m_duration) {
        p.setPen(QPen(Theme::color("#3c4941", "#bdcec2"), 1, Qt::DashLine));
        p.setBrush(QBrush(Theme::color("#27332c", "#e3ece6"), Qt::BDiagPattern));
        p.drawRoundedRect(source, 4, 4);
        p.setBrush(Theme::color("#202923", "#edf2ee"));
        p.drawRoundedRect(QRectF(source.left(), 122, source.width(), 33), 4, 4);
    }
    p.setPen(QPen(Theme::color("#a3e8c2", "#218358"), 1.5));
    p.setBrush(Theme::color("#304b3b", "#d1e7da"));
    p.drawRoundedRect(clip, 4, 4);
    p.save();
    p.setClipRect(clip.adjusted(1, 21, -1, -1));
    if (!m_thumb.isNull())
        for (double x = source.left(); x < clip.right(); x += m_thumb.width())
            p.drawImage(QPointF(x, clip.top() + 21), m_thumb);
    p.restore();
    p.setPen(Theme::color("#d0e9d8", "#1e4c32"));
    f.setPixelSize(10);
    p.setFont(f);
    const QString length = preciseTime(m_trimEnd - m_trimStart);
    const int lengthWidth = clip.width() > 200 ? QFontMetrics(f).horizontalAdvance(length) + 15 : 0;
    p.drawText(clip.adjusted(11, 2, -11 - lengthWidth, -44), Qt::AlignVCenter,
               QFontMetrics(f).elidedText(m_name, Qt::ElideRight, qMax(0, int(clip.width()) - 22 - lengthWidth)));
    if (lengthWidth)
        p.drawText(clip.adjusted(11, 2, -11, -44), Qt::AlignRight | Qt::AlignVCenter, length);
    for (const auto edge : {Edge::Start, Edge::End}) {
        const double x = edge == Edge::Start ? clip.left() : clip.right();
        const bool active = m_dragEdge == edge || m_hoverEdge == edge;
        p.setPen(Qt::NoPen);
        p.setBrush(active ? Theme::color("#c8f9dd", "#0c6540") : Theme::color("#8fd4ad", "#398e61"));
        p.drawRoundedRect(QRectF(x - 3, clip.top() + 1, 6, clip.height() - 2), 2, 2);
        p.setPen(QPen(Theme::color("#23412f", "#f6fff8"), 1));
        p.drawLine(QPointF(x, clip.center().y() - 6), QPointF(x, clip.center().y() + 6));
    }
    QRectF audio(clip.left(), 122, clip.width(), 33);
    p.setPen(QPen((m_removed ? Theme::color("#466452", "#90b39c") : Theme::color("#587e65", "#539468")), 1,
                  m_removed ? Qt::DashLine : Qt::SolidLine));
    p.setBrush((m_removed ? Theme::color("#213128", "#e2eee6") : Theme::color("#2f4939", "#c9e5d3")));
    p.drawRoundedRect(audio, 4, 4);
    p.setPen((m_removed ? Theme::color("#8bb49a", "#3e6950") : Theme::color("#b5d7c1", "#255d39")));
    const QString audioText = m_audioTracks == 0 ? tr("No audio in source")
                              : m_removed        ? tr("Audio removed  ✓")
                                                 : tr("Original audio");
    p.drawText(audio.adjusted(8, 0, -8, 0), Qt::AlignCenter,
               QFontMetrics(f).elidedText(audioText, Qt::ElideRight, qMax(0, int(audio.width()) - 16)));
    const double head = pixelForSeconds(m_position);
    p.setPen(QPen(Theme::color("#d0e9d9", "#1f7853"), 1));
    p.drawLine(QPointF(head, 27), QPointF(head, height()));
    p.setBrush(Theme::color("#d0e9d9", "#1f7853"));
    QPolygonF marker;
    marker << QPointF(head - 4, 21) << QPointF(head + 4, 21) << QPointF(head + 4, 28) << QPointF(head, 32)
           << QPointF(head - 4, 28);
    p.drawPolygon(marker);
    if (hasFocus()) {
        p.setPen(QPen(Theme::color("#76ad8e", "#25704b"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -2, -2));
    }
}
void TimelineWidget::seekAt(double x) {
    if (!m_hasMedia || !isEnabled())
        return;
    emit seekRequested(qBound(m_trimStart, secondsForPixel(x), lastIncludedTime()));
}
void TimelineWidget::dragEdge(double x) {
    if (m_dragEdge == Edge::None || !m_hasMedia || !isEnabled())
        return;
    const double time = snapTime(secondsForPixel(x - m_grabOffset));
    if (m_dragEdge == Edge::Start)
        setTrimRange(qMin(time, m_trimEnd - minimumDuration()), m_trimEnd);
    else
        setTrimRange(m_trimStart, qMax(time, m_trimStart + minimumDuration()));
    emit trimRangeChanged(m_trimStart, m_trimEnd);
    emit seekRequested(m_dragEdge == Edge::Start ? m_trimStart : lastIncludedTime());
    const QString edgeText = m_dragEdge == Edge::Start ? tr("Start: %1").arg(preciseTime(m_trimStart))
                                                     : tr("End: %1").arg(preciseTime(m_trimEnd));
    QToolTip::showText(mapToGlobal(QPoint(qRound(x), 34)),
                      edgeText + '\n' + tr("Duration: %1").arg(preciseTime(m_trimEnd - m_trimStart)), this);
}
void TimelineWidget::finishTrim(bool restore) {
    if (m_dragEdge == Edge::None)
        return;
    if (restore) {
        setTrimRange(m_originalStart, m_originalEnd);
        emit trimRangeChanged(m_trimStart, m_trimEnd);
        emit seekRequested(qBound(m_trimStart, m_position, lastIncludedTime()));
    }
    m_dragEdge = Edge::None;
    QToolTip::hideText();
    emit trimGestureFinished();
    update();
}
void TimelineWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && isEnabled()) {
        if (!m_hasMedia)
            emit importRequested();
        else {
            setFocus(Qt::MouseFocusReason);
            m_dragEdge = edgeAt(event->position());
            if (m_dragEdge != Edge::None) {
                m_originalStart = m_trimStart;
                m_originalEnd = m_trimEnd;
                m_grabOffset = event->position().x() - pixelForSeconds(m_dragEdge == Edge::Start ? m_trimStart : m_trimEnd);
                emit trimGestureStarted();
                emit seekRequested(m_dragEdge == Edge::Start ? m_trimStart : lastIncludedTime());
                setCursor(Qt::SizeHorCursor);
                update();
            } else if (event->position().x() >= 132) {
                m_scrubbing = true;
                seekAt(event->position().x());
            }
        }
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}
void TimelineWidget::mouseMoveEvent(QMouseEvent *event) {
    // A release can be consumed by a window switch or modal UI. Do not keep
    // changing the trim after the pointer returns with no button held.
    if (!event->buttons().testFlag(Qt::LeftButton)) {
        finishTrim(false);
        m_scrubbing = false;
        updateHover(event->position());
    } else if (m_dragEdge != Edge::None)
        dragEdge(event->position().x());
    else if (m_scrubbing && event->buttons().testFlag(Qt::LeftButton))
        seekAt(event->position().x());
    else
        updateHover(event->position());
}
void TimelineWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (m_dragEdge != Edge::None)
            dragEdge(event->position().x());
        finishTrim(false);
        m_scrubbing = false;
        updateHover(event->position());
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}
void TimelineWidget::leaveEvent(QEvent *event) {
    if (m_dragEdge == Edge::None) {
        m_hoverEdge = Edge::None;
        unsetCursor();
        update();
    }
    QWidget::leaveEvent(event);
}
void TimelineWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && m_dragEdge != Edge::None) {
        finishTrim(true);
        event->accept();
    } else if (!m_hasMedia || !isEnabled()) {
        QWidget::keyPressEvent(event);
    } else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        emit seekRequested(qBound(m_trimStart,
                                  m_position + (event->key() == Qt::Key_Left ? -1 : 1) *
                                                   (event->modifiers().testFlag(Qt::ShiftModifier) ? 5 : 1),
                                  lastIncludedTime()));
        event->accept();
    } else if (event->key() == Qt::Key_Home) {
        emit seekRequested(m_trimStart);
        event->accept();
    } else if (event->key() == Qt::Key_End) {
        emit seekRequested(lastIncludedTime());
        event->accept();
    } else
        QWidget::keyPressEvent(event);
}

void TimelineWidget::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        setAccessibleName(tr("Video timeline. Drag either clip edge to trim. Use left and right arrows to seek."));
        setToolTip(tr("Drag either edge to trim. Drag outward to restore footage. Press Escape to cancel a drag."));
        update();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        update();
    } else if (event->type() == QEvent::EnabledChange && !isEnabled()) {
        finishTrim(false);
        m_scrubbing = false;
        m_hoverEdge = Edge::None;
        unsetCursor();
    }
}
