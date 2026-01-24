#include "theme_manager.h"
#include <QApplication>
#include <QPalette>
#include <QFontMetrics>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <QDebug>
#include <QFileInfo>
#include <QDir>

namespace acutesim {
namespace gui {
namespace styling {

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager() : fileWatcher_(new QFileSystemWatcher(this)) {
    initTokenRegistry();
    // Default to Dark Theme
    loadTheme("Dark");
}

void ThemeManager::initTokenRegistry() {
    // Phase 11.5: Formal Token Definitions
    auto reg = [&](const QString& key, const QColor& def, bool req = true, const QString& desc = "") {
        tokenRegistry_[key] = {key, def, req, desc};
    };

    // UI Chrome
    reg("ui.background", QColor("#1e1e1e"), true, "Global background");
    reg("ui.text", QColor("#cccccc"), true, "Primary text color");
    reg("ui.header", QColor("#252526"), true, "Sidebar/Dock headers");
    reg("ui.border", QColor("#3e3e42"), true, "Dividers and borders");
    reg("ui.accent", QColor("#007acc"), true, "Focus/Selection");
    reg("ui.error", QColor("#f48771"), true, "Error states");
    
    // Inputs
    reg("input.background", QColor("#3c3c3c"), true, "Text fields");
    reg("input.text", QColor("#cccccc"), true, "Input text");
    
    // Schematic (Essential for rendering)
    reg("schematic.background", QColor("#1e1e1e"), false, "Canvas bg (defaults to ui.background)");
    reg("schematic.grid", QColor(60, 60, 60), true, "Grid dots/lines");
    reg("schematic.wire", QColor("#cccccc"), true, "Default wire color");
    reg("schematic.pin", QColor("#cccccc"), true, "Component pins");
    reg("schematic.selection", QColor(0, 122, 204, 100), true, "Selection overlay");
}

bool ThemeManager::loadThemeFromJson(const QString& jsonPath) {
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open theme file:" << jsonPath;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "Invalid JSON theme:" << jsonPath;
        return false;
    }

    // Update for Hot Reload
    if (jsonPath != currentThemePath_) {
        if (!currentThemePath_.isEmpty()) fileWatcher_->removePath(currentThemePath_);
        currentThemePath_ = jsonPath;
        fileWatcher_->addPath(currentThemePath_);
    }

    QJsonObject root = doc.object();
    QString name = root["name"].toString("Generic Theme");
    
    // Atomic Switch: Load into temp map first
    QMap<QString, QColor> newColors;
    
    // Handle Inheritance
    QString parentTheme = root["extends"].toString();
    if (!parentTheme.isEmpty()) {
        // Try file-based inheritance
        QFileInfo currentInfo(jsonPath);
        QString parentPath = currentInfo.dir().filePath(parentTheme + ".json");
        QFile pFile(parentPath);
        
        if (pFile.exists() && pFile.open(QIODevice::ReadOnly)) {
             QJsonDocument pDoc = QJsonDocument::fromJson(pFile.readAll());
             if (pDoc.isObject()) {
                 QJsonObject pColors = pDoc.object()["colors"].toObject();
                 for (auto it = pColors.begin(); it != pColors.end(); ++it) {
                     newColors[it.key()] = QColor(it.value().toString());
                 }
             }
        }
    }

    QJsonObject colorsObj = root["colors"].toObject();
    for (auto it = colorsObj.begin(); it != colorsObj.end(); ++it) {
        newColors[it.key()] = QColor(it.value().toString());
    }

    // Validation
    for (const auto& token : tokenRegistry_) {
        if (token.required && !newColors.contains(token.key)) {
            qWarning() << "Theme" << name << "missing required token:" << token.key << "- using default.";
            newColors[token.key] = token.defaultValue;
        }
    }
    
    // Commit
    colors_ = newColors;
    currentThemeName_ = name;
    
