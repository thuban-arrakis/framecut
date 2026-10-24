#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QSettings;
class QTranslator;

// Application-wide preferences, deliberately independent of the open media session.
class AppSettings final : public QObject {
    Q_OBJECT
public:
    static AppSettings *instance();
    QString theme() const { return m_theme; }
    QString language() const { return m_language; }

public slots:
    void setTheme(const QString &theme);
    void setLanguage(const QString &language);

signals:
    void themeChanged();
    void languageChanged();

private:
    explicit AppSettings(QObject *parent = nullptr);
    ~AppSettings() override;
    bool applyLanguage(const QString &language);

    QString m_theme = QStringLiteral("dark");
    QString m_language = QStringLiteral("en");
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<QTranslator> m_appTranslator;
    std::unique_ptr<QTranslator> m_qtTranslator;
};
