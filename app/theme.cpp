#include "app/theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

namespace app {

namespace {
Theme g_theme;
Theme::Mode g_mode = Theme::Mode::Auto;

Theme tokens(bool dark) {
    Theme t;
    t.dark = dark;
    if (dark) {
        t.window = QColor("#1b2027");  t.panel = QColor("#222831");  t.field = QColor("#171b21");
        t.raised = QColor("#2c333e");  t.line = QColor("#38404c");   t.ink = QColor("#dde3ea");
        t.muted = QColor("#8d98a6");   t.via = QColor("#4fb6c8");    t.viaInk = QColor("#08242a");
        t.pass = QColor("#5bb98b");    t.warn = QColor("#d9a441");   t.fail = QColor("#e5636a");
        t.canvas = QColor("#14181e");  t.canvasGrid = QColor("#2a313b");  t.canvasInk = QColor("#c9d2dc");
    } else {
        t.window = QColor("#e9edf1");  t.panel = QColor("#f4f6f8");  t.field = QColor("#ffffff");
        t.raised = QColor("#dde2e8");  t.line = QColor("#c6cdd5");   t.ink = QColor("#1c232b");
        t.muted = QColor("#5d6875");   t.via = QColor("#0f7f93");    t.viaInk = QColor("#ffffff");
        t.pass = QColor("#1f8a58");    t.warn = QColor("#a87410");   t.fail = QColor("#c23a42");
        t.canvas = QColor("#fbfcfd");  t.canvasGrid = QColor("#dfe4ea");  t.canvasInk = QColor("#27303a");
    }
    return t;
}

QPalette paletteOf(const Theme& t) {
    QPalette p;
    p.setColor(QPalette::Window, t.window);
    p.setColor(QPalette::WindowText, t.ink);
    p.setColor(QPalette::Base, t.field);
    p.setColor(QPalette::AlternateBase, t.panel);
    p.setColor(QPalette::Text, t.ink);
    p.setColor(QPalette::PlaceholderText, t.muted);
    p.setColor(QPalette::Button, t.raised);
    p.setColor(QPalette::ButtonText, t.ink);
    p.setColor(QPalette::BrightText, t.fail);
    p.setColor(QPalette::Highlight, t.via);
    p.setColor(QPalette::HighlightedText, t.viaInk);
    p.setColor(QPalette::ToolTipBase, t.raised);
    p.setColor(QPalette::ToolTipText, t.ink);
    p.setColor(QPalette::Link, t.via);
    p.setColor(QPalette::Mid, t.line);
    p.setColor(QPalette::Dark, t.line);
    p.setColor(QPalette::Midlight, t.raised);
    p.setColor(QPalette::Light, t.raised);
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, t.muted);
    return p;
}

QString styleSheetOf(const Theme& t) {
    QString s = R"(
QMainWindow, QDialog { background: @window; }
QMainWindow::separator { background: @line; width: 1px; height: 1px; }
QToolTip { background: @raised; color: @ink; border: 1px solid @line; padding: 3px 5px; }
QStatusBar { background: @panel; color: @muted; border-top: 1px solid @line; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @muted; }
QMenuBar { background: @panel; color: @ink; border-bottom: 1px solid @line; }
QMenuBar::item { background: transparent; padding: 4px 9px; }
QMenuBar::item:selected { background: @raised; }
QMenu { background: @panel; color: @ink; border: 1px solid @line; padding: 4px 0; }
QMenu::item { padding: 4px 22px 4px 14px; }
QMenu::item:selected { background: @via; color: @viaInk; }
QMenu::separator { height: 1px; background: @line; margin: 4px 0; }
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: @field; color: @ink; border: 1px solid @line; border-radius: 3px; padding: 3px 5px;
    selection-background-color: @via; selection-color: @viaInk; }
QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border-color: @via; }
QComboBox QAbstractItemView { background: @panel; border: 1px solid @line; selection-background-color: @via; selection-color: @viaInk; }
QPushButton { background: @raised; color: @ink; border: 1px solid @line; border-radius: 3px; padding: 4px 12px; }
QPushButton:hover { border-color: @muted; }
QPushButton:pressed { background: @line; }
QPushButton:default { background: @via; color: @viaInk; border-color: @via; }
QPushButton:focus { border-color: @via; }
QTableWidget, QTableView, QListWidget, QTreeWidget, QTreeView {
    background: @field; alternate-background-color: @panel; color: @ink; border: none; gridline-color: @line;
    selection-background-color: @via; selection-color: @viaInk; outline: 0; }
