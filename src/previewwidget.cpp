#include "previewwidget.h"
#include "appsettings.h"
#include "theme.h"

#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QVideoFrameFormat>
#include <algorithm>
extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace {
struct EmptyLayout {
    qreal artCenter = 0;
    qreal artScale = 1;
    QRectF eyebrow, title, description, footer;
    int buttonY = 0;
};

EmptyLayout emptyLayout(int width, int height) {
    const bool compact = height < 360;
    const int artHeight = compact ? 50 : 72;
    const int titleHeight = compact ? 50 : 62;
    const int descriptionHeight = compact ? 40 : 46;
    const int totalHeight = artHeight + 8 + 24 + titleHeight + 4 + descriptionHeight + 12 + 36 + 8 + 28;
    const qreal top = qMax(8., (height - totalHeight) / 2.);
    EmptyLayout layout;
    layout.artCenter = top + artHeight / 2.;
    layout.artScale = compact ? .58 : .8;
    layout.eyebrow = QRectF(14, top + artHeight + 8, width - 28, 20);
    layout.title = QRectF(16, layout.eyebrow.y() + 24, width - 32, titleHeight);
    layout.description = QRectF(18, layout.title.bottom() + 4, width - 36, descriptionHeight);
    layout.buttonY = qRound(layout.description.bottom() + 12);
    layout.footer = QRectF(12, layout.buttonY + 44, width - 24, 28);
    return layout;
}

QFont fittingFont(QFont font, const QString &text, const QRectF &rect, int maximum, int minimum,
                  int flags = Qt::AlignCenter | Qt::TextWordWrap) {
    for (int size = maximum; size >= minimum; --size) {
        font.setPixelSize(size);
        const QRect bounds = QFontMetrics(font).boundingRect(rect.toRect(), flags, text);
        if (bounds.width() <= rect.width() && bounds.height() <= rect.height())
            break;
    }
    return font;
}

AVPixelFormat pixelFormat(QVideoFrameFormat::PixelFormat format) {
    using F = QVideoFrameFormat;
    switch (format) {
    case F::Format_YUV420P:
    case F::Format_YV12:
        return AV_PIX_FMT_YUV420P;
    case F::Format_YUV422P:
        return AV_PIX_FMT_YUV422P;
    case F::Format_NV12:
        return AV_PIX_FMT_NV12;
    case F::Format_NV21:
        return AV_PIX_FMT_NV21;
    case F::Format_UYVY:
        return AV_PIX_FMT_UYVY422;
    case F::Format_YUYV:
        return AV_PIX_FMT_YUYV422;
    case F::Format_P010:
        return AV_PIX_FMT_P010LE;
    case F::Format_P016:
        return AV_PIX_FMT_P016LE;
    case F::Format_YUV420P10:
        return AV_PIX_FMT_YUV420P10LE;
    case F::Format_Y8:
        return AV_PIX_FMT_GRAY8;
    case F::Format_Y16:
        return AV_PIX_FMT_GRAY16LE;
    default:
        return AV_PIX_FMT_NONE;
    }
}
QImage oriented(QImage image, const QVideoFrame &frame) {
    const auto format = frame.surfaceFormat();
    if (format.scanLineDirection() == QVideoFrameFormat::BottomToTop)
        image = image.mirrored(false, true);
    const QRect crop = format.viewport().intersected(image.rect());
    if (crop.isValid() && crop != image.rect())
        image = image.copy(crop);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (int(format.rotation()) != 0)
        image = image.transformed(QTransform().rotate(int(format.rotation())));
    if (format.isMirrored())
        image = image.mirrored(true, false);
    if (int(frame.rotation()) != 0)
        image = image.transformed(QTransform().rotate(int(frame.rotation())));
    if (frame.mirrored())
        image = image.mirrored(true, false);
#else
    if (int(frame.rotationAngle()) != 0)
        image = image.transformed(QTransform().rotate(int(frame.rotationAngle())));
    if (frame.mirrored())
        image = image.mirrored(true, false);
#endif
    return image;
}
} // namespace

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent), m_importButton(new QPushButton(this)) {
    setObjectName("previewWidget");
    setMinimumSize(280, 290);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_importButton->setObjectName("previewImportButton");
    m_importButton->setProperty("primary", true);
    m_importButton->setFixedHeight(36);
    refreshText();
    connect(m_importButton, &QPushButton::clicked, this, &PreviewWidget::importRequested);
    connect(AppSettings::instance(), &AppSettings::themeChanged, this, [this] { update(); });
}
PreviewWidget::~PreviewWidget() {
    if (m_scaler)
        sws_freeContext(m_scaler);
}
void PreviewWidget::clearFrame() {
    m_image = {};
    m_unavailable = false;
    update();
}
void PreviewWidget::setPoster(const QImage &image) {
    m_image = image;
    m_unavailable = false;
    update();
}
void PreviewWidget::setEmpty(bool empty) {
    m_empty = empty;
    if (empty) {
        m_image = {};
        m_unavailable = false;
    }
    m_importButton->setVisible(empty);
    setCursor(empty ? Qt::ArrowCursor : Qt::PointingHandCursor);
    update();
}
void PreviewWidget::setAudioRemoved(bool removed) {
    if (m_removed != removed) {
        m_removed = removed;
        update();
    }
}
void PreviewWidget::setUnavailable(bool unavailable) {
    m_unavailable = unavailable;
    update();
}
void PreviewWidget::setFrame(const QVideoFrame &input) {
    if (!input.isValid())
        return;
    QVideoFrame frame(input);
    if (!frame.map(QVideoFrame::ReadOnly)) {
        setUnavailable(true);
        return;
    }
    QImage next;
    const auto direct = QVideoFrameFormat::imageFormatFromPixelFormat(frame.pixelFormat());
    if (direct != QImage::Format_Invalid && frame.bits(0) && frame.bytesPerLine(0) > 0) {
        next = QImage(frame.bits(0), frame.width(), frame.height(), frame.bytesPerLine(0), direct).copy();
    } else if (frame.pixelFormat() == QVideoFrameFormat::Format_Jpeg) {
        next = QImage::fromData(frame.bits(0), frame.mappedBytes(0), "JPG");
    } else {
        const AVPixelFormat format = pixelFormat(frame.pixelFormat());
        const auto *descriptor = format == AV_PIX_FMT_NONE ? nullptr : av_pix_fmt_desc_get(format);
        int requiredPlanes = format == AV_PIX_FMT_NONE ? 0 : av_pix_fmt_count_planes(format);
        if (descriptor && frame.planeCount() >= requiredPlanes && frame.width() > 0 && frame.height() > 0) {
            const uint8_t *source[4] = {};
            int strides[4] = {};
            bool valid = true;
            for (int plane = 0; plane < requiredPlanes; ++plane) {
                source[plane] = frame.bits(plane);
                strides[plane] = frame.bytesPerLine(plane);
                const int rows =
                    plane == 1 || plane == 2
                        ? (frame.height() + (1 << descriptor->log2_chroma_h) - 1) >> descriptor->log2_chroma_h
                        : frame.height();
                if (!source[plane] || strides[plane] <= 0 ||
                    qint64(frame.mappedBytes(plane)) < qint64(strides[plane]) * rows)
                    valid = false;
            }
            if (frame.pixelFormat() == QVideoFrameFormat::Format_YV12) {
                std::swap(source[1], source[2]);
                std::swap(strides[1], strides[2]);
            }
            if (valid) {
                m_scaler = sws_getCachedContext(m_scaler, frame.width(), frame.height(), format,
                                                frame.width(), frame.height(), AV_PIX_FMT_RGBA, SWS_BILINEAR,
                                                nullptr, nullptr, nullptr);
                if (m_scaler) {
                    int colorSpace = SWS_CS_DEFAULT;
                    if (frame.surfaceFormat().colorSpace() == QVideoFrameFormat::ColorSpace_BT709)
                        colorSpace = SWS_CS_ITU709;
                    if (frame.surfaceFormat().colorSpace() == QVideoFrameFormat::ColorSpace_BT2020)
                        colorSpace = SWS_CS_BT2020;
                    const int *coefficients = sws_getCoefficients(colorSpace);
                    sws_setColorspaceDetails(m_scaler, coefficients,
                                             frame.surfaceFormat().colorRange() ==
                                                 QVideoFrameFormat::ColorRange_Full,
                                             coefficients, 1, 0, 1 << 16, 1 << 16);
                    next = QImage(frame.size(), QImage::Format_RGBA8888);
                    uint8_t *destination[4] = {next.bits(), nullptr, nullptr, nullptr};
                    int destinationStrides[4] = {int(next.bytesPerLine()), 0, 0, 0};
                    if (sws_scale(m_scaler, source, strides, 0, frame.height(), destination,
                                  destinationStrides) <= 0)
                        next = {};
                }
            }
        }
    }
    frame.unmap();
    if (next.isNull()) {
        setUnavailable(true);
        return;
    }
    m_image = oriented(next, input);
    m_unavailable = false;
    update();
    emit framePresented();
}

void PreviewWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Loaded footage stays color-accurate; theme changes only the surrounding UI.
    p.fillRect(rect(), m_empty ? Theme::color("#111315", "#f6f9f7") : QColor("#111315"));
    p.setPen(Theme::color("#2c3033", "#d4ded8"));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    if (m_empty) {
        const EmptyLayout layout = emptyLayout(width(), height());
        const qreal cx = width() / 2.;
        QRadialGradient glow(QPointF(cx, layout.artCenter + 30), qMin(width(), height()) * .7);
        glow.setColorAt(0, Theme::color("#1d2922", "#e4f3eb"));
        glow.setColorAt(1, Theme::color("#111315", "#f6f9f7"));
        p.fillRect(rect().adjusted(1, 1, -1, -1), glow);
        p.save();
        p.translate(cx, layout.artCenter);
        p.scale(layout.artScale, layout.artScale);
        p.save();
        p.translate(-8, -6);
        p.rotate(-12);
        p.setPen(Theme::color("#465449", "#96b6a2"));
        p.setBrush(Theme::color("#232c26", "#d8e8dd"));
        p.drawRoundedRect(QRectF(-51, -32, 102, 68), 9, 9);
        p.restore();
        p.translate(5, 0);
        p.rotate(5);
        QLinearGradient gradient(-50, -35, 50, 35);
        gradient.setColorAt(0, Theme::color("#3a4e41", "#d1e8da"));
        gradient.setColorAt(1, Theme::color("#27352d", "#b8d5c4"));
        p.setBrush(gradient);
        p.setPen(Theme::color("#667e6e", "#789e87"));
        p.drawRoundedRect(QRectF(-51, -34, 102, 68), 9, 9);
        p.setPen(QPen(Theme::color("#8fb69f", "#3e7957"), 3, Qt::SolidLine, Qt::RoundCap));
        const int bars[] = {10, 18, 27, 34, 27, 18, 10};
        for (int i = 0; i < 7; ++i)
            p.drawLine(QPointF(-21 + i * 7, -bars[i] / 2.), QPointF(-21 + i * 7, bars[i] / 2.));
        p.setPen(QPen(Theme::color("#2c3d32", "#c6dfd0"), 6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(-24, 20, 24, -22);
        p.setPen(QPen(Theme::color("#bee9ce", "#246d46"), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(-24, 22, 24, -20);
        p.restore();

        QFont f = font();
        f.setWeight(QFont::DemiBold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, .8);
        const QString eyebrow = tr("YOUR PICTURE. PERFECTLY QUIET.");
        p.setFont(fittingFont(f, eyebrow, layout.eyebrow, 9, 8));
        p.setPen(Theme::color("#81a88e", "#426f55"));
        p.drawText(layout.eyebrow, Qt::AlignCenter | Qt::TextWordWrap, eyebrow);

        f.setLetterSpacing(QFont::AbsoluteSpacing, -.4);
        const QString title = tr("Let the visuals speak.");
        p.setFont(fittingFont(f, title, layout.title, width() < 430 ? 24 : 29, 17));
        p.setPen(Theme::color("#e6ede8", "#203e2c"));
        p.drawText(layout.title, Qt::AlignCenter | Qt::TextWordWrap, title);

        f.setWeight(QFont::Normal);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0);
        const QString description =
            tr("Drop in a video. Take out the sound.\nMake something that says more.");
        p.setFont(fittingFont(f, description, layout.description, 12, 10));
        p.setPen(Theme::color("#839087", "#546f5e"));
        p.drawText(layout.description, Qt::AlignCenter | Qt::TextWordWrap, description);

        const QString footer = tr("or drag and drop it anywhere");
        p.setFont(fittingFont(f, footer, layout.footer, 10, 9));
        p.setPen(Theme::color("#718b7b", "#526d5b"));
        p.drawText(layout.footer, Qt::AlignCenter | Qt::TextWordWrap, footer);
        return;
    }
    if (!m_image.isNull()) {
        QSizeF size = m_image.size();
        size.scale(QSizeF(width(), height()), Qt::KeepAspectRatio);
        const QRectF target((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(),
                            size.height());
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, m_image);
    }
    if (m_unavailable) {
        p.fillRect(rect(), QColor(16, 21, 19, 235));
        const QRectF titleRect(20, height() / 2. - 48, width() - 40, 52);
        const QRectF messageRect(20, height() / 2. + 9, width() - 40, 62);
        QFont f = font();
        p.setFont(fittingFont(f, tr("Preview unavailable"), titleRect, 18, 14));
        p.setPen(QColor("#dce9e1"));
        p.drawText(titleRect, Qt::AlignCenter | Qt::TextWordWrap, tr("Preview unavailable"));
        const QString message = tr("You can still remove audio and export this video.");
        p.setFont(fittingFont(f, message, messageRect, 12, 10));
        p.setPen(QColor("#b0c7ba"));
        p.drawText(messageRect, Qt::AlignCenter | Qt::TextWordWrap, message);
    }
    // The translucent badge overlays footage, so its contrast is independent of
    // the theme.
    const QString badgeText = m_removed ? tr("Audio removed") : tr("Original audio");
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    const QRect badge(13, 13, qMin(width() - 26, QFontMetrics(f).horizontalAdvance(badgeText) + 24), 25);
    p.setPen(QColor("#6d8e7b"));
    p.setBrush(QColor(17, 29, 22, 235));
    p.drawRoundedRect(badge, 4, 4);
    p.setPen(QColor("#d1efde"));
    p.drawText(badge, Qt::AlignCenter, badgeText);
}
void PreviewWidget::refreshText() {
    setAccessibleName(tr("Video preview"));
    m_importButton->setText(tr("Import a video"));
    const int textWidth = m_importButton->fontMetrics().horizontalAdvance(m_importButton->text());
    m_importButton->setFixedWidth(qMax(148, qMin(width() - 32, textWidth + 34)));
    positionImportButton();
}
void PreviewWidget::positionImportButton() {
    m_importButton->move((width() - m_importButton->width()) / 2, emptyLayout(width(), height()).buttonY);
}
void PreviewWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !m_empty)
        emit clicked();
    QWidget::mousePressEvent(event);
}
void PreviewWidget::resizeEvent(QResizeEvent *) { refreshText(); }
void PreviewWidget::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange || event->type() == QEvent::FontChange) {
        refreshText();
        update();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        update();
    }
}
