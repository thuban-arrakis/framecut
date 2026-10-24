#pragma once
#include <QApplication>
#include <QIcon>
#include "appsettings.h"

inline void configureSoftwareMedia() {
    QCoreApplication::setAttribute(Qt::AA_ForceRasterWidgets);
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
    qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", ",");
    qputenv("QT_DISABLE_HW_TEXTURES_CONVERSION", "1");
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
}

inline void configureApplication(QApplication &app) {
    app.setApplicationName("Framecut");
    app.setApplicationDisplayName("Framecut");
    app.setApplicationVersion("0.3.0");
    app.setOrganizationName("Framecut");
    app.setOrganizationDomain("framecut.local");
    app.setWindowIcon(QIcon(":/assets/icon.png"));
    app.setStyle("Fusion");
    AppSettings::instance();
}
