#pragma once

#include <QObject>
#include <QColor>
#include <QFont>
#include <QMap>
#include <QString>

namespace acutesim {
namespace gui {
namespace styling {

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance();

    // Theme Management
    void loadTheme(const QString& themeName);
    QString currentTheme() const;

    // Accessors
    QColor getColor(const QString& token) const;
    QFont getFont(const QString& token = "ui.font") const;
    QFont getScaledFont(const QString& usage, int pointSize) const; // Phase 11.4
    QFontMetrics getScaledFontMetrics(const QString& usage, int pointSize) const; // Phase 11.4
    QString getStyleSheet() const; // For QWidget::setStyleSheet

    // Helpers for common tokens
    static QColor background();
    static QColor foreground();
    static QColor accent();
    static QColor error();

signals:
    void themeChanged();

private:
    ThemeManager();
    ~ThemeManager() override = default;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    void loadDarkTheme();
    void loadLightTheme();

    QString currentThemeName_;
    QMap<QString, QColor> colors_;
    QMap<QString, QFont> fonts_;
};

} // namespace styling
} // namespace gui
} // namespace acutesim
