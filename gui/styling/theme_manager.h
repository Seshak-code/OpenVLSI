#pragma once

#include <QObject>
#include <QColor>
#include <QFont>
#include <QMap>
#include <QString>
#include <QFileSystemWatcher>

namespace acutesim {
namespace gui {
namespace styling {

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance();

    // Theme Management
    void loadTheme(const QString& themeName);
    bool loadThemeFromJson(const QString& jsonPath); // Phase 11.5
    void loadHighContrastTheme(); // Phase 11.5: Fallback / Accessibility
    void enableHotReload(bool enable); // Phase 11.5: Dev Workflow
    
    QString currentTheme() const;

    // Accessors
    QColor getColor(const QString& token) const;
    QFont getFont(const QString& token = "ui.font") const;
    QFont getScaledFont(const QString& usage, int pointSize) const; // Phase 11.4
    QFontMetrics getScaledFontMetrics(const QString& usage, int pointSize) const; // Phase 11.4
    QString getStyleSheet() const; // For QWidget::setStyleSheet
    QString getWidgetStyleSheet(const QString& widgetType) const; // Component-specific CSS

    // Helpers for common tokens
    static QColor background();
    static QColor foreground();
    static QColor accent();
    static QColor error();
    
    // Accessibility & Validation (Phase 11.5)
    static double getContrastRatio(const QColor& c1, const QColor& c2);
    static bool validateContrast(const QColor& c1, const QColor& c2, bool strictAAA = false);

signals:
    void themeChanged();

private:
    ThemeManager();
    ~ThemeManager() override = default;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    void loadDarkTheme();
    void loadLightTheme();
    
    // Token Registry (Phase 11.5)
    struct ThemeToken {
        QString key;
        QColor defaultValue;
        bool required;
        QString description;
    };
    void initTokenRegistry();
    void validateActiveTheme(); // Checks against registry and WCAG
    
    QString currentThemeName_;
    QMap<QString, QColor> colors_;
    QMap<QString, QFont> fonts_;
    QMap<QString, ThemeToken> tokenRegistry_;
    
    // Hot Reload
    QFileSystemWatcher* fileWatcher_;
    QString currentThemePath_;
    void onThemeFileChanged(const QString& path);
};

} // namespace styling
} // namespace gui
} // namespace acutesim
