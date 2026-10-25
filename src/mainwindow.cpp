#include "mainwindow.h"
#include "appsettings.h"
#include "controller.h"
#include "previewwidget.h"
#include "theme.h"
#include "timelinewidget.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVideoSink>
#include <cmath>
#include <functional>

namespace {
QString timecode(double seconds) {
    const int value = qMax(0, int(seconds));
    return QString("%1:%2:%3")
        .arg(value / 3600, 2, 10, QChar('0'))
        .arg(value / 60 % 60, 2, 10, QChar('0'))
        .arg(value % 60, 2, 10, QChar('0'));
}
QString preciseTimecode(double seconds) {
    const qint64 milliseconds = qMax<qint64>(0, qRound64(seconds * 1000));
    return timecode(milliseconds / 1000.) +
           QString(".%1").arg(milliseconds % 1000, 3, 10, QChar('0'));
}
QString byteSize(qint64 size) {
    if (size >= 1024LL * 1024 * 1024)
        return QCoreApplication::translate("MainWindow", "%1 GB")
            .arg(QLocale().toString(double(size) / (1024 * 1024 * 1024), 'f', 2));
    if (size >= 1024 * 1024)
        return QCoreApplication::translate("MainWindow", "%1 MB")
            .arg(QLocale().toString(double(size) / (1024 * 1024), 'f', 1));
    return QCoreApplication::translate("MainWindow", "%1 KB")
        .arg(QLocale().toString(qMax<qint64>(1, size / 1024)));
}
QIcon icon(const QString &kind, QColor color = Theme::color("#b3c3b9", "#42594c")) {
    QPixmap pixmap(40, 40);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (kind == "folder") {
        QPainterPath path;
        path.moveTo(2, 6);
        path.lineTo(2, 16);
        path.quadTo(2, 17, 3, 17);
        path.lineTo(17, 17);
        path.lineTo(18, 7);
        path.lineTo(9, 7);
        path.lineTo(7, 4);
        path.lineTo(3, 4);
        path.quadTo(2, 4, 2, 6);
        p.drawPath(path);
        p.drawLine(2, 8, 17, 8);
    } else if (kind == "plus") {
        p.drawLine(10, 4, 10, 16);
        p.drawLine(4, 10, 16, 10);
    } else if (kind == "export") {
        p.drawLine(4, 15, 15, 4);
        p.drawLine(7, 4, 15, 4);
        p.drawLine(15, 4, 15, 12);
    } else if (kind == "play") {
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        QPolygonF tri;
        tri << QPointF(6, 4) << QPointF(16, 10) << QPointF(6, 16);
        p.drawPolygon(tri);
    } else if (kind == "pause") {
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(5, 4, 3, 12, 1, 1);
        p.drawRoundedRect(12, 4, 3, 12, 1, 1);
    } else if (kind == "audio" || kind == "mute") {
        QPolygonF speaker;
        speaker << QPointF(3, 8) << QPointF(6, 8) << QPointF(10, 5) << QPointF(10, 15) << QPointF(6, 12)
                << QPointF(3, 12);
        p.drawPolygon(speaker);
        if (kind == "mute") {
            p.drawLine(13, 8, 17, 12);
            p.drawLine(13, 12, 17, 8);
        } else {
            p.drawArc(7, 3, 10, 14, -60 * 16, 120 * 16);
        }
    } else if (kind == "headphones") {
        p.drawArc(3, 3, 14, 14, 0, 180 * 16);
        p.drawRoundedRect(3, 9, 3, 7, 1, 1);
        p.drawRoundedRect(14, 9, 3, 7, 1, 1);
    } else if (kind == "film") {
        p.drawRoundedRect(3, 3, 14, 14, 1, 1);
        p.drawLine(6, 3, 6, 17);
        p.drawLine(14, 3, 14, 17);
        for (int y : {7, 12}) {
            p.drawLine(3, y, 6, y);
            p.drawLine(14, y, 17, y);
        }
    } else if (kind == "keyboard") {
        p.drawRoundedRect(2, 5, 16, 11, 2, 2);
        for (int y : {8, 11})
            for (int x : {5, 8, 11, 14})
                p.drawPoint(x, y);
        p.drawLine(7, 14, 13, 14);
    } else if (kind == "shield") {
        QPainterPath path;
        path.moveTo(10, 2);
        path.lineTo(17, 5);
        path.lineTo(16, 12);
        path.quadTo(14, 16, 10, 18);
        path.quadTo(5, 16, 4, 12);
        path.lineTo(3, 5);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(6, 10, 9, 13);
        p.drawLine(9, 13, 14, 7);
    } else if (kind == "back") {
        p.drawLine(5, 4, 5, 16);
        p.drawLine(14, 5, 7, 10);
        p.drawLine(7, 10, 14, 15);
    } else if (kind == "forward") {
        p.drawLine(15, 4, 15, 16);
        p.drawLine(6, 5, 13, 10);
        p.drawLine(13, 10, 6, 15);
    } else if (kind == "close") {
        p.drawLine(5, 5, 15, 15);
        p.drawLine(15, 5, 5, 15);
    } else if (kind == "settings") {
        p.drawEllipse(QRectF(4, 4, 12, 12));
        p.drawEllipse(QRectF(8, 8, 4, 4));
        for (int angle : {0, 60, 120, 180, 240, 300}) {
            p.save();
            p.translate(10, 10);
            p.rotate(angle);
            p.drawLine(0, -6, 0, -9);
            p.restore();
        }
    } else if (kind == "trash") {
        p.drawLine(4, 5, 16, 5);
        p.drawLine(8, 3, 12, 3);
        p.drawLine(6, 7, 7, 17);
        p.drawLine(7, 17, 13, 17);
        p.drawLine(13, 17, 14, 7);
        p.drawLine(9, 8, 9, 14);
        p.drawLine(11, 8, 11, 14);
    } else if (kind == "check") {
        p.drawLine(4, 10, 8, 14);
        p.drawLine(8, 14, 16, 5);
    }
    return QIcon(pixmap);
}
QString translated(const char *source) { return QCoreApplication::translate("MainWindow", source); }
QLabel *label(const QString &text, const char *name = nullptr) {
    auto *result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    if (name)
        result->setObjectName(name);
    return result;
}
QLabel *label(const char *source, const char *name = nullptr) {
    auto *result = label(translated(source), name);
    if (*source)
        result->setProperty("translationSource", QByteArray(source));
    return result;
}
void bindText(QObject *object, const char *source, const char *property = "text") {
    object->setProperty((QByteArray("translation_") + property).constData(), QByteArray(source));
    object->setProperty(property, translated(source));
}
void refreshTranslations(QObject *root) {
    QList<QObject *> objects = root->findChildren<QObject *>();
    objects.prepend(root);
    for (QObject *object : objects) {
        const QByteArray source = object->property("translationSource").toByteArray();
        if (!source.isEmpty())
            object->setProperty("text", translated(source.constData()));
        for (const QByteArray &key : object->dynamicPropertyNames()) {
            if (key.startsWith("translation_")) {
                const QByteArray property = key.mid(12);
                object->setProperty(
                    property.constData(),
                    translated(object->property(key.constData()).toByteArray().constData()));
            }
        }
    }
}
QFrame *line() {
    auto *result = new QFrame;
    result->setObjectName("divider");
    result->setFixedHeight(1);
    return result;
}
QPushButton *button(const QString &text, const QString &glyph = {}, bool primary = false) {
    auto *result = new QPushButton(text);
    if (!glyph.isEmpty()) {
        result->setProperty("iconGlyph", glyph);
        result->setIcon(icon(glyph, primary ? Theme::color("#153e2b", "#ffffff")
                                            : Theme::color("#aec3b6", "#42594c")));
    }
    result->setIconSize(QSize(17, 17));
    if (primary)
        result->setProperty("primary", true);
    return result;
}
QPushButton *button(const char *source, const QString &glyph = {}, bool primary = false) {
    auto *result = button(translated(source), glyph, primary);
    if (*source)
        result->setProperty("translationSource", QByteArray(source));
    return result;
}
QWidget *panel(const char *name) {
    auto *result = new QFrame;
    result->setObjectName(name);
    return result;
}
class AudioSwitch final : public QAbstractButton {
  public:
    explicit AudioSwitch(QWidget *parent = nullptr) : QAbstractButton(parent) {
        setCheckable(true);
        setFixedSize(30, 18);
        setCursor(Qt::PointingHandCursor);
        bindText(this, QT_TRANSLATE_NOOP("MainWindow", "Remove audio"), "accessibleName");
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(isEnabled() ? 1 : .35);
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? Theme::color("#7bedc6", "#087a58")
                               : Theme::color("#58635c", "#b5c6bc"));
        p.drawRoundedRect(rect(), 9, 9);
        p.setBrush(isChecked() ? Theme::color("#254c35", "#ffffff")
                               : Theme::color("#d0d9d3", "#ffffff"));
        p.drawEllipse(isChecked() ? 14 : 2, 2, 14, 14);
        if (hasFocus()) {
            p.setPen(QPen(Theme::color("#c7f8dc", "#07583e"), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 9, 9);
        }
    }
};
class MediaDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return QSize(180, 151);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        QRect imageRect = option.rect.adjusted(2, 6, -2, -42);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        p->setPen(
            QPen(selected ? Theme::color("#83dcb1", "#087a58") : Theme::color("#414d45", "#c4d2c8"),
                 selected ? 2 : 1));
        p->setBrush(Theme::color("#161d19", "#edf3ef"));
        p->drawRoundedRect(imageRect, 5, 5);
        QPixmap image = qvariant_cast<QPixmap>(index.data(Qt::DecorationRole));
        if (!image.isNull()) {
            p->save();
            QPainterPath clip;
            clip.addRoundedRect(imageRect.adjusted(2, 2, -2, -2), 3, 3);
            p->setClipPath(clip);
            QSize size = image.size();
            size.scale(imageRect.size(), Qt::KeepAspectRatioByExpanding);
            p->drawPixmap(QRect(imageRect.center() - QPoint(size.width() / 2, size.height() / 2), size),
                          image);
            p->restore();
        }
        QFont f = option.font;
        f.setPixelSize(10);
        p->setFont(f);
        QString duration = index.data(Qt::UserRole + 1).toString();
        QRect durationRect(imageRect.right() - 47, imageRect.bottom() - 23, 41, 17);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(12, 24, 16, 210));
        p->drawRoundedRect(durationRect, 3, 3);
        p->setPen(QColor("#e2f0e7"));
        p->drawText(durationRect, Qt::AlignCenter, duration);
        if (selected) {
            p->setBrush(QColor("#82dfb2"));
            p->setPen(Qt::NoPen);
            p->drawEllipse(imageRect.right() - 21, imageRect.top() + 7, 14, 14);
            p->setPen(QPen(QColor("#214731"), 1.4));
            p->drawLine(imageRect.right() - 18, imageRect.top() + 14, imageRect.right() - 15,
                        imageRect.top() + 17);
            p->drawLine(imageRect.right() - 15, imageRect.top() + 17, imageRect.right() - 10,
                        imageRect.top() + 11);
        }
        p->setPen(Theme::color("#c3d0c7", "#243f30"));
        f.setPixelSize(11);
        p->setFont(f);
        p->drawText(option.rect.adjusted(3, option.rect.height() - 34, -3, -17), Qt::AlignVCenter,
                    QFontMetrics(f).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                               option.rect.width() - 6));
        p->setPen(Theme::color("#7b8c81", "#526b5c"));
        f.setPixelSize(9);
        p->setFont(f);
        p->drawText(option.rect.adjusted(3, option.rect.height() - 17, -3, 0), Qt::AlignVCenter,
                    index.data(Qt::UserRole + 2).toString());
        p->restore();
    }
};
const char *editorStyleSheet = R"QSS(
QMainWindow,QDialog{background:#171b19;color:#e4ece6;}
QWidget{font-family:"Inter","SF Pro Text","Segoe UI","DejaVu Sans";font-size:12px;color:#d6e2d9;}
QMenuBar{background:#232825;color:#c6d9cb;}QMenuBar::item:selected{background:#3b5042;}QMenu{background:#252f28;color:#d2e5d8;border:1px solid #526957;}QMenu::item{padding:7px 26px 7px 15px;}QMenu::item:selected{background:#3d5847;}QMenu::item:disabled{color:#647d6b;}
QToolTip{background:#29392f;color:#d6ebde;border:1px solid #5c7865;padding:5px;}
QFrame#topbar{background:#232825;border-bottom:1px solid #364139;}
QFrame#mediaPanel,QFrame#inspectorPanel{background:#202522;}
QFrame#previewPanel{background:#181e1a;}
QFrame#rail{background:#1b211d;border-right:1px solid #2a352d;}
QFrame#timelinePanel{background:#1c231f;}
QFrame#timelineToolbar{background:#242c26;border-bottom:1px solid #3a463d;}
QFrame#statusbar{background:#242b26;border-top:1px solid #364139;}
QLabel{background:transparent;}
QLabel#brand{font-size:22px;font-weight:700;color:#dcefe3;}
QLabel#muted{color:#829388;font-size:11px;}
QLabel#subtle,QLabel#versionLabel{color:#657d6d;font-size:10px;}
QLabel#eyebrow{color:#759d83;font-size:9px;}
QLabel#sectionTitle{font-size:12px;font-weight:600;color:#c8dace;}
QLabel#projectTitle{color:#a5b9ac;font-size:11px;}
QLabel#statusText{color:#8da895;font-size:10px;}
QLabel#timecode{color:#9eb8a8;font-size:10px;}
QLabel#value{font-size:11px;color:#c3d9cb;}
QPushButton{background:#303d34;border:1px solid #4b5c50;border-radius:5px;min-height:32px;padding:0 12px;color:#cddfd3;font-size:11px;}
QPushButton:hover{background:#3d5144;border-color:#728f7c;}
QPushButton:pressed{background:#4b6353;}
QPushButton:disabled{color:#627769;background:#28342c;border-color:#35463b;}
QPushButton[primary="true"]{background:#81e9bc;color:#143c28;border:1px solid #81e9bc;font-weight:600;}
QPushButton[primary="true"]:hover{background:#a2f3d0;border-color:#a2f3d0;}
QPushButton[primary="true"]:disabled{background:#3a6450;border-color:#3a6450;color:#79998a;}
QPushButton[iconButton="true"]{padding:0;min-height:29px;border:0;background:transparent;}
QPushButton[iconButton="true"]:hover{background:#34483a;}
QPushButton[textButton="true"]{background:transparent;border:0;padding:0;min-height:22px;color:#b4d7c1;text-align:left;font-size:10px;}
QPushButton[textButton="true"]:hover{color:#90edbd;}
QPushButton:focus{border-color:#a2e6bd;}
QToolButton{background:transparent;border:0;border-radius:5px;color:#9fb6a7;font-size:10px;padding:9px 4px;}
QToolButton:hover{background:#2f4034;}
QToolButton:checked{background:#2d4134;color:#9de6bd;}
QListWidget{background:transparent;border:0;outline:0;padding:0;}
QListWidget::item{background:transparent;border:0;}
QListWidget::item:selected{background:transparent;}
QTabWidget::pane{border:0;border-top:1px solid #36443a;background:#202522;}
QTabBar::tab{background:transparent;padding:16px 10px 13px;color:#789183;border-bottom:2px solid transparent;}
QTabBar::tab:selected{color:#d6e8dc;border-bottom:2px solid #8ce9bb;}
QScrollArea{background:transparent;border:0;}
QScrollArea>QWidget>QWidget{background:transparent;}
QFrame#divider{background:#34473b;}
QFrame#audioCard{background:#2c3b31;border:1px solid #506d59;border-radius:6px;}
QFrame#qualityCard{background:#28372d;border:1px solid #415a49;border-radius:5px;}
QFrame#busyBanner{background:#283b2e;border-bottom:1px solid #536d5b;}
QFrame#noticeBanner{background:#453832;border-bottom:1px solid #705c4e;}
QProgressBar{border:0;background:#455d4e;border-radius:3px;height:5px;max-height:5px;text-align:center;}
QProgressBar::chunk{background:#9af0c1;border-radius:3px;}
QSlider::groove:horizontal{height:3px;background:#435c4c;border-radius:1px;}
QSlider::sub-page:horizontal{background:#92dcaf;}
QSlider::handle:horizontal{width:9px;height:9px;margin:-3px 0;background:#b4e9c8;border-radius:4px;}
QSlider::handle:horizontal:disabled{background:#597863;}
QSplitter::handle{background:#101a14;}
QComboBox{background:#303d34;color:#d6e2d9;border:1px solid #526957;border-radius:5px;padding:7px 28px 7px 10px;min-height:22px;}QComboBox QAbstractItemView{background:#252f28;color:#d6e2d9;selection-background-color:#3d5847;selection-color:#d6e2d9;}
QDoubleSpinBox{background:#18271e;color:#d6e2d9;border:1px solid #526957;border-radius:5px;padding:7px 9px;min-height:24px;font-size:13px;selection-background-color:#54745d;}
QDoubleSpinBox:focus{border-color:#9be8ba;}QDoubleSpinBox:disabled{color:#627769;background:#28342c;border-color:#35463b;}
QLabel#trimDuration{font-size:25px;font-weight:600;color:#b4edcb;}
QScrollBar:vertical{width:5px;background:transparent;}QScrollBar::handle:vertical{background:#526b59;border-radius:2px;min-height:20px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}
)QSS";

const char *lightStyleSheet = R"QSS(
QMainWindow,QDialog{background:#f4f7f5;color:#20382a;}
QWidget{color:#20382a;}
QMenuBar{background:#edf3ef;color:#2a4434;}QMenuBar::item:selected{background:#d5e6db;}QMenu{background:#ffffff;color:#20382a;border:1px solid #b9cfc0;}QMenu::item:selected{background:#d7ede1;}QMenu::item:disabled{color:#8a9e90;}
QToolTip{background:#ffffff;color:#20382a;border:1px solid #afc8b9;}
QFrame#topbar{background:#ffffff;border-bottom:1px solid #c9d8ce;}
QFrame#mediaPanel,QFrame#inspectorPanel{background:#f8faf8;}
QFrame#previewPanel{background:#eef3ef;}
QFrame#rail{background:#e8f0eb;border-right:1px solid #cad9d0;}
QFrame#timelinePanel{background:#f4f8f5;}
QFrame#timelineToolbar{background:#eaf2ed;border-bottom:1px solid #c4d7ca;}
QFrame#statusbar{background:#eaf2ed;border-top:1px solid #c4d7ca;}
QLabel#brand{color:#173c29;}QLabel#muted{color:#526b5b;}QLabel#subtle,QLabel#versionLabel{color:#586e61;}QLabel#eyebrow{color:#48765c;}
QLabel#sectionTitle{color:#274f38;}QLabel#projectTitle{color:#4e6c59;}QLabel#statusText{color:#456b53;}QLabel#timecode{color:#3f6650;}QLabel#value{color:#244931;}
QPushButton{background:#ffffff;border-color:#b6cdbd;color:#294e37;}
QPushButton:hover{background:#e5f3e9;border-color:#75a88b;}QPushButton:pressed{background:#cce5d5;}
QPushButton:disabled{color:#8b9c91;background:#eaf0ec;border-color:#d3dfd7;}
QPushButton[primary="true"]{background:#087a58;color:#ffffff;border-color:#087a58;}
QPushButton[primary="true"]:hover{background:#096247;border-color:#096247;}
QPushButton[primary="true"]:disabled{background:#c8dcd1;border-color:#c8dcd1;color:#6e8c7d;}
QPushButton[iconButton="true"]{background:transparent;border:0;}QPushButton[iconButton="true"]:hover{background:#d5e8dc;}
QPushButton[textButton="true"]{color:#216d46;background:transparent;border:0;}QPushButton[textButton="true"]:hover{color:#064d32;}
QPushButton:focus{border-color:#087a58;}
QToolButton{color:#416e53;}QToolButton:hover{background:#d6e8dc;}QToolButton:checked{background:#c6e3d1;color:#075d3b;}
QTabWidget::pane{border-top:1px solid #c5d7cc;background:#f8faf8;}QTabBar::tab{color:#5b7765;}QTabBar::tab:selected{color:#205339;border-bottom-color:#087a58;}
QFrame#divider{background:#c9d9ce;}QFrame#audioCard{background:#e3f3e9;border-color:#a8cbb6;}
QFrame#qualityCard{background:#eaf5ed;border-color:#c0d9c9;}QFrame#busyBanner{background:#e1f2e7;border-bottom-color:#9dc6ad;}
QFrame#noticeBanner{background:#fff0df;border-bottom-color:#d6b897;}
QProgressBar{background:#c7ddcf;}QProgressBar::chunk{background:#087a58;}
QSlider::groove:horizontal{background:#c3d8cb;}QSlider::sub-page:horizontal{background:#168a61;}QSlider::handle:horizontal{background:#087a58;}QSlider::handle:horizontal:disabled{background:#9ebca9;}
QSplitter::handle{background:#c9d8cf;}QScrollBar::handle:vertical{background:#93b39f;}
QComboBox{background:#ffffff;color:#20382a;border:1px solid #adc7b7;border-radius:5px;padding:7px 28px 7px 10px;min-height:22px;}
QComboBox QAbstractItemView{background:#ffffff;color:#20382a;selection-background-color:#d7ede1;selection-color:#173c29;}
QDoubleSpinBox{background:#ffffff;color:#20382a;border-color:#adc7b7;selection-background-color:#c5e2cf;}
QDoubleSpinBox:focus{border-color:#087a58;}QDoubleSpinBox:disabled{color:#8b9c91;background:#eaf0ec;border-color:#d3dfd7;}
QLabel#trimDuration{color:#126343;}
)QSS";
} // namespace

MainWindow::MainWindow(MediaController *controller, QWidget *parent)
    : QMainWindow(parent), m_controller(controller), m_player(new QMediaPlayer(this)),
      m_audio(new QAudioOutput(this)), m_sink(new QVideoSink(this)) {
    setWindowTitle("Framecut");
    setMinimumSize(1100, 720);
    resize(1440, 900);
    setAcceptDrops(true);
    setStyleSheet(QString::fromUtf8(editorStyleSheet));
    m_player->setObjectName("mediaPlayer");
    m_audio->setObjectName("audioOutput");
    const QAudioDevice initialOutput = QMediaDevices::defaultAudioOutput();
    // Qt 6.4's FFmpeg audio renderer can dereference a null sink without a
    // device. Retain the mute state while leaving the player without an audio
    // sink.
    if (!initialOutput.isNull()) {
        m_audio->setDevice(initialOutput);
        m_player->setAudioOutput(m_audio);
    }
    m_player->setVideoSink(m_sink);
    m_audio->setVolume(.8);
    m_audio->setMuted(true);
    auto *root = new QWidget;
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setCentralWidget(root);
    auto *top = new QFrame;
    top->setObjectName("topbar");
    top->setFixedHeight(58);
    auto *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(21, 0, 20, 0);
    topLayout->setSpacing(13);
    auto *mark = new QLabel;
    mark->setPixmap(
        QPixmap(":/assets/icon.png").scaled(29, 29, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    mark->setFixedSize(30, 30);
    topLayout->addWidget(mark);
    topLayout->addWidget(label(QStringLiteral("framecut."), "brand"));
    topLayout->addSpacing(20);
    topLayout->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Workspace  /"), "subtle"));
    m_projectTitle = label(QT_TRANSLATE_NOOP("MainWindow", "Untitled project"), "projectTitle");
    m_projectTitle->setMinimumWidth(0);
    m_projectTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    topLayout->addWidget(m_projectTitle, 1);
    topLayout->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "●  All local. All yours."), "muted"));
    topLayout->addSpacing(12);
    auto *settingsButton = button(QT_TRANSLATE_NOOP("MainWindow", "Settings"), "settings");
    settingsButton->setObjectName("settingsButton");
    bindText(settingsButton, QT_TRANSLATE_NOOP("MainWindow", "Appearance and language"), "toolTip");
    topLayout->addWidget(settingsButton);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::showSettings);
    m_export = button(QT_TRANSLATE_NOOP("MainWindow", "Export"), "export", true);
    m_export->setObjectName("exportButton");
    m_export->setMinimumWidth(106);
    topLayout->addWidget(m_export);
    rootLayout->addWidget(top);
    m_busyBanner = new QFrame;
    m_busyBanner->setObjectName("busyBanner");
    auto *busyLayout = new QHBoxLayout(m_busyBanner);
    busyLayout->setContentsMargins(20, 9, 15, 9);
    m_busyMessage = label(QT_TRANSLATE_NOOP("MainWindow", "Preparing your video…"));
    busyLayout->addWidget(m_busyMessage);
    m_progress = new QProgressBar;
    m_progress->setObjectName("progressBar");
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    busyLayout->addWidget(m_progress, 1);
    m_cancel = button(QT_TRANSLATE_NOOP("MainWindow", "Cancel"));
    m_cancel->setObjectName("cancelButton");
    busyLayout->addWidget(m_cancel);
    rootLayout->addWidget(m_busyBanner);
    m_busyBanner->hide();
    m_notice = new QFrame;
    m_notice->setObjectName("noticeBanner");
    auto *noticeLayout = new QHBoxLayout(m_notice);
    noticeLayout->setContentsMargins(20, 9, 15, 9);
    m_noticeText = label("");
    m_noticeText->setWordWrap(true);
    noticeLayout->addWidget(m_noticeText, 1);
    auto *closeNotice = button("", "close");
    closeNotice->setProperty("iconButton", true);
    closeNotice->setFixedWidth(27);
    bindText(closeNotice, QT_TRANSLATE_NOOP("MainWindow", "Dismiss notification"), "accessibleName");
    noticeLayout->addWidget(closeNotice);
    connect(closeNotice, &QPushButton::clicked, m_notice, &QWidget::hide);
    rootLayout->addWidget(m_notice);
    m_notice->hide();
    auto *vertical = new QSplitter(Qt::Vertical);
    vertical->setChildrenCollapsible(false);
    vertical->setHandleWidth(2);
    rootLayout->addWidget(vertical, 1);
    auto *upper = new QWidget;
    auto *upperLayout = new QHBoxLayout(upper);
    upperLayout->setContentsMargins(0, 0, 0, 0);
    upperLayout->setSpacing(0);
    auto *rail = panel("rail");
    rail->setFixedWidth(76);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(5, 14, 5, 8);
    railLayout->setSpacing(7);
    auto railButton = [&](const char *text, const QString &glyph) {
        auto *b = new QToolButton;
        bindText(b, text);
        b->setProperty("iconGlyph", glyph);
        b->setIcon(icon(glyph));
        b->setIconSize(QSize(21, 21));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setFixedSize(66, 64);
        railLayout->addWidget(b);
        return b;
    };
    auto *mediaRail = railButton(QT_TRANSLATE_NOOP("MainWindow", "Media"), "folder");
    mediaRail->setCheckable(true);
    mediaRail->setChecked(true);
    auto *audioRail = railButton(QT_TRANSLATE_NOOP("MainWindow", "Audio"), "headphones");
    railLayout->addStretch();
    auto *shortcutsRail = railButton(QT_TRANSLATE_NOOP("MainWindow", "Keys"), "keyboard");
    bindText(shortcutsRail, QT_TRANSLATE_NOOP("MainWindow", "Keyboard shortcuts"), "toolTip");
    bindText(shortcutsRail, QT_TRANSLATE_NOOP("MainWindow", "Keyboard shortcuts"), "accessibleName");
    upperLayout->addWidget(rail);
    auto *horizontal = new QSplitter(Qt::Horizontal);
    horizontal->setChildrenCollapsible(false);
    horizontal->setHandleWidth(2);
    upperLayout->addWidget(horizontal, 1);
    auto *mediaPanel = panel("mediaPanel");
    mediaPanel->setMinimumWidth(195);
    auto *mediaLayout = new QVBoxLayout(mediaPanel);
    mediaLayout->setContentsMargins(15, 17, 15, 12);
    mediaLayout->setSpacing(13);
    auto *mediaHeading = new QHBoxLayout;
    mediaHeading->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Media"), "sectionTitle"));
    mediaHeading->addStretch();
    mediaHeading->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "LOCAL"), "eyebrow"));
    mediaLayout->addLayout(mediaHeading);
    auto *libraryHeading = new QHBoxLayout;
    libraryHeading->setContentsMargins(0, 9, 0, 0);
    libraryHeading->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Project media"), "muted"));
    m_count = label(QStringLiteral("0"), "subtle");
    libraryHeading->addWidget(m_count);
    libraryHeading->addStretch();
    mediaLayout->addLayout(libraryHeading);
    m_import = button(QT_TRANSLATE_NOOP("MainWindow", "Import media"), "plus");
    m_import->setObjectName("importButton");
    mediaLayout->addWidget(m_import);
    m_remove = button(QT_TRANSLATE_NOOP("MainWindow", "Remove from project"), "trash");
    m_remove->setObjectName("removeMediaButton");
    bindText(m_remove,
             QT_TRANSLATE_NOOP("MainWindow", "Remove from project (original file stays on disk)"),
             "toolTip");
    mediaLayout->addWidget(m_remove);
    connect(m_remove, &QPushButton::clicked, this, &MainWindow::removeSelectedMedia);
    m_library = new QListWidget;
    m_library->setObjectName("mediaList");
    m_library->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_library, &QListWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        if (auto *item = m_library->itemAt(point))
            m_library->setCurrentItem(item);
        QMenu menu(this);
        auto *remove = menu.addAction(tr("Remove from project"));
        remove->setEnabled(m_library->currentItem() && !m_controller->busy());
        connect(remove, &QAction::triggered, this, &MainWindow::removeSelectedMedia);
        menu.exec(m_library->viewport()->mapToGlobal(point));
    });
    for (const auto key : {Qt::Key_Delete, Qt::Key_Backspace}) {
        auto *shortcut = new QShortcut(QKeySequence(key), m_library);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, &MainWindow::removeSelectedMedia);
    }
    m_library->setItemDelegate(new MediaDelegate(m_library));
    m_library->setSpacing(4);
    m_library->setSelectionMode(QAbstractItemView::SingleSelection);
    m_library->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mediaLayout->addWidget(m_library, 1);
    m_library->hide();
    m_emptyLibrary = new QWidget;
    auto *emptyLibraryLayout = new QVBoxLayout(m_emptyLibrary);
    emptyLibraryLayout->setContentsMargins(0, 0, 0, 0);
    emptyLibraryLayout->setSpacing(11);
    emptyLibraryLayout->addStretch();
    auto *fileSymbol = label("");
    fileSymbol->setPixmap(icon("film", Theme::color("#718d7b", "#607f6a")).pixmap(42, 42));
    fileSymbol->setProperty("pixmapGlyph", "film");
    fileSymbol->setProperty("pixmapSize", 42);
    fileSymbol->setAlignment(Qt::AlignCenter);
    emptyLibraryLayout->addWidget(fileSymbol);
    auto *cleanSlate = label(QT_TRANSLATE_NOOP("MainWindow", "A clean slate."));
    cleanSlate->setAlignment(Qt::AlignCenter);
    emptyLibraryLayout->addWidget(cleanSlate);
    auto *emptyHint =
        label(QT_TRANSLATE_NOOP("MainWindow",
                                "Add a video to your workspace.\nEverything stays on your device."),
              "muted");
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setWordWrap(true);
    emptyLibraryLayout->addWidget(emptyHint);
    emptyLibraryLayout->addStretch();
    mediaLayout->addWidget(m_emptyLibrary, 1);
    mediaLayout->addWidget(line());
    auto *sampleHeading = label(QT_TRANSLATE_NOOP("MainWindow", "Just looking around?"), "muted");
    mediaLayout->addWidget(sampleHeading);
    m_sample = button(QT_TRANSLATE_NOOP("MainWindow", "Try a sample video  ↗"));
    m_sample->setObjectName("sampleButton");
    m_sample->setProperty("textButton", true);
    mediaLayout->addWidget(m_sample);
    auto *formats = label(QT_TRANSLATE_NOOP("MainWindow", "MP4, MOV, MKV, WEBM & more"), "subtle");
    formats->setAlignment(Qt::AlignCenter);
    mediaLayout->addWidget(line());
    mediaLayout->addWidget(formats);
    horizontal->addWidget(mediaPanel);
    auto *previewPanel = panel("previewPanel");
    previewPanel->setMinimumWidth(345);
    auto *previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(17, 15, 17, 10);
    previewLayout->setSpacing(12);
    auto *playerHeading = new QHBoxLayout;
    playerHeading->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Player"), "sectionTitle"));
    playerHeading->addStretch();
    m_previewInfo = label(QT_TRANSLATE_NOOP("MainWindow", "Original quality"), "subtle");
    playerHeading->addWidget(m_previewInfo);
    previewLayout->addLayout(playerHeading);
    m_preview = new PreviewWidget;
    previewLayout->addWidget(m_preview, 1);
    m_seek = new QSlider(Qt::Horizontal);
    m_seek->setObjectName("seekSlider");
    bindText(m_seek, QT_TRANSLATE_NOOP("MainWindow", "Seek video"), "accessibleName");
    m_seek->setRange(0, 10000);
    m_seek->setFixedHeight(13);
    previewLayout->addWidget(m_seek);
    auto *playback = new QHBoxLayout;
    playback->setSpacing(6);
    m_time = label(QStringLiteral("00:00:00  /  00:00:00"), "timecode");
    m_time->setMinimumWidth(145);
    playback->addWidget(m_time);
    playback->addStretch();
    auto transport = [&](const QString &glyph, const QString &name, const char *tip) {
        auto *b = button("", glyph);
        b->setObjectName(name);
        b->setProperty("iconButton", true);
        b->setFixedSize(29, 29);
        bindText(b, tip, "toolTip");
        bindText(b, tip, "accessibleName");
        playback->addWidget(b);
        m_playbackButtons.append(b);
        return b;
    };
    auto *start = transport("back", "startButton", QT_TRANSLATE_NOOP("MainWindow", "Go to start"));
    m_play = transport("play", "playButton", QT_TRANSLATE_NOOP("MainWindow", "Play or pause (Space)"));
    auto *end = transport("forward", "endButton", QT_TRANSLATE_NOOP("MainWindow", "Go to end"));
    playback->addStretch();
    playback->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Fit"), "subtle"));
    previewLayout->addLayout(playback);
    horizontal->addWidget(previewPanel);
    auto *inspectorPanel = panel("inspectorPanel");
    inspectorPanel->setMinimumWidth(250);
    auto *inspectorLayout = new QVBoxLayout(inspectorPanel);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(0);
    m_inspector = new QTabWidget;
    inspectorLayout->addWidget(m_inspector, 1);
    auto *audioTab = new QWidget;
    auto *audioLayout = new QVBoxLayout(audioTab);
    audioLayout->setContentsMargins(17, 22, 17, 12);
    audioLayout->setSpacing(15);
    audioLayout->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Audio settings"), "sectionTitle"));
    auto *audioCard = new QFrame;
    audioCard->setObjectName("audioCard");
    auto *cardLayout = new QVBoxLayout(audioCard);
    cardLayout->setContentsMargins(12, 14, 12, 12);
    cardLayout->setSpacing(13);
    auto *toggleRow = new QHBoxLayout;
    auto *muteIcon = label("");
    muteIcon->setPixmap(icon("mute", Theme::color("#93dcaf", "#087a58")).pixmap(19, 19));
    muteIcon->setProperty("pixmapGlyph", "mute");
    muteIcon->setProperty("pixmapSize", 19);
    toggleRow->addWidget(muteIcon);
    toggleRow->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Remove audio")), 1);
    m_audioToggle = new AudioSwitch;
    m_audioToggle->setObjectName("removeAudioToggle");
    m_audioToggle->setChecked(true);
    toggleRow->addWidget(m_audioToggle);
    cardLayout->addLayout(toggleRow);
    m_audioDescription =
        label(QT_TRANSLATE_NOOP("MainWindow", "Keep the picture. Lose the noise."), "muted");
    m_audioDescription->setWordWrap(true);
    cardLayout->addWidget(m_audioDescription);
    cardLayout->addWidget(line());
    m_audioState = label(QT_TRANSLATE_NOOP("MainWindow", "Silent preview & export"), "subtle");
    m_audioState->setWordWrap(true);
    cardLayout->addWidget(m_audioState);
    audioLayout->addWidget(audioCard);
    auto *listenHint = label(
        QT_TRANSLATE_NOOP("MainWindow", "Turn this off to listen to the original audio."), "subtle");
    listenHint->setWordWrap(true);
    audioLayout->addWidget(listenHint);
    audioLayout->addSpacing(3);
    audioLayout->addWidget(line());
    audioLayout->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Export settings"), "sectionTitle"));
    auto properties = [&](QVBoxLayout *layout, const QList<QPair<QString, const char *>> &rows) {
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(13);
        int row = 0;
        for (const auto &entry : rows) {
            grid->addWidget(label(entry.second, "muted"), row, 0);
            auto *value = label(QStringLiteral("—"), "value");
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            // Selectable labels use QTextControl, whose text height can exceed
            // QLabel's cached hint after a stylesheet or locale change.
            value->setMinimumHeight(22);
            m_properties[entry.first] = value;
            grid->addWidget(value, row, 1);
            ++row;
        }
        grid->setColumnStretch(1, 1);
        layout->addLayout(grid);
    };
    properties(audioLayout, {{"resolution", QT_TRANSLATE_NOOP("MainWindow", "Resolution")},
                             {"fps", QT_TRANSLATE_NOOP("MainWindow", "Frame rate")},
                             {"format", QT_TRANSLATE_NOOP("MainWindow", "Format")},
                             {"quality", QT_TRANSLATE_NOOP("MainWindow", "Quality")},
                             {"audio", QT_TRANSLATE_NOOP("MainWindow", "Audio")}});
    audioLayout->addSpacing(6);
    auto *quality = new QFrame;
    quality->setObjectName("qualityCard");
    auto *qualityLayout = new QHBoxLayout(quality);
    qualityLayout->setContentsMargins(11, 12, 11, 12);
    auto *shield = label("");
    shield->setPixmap(icon("shield", Theme::color("#95caa7", "#087a58")).pixmap(20, 20));
    shield->setProperty("pixmapGlyph", "shield");
    shield->setProperty("pixmapSize", 20);
    qualityLayout->addWidget(shield);
    auto *qualityText = new QVBoxLayout;
    qualityText->setSpacing(5);
    m_qualityHeading = label("");
    m_qualityHeading->setWordWrap(true);
    qualityText->addWidget(m_qualityHeading);
    m_qualityDescription = label("", "subtle");
    m_qualityDescription->setWordWrap(true);
    qualityText->addWidget(m_qualityDescription);
    qualityLayout->addLayout(qualityText);
    audioLayout->addWidget(quality);
    audioLayout->addStretch();
    auto *audioScroll = new QScrollArea;
    audioScroll->setWidgetResizable(true);
    audioScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    audioScroll->setWidget(audioTab);
    m_inspector->addTab(audioScroll, tr("Audio"));
    auto *detailsTab = new QWidget;
    auto *detailsLayout = new QVBoxLayout(detailsTab);
    detailsLayout->setContentsMargins(17, 22, 17, 12);
    detailsLayout->setSpacing(19);
    detailsLayout->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Source video"), "sectionTitle"));
    m_properties["name"] = label(QT_TRANSLATE_NOOP("MainWindow", "No video selected"), "muted");
    m_properties["name"]->setWordWrap(true);
    detailsLayout->addWidget(m_properties["name"]);
    detailsLayout->addWidget(line());
    properties(detailsLayout, {{"duration", QT_TRANSLATE_NOOP("MainWindow", "Duration")},
                               {"detailsResolution", QT_TRANSLATE_NOOP("MainWindow", "Resolution")},
                               {"detailsFps", QT_TRANSLATE_NOOP("MainWindow", "Frame rate")},
                               {"codec", QT_TRANSLATE_NOOP("MainWindow", "Video codec")},
                               {"size", QT_TRANSLATE_NOOP("MainWindow", "File size")},
                               {"tracks", QT_TRANSLATE_NOOP("MainWindow", "Audio tracks")}});
    detailsLayout->addWidget(line());
    auto *sourceHint =
        label(QT_TRANSLATE_NOOP("MainWindow",
                                "Your original stays untouched. Exports are saved as a new video."),
              "muted");
    sourceHint->setWordWrap(true);
    detailsLayout->addWidget(sourceHint);
    detailsLayout->addStretch();
    auto *detailsScroll = new QScrollArea;
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailsScroll->setWidget(detailsTab);
    m_inspector->addTab(detailsScroll, tr("Details"));
    auto *trimTab = new QWidget;
    trimTab->setObjectName("trimTab");
    auto *trimLayout = new QVBoxLayout(trimTab);
    trimLayout->setContentsMargins(17, 22, 17, 12);
    trimLayout->setSpacing(14);
    trimLayout->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Trim your clip"), "sectionTitle"));
    auto *trimHint = label(QT_TRANSLATE_NOOP("MainWindow", "Drag either edge of the timeline clip, or enter exact source times below."), "muted");
    trimHint->setWordWrap(true);
    trimLayout->addWidget(trimHint);
    auto trimField = [&](const char *caption, const char *name) {
        auto *fieldLabel = label(caption, "muted");
        trimLayout->addWidget(fieldLabel);
        auto *field = new QDoubleSpinBox;
        field->setObjectName(name);
        field->setDecimals(3);
        field->setRange(0, 0);
        field->setKeyboardTracking(false);
        // NoButtons can collapse the styled editor to one pixel on Qt/macOS.
        // Retaining the arrows also gives keyboard/mouse frame-step controls.
        field->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        if (auto *editor = field->findChild<QLineEdit *>())
            editor->setTextMargins(8, 0, 0, 0);
        field->setAlignment(Qt::AlignLeft);
        bindText(field, caption, "accessibleName");
        fieldLabel->setBuddy(field);
        trimLayout->addWidget(field);
        return field;
    };
    m_trimStart = trimField(QT_TRANSLATE_NOOP("MainWindow", "Start"), "trimStartSpin");
    m_trimEnd = trimField(QT_TRANSLATE_NOOP("MainWindow", "End"), "trimEndSpin");
    trimLayout->addWidget(line());
    trimLayout->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Selected duration"), "muted"));
    m_trimDuration = label("", "trimDuration");
    m_trimDuration->setAccessibleName(tr("Selected duration"));
    trimLayout->addWidget(m_trimDuration);
    m_resetTrim = button(QT_TRANSLATE_NOOP("MainWindow", "Reset trim"));
    m_resetTrim->setObjectName("resetTrimButton");
    trimLayout->addWidget(m_resetTrim);
    auto *restoreHint = label(QT_TRANSLATE_NOOP("MainWindow", "Trimmed footage stays available. Drag the edges outward or reset to restore it."), "subtle");
    restoreHint->setWordWrap(true);
    trimLayout->addWidget(restoreHint);
    trimLayout->addWidget(line());
    m_trimExportHint = label("", "subtle");
    m_trimExportHint->setWordWrap(true);
    trimLayout->addWidget(m_trimExportHint);
    trimLayout->addStretch();
    auto *trimScroll = new QScrollArea;
    trimScroll->setWidgetResizable(true);
    trimScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    trimScroll->setWidget(trimTab);
    m_inspector->addTab(trimScroll, tr("Trim"));
    auto *privacy =
        label(QT_TRANSLATE_NOOP("MainWindow", "●   Private by design. No uploads."), "subtle");
    privacy->setWordWrap(true);
    privacy->setContentsMargins(17, 12, 17, 15);
    inspectorLayout->addWidget(privacy);
    horizontal->addWidget(inspectorPanel);
    horizontal->setSizes({240, 800, 280});
    horizontal->setStretchFactor(0, 0);
    horizontal->setStretchFactor(1, 1);
    horizontal->setStretchFactor(2, 0);
    vertical->addWidget(upper);
    auto *timelinePanel = panel("timelinePanel");
    auto *timelineLayout = new QVBoxLayout(timelinePanel);
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->setSpacing(0);
    auto *timelineToolbar = new QFrame;
    timelineToolbar->setObjectName("timelineToolbar");
    timelineToolbar->setFixedHeight(45);
    auto *timelineToolbarLayout = new QHBoxLayout(timelineToolbar);
    timelineToolbarLayout->setContentsMargins(20, 0, 20, 0);
    timelineToolbarLayout->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Timeline"), "sectionTitle"));
    timelineToolbarLayout->addSpacing(16);
    m_timelineInfo = label(QT_TRANSLATE_NOOP("MainWindow", "Your story starts here"), "subtle");
    timelineToolbarLayout->addWidget(m_timelineInfo);
    timelineToolbarLayout->addStretch();
    m_trimButton = button(QT_TRANSLATE_NOOP("MainWindow", "Trim"));
    m_trimButton->setObjectName("trimButton");
    bindText(m_trimButton, QT_TRANSLATE_NOOP("MainWindow", "Drag clip edges or enter exact start and end times"), "toolTip");
    timelineToolbarLayout->addWidget(m_trimButton);
    timelineToolbarLayout->addSpacing(12);
    m_audioAction = button(QT_TRANSLATE_NOOP("MainWindow", "Audio removed"), "mute");
    m_audioAction->setObjectName("timelineAudioButton");
    m_audioAction->setProperty("textButton", true);
    timelineToolbarLayout->addWidget(m_audioAction);
    timelineLayout->addWidget(timelineToolbar);
    m_timeline = new TimelineWidget;
    timelineLayout->addWidget(m_timeline, 1);
    vertical->addWidget(timelinePanel);
    vertical->setSizes({600, 230});
    vertical->setStretchFactor(0, 1);
    vertical->setStretchFactor(1, 0);
    auto *status = new QFrame;
    status->setObjectName("statusbar");
    status->setFixedHeight(28);
    auto *statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(18, 0, 18, 0);
    m_status = label(QT_TRANSLATE_NOOP("MainWindow", "●  Ready   ·   0 assets"), "statusText");
    statusLayout->addWidget(m_status);
    statusLayout->addStretch();
    statusLayout->addWidget(label(
        QT_TRANSLATE_NOOP("MainWindow", "A little less noise. A lot more possibility."), "subtle"));
    statusLayout->addStretch();
    statusLayout->addWidget(label(
        tr("On-device editing   ·   v%1").arg(QApplication::applicationVersion()), "versionLabel"));
    rootLayout->addWidget(status);

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    bindText(fileMenu, QT_TRANSLATE_NOOP("MainWindow", "&File"), "title");
    auto *importAction = fileMenu->addAction(tr("Import video…"));
    bindText(importAction, QT_TRANSLATE_NOOP("MainWindow", "Import video…"));
    importAction->setShortcut(QKeySequence::Open);
    connect(importAction, &QAction::triggered, controller, &MediaController::importVideo);
    auto *exportAction = fileMenu->addAction(tr("Export video…"));
    bindText(exportAction, QT_TRANSLATE_NOOP("MainWindow", "Export video…"));
    exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(exportAction, &QAction::triggered, controller, &MediaController::exportVideo);
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("Quit Framecut"));
    bindText(quitAction, QT_TRANSLATE_NOOP("MainWindow", "Quit Framecut"));
    quitAction->setMenuRole(QAction::QuitRole);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    bindText(editMenu, QT_TRANSLATE_NOOP("MainWindow", "&Edit"), "title");
    m_undoTrimAction = editMenu->addAction(tr("Undo trim"));
    m_undoTrimAction->setObjectName("undoTrimAction");
    bindText(m_undoTrimAction, QT_TRANSLATE_NOOP("MainWindow", "Undo trim"));
    m_undoTrimAction->setShortcuts(QKeySequence::Undo);
    connect(m_undoTrimAction, &QAction::triggered, controller, &MediaController::undoTrim);
    m_redoTrimAction = editMenu->addAction(tr("Redo trim"));
    m_redoTrimAction->setObjectName("redoTrimAction");
    bindText(m_redoTrimAction, QT_TRANSLATE_NOOP("MainWindow", "Redo trim"));
    m_redoTrimAction->setShortcuts(QKeySequence::Redo);
    connect(m_redoTrimAction, &QAction::triggered, controller, &MediaController::redoTrim);
    editMenu->addSeparator();
    m_removeAction = editMenu->addAction(tr("Remove from project"));
    m_removeAction->setObjectName("removeMediaAction");
    bindText(m_removeAction, QT_TRANSLATE_NOOP("MainWindow", "Remove from project"));
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::removeSelectedMedia);
    auto *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    bindText(settingsMenu, QT_TRANSLATE_NOOP("MainWindow", "&Settings"), "title");
    auto *preferencesAction = settingsMenu->addAction(tr("Appearance and language…"));
    bindText(preferencesAction, QT_TRANSLATE_NOOP("MainWindow", "Appearance and language…"));
    preferencesAction->setMenuRole(QAction::PreferencesRole);
    connect(preferencesAction, &QAction::triggered, this, &MainWindow::showSettings);
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    bindText(helpMenu, QT_TRANSLATE_NOOP("MainWindow", "&Help"), "title");
    auto *shortcutsAction = helpMenu->addAction(tr("Keyboard shortcuts"));
    bindText(shortcutsAction, QT_TRANSLATE_NOOP("MainWindow", "Keyboard shortcuts"));
    connect(shortcutsAction, &QAction::triggered, this, &MainWindow::showShortcuts);
    auto shortcut = [&](const QKeySequence &key, const std::function<void()> &action) {
        auto *s = new QShortcut(key, this);
        connect(s, &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence(Qt::Key_Space), [this] { togglePlayback(); });
    shortcut(QKeySequence(Qt::Key_Left), [this] { seek(m_player->position() / 1000. - 1); });
    shortcut(QKeySequence(Qt::Key_Right), [this] { seek(m_player->position() / 1000. + 1); });
    shortcut(QKeySequence(Qt::SHIFT | Qt::Key_Left), [this] { seek(m_player->position() / 1000. - 5); });
    shortcut(QKeySequence(Qt::SHIFT | Qt::Key_Right),
             [this] { seek(m_player->position() / 1000. + 5); });
    connect(m_import, &QPushButton::clicked, controller, &MediaController::importVideo);
    connect(m_sample, &QPushButton::clicked, controller, &MediaController::loadSample);
    connect(m_export, &QPushButton::clicked, controller, &MediaController::exportVideo);
    connect(m_cancel, &QPushButton::clicked, controller, &MediaController::cancelOperation);
    connect(m_preview, &PreviewWidget::importRequested, controller, &MediaController::importVideo);
    connect(m_timeline, &TimelineWidget::importRequested, controller, &MediaController::importVideo);
    connect(m_preview, &PreviewWidget::clicked, this, &MainWindow::togglePlayback);
    connect(m_play, &QPushButton::clicked, this, &MainWindow::togglePlayback);
    connect(start, &QPushButton::clicked, this, [this] { seek(m_controller->trimStart()); });
    connect(end, &QPushButton::clicked, this, [this] { seek(m_controller->trimEnd()); });
    connect(m_audioToggle, &QAbstractButton::toggled, controller, &MediaController::setRemoveAudio);
    connect(m_audioAction, &QPushButton::clicked, this,
            [this] { m_controller->setRemoveAudio(!m_controller->removeAudio()); });
    connect(m_timeline, &TimelineWidget::seekRequested, this, &MainWindow::seek);
    connect(m_trimButton, &QPushButton::clicked, this, [this] {
        m_inspector->setCurrentIndex(2);
        m_timeline->setFocus();
    });
    connect(m_resetTrim, &QPushButton::clicked, this, [this] {
        m_player->pause();
        m_controller->resetTrim();
    });
    connect(m_trimStart, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_controller->busy()) return;
        m_player->pause();
        m_controller->setTrimRange(value, m_controller->trimEnd());
        seek(m_controller->trimStart());
    });
    connect(m_trimEnd, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_controller->busy()) return;
        m_player->pause();
        m_controller->setTrimRange(m_controller->trimStart(), value);
        seek(lastTrimFrame());
    });
    connect(m_timeline, &TimelineWidget::trimGestureStarted, this, [this] {
        m_player->pause();
        m_inspector->setCurrentIndex(2);
        m_controller->beginTrimGesture();
    });
    connect(m_timeline, &TimelineWidget::trimRangeChanged, controller, &MediaController::setTrimRange);
    connect(m_timeline, &TimelineWidget::trimGestureFinished, controller, &MediaController::endTrimGesture);
    connect(m_seek, &QSlider::valueChanged, this, [this](int value) {
        if (m_controller->currentMedia())
            seek(m_controller->trimStart() + value / 10000. * m_controller->trimmedDuration());
    });
    connect(m_library, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            m_controller->selectMedia(item->data(Qt::UserRole).toString());
    });
    connect(mediaRail, &QToolButton::clicked, this, [this] { m_import->setFocus(); });
    connect(audioRail, &QToolButton::clicked, this, [this] {
        m_inspector->setCurrentIndex(0);
        m_audioToggle->setFocus();
    });
    connect(shortcutsRail, &QToolButton::clicked, this, &MainWindow::showShortcuts);
    connect(m_sink, &QVideoSink::videoFrameChanged, m_preview, &PreviewWidget::setFrame);
    connect(m_player, &QMediaPlayer::positionChanged, this, &MainWindow::updatePosition);
    m_rangeTimer = new QTimer(this);
    m_rangeTimer->setTimerType(Qt::PreciseTimer);
    m_rangeTimer->setInterval(16);
    connect(m_rangeTimer, &QTimer::timeout, this, [this] { updatePosition(m_player->position()); });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (m_pendingInitialSeek && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
            m_pendingInitialSeek = false;
            m_player->setPosition(qRound64(m_controller->trimStart() * 1000));
            updatePosition(m_player->position());
        }
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (state == QMediaPlayer::PlayingState) m_rangeTimer->start();
                else m_rangeTimer->stop();
                m_play->setProperty("iconGlyph", state == QMediaPlayer::PlayingState ? "pause" : "play");
                m_play->setIcon(icon(state == QMediaPlayer::PlayingState ? "pause" : "play"));
                m_play->setAccessibleName(state == QMediaPlayer::PlayingState ? tr("Pause video")
                                                                              : tr("Play video"));
            });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &) {
        m_preview->setUnavailable(true);
        showNotice(tr("The preview could not be played. You can still export your video."));
    });
    connect(controller, &MediaController::libraryChanged, this, &MainWindow::rebuildLibrary);
    connect(controller, &MediaController::mediaChanged, this, &MainWindow::changeMedia);
    connect(controller, &MediaController::trimChanged, this, &MainWindow::updateTrim);
    connect(controller, &MediaController::audioChanged, this, [this](bool remove) {
        m_audio->setMuted(remove);
        m_preview->setAudioRemoved(remove);
        m_timeline->setAudioRemoved(remove);
        updateUi();
        if (!remove && !m_player->audioOutput())
            showNotice(tr("No audio output is available. Video preview and exports still work."),
                       false);
    });
    connect(controller, &MediaController::busyChanged, this, [this](bool busy, const QString &message) {
        if (busy) {
            m_player->pause();
            m_notice->hide();
        }
        m_cancelling = false;
        m_busyMessage->setText(message);
        m_busyBanner->setVisible(busy);
        m_progress->setRange(0, m_controller->exporting() ? 1000 : 0);
        m_progress->setValue(0);
        m_cancel->setEnabled(true);
        updateUi();
    });
    connect(controller, &MediaController::progressChanged, this, [this](double value) {
        m_progress->setRange(0, 1000);
        m_progress->setValue(qBound(0, int(value * 10), 1000));
    });
    connect(controller, &MediaController::exportCompleted, this, &MainWindow::showExportResult);
    connect(controller, &MediaController::errorOccurred, this,
            [this](const QString &message) { showNotice(message); });
    connect(controller, &MediaController::operationCancelled, this,
            [this] { showNotice(tr("Operation cancelled. Your original video is untouched."), false); });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        m_cancelling = true;
        m_cancel->setEnabled(false);
        m_busyMessage->setText(tr("Cancelling…"));
    });
    connect(controller, &MediaController::busyChanged, exportAction,
            [this, exportAction](bool, const QString &) {
                exportAction->setEnabled(m_controller->currentMedia() && (m_controller->removeAudio() || m_controller->hasTrim()) &&
                                         !m_controller->busy());
            });
    auto updateExportAction = [this, exportAction] {
        exportAction->setEnabled(m_controller->currentMedia() && (m_controller->removeAudio() || m_controller->hasTrim()) &&
                                 !m_controller->busy());
    };
    connect(controller, &MediaController::mediaChanged, this, updateExportAction);
    connect(controller, &MediaController::audioChanged, this, updateExportAction);
    connect(controller, &MediaController::trimChanged, this, updateExportAction);
    updateExportAction();
    auto *devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::audioOutputsChanged, this, &MainWindow::refreshAudioOutput);
    m_uiReady = true;
    connect(AppSettings::instance(), &AppSettings::themeChanged, this, &MainWindow::applyTheme);
    connect(AppSettings::instance(), &AppSettings::languageChanged, this, &MainWindow::retranslateUi);
    applyTheme();
    retranslateUi();
    updateUi();
}

MainWindow::~MainWindow() {
    m_player->stop();
    m_player->setVideoSink(nullptr);
}
void MainWindow::refreshAudioOutput() {
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull() && !m_player->audioOutput())
        return;
    if (!device.isNull() && m_player->audioOutput() == m_audio && m_audio->device() == device)
        return;
    const bool resume = m_player->playbackState() == QMediaPlayer::PlayingState;
    m_player->pause();
    m_player->setAudioOutput(nullptr);
    if (!device.isNull()) {
        m_audio->setDevice(device);
        m_audio->setMuted(m_controller->removeAudio());
        m_player->setAudioOutput(m_audio);
    }
    updateUi();
    if (m_controller->currentMedia() && !m_controller->removeAudio()) {
        showNotice(device.isNull() ? tr("Audio output disconnected. Video preview is still available.")
                                   : tr("Audio output connected."),
                   false);
    }
    if (resume && !m_controller->busy())
        m_player->play();
}
void MainWindow::rebuildLibrary() {
    const QSignalBlocker block(m_library);
    QHash<QString, QListWidgetItem *> existing;
    for (int row = 0; row < m_library->count(); ++row) {
        auto *item = m_library->item(row);
        existing.insert(item->data(Qt::UserRole).toString(), item);
    }
    QListWidgetItem *selected = nullptr;
    const auto *current = m_controller->currentMedia();
    int row = 0;
    for (const auto &media : m_controller->library()) {
        auto *item = existing.value(media.id);
        if (!item) {
            item = new QListWidgetItem;
            m_library->insertItem(row, item);
        } else if (m_library->item(row) != item) {
            m_library->takeItem(m_library->row(item));
            m_library->insertItem(row, item);
        }
        ++row;
        item->setText(media.name);
        item->setData(Qt::UserRole, media.id);
        item->setData(Qt::UserRole + 1, timecode(media.duration).mid(3));
        item->setData(
            Qt::UserRole + 2,
            QString("%1 × %2  ·  %3").arg(media.width).arg(media.height).arg(byteSize(media.size)));
        // A selection change does not change the library. Keep its thumbnails
        // and item identities instead of decoding every image again.
        if (item->data(Qt::UserRole + 3).toString() != media.thumbnailPath) {
            item->setData(Qt::DecorationRole, QPixmap(media.thumbnailPath));
            item->setData(Qt::UserRole + 3, media.thumbnailPath);
        }
        item->setToolTip(media.name);
        if (current && media.id == current->id)
            selected = item;
    }
    while (m_library->count() > row)
        delete m_library->takeItem(row);
    m_library->setCurrentItem(selected);
    m_library->setVisible(!m_controller->library().isEmpty());
    m_emptyLibrary->setVisible(m_controller->library().isEmpty());
    m_count->setText(QString::number(m_controller->library().size()));
    updateUi();
}
void MainWindow::changeMedia() {
    m_player->stop();
    m_atTrimEnd = false;
    m_preview->clearFrame();
    const auto *media = m_controller->currentMedia();
    m_pendingInitialSeek = media;
    m_preview->setEmpty(!media);
    m_timeline->setMedia(media);
    if (media) {
        m_preview->setPoster(QImage(media->thumbnailPath));
        m_player->setSource(QUrl::fromLocalFile(media->previewPath));
        m_player->setPosition(qRound64(m_controller->trimStart() * 1000));
        m_projectTitle->setText(QFileInfo(media->name).completeBaseName());
        m_projectTitle->setToolTip(media->name);
        m_previewInfo->setText(QString("%1 × %2").arg(media->width).arg(media->height));
    } else {
        m_player->setSource({});
        m_projectTitle->setToolTip({});
        m_projectTitle->setText(tr("Untitled project"));
        m_previewInfo->setText(tr("Original quality"));
    }
    m_audio->setMuted(m_controller->removeAudio());
    m_preview->setAudioRemoved(m_controller->removeAudio());
    m_timeline->setAudioRemoved(m_controller->removeAudio());
    rebuildLibrary();
    updatePosition(qRound64(m_controller->trimStart() * 1000));
    updateUi();
}
void MainWindow::updateUi() {
    const auto *media = m_controller->currentMedia();
    const bool busy = m_controller->busy(), remove = m_controller->removeAudio(), trimmed = m_controller->hasTrim();
    m_import->setEnabled(!busy);
    m_sample->setEnabled(!busy);
    m_export->setEnabled(media && (remove || trimmed) && !busy);
    m_library->setEnabled(!busy);
    const bool canRemove = m_library->currentItem() && !busy;
    m_remove->setEnabled(canRemove);
    m_removeAction->setEnabled(canRemove);
    m_audioToggle->setEnabled(media && media->audioTracks > 0 && !busy);
    {
        const QSignalBlocker block(m_audioToggle);
        m_audioToggle->setChecked(remove);
    }
    m_audioAction->setEnabled(media && media->audioTracks > 0 && !busy);
    m_audioAction->setText(remove ? tr("Audio removed") : tr("Original audio"));
    m_audioAction->setProperty("iconGlyph", remove ? "mute" : "audio");
    m_audioAction->setIcon(icon(remove ? "mute" : "audio"));
    m_seek->setEnabled(media && !busy);
    m_timeline->setEnabled(!busy);
    m_trimButton->setEnabled(media && !busy);
    m_trimStart->setEnabled(media && !busy);
    m_trimEnd->setEnabled(media && !busy);
    m_resetTrim->setEnabled(media && trimmed && !busy);
    m_undoTrimAction->setEnabled(m_controller->canUndoTrim() && !busy);
    m_redoTrimAction->setEnabled(m_controller->canRedoTrim() && !busy);
    {
        const QSignalBlocker startBlock(m_trimStart), endBlock(m_trimEnd);
        const double duration = media ? media->duration : 0;
        const double step = media ? qMin(duration, media->fps > 0 ? 1. / media->fps : .04) : .04;
        m_trimStart->setLocale(QLocale());
        m_trimEnd->setLocale(QLocale());
        m_trimStart->setSuffix(tr(" s"));
        m_trimEnd->setSuffix(tr(" s"));
        m_trimStart->setSingleStep(step);
        m_trimEnd->setSingleStep(step);
        m_trimStart->setRange(0, qMax(0., m_controller->trimEnd() - step));
        m_trimEnd->setRange(media ? qMin(duration, m_controller->trimStart() + step) : 0, duration);
        m_trimStart->setValue(m_controller->trimStart());
        m_trimEnd->setValue(m_controller->trimEnd());
    }
    m_trimDuration->setText(preciseTimecode(m_controller->trimmedDuration()));
    m_trimDuration->setAccessibleName(tr("Selected duration"));
    m_timeline->setTrimRange(m_controller->trimStart(), m_controller->trimEnd());
    m_trimExportHint->setText(trimmed ? tr("Trimmed export: high-quality MP4, with the selected start and end.")
                                    : tr("The full clip is selected. Drag an edge inward to shorten it."));
    for (auto *button : m_playbackButtons)
        button->setEnabled(media && !busy);
    m_preview->setEnabled(!busy);
    m_audioDescription->setText(
        !media                    ? tr("Keep the picture. Lose the noise.")
        : media->audioTracks == 0 ? tr("This video is already silent.")
        : remove                  ? tr("%n audio track(s) will be removed.", nullptr, media->audioTracks)
                                  : trimmed ? tr("The first audio track will be kept in the trimmed video.")
                                            : tr("Preview uses the original audio."));
    m_audioState->setText(remove                    ? tr("✓  Silent preview & export")
                          : m_player->audioOutput() ? tr("Listening to original audio")
                                                    : tr("No audio output connected"));
    if (media && !remove && !m_player->audioOutput())
        m_audioDescription->setText(tr("Video preview is available without sound."));
    m_properties["resolution"]->setText(media ? QString("%1 × %2").arg(media->width).arg(media->height)
                                              : tr("Same as source"));
    m_properties["fps"]->setText(media ? tr("%1 fps").arg(QLocale().toString(media->fps, 'f', 2))
                                       : tr("Same as source"));
    m_properties["format"]->setText(trimmed ? QStringLiteral("MP4 · H.264")
                                  : media ? QFileInfo(media->name).suffix().toUpper()
                                          : tr("Same as source"));
    m_properties["quality"]->setText(trimmed ? tr("High quality") : tr("Original  ●"));
    m_properties["audio"]->setText(!media || media->audioTracks == 0 || remove ? tr("None")
                                  : trimmed ? tr("Original audio") : tr("Enable removal"));
    m_qualityHeading->setText(trimmed ? tr("Keep the moment.") : tr("Every pixel, preserved."));
    m_qualityDescription->setText(trimmed ? tr("Precise trims are encoded as a new, high-quality MP4.")
                                        : tr("No re-encoding. No quality loss."));
    m_properties["name"]->setText(media ? media->name : tr("No video selected"));
    m_properties["duration"]->setText(media ? timecode(media->duration) : QStringLiteral("—"));
    m_properties["detailsResolution"]->setText(media ? m_properties["resolution"]->text()
                                                     : QStringLiteral("—"));
    m_properties["detailsFps"]->setText(media ? m_properties["fps"]->text() : QStringLiteral("—"));
    m_properties["codec"]->setText(media ? media->codec.toUpper() : QStringLiteral("—"));
    m_properties["size"]->setText(media ? byteSize(media->size) : QStringLiteral("—"));
    m_properties["tracks"]->setText(media ? QString::number(media->audioTracks) : QStringLiteral("—"));
    m_timelineInfo->setText(media ? tr("1 clip   ·   %1").arg(preciseTimecode(m_controller->trimmedDuration()))
                                  : tr("Your story starts here"));
    const auto count = m_controller->library().size();
    m_status->setText(
        tr("●  %1   ·   %2")
            .arg(busy ? m_controller->exporting() ? tr("Exporting…") : tr("Preparing video…")
                      : tr("Ready"))
            .arg(tr("%n asset(s)", nullptr, int(count))));
    m_projectTitle->setText(media ? QFileInfo(media->name).completeBaseName() : tr("Untitled project"));
    m_previewInfo->setText(media ? QString("%1 × %2").arg(media->width).arg(media->height)
                                 : tr("Original quality"));
    if (busy)
        m_busyMessage->setText(m_cancelling                ? tr("Cancelling…")
                               : m_controller->exporting() ? tr("Exporting video…")
                                                           : tr("Preparing video preview…"));
}
double MainWindow::lastTrimFrame() const {
    const auto *media = m_controller->currentMedia();
    const double frame = media && media->fps > 0 ? 1. / media->fps : .04;
    return qMax(m_controller->trimStart(), m_controller->trimEnd() - frame);
}
void MainWindow::updateTrim() {
    if (!m_uiReady) return;
    m_player->pause();
    m_atTrimEnd = false;
    const double position = m_player->position() / 1000.;
    if (m_controller->currentMedia() &&
        (position < m_controller->trimStart() || position >= m_controller->trimEnd())) {
        const double bounded = position < m_controller->trimStart() ? m_controller->trimStart() : lastTrimFrame();
        m_player->setPosition(qRound64(bounded * 1000));
    }
    updateUi();
    updatePosition(m_player->position());
}
void MainWindow::updatePosition(qint64 position) {
    const auto *media = m_controller->currentMedia();
    double seconds = position / 1000.;
    const double start = m_controller->trimStart(), end = m_controller->trimEnd();
    if (media && !m_adjustingBoundary && m_player->playbackState() == QMediaPlayer::PlayingState) {
        if (seconds >= end - .001) {
            m_adjustingBoundary = true;
            m_player->pause();
            m_atTrimEnd = true;
            m_player->setPosition(qRound64(lastTrimFrame() * 1000));
            seconds = lastTrimFrame();
            m_adjustingBoundary = false;
        } else if (seconds < start - .002) {
            m_adjustingBoundary = true;
            m_player->setPosition(qRound64(start * 1000));
            seconds = start;
            m_adjustingBoundary = false;
        }
    }
    const double duration = m_controller->trimmedDuration();
    const double elapsed = media ? qBound(0., seconds - start, duration) : 0;
    m_time->setText(timecode(elapsed) + "  /  " + timecode(duration));
    if (!m_seek->isSliderDown()) {
        const QSignalBlocker block(m_seek);
        m_seek->setValue(media && duration > 0 ? int(elapsed / duration * 10000) : 0);
    }
    m_timeline->setPosition(seconds);
}
void MainWindow::togglePlayback() {
    if (!m_controller->currentMedia() || m_controller->busy())
        return;
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else {
        const double position = m_player->position() / 1000.;
        if (m_atTrimEnd || m_player->mediaStatus() == QMediaPlayer::EndOfMedia ||
            position < m_controller->trimStart() || position >= lastTrimFrame() - .001)
            m_player->setPosition(qRound64(m_controller->trimStart() * 1000));
        m_atTrimEnd = false;
        m_player->play();
    }
}
void MainWindow::seek(double seconds) {
    if (const auto *media = m_controller->currentMedia(); media && !m_controller->busy()) {
        m_atTrimEnd = seconds >= m_controller->trimEnd() - .001;
        m_player->setPosition(qRound64(qBound(m_controller->trimStart(), seconds, lastTrimFrame()) * 1000));
        updatePosition(m_player->position());
    }
}
void MainWindow::showNotice(const QString &text, bool error) {
    m_noticeText->setText(text);
    m_noticeError = error;
    m_noticeText->setStyleSheet(QString("color:%1;")
                                    .arg(error ? Theme::color("#e2c8b9", "#744b27").name()
                                               : Theme::color("#c1dfc8", "#285839").name()));
    m_notice->show();
}
void MainWindow::showExportResult(const QString &path, qint64 size) {
    auto *dialog = new QDialog(this);
    dialog->setObjectName("exportSuccessDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    bindText(dialog, QT_TRANSLATE_NOOP("MainWindow", "Export complete"), "windowTitle");
    dialog->setMinimumWidth(430);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(18);
    auto *success = label("");
    success->setPixmap(icon("check", Theme::color("#a6edc0", "#087a58")).pixmap(42, 42));
    success->setProperty("pixmapGlyph", "check");
    success->setProperty("pixmapSize", 42);
    success->setAlignment(Qt::AlignCenter);
    layout->addWidget(success);
    auto *title = label(QT_TRANSLATE_NOOP("MainWindow", "Your video is ready."));
    title->setStyleSheet("font-size:23px;font-weight:600;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto *subtitle = label(m_controller->hasTrim()
                              ? QT_TRANSLATE_NOOP("MainWindow", "Just the part you chose.\nYour original stays untouched.")
                              : QT_TRANSLATE_NOOP("MainWindow", "Same quality, without the audio.\nYour original stays untouched."),
                          "muted");
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);
    auto *file = label(QFileInfo(path).fileName());
    file->setAlignment(Qt::AlignCenter);
    file->setWordWrap(true);
    file->setToolTip(path);
    layout->addWidget(file);
    const auto *media = m_controller->currentMedia();
    auto *details = label(tr("%1   ·   %2")
                              .arg(byteSize(size))
                              .arg(m_controller->removeAudio() || !media || media->audioTracks == 0
                                       ? tr("No audio tracks") : tr("Original audio included")), "subtle");
    details->setAlignment(Qt::AlignCenter);
    layout->addWidget(details);
    auto *buttons = new QHBoxLayout;
    auto *back = button(QT_TRANSLATE_NOOP("MainWindow", "Back to editing"));
    auto *reveal = button(QT_TRANSLATE_NOOP("MainWindow", "Show in folder"), "folder", true);
    reveal->setObjectName("revealButton");
    buttons->addWidget(back);
    buttons->addWidget(reveal);
    layout->addLayout(buttons);
    connect(back, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(reveal, &QPushButton::clicked, m_controller, &MediaController::revealExport);
    dialog->open();
}
void MainWindow::showShortcuts() {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    bindText(dialog, QT_TRANSLATE_NOOP("MainWindow", "Keyboard shortcuts"), "windowTitle");
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(30, 26, 30, 26);
    layout->setSpacing(20);
    auto *title = label(QT_TRANSLATE_NOOP("MainWindow", "A few useful shortcuts."));
    title->setStyleSheet("font-size:22px;font-weight:600;");
    layout->addWidget(title);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(55);
    grid->setVerticalSpacing(16);
#ifdef Q_OS_MAC
    const QString modifier = "⌘";
#else
    const QString modifier = "Ctrl";
#endif
    const QList<QPair<const char *, QString>> rows = {
        {QT_TRANSLATE_NOOP("MainWindow", "Import a video"), modifier + " O"},
        {QT_TRANSLATE_NOOP("MainWindow", "Export video"), modifier + " E"},
        {QT_TRANSLATE_NOOP("MainWindow", "Undo trim"), modifier + " Z"},
        {QT_TRANSLATE_NOOP("MainWindow", "Redo trim"), modifier + " Shift Z"},
        {QT_TRANSLATE_NOOP("MainWindow", "Play / pause"), tr("Space")},
        {QT_TRANSLATE_NOOP("MainWindow", "Seek 1 second"), "← / →"},
        {QT_TRANSLATE_NOOP("MainWindow", "Seek 5 seconds"), "Shift + ← / →"},
        {QT_TRANSLATE_NOOP("MainWindow", "Remove selected media"),
         tr("Delete / Backspace (media list)")}};
    int row = 0;
    for (const auto &entry : rows) {
        grid->addWidget(label(entry.first, "muted"), row, 0);
        auto *key = label(entry.second);
        key->setAlignment(Qt::AlignRight);
        grid->addWidget(key, row++, 1);
    }
    layout->addLayout(grid);
    auto *done = button(QT_TRANSLATE_NOOP("MainWindow", "Got it"), {}, true);
    layout->addWidget(done);
    connect(done, &QPushButton::clicked, dialog, &QDialog::accept);
    dialog->open();
}
void MainWindow::removeSelectedMedia() {
    if (m_controller->busy())
        return;
    if (auto *item = m_library->currentItem())
        m_controller->removeMedia(item->data(Qt::UserRole).toString());
}

void MainWindow::showSettings() {
    if (m_settingsDialog) {
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
        return;
    }
    auto *dialog = new QDialog(this);
    m_settingsDialog = dialog;
    dialog->setObjectName("settingsDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setMinimumWidth(430);
    bindText(dialog, QT_TRANSLATE_NOOP("MainWindow", "Settings"), "windowTitle");
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(28, 26, 28, 26);
    layout->setSpacing(20);
    auto *heading = label(QT_TRANSLATE_NOOP("MainWindow", "Make yourself at home."));
    heading->setStyleSheet("font-size:21px;font-weight:600;");
    heading->setWordWrap(true);
    layout->addWidget(heading);
    auto *description =
        label(QT_TRANSLATE_NOOP("MainWindow", "Changes apply immediately and are saved for next time."),
              "muted");
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(18);
    grid->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Theme")), 0, 0);
    auto *theme = new QComboBox;
    theme->setObjectName("themeCombo");
    bindText(theme, QT_TRANSLATE_NOOP("MainWindow", "Theme"), "accessibleName");
    theme->addItem(tr("Dark"), "dark");
    theme->addItem(tr("Light"), "light");
    grid->addWidget(theme, 0, 1);
    grid->addWidget(label(QT_TRANSLATE_NOOP("MainWindow", "Language")), 1, 0);
    auto *language = new QComboBox;
    language->setObjectName("languageCombo");
    bindText(language, QT_TRANSLATE_NOOP("MainWindow", "Language"), "accessibleName");
    language->addItem(QStringLiteral("English"), "en");
    language->addItem(QStringLiteral("Русский"), "ru");
    grid->addWidget(language, 1, 1);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);
    auto *done = button(QT_TRANSLATE_NOOP("MainWindow", "Done"), {}, true);
    done->setObjectName("settingsDoneButton");
    layout->addWidget(done);
    auto refresh = [theme, language] {
        const QSignalBlocker themeBlock(theme), languageBlock(language);
        theme->setItemText(0, MainWindow::tr("Dark"));
        theme->setItemText(1, MainWindow::tr("Light"));
        theme->setCurrentIndex(theme->findData(AppSettings::instance()->theme()));
        language->setCurrentIndex(language->findData(AppSettings::instance()->language()));
    };
    refresh();
    connect(theme, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [theme](int index) {
        if (index >= 0)
            AppSettings::instance()->setTheme(theme->itemData(index).toString());
    });
    connect(language, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [language](int index) {
        if (index >= 0)
            AppSettings::instance()->setLanguage(language->itemData(index).toString());
    });
    connect(AppSettings::instance(), &AppSettings::themeChanged, dialog, refresh);
    connect(AppSettings::instance(), &AppSettings::languageChanged, dialog, refresh);
    connect(done, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(dialog, &QObject::destroyed, this, [this] { m_settingsDialog = nullptr; });
    dialog->open();
}

void MainWindow::applyTheme() {
    if (!m_uiReady)
        return;
    setStyleSheet(QString::fromUtf8(editorStyleSheet) +
                  (Theme::isLight() ? QString::fromUtf8(lightStyleSheet) : QString{}));
    for (auto *button : findChildren<QAbstractButton *>()) {
        const QString glyph = button->property("iconGlyph").toString();
        if (!glyph.isEmpty())
            button->setIcon(icon(glyph, button->property("primary").toBool()
                                            ? Theme::color("#153e2b", "#ffffff")
                                            : Theme::color("#aec3b6", "#42594c")));
        button->update();
    }
    for (auto *widget : findChildren<QLabel *>()) {
        const QString glyph = widget->property("pixmapGlyph").toString();
        const int size = widget->property("pixmapSize").toInt();
        if (!glyph.isEmpty() && size > 0)
            widget->setPixmap(icon(glyph, Theme::color("#95caa7", "#087a58")).pixmap(size, size));
    }
    m_noticeText->setStyleSheet(QString("color:%1;")
                                    .arg(m_noticeError ? Theme::color("#e2c8b9", "#744b27").name()
                                                       : Theme::color("#c1dfc8", "#285839").name()));
    m_library->viewport()->update();
    m_preview->update();
    m_timeline->update();
    update();
}

void MainWindow::retranslateUi() {
    if (!m_uiReady)
        return;
    // Notices can contain controller/backend text translated when emitted.
    // Dismiss the completed notice rather than retain a mixed-language message.
    m_notice->hide();
    refreshTranslations(this);
    m_inspector->setTabText(0, tr("Audio"));
    m_inspector->setTabText(1, tr("Details"));
    m_inspector->setTabText(2, tr("Trim"));
    if (auto *version = findChild<QLabel *>("versionLabel"))
        version->setText(tr("On-device editing   ·   v%1").arg(QApplication::applicationVersion()));
    m_play->setAccessibleName(
        m_player->playbackState() == QMediaPlayer::PlayingState ? tr("Pause video") : tr("Play video"));
    rebuildLibrary();
    updatePosition(m_player->position());
    updateUi();
}

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (!m_controller->busy() && event->mimeData()->hasUrls() && !event->mimeData()->urls().isEmpty() &&
        event->mimeData()->urls().first().isLocalFile())
        event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *event) {
    if (!m_controller->busy() && event->mimeData()->hasUrls() && !event->mimeData()->urls().isEmpty() &&
        event->mimeData()->urls().first().isLocalFile()) {
        m_controller->importFile(event->mimeData()->urls().first().toLocalFile());
        event->acceptProposedAction();
    }
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_controller->busy()) {
        auto answer = QMessageBox::question(this, tr("Operation in progress"),
                                            tr("Cancel the current operation and close Framecut?"),
                                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_controller->cancelOperation();
    }
    m_player->stop();
    event->accept();
}
