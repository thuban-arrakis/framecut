#include "appsettings.h"
#include "theme.h"

#include <QApplication>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QTranslator>
#include <QVariant>

AppSettings *AppSettings::instance() {
    static QPointer<AppSettings> settings;
    if (!settings) settings = new AppSettings(qApp);
    return settings;
}

AppSettings::AppSettings(QObject *parent) : QObject(parent) {
    const QString isolatedFile = qEnvironmentVariable("FRAMECUT_SETTINGS_FILE");
    if (!isolatedFile.isEmpty()) {
        m_settings = std::make_unique<QSettings>(isolatedFile, QSettings::IniFormat);
    } else {
        m_settings = std::make_unique<QSettings>();
    }
    m_theme = m_settings->value("appearance/theme", "dark").toString();
    if (m_theme != "light") m_theme = "dark";
    qApp->setProperty("framecutTheme", m_theme);
    Theme::applyPalette();

    const QString language = m_settings->value("interface/language", "en").toString();
    if (language == "ru" && applyLanguage(language)) m_language = language;
    else applyLanguage("en");
    qApp->setProperty("framecutLanguage", m_language);
}

AppSettings::~AppSettings() = default;

void AppSettings::setTheme(const QString &theme) {
    if ((theme != "dark" && theme != "light") || theme == m_theme) return;
    m_theme = theme;
    m_settings->setValue("appearance/theme", theme);
    m_settings->sync();
    qApp->setProperty("framecutTheme", theme);
    Theme::applyPalette();
    emit themeChanged();
}

bool AppSettings::applyLanguage(const QString &language) {
    if (language == "ru") {
        auto translatedApp = std::make_unique<QTranslator>();
        if (!translatedApp->load(":/translations/framecut_ru.qm")) return false;
        auto translatedQt = std::make_unique<QTranslator>();
        const bool hasQtTranslation = translatedQt->load(":/translations/qtbase_ru.qm");
        if (m_appTranslator) qApp->removeTranslator(m_appTranslator.get());
        if (m_qtTranslator) qApp->removeTranslator(m_qtTranslator.get());
        m_appTranslator = std::move(translatedApp);
        m_qtTranslator = std::move(translatedQt);
        if (hasQtTranslation) qApp->installTranslator(m_qtTranslator.get());
        qApp->installTranslator(m_appTranslator.get());
        QLocale::setDefault(QLocale(QLocale::Russian, QLocale::Russia));
    } else {
        if (m_appTranslator) qApp->removeTranslator(m_appTranslator.get());
        if (m_qtTranslator) qApp->removeTranslator(m_qtTranslator.get());
        m_appTranslator.reset();
        m_qtTranslator.reset();
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    }
    return true;
}

void AppSettings::setLanguage(const QString &language) {
    if ((language != "en" && language != "ru") || language == m_language) return;
    if (!applyLanguage(language)) return;
    m_language = language;
    m_settings->setValue("interface/language", language);
    m_settings->sync();
    qApp->setProperty("framecutLanguage", language);
    emit languageChanged();
}
