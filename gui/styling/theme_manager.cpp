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
    reg("ui.textDim", QColor("#888888"), true, "Dimmed/secondary text");
    reg("ui.header", QColor("#252526"), true, "Sidebar/Dock headers");
    reg("ui.border", QColor("#3e3e42"), true, "Dividers and borders");
    reg("ui.accent", QColor("#007acc"), true, "Focus/Selection");
    reg("ui.error", QColor("#f48771"), true, "Error states");
    reg("ui.sidebar", QColor("#252526"), false, "Sidebar background");
    reg("ui.hoverBackground", QColor("#2a2d2e"), false, "Hover state background");

    // Tab Styling (VS Code-style modern tabs)
    reg("tab.background", QColor("#2d2d30"), false, "Inactive tab background");
    reg("tab.activeBackground", QColor("#1e1e1e"), false, "Active tab background");
    reg("tab.hoverBackground", QColor("#2a2d2e"), false, "Tab hover background");
    reg("tab.border", QColor("#3e3e42"), false, "Tab border color");
    reg("tab.activeBorderTop", QColor("#007acc"), false, "Active tab top accent");
    
    // Inputs
    reg("input.background", QColor("#3c3c3c"), true, "Text fields");
    reg("input.text", QColor("#cccccc"), true, "Input text");
    
    // Schematic (Essential for rendering)
    reg("schematic.background", QColor("#1e1e1e"), false, "Canvas bg (defaults to ui.background)");
    reg("schematic.grid", QColor(60, 60, 60), true, "Grid dots/lines");
    reg("schematic.wire", QColor("#cccccc"), true, "Default wire color");
    reg("schematic.pin", QColor("#cccccc"), true, "Component pins");
    reg("schematic.selection", QColor(0, 122, 204, 100), true, "Selection overlay");
    reg("schematic.component_outline", QColor("#ffffff"), true, "Component body outline");
    reg("schematic.component_fill", QColor("#1e1e1e"), true, "Component body fill");
    reg("schematic.probe_dash", QColor(200, 200, 200), true, "Probe connecting line");
    reg("schematic.ghost", QColor(0, 200, 255), true, "Ghost/Preview color");

    // Table Styling
    reg("table.header", QColor("#252526"), true, "Table header background");
    reg("table.row_selected", QColor("#094771"), true, "Selected row background");
    reg("table.grid", QColor("#333333"), true, "Table grid lines");

    // InlineDialog
    reg("dialog.background", QColor("#252526"), false, "InlineDialog bubble background");
    reg("dialog.border", QColor("#3e3e42"), false, "InlineDialog bubble border");
    reg("dialog.scrim", QColor("#000000"), false, "Scrim overlay color");
    reg("dialog.buttonPrimary", QColor("#007acc"), false, "Primary action button");
    reg("dialog.buttonSecondary", QColor("#3c3c3c"), false, "Secondary action button");

    // Toolbar / Statusbar
    reg("toolbar.background", QColor("#333333"), false, "Toolbar background");
    reg("statusbar.background", QColor("#007acc"), false, "Status bar accent background");

    // Scrollbar
    reg("scrollbar.track", QColor("#1e1e1e"), false, "Scrollbar track background");
    reg("scrollbar.thumb", QColor("#424242"), false, "Scrollbar thumb color");
    reg("scrollbar.thumbHover", QColor("#555555"), false, "Scrollbar thumb hover color");

    // Buttons
    reg("button.background", QColor("#3c3c3c"), false, "Generic button background");
    reg("button.foreground", QColor("#cccccc"), false, "Generic button text");
    reg("button.hoverBackground", QColor("#505050"), false, "Generic button hover background");

    // Trees
    reg("tree.hoverBackground", QColor("#2a2d2e"), false, "Tree item hover background");
    reg("tree.selectedBackground", QColor("#094771"), false, "Tree item selected background");

    // Editor
    reg("editor.background", QColor("#1e1e1e"), false, "Code editor background");
    reg("editor.foreground", QColor("#d4d4d4"), false, "Code editor text color");
    reg("editor.lineNumber", QColor("#858585"), false, "Line number gutter color");
    reg("editor.currentLine", QColor("#282828"), false, "Current line highlight");
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
    QString css;

    // ── Base ────────────────────────────────────────────────────────────────
    css += QString("QWidget { background-color: %1; color: %2; }")
               .arg(getColor("ui.background").name(), getColor("ui.text").name());

    // ── Inputs ──────────────────────────────────────────────────────────────
    css += QString("QLineEdit, QTextEdit, QPlainTextEdit {"
                   "  background-color: %1; color: %2; border: 1px solid %3;"
                   "  border-radius: 3px; padding: 2px 4px; }")
               .arg(getColor("input.background").name(),
                    getColor("input.text").name(),
                    getColor("ui.border").name());

    // ── Dock titles ─────────────────────────────────────────────────────────
    css += QString("QDockWidget::title { background: %1; padding: 4px; }")
               .arg(getColor("ui.header").name());

    // ── Splitter ────────────────────────────────────────────────────────────
    css += QString("QSplitter::handle { background-color: %1; }")
               .arg(getColor("ui.border").name());

    // ── MenuBar / Menu ───────────────────────────────────────────────────────
    css += QString(
        "QMenuBar { background-color: %1; color: %2; border-bottom: 1px solid %3; }"
        "QMenuBar::item { background: transparent; padding: 4px 8px; }"
        "QMenuBar::item:selected { background: %4; border-radius: 3px; }"
        "QMenu { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; }"
        "QMenu::item { padding: 6px 24px 6px 12px; border-radius: 3px; }"
        "QMenu::item:selected { background-color: %4; }"
        "QMenu::separator { height: 1px; background: %3; margin: 3px 0; }")
        .arg(getColor("ui.header").name(), getColor("ui.text").name(),
             getColor("ui.border").name(), getColor("ui.accent").name());

    // ── Toolbar ──────────────────────────────────────────────────────────────
    css += QString(
        "QToolBar { background-color: %1; border: none; spacing: 4px; padding: 2px; }"
        "QToolBar::separator { background: %2; width: 1px; margin: 4px 2px; }")
        .arg(getColor("toolbar.background").name(), getColor("ui.border").name());

    // ── StatusBar ────────────────────────────────────────────────────────────
    css += QString(
        "QStatusBar { background-color: %1; color: #ffffff; border-top: none; }"
        "QStatusBar::item { border: none; }")
        .arg(getColor("statusbar.background").name());

    // ── ScrollBar ────────────────────────────────────────────────────────────
    css += QString(
        "QScrollBar:vertical   { background: %1; width: 8px;  border: none; }"
        "QScrollBar:horizontal { background: %1; height: 8px; border: none; }"
        "QScrollBar::handle:vertical, QScrollBar::handle:horizontal {"
        "  background: %2; border-radius: 4px; min-height: 20px; min-width: 20px; }"
        "QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {"
        "  background: %3; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: none; }")
        .arg(getColor("scrollbar.track").name(),
             getColor("scrollbar.thumb").name(),
             getColor("scrollbar.thumbHover").name());

    // ── ToolTip ──────────────────────────────────────────────────────────────
    css += QString(
        "QToolTip { background-color: %1; color: %2; border: 1px solid %3;"
        "  border-radius: 3px; padding: 4px 6px; }")
        .arg(getColor("ui.header").name(), getColor("ui.text").name(),
             getColor("ui.border").name());

    // ── ComboBox ─────────────────────────────────────────────────────────────
    css += QString(
        "QComboBox { background-color: %1; color: %2; border: 1px solid %3;"
        "  border-radius: 3px; padding: 2px 6px; }"
        "QComboBox::drop-down { border: none; width: 18px; }"
        "QComboBox QAbstractItemView { background: %4; color: %2; border: 1px solid %3;"
        "  selection-background-color: %5; }")
        .arg(getColor("input.background").name(), getColor("input.text").name(),
             getColor("ui.border").name(), getColor("ui.header").name(),
             getColor("tree.selectedBackground").name());

    // ── PushButton ───────────────────────────────────────────────────────────
    css += QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
        "  border-radius: 3px; padding: 4px 10px; }"
        "QPushButton:hover { background-color: %4; }"
        "QPushButton:pressed { background-color: %3; }"
        "QPushButton:disabled { color: %5; }")
        .arg(getColor("button.background").name(), getColor("button.foreground").name(),
             getColor("ui.border").name(), getColor("button.hoverBackground").name(),
             getColor("ui.textDim").name());

    // ── TreeWidget / TreeView ────────────────────────────────────────────────
    css += QString(
        "QTreeWidget, QTreeView { background-color: %1; color: %2; border: none;"
        "  alternate-background-color: %1; }"
        "QTreeWidget::item, QTreeView::item { padding: 3px 2px; }"
        "QTreeWidget::item:hover, QTreeView::item:hover { background-color: %3; }"
        "QTreeWidget::item:selected, QTreeView::item:selected { background-color: %4; }"
        "QHeaderView::section { background: %5; color: %6; border: none;"
        "  padding: 3px 4px; font-size: 10px; }")
        .arg(getColor("ui.background").name(), getColor("ui.text").name(),
             getColor("tree.hoverBackground").name(), getColor("tree.selectedBackground").name(),
             getColor("table.header").name(), getColor("ui.textDim").name());

    // ── TabWidget / TabBar (global baseline — per-widget can override) ────────
    css += QString(
        "QTabWidget::pane { background-color: %1; border: none; border-top: 1px solid %2; }"
        "QTabBar { background-color: %3; border: none; }"
        "QTabBar::tab { background-color: %3; color: %4; border: none;"
        "  padding: 6px 14px; margin-right: 2px; border-top-left-radius: 4px;"
        "  border-top-right-radius: 4px; }"
        "QTabBar::tab:hover { background-color: %5; }"
        "QTabBar::tab:selected { background-color: %1; color: %6;"
        "  border-top: 2px solid %7; }"
        "QTabBar::close-button { subcontrol-position: right; padding: 2px; }"
        "QTabBar::close-button:hover { background: rgba(255,255,255,0.1);"
        "  border-radius: 3px; }")
        .arg(getColor("tab.activeBackground").name(), getColor("tab.border").name(),
             getColor("tab.background").name(), getColor("ui.textDim").name(),
             getColor("tab.hoverBackground").name(), getColor("ui.text").name(),
             getColor("tab.activeBorderTop").name());

    return css;
}

