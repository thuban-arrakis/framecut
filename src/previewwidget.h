#pragma once

#include <QImage>
#include <QVideoFrame>
#include <QWidget>

class QPushButton;
struct SwsContext;

class PreviewWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit PreviewWidget(QWidget *parent = nullptr);
    ~PreviewWidget() override;
    QImage currentImage() const { return m_image; }
  public slots:
    void setFrame(const QVideoFrame &frame);
    void clearFrame();
    void setPoster(const QImage &image);
    void setEmpty(bool empty);
    void setAudioRemoved(bool removed);
    void setUnavailable(bool unavailable);
  signals:
    void clicked();
    void importRequested();
    void framePresented();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *) override;
    void changeEvent(QEvent *event) override;

  private:
    void refreshText();
    void positionImportButton();
    QImage m_image;
    SwsContext *m_scaler = nullptr;
    QPushButton *m_importButton;
    bool m_empty = true, m_removed = true, m_unavailable = false;
};