QTreeView::item, QListView::item { padding: 2px 0; }
QHeaderView::section { background: @panel; color: @muted; border: none; border-right: 1px solid @line;
    border-bottom: 1px solid @line; padding: 3px 6px; }
QTableCornerButton::section { background: @panel; border: none; }
QTabWidget::pane { border: none; }
QTabBar { background: @panel; qproperty-drawBase: 0; }
QTabBar::tab { background: @panel; color: @muted; padding: 6px 12px; border-right: 1px solid @line;
    border-bottom: 1px solid @line; }
QTabBar::tab:selected { background: @window; color: @ink; border-top: 2px solid @via; border-bottom: 1px solid @window; }
QTabBar::tab:hover:!selected { color: @ink; }
QDockWidget { color: @muted; }
QDockWidget::title { background: @panel; padding: 4px 8px; border-bottom: 1px solid @line; }
QSplitter::handle { background: @line; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle { background: @line; border-radius: 4px; min-height: 24px; min-width: 24px; margin: 1px; }
QScrollBar::handle:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; height: 0; width: 0; }
QCheckBox { color: @ink; }
QLabel { color: @ink; }
QLabel[role="muted"] { color: @muted; }
QLabel[role="title"] { font-size: 20px; font-weight: 600; }
QLabel[role="heading"] { font-weight: 600; }
QWidget#rail { background: @panel; border-right: 1px solid @line; }
QToolButton#railButton { background: transparent; border: none; border-left: 2px solid transparent; padding: 9px 0; }
QToolButton#railButton:hover { background: @raised; }
QToolButton#railButton:checked { background: @window; border-left: 2px solid @via; }
QWidget#navigator { background: @panel; }
QWidget#navigator QTreeWidget { background: @panel; }
QFrame#palette { background: @panel; border: 1px solid @muted; border-radius: 6px; }
QFrame#palette QLineEdit { border: none; border-bottom: 1px solid @line; border-radius: 0; background: transparent; padding: 9px 12px; font-size: 15px; }
QFrame#palette QListWidget { background: transparent; }
QFrame#palette QListWidget::item { padding: 5px 12px; }
QPushButton#linkButton { background: transparent; border: none; color: @via; text-align: left; padding: 3px 0; }
QPushButton#linkButton:hover { text-decoration: underline; }
QToolButton#tabClose { background: transparent; border: none; border-radius: 3px; padding: 1px; }
QToolButton#tabClose:hover { background: @raised; }
QToolButton#searchButton { background: @field; color: @muted; border: 1px solid @line; border-radius: 3px; padding: 3px 10px; }
QToolButton#searchButton:hover { color: @ink; border-color: @muted; }
)";
    const std::pair<const char*, QColor> names[] = {
        {"@window", t.window}, {"@panel", t.panel}, {"@field", t.field}, {"@raised", t.raised}, {"@line", t.line},
        {"@ink", t.ink}, {"@muted", t.muted}, {"@viaInk", t.viaInk}, {"@via", t.via}};  // @viaInk before @via
    for (const auto& [name, color] : names) s.replace(name, color.name());
    return s;
}

bool osIsDark() {
    return qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}
}  // namespace

const Theme& Theme::current() {
    if (!g_theme.window.isValid()) g_theme = tokens(true);
    return g_theme;
}

Theme::Mode Theme::mode() { return g_mode; }

QString Theme::modeName(Mode m) { return m == Mode::Light ? "light" : m == Mode::Dark ? "dark" : "auto"; }

bool Theme::parseMode(const QString& name, Mode* out) {
    if (name == "auto") *out = Mode::Auto;
    else if (name == "light") *out = Mode::Light;
    else if (name == "dark") *out = Mode::Dark;
    else return false;
    return true;
}

QObject* Theme::notifier() {
    static ThemeNotifier n;
    return &n;
}

void Theme::apply(QApplication& a, Mode m) {
    static const bool once = [&a] {
        a.setStyle(QStyleFactory::create("Fusion"));  // draws from the palette on every platform
        QObject::connect(a.styleHints(), &QStyleHints::colorSchemeChanged, &a, [&a] {
            if (g_mode == Mode::Auto) apply(a, Mode::Auto);
        });
        return true;
    }();
    (void)once;
    g_mode = m;
    g_theme = tokens(m == Mode::Dark || (m == Mode::Auto && osIsDark()));
    a.setPalette(paletteOf(g_theme));
    a.setStyleSheet(styleSheetOf(g_theme));
    emit static_cast<ThemeNotifier*>(notifier())->changed();
}

}  // namespace app
