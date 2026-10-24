#pragma once
#include "mediaengine.h"
#include <QImage>
#include <QWidget>

class TimelineWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit TimelineWidget(QWidget *parent = nullptr);
    void setMedia(const MediaInfo *media);
    void setPosition(double seconds);
    void setAudioRemoved(bool removed);
    void setTrimRange(double start, double end);
    QRectF clipRect() const;
    QRectF leftHandleRect() const;
    QRectF rightHandleRect() const;
  signals:
    void seekRequested(double seconds);
    void importRequested();
    void trimRangeChanged(double start, double end);
    void trimGestureStarted();
    void trimGestureFinished();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    enum class Edge { None, Start, End };
    double timelineSpan() const;
    double pixelForSeconds(double seconds) const;
    double secondsForPixel(double x) const;
    double minimumDuration() const;
    double lastIncludedTime() const;
    double snapTime(double seconds) const;
    Edge edgeAt(const QPointF &position) const;
    void updateHover(const QPointF &position);
    void dragEdge(double x);
    void finishTrim(bool restore);
    void seekAt(double x);
    QString m_name;
    QImage m_thumb;
    double m_duration = 0, m_position = 0, m_fps = 0;
    double m_trimStart = 0, m_trimEnd = 0;
    double m_originalStart = 0, m_originalEnd = 0, m_grabOffset = 0;
    Edge m_dragEdge = Edge::None, m_hoverEdge = Edge::None;
    bool m_scrubbing = false;
    bool m_removed = true, m_hasMedia = false;
    int m_audioTracks = 0;
};
