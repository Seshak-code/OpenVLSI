#include "theme_manager.h"
#include <QApplication>
#include <QPalette>
#include <QFontMetrics>

namespace acutesim {
namespace gui {
namespace styling {

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager() {
    // Default to Dark Theme
    loadTheme("Dark");
}

void ThemeManager::loadTheme(const QString& themeName) {
    if (currentThemeName_ == themeName) return;

    currentThemeName_ = themeName;
    colors_.clear();

    if (themeName == "Light") {
        loadLightTheme();
    } else {
        loadDarkTheme(); // Default
    }

    emit themeChanged();
}

QString ThemeManager::currentTheme() const {
    return currentThemeName_;
}

QColor ThemeManager::getColor(const QString& token) const {
    if (colors_.contains(token)) {
        return colors_.value(token);
    }
    return Qt::red; // Error color for missing token
}

QFont ThemeManager::getFont(const QString& token) const {
    if (fonts_.contains(token)) {
        return fonts_.value(token);
    }
    return QApplication::font();
}

QFont ThemeManager::getScaledFont(const QString& usage, int pointSize) const {
    QFont f = getFont(usage);
    f.setPointSize(pointSize);
    return f;
}

QFontMetrics ThemeManager::getScaledFontMetrics(const QString& usage, int pointSize) const {
    return QFontMetrics(getScaledFont(usage, pointSize));
}

QString ThemeManager::getStyleSheet() const {
    // Generate global stylesheet based on tokens
    // This allows QWidgets to respect the theme automatically
    QString css;
    css += QString("QWidget { background-color: %1; color: %2; }")
               .arg(getColor("ui.background").name())
               .arg(getColor("ui.text").name());
    
    css += QString("QLineEdit, QTextEdit, QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3; }")
               .arg(getColor("input.background").name())
               .arg(getColor("input.text").name())
               .arg(getColor("ui.border").name());

    css += QString("QDockWidget::title { background: %1; }")
                .arg(getColor("ui.header").name());

    css += QString("QSplitter::handle { background-color: %1; }")
                .arg(getColor("ui.border").name());

    return css;
}

// Static Helpers
QColor ThemeManager::background() { return instance().getColor("ui.background"); }
QColor ThemeManager::foreground() { return instance().getColor("ui.text"); }
QColor ThemeManager::accent() { return instance().getColor("ui.accent"); }
QColor ThemeManager::error() { return instance().getColor("ui.error"); }

void ThemeManager::loadDarkTheme() {
    // VS Code Dark Modern Colors
    colors_["ui.background"] = QColor("#1e1e1e");
    colors_["ui.text"] = QColor("#cccccc");
    colors_["ui.header"] = QColor("#252526"); // Sidebar/Dock header
    colors_["ui.border"] = QColor("#3e3e42");
    colors_["ui.accent"] = QColor("#007acc"); // VS Code Blue
    colors_["ui.error"] = QColor("#f48771");  // Soft Red
    
    colors_["input.background"] = QColor("#3c3c3c");
    colors_["input.text"] = QColor("#cccccc");
    
    colors_["syntax.keyword"] = QColor("#569cd6");
    colors_["syntax.string"] = QColor("#ce9178");
    colors_["syntax.comment"] = QColor("#6a9955");
    
    // Fonts
    QFont defaultFont("Inter"); // Or system default
    if (defaultFont.exactMatch()) {
        defaultFont.setPointSize(10);
    } else {
        defaultFont = QApplication::font(); // Fallback
    }
    fonts_["ui.font"] = defaultFont;
}

void ThemeManager::loadLightTheme() {
    // VS Code Light Modern
    colors_["ui.background"] = QColor("#ffffff");
    colors_["ui.text"] = QColor("#333333");
    colors_["ui.header"] = QColor("#f3f3f3");
    colors_["ui.border"] = QColor("#e0e0e0");
    colors_["ui.accent"] = QColor("#007acc");
    colors_["ui.error"] = QColor("#e51400");
    
    colors_["input.background"] = QColor("#ffffff");
    colors_["input.text"] = QColor("#333333");
}

} // namespace styling
} // namespace gui
} // namespace acutesim