    validateActiveTheme(); // Check contrast
    emit themeChanged();
    return true;
}

void ThemeManager::loadHighContrastTheme() {
    if (currentThemeName_ == "High Contrast") return;
    
    colors_.clear();
    currentThemeName_ = "High Contrast";
    
    // WCAG AAA Palette
    colors_["ui.background"] = QColor("#000000");
    colors_["ui.text"] = QColor("#FFFFFF");        // 21:1
    colors_["ui.header"] = QColor("#000000");      // Bordered
    colors_["ui.border"] = QColor("#FFFFFF");
    colors_["ui.accent"] = QColor("#FFFF00");      // Yellow
    colors_["ui.error"] = QColor("#FF00FF");       // Magenta
    
    colors_["input.background"] = QColor("#000000");
    colors_["input.text"] = QColor("#FFFFFF");
    
    colors_["schematic.grid"] = QColor("#FFFFFF");
    colors_["schematic.wire"] = QColor("#FFFF00");
    colors_["schematic.pin"] = QColor("#00FFFF");  // Cyan
    
    emit themeChanged();
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
    // Fallback to registry default if available
    if (tokenRegistry_.contains(token)) {
        return tokenRegistry_[token].defaultValue;
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

    // Schematic
    colors_["schematic.background"] = colors_["ui.background"]; // Match global bg
    colors_["schematic.grid"] = QColor(60, 60, 60);
    colors_["schematic.wire"] = QColor("#cccccc");
    colors_["schematic.pin"] = QColor("#cccccc");
    colors_["schematic.selection"] = QColor(0, 122, 204, 100);
    
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


// Accessibility Helpers
double ThemeManager::getContrastRatio(const QColor& c1, const QColor& c2) {
    auto luminance = [](const QColor& c) -> double {
        // sRGB relative luminance formula
        auto adjust = [](double val) {
            val /= 255.0;
            return (val <= 0.03928) ? (val / 12.92) : std::pow((val + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * adjust(c.red()) + 0.7152 * adjust(c.green()) + 0.0722 * adjust(c.blue());
    };
    
    double l1 = luminance(c1);
    double l2 = luminance(c2);
    
    if (l1 < l2) std::swap(l1, l2);
    return (l1 + 0.05) / (l2 + 0.05);
}

bool ThemeManager::validateContrast(const QColor& c1, const QColor& c2, bool strictAAA) {
    double ratio = getContrastRatio(c1, c2);
    double threshold = strictAAA ? 7.0 : 4.5; // AA normal text
    return ratio >= threshold;
}

void ThemeManager::validateActiveTheme() {
    // Check critical pairs
    // ... logic ...
    QColor bg = getColor("ui.background");
    QColor text = getColor("ui.text");
    
    if (!validateContrast(text, bg, false)) {
        qWarning() << "Accessibility Warning: Low contrast between ui.text and ui.background (" 
                   << getContrastRatio(text, bg) << ":1)";
    }
    
    // Check wire contrast
    if (colors_.contains("schematic.wire")) {
        QColor wire = colors_["schematic.wire"];
        QColor schemBg = colors_.contains("schematic.background") ? colors_["schematic.background"] : bg;
        
        if (!validateContrast(wire, schemBg, false)) {
            qWarning() << "Accessibility Warning: Low contrast on schematic wires (" 
                       << getContrastRatio(wire, schemBg) << ":1)";
        }
    }
}

void ThemeManager::enableHotReload(bool enable) {
    if (enable) {
        if (!currentThemePath_.isEmpty()) {
            fileWatcher_->addPath(currentThemePath_);
        }
        connect(fileWatcher_, &QFileSystemWatcher::fileChanged, this, &ThemeManager::onThemeFileChanged, Qt::UniqueConnection);
    } else {
        if (!currentThemePath_.isEmpty()) {
             fileWatcher_->removePath(currentThemePath_);
        }
        disconnect(fileWatcher_, &QFileSystemWatcher::fileChanged, this, &ThemeManager::onThemeFileChanged);
    }
}

void ThemeManager::onThemeFileChanged(const QString& path) {
    if (path == currentThemePath_) {
        qDebug() << "Hot Reloading Theme:" << path;
        loadThemeFromJson(path);
    }
}

} // namespace styling
} // namespace gui
} // namespace acutesim