QString ThemeManager::getWidgetStyleSheet(const QString& widgetType) const {
    if (widgetType == "QTableWidget") {
        return QString(R"(
            QTableWidget {
                background: transparent;
                border: none;
                font-size: 11px;
                gridline-color: %1;
            }
            QTableWidget::item {
                padding: 2px 4px;
                border: none;
            }
            QTableWidget::item:selected { background: %2; }
            QHeaderView::section {
                background: %3;
                color: %4;
                border: none;
                padding: 3px 4px;
                font-size: 10px;
            }
        )")
        .arg(getColor("table.grid").name())
        .arg(getColor("table.row_selected").name())
        .arg(getColor("table.header").name())
        .arg(getColor("ui.textDim").name());
    }
    return "";
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
    colors_["schematic.component_outline"] = QColor("#cccccc");
    colors_["schematic.component_fill"] = QColor("#1e1e1e");
    colors_["schematic.probe_dash"] = QColor(200, 200, 200);
    colors_["schematic.ghost"] = QColor(0, 200, 255);
    
    // Tables
    colors_["table.header"] = QColor("#252526");
    colors_["table.row_selected"] = QColor("#094771");
    colors_["table.grid"] = QColor("#333333");

    // Toolbar / Statusbar / Scrollbar / Buttons / Trees / Editor
    colors_["toolbar.background"] = QColor("#333333");
    colors_["statusbar.background"] = QColor("#007acc");
    colors_["scrollbar.track"] = QColor("#1e1e1e");
    colors_["scrollbar.thumb"] = QColor("#424242");
    colors_["scrollbar.thumbHover"] = QColor("#555555");
    colors_["button.background"] = QColor("#3c3c3c");
    colors_["button.foreground"] = QColor("#cccccc");
    colors_["button.hoverBackground"] = QColor("#505050");
    colors_["tree.hoverBackground"] = QColor("#2a2d2e");
    colors_["tree.selectedBackground"] = QColor("#094771");
    colors_["editor.background"] = QColor("#1e1e1e");
    colors_["editor.foreground"] = QColor("#d4d4d4");
    colors_["editor.lineNumber"] = QColor("#858585");
    colors_["editor.currentLine"] = QColor("#282828");

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

    // Schematic - Light
    colors_["schematic.component_outline"] = QColor("#333333");
    colors_["schematic.component_fill"] = QColor("#ffffff");
    colors_["schematic.wire"] = QColor("#333333");
    colors_["schematic.pin"] = QColor("#333333");
    colors_["schematic.probe_dash"] = QColor("#666666");

    // Tables - Light
    colors_["table.header"] = QColor("#f3f3f3");
    colors_["table.row_selected"] = QColor("#0060c0");
    colors_["table.grid"] = QColor("#e0e0e0");

    // Toolbar / Statusbar / Scrollbar / Buttons / Trees / Editor (Light)
    colors_["toolbar.background"] = QColor("#f0f0f0");
    colors_["statusbar.background"] = QColor("#007acc");
    colors_["scrollbar.track"] = QColor("#f0f0f0");
    colors_["scrollbar.thumb"] = QColor("#c0c0c0");
    colors_["scrollbar.thumbHover"] = QColor("#a0a0a0");
    colors_["button.background"] = QColor("#e8e8e8");
    colors_["button.foreground"] = QColor("#333333");
    colors_["button.hoverBackground"] = QColor("#d4d4d4");
    colors_["tree.hoverBackground"] = QColor("#e8e8e8");
    colors_["tree.selectedBackground"] = QColor("#0060c0");
    colors_["editor.background"] = QColor("#ffffff");
    colors_["editor.foreground"] = QColor("#333333");
    colors_["editor.lineNumber"] = QColor("#999999");
    colors_["editor.currentLine"] = QColor("#f5f5f5");
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
