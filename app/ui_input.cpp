// The input layer: testing the window as a person uses it. ui_locate says where a control is on the screen; ui_input delivers mouse
// and keyboard through Qt's window-system queue, where the platform's own events enter, so hit testing, menus, focus, shortcuts and
// modal dialogs all run for real; tools/os_input does the same from outside the process with the OS's events (tools/ui_test.py).
#include "app/canvas.h"
#include "app/layout_viewer.h"
#include "app/shell.h"
#include "app/ui_commands.h"
#include "app/view_editor.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace app {

namespace {

[[noreturn]] void refuse(const QString& why) { throw std::runtime_error(why.toStdString()); }

QString plain(QString text) {
    text.remove('&');
    return text.section('\t', 0, 0).trimmed();
}

QJsonObject rectJson(const QRect& r) {
    return {{"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()}, {"cx", r.center().x()}, {"cy", r.center().y()}};
}

QJsonObject globalRect(QWidget* w, const QRect& local) {
    if (!w->isVisible()) refuse("it is not on screen");
    return rectJson(QRect(w->mapToGlobal(local.topLeft()), local.size()));
}

// The window that receives input at a global point, as the platform decides: an open menu first, then the window on top
QWidget* windowAt(const QPoint& at) {
    if (QWidget* popup = QApplication::activePopupWidget()) return popup;
    if (QWidget* modal = QApplication::activeModalWidget()) {
        if (modal->frameGeometry().contains(at)) return modal;
        return modal;
    }
    if (QWidget* top = QApplication::topLevelAt(at)) return top;
    return QApplication::activeWindow();
}

Qt::MouseButton buttonNamed(const QString& name) {
    if (name.isEmpty() || name == "left") return Qt::LeftButton;
    if (name == "right") return Qt::RightButton;
    if (name == "middle") return Qt::MiddleButton;
    refuse("a button is left, right or middle");
}

Qt::KeyboardModifiers modifiersNamed(const QJsonArray& names) {
    Qt::KeyboardModifiers out;
    for (const auto& v : names) {
        const QString n = v.toString();
        if (n == "shift") out |= Qt::ShiftModifier;
        else if (n == "ctrl") out |= Qt::ControlModifier;
        else if (n == "alt") out |= Qt::AltModifier;
        else if (n == "meta") out |= Qt::MetaModifier;
        else refuse("a modifier is shift, ctrl, alt or meta");
    }
    return out;
}

int g_pending = 0;  // input scheduled and not yet delivered
int g_dropped = 0;  // input with no window to go to: a test reads this, or it would pass on nothing

// Where the platform sends keys: an open menu, else the window with the focus
QWindow* keyWindow() {
    if (QWidget* popup = QApplication::activePopupWidget())
        if (popup->windowHandle()) return popup->windowHandle();
    if (QWindow* w = QGuiApplication::focusWindow()) return w;
    // A platform with no window manager (offscreen) does not give focus back when a menu closes: the main window has it
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
        for (QWidget* top : QApplication::topLevelWidgets())
            if (top->isVisible() && top->windowHandle() && qobject_cast<Shell*>(top)) {
                QWindowSystemInterface::handleFocusWindowChanged(top->windowHandle(), Qt::ActiveWindowFocusReason);
                QWindowSystemInterface::flushWindowSystemEvents();
                return top->windowHandle();
            }
    ++g_dropped;
    return nullptr;
}

// Later, not now: an event may open a modal dialog, whose loop must not hold up this command's reply
void deliver(std::function<void()> send) {
    ++g_pending;
    QTimer::singleShot(0, qApp, [send = std::move(send)] {
        send();
        --g_pending;
    });
}

// Each event a later time than the last: hands take time, and Qt does not call two presses in the same millisecond a double click
ulong inputTime() {
    static QElapsedTimer clock = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    static ulong last = 0;
    last = std::max<ulong>(last + 1, (ulong)clock.elapsed());
    return last;
}

void mouse(const QPoint& at, Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type, Qt::KeyboardModifiers mods) {
    deliver([=] {
        QWidget* w = windowAt(at);
        if (!w || !w->window()->windowHandle()) {
            ++g_dropped;
            return;
        }
        QWindow* window = w->window()->windowHandle();
        QWindowSystemInterface::handleMouseEvent(window, inputTime(), QPointF(window->mapFromGlobal(at)), QPointF(at), state, button, type, mods);
    });
}

void clickAt(const QPoint& at, Qt::MouseButton button, Qt::KeyboardModifiers mods, bool twice) {
    mouse(at, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, mods);
    for (int k = 0; k < (twice ? 2 : 1); ++k) {
        mouse(at, button, button, QEvent::MouseButtonPress, mods);
        mouse(at, Qt::NoButton, button, QEvent::MouseButtonRelease, mods);
    }
}

}  // namespace

void addInputCommands(Session& session, Shell& shell) {
    Session* s = &session;
    Shell* sh = &shell;
    const auto add = [s](cmd::Command c) {
        c.readOnly = true;
        s->addCommand(std::move(c));
    };

    add({"ui_locate", "Where a control is on the screen, in global points, with its centre: kind rail (text a stage), console, menu (a menu bar title), menu_item (in the open menu), "
                      "button, tab, tab_close, tree_item (any visible list's row holding text), layout_point (x, y database units in the active layout), or editor_point (in the active schematic or symbol).",
         cmd::object({{"kind", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"window", "rail", "console", "menu", "menu_item", "button", "tab", "tab_close", "tree_item", "layout_point", "editor_point"}}, {"description", "what to find"}}}},
                     {{"text", cmd::param("string", "its text")}, {"x", cmd::param("number", "x, database units")}, {"y", cmd::param("number", "y, database units")}}),
         [sh](cmd::Context&, const QJsonObject& a) {
             const QString kind = a["kind"].toString(), text = a["text"].toString();
             QWidget* top = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : sh;
             if (kind == "window") return QJsonValue(globalRect(sh, sh->rect()));
             if (kind == "rail") {
                 QWidget* b = sh->railButton(text);
                 if (!b) refuse("no stage '" + text + "' on the rail");
                 return QJsonValue(globalRect(b, b->rect()));
             }
             if (kind == "console") return QJsonValue(globalRect(sh->consoleRailButton(), sh->consoleRailButton()->rect()));
             if (kind == "menu") {
                 QMenuBar* bar = sh->menuBar();
                 if (bar->isNativeMenuBar()) refuse("the system's menu bar has no rectangle here: start the app with --window-menus");
                 for (QAction* act : bar->actions())
                     if (plain(act->text()) == text && !bar->actionGeometry(act).isEmpty()) return QJsonValue(globalRect(bar, bar->actionGeometry(act)));
                 refuse("no menu '" + text + "'");
             }
             if (kind == "menu_item") {
                 auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                 if (!menu) refuse("no menu is open");
                 for (QAction* act : menu->actions())
                     if (plain(act->text()) == text && act->isVisible()) {
                         if (!act->isEnabled()) refuse("'" + text + "' is disabled");
                         return QJsonValue(globalRect(menu, menu->actionGeometry(act)));
                     }
                 refuse("no item '" + text + "' in the open menu");
             }
             if (kind == "button") {
                 for (QPushButton* b : top->findChildren<QPushButton*>())
                     if (b->isVisible() && plain(b->text()) == text) return QJsonValue(globalRect(b, b->rect()));
                 refuse("no visible button '" + text + "'");
             }
             if (kind == "tab" || kind == "tab_close") {
                 QTabBar* bar = sh->tabs()->tabBar();
                 for (int k = 0; k < bar->count(); ++k) {
                     if (bar->tabText(k) != text) continue;
                     if (kind == "tab") return QJsonValue(globalRect(bar, bar->tabRect(k)));
                     QWidget* close = bar->tabButton(k, QTabBar::RightSide);
                     if (!close) refuse("that tab has no close button");
                     return QJsonValue(globalRect(close, close->rect()));
                 }
                 refuse("no tab '" + text + "'");
             }
             if (kind == "tree_item") {
                 for (QTreeWidget* tree : top->findChildren<QTreeWidget*>()) {
                     if (!tree->isVisible()) continue;
                     const auto found = tree->findItems(text, Qt::MatchContains | Qt::MatchRecursive);
                     for (QTreeWidgetItem* i : found) {
                         tree->scrollToItem(i);
                         const QRect r = tree->visualItemRect(i);
                         if (!r.isEmpty()) return QJsonValue(globalRect(tree->viewport(), r));
                     }
                 }
                 refuse("no visible row holds '" + text + "'");
             }
             if (kind == "layout_point") {
                 auto* v = qobject_cast<LayoutViewer*>(sh->current());
                 if (!v) refuse("the active tab is not a layout");
                 const QPointF at = v->canvas()->toScreen({a["x"].toDouble() - v->flat().originX, a["y"].toDouble() - v->flat().originY});
                 return QJsonValue(globalRect(v->canvas(), QRect(at.toPoint(), QSize(1, 1))));
             }
             auto* e = qobject_cast<ViewEditor*>(sh->current());
             if (!e) refuse("the active tab is not a schematic or symbol");
             const QPointF at = e->canvas()->toScreen({a["x"].toDouble(), a["y"].toDouble()});
             return QJsonValue(globalRect(e->canvas(), QRect(at.toPoint(), QSize(1, 1))));
         }});

    add({"ui_input", "Delivers input through the window system's queue, as the platform does, at global points (ui_locate): action move, press, release, click, double_click, drag (to x2, y2), wheel (delta) or key (key as in a shortcut, \\\"Ctrl+K\\\" or \\\"Esc\\\"; text types). It returns at once: poll ui_input_pending.",
         cmd::object({{"action", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"move", "press", "release", "click", "double_click", "drag", "wheel", "key", "type"}}, {"description", "what to do"}}}},
                     {{"x", cmd::param("number", "global x")}, {"y", cmd::param("number", "global y")}, {"x2", cmd::param("number", "a drag's end")}, {"y2", cmd::param("number", "a drag's end")},
                      {"button", cmd::param("string", "left (default), right or middle")}, {"modifiers", QJsonObject{{"type", "array"}, {"description", "shift, ctrl, alt, meta"}, {"items", QJsonObject{{"type", "string"}}}}},
                      {"delta", cmd::param("number", "a wheel's angle: 120 is one notch away from you")}, {"key", cmd::param("string", "a key, as in a shortcut")}, {"text", cmd::param("string", "what to type")}}),
         [](cmd::Context&, const QJsonObject& a) {
             const QString action = a["action"].toString();
             const QPoint at((int)std::lround(a["x"].toDouble()), (int)std::lround(a["y"].toDouble()));
             const Qt::MouseButton button = buttonNamed(a["button"].toString());
             const Qt::KeyboardModifiers mods = modifiersNamed(a["modifiers"].toArray());
             if (action == "move") {
                 mouse(at, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, mods);
             } else if (action == "press") {
                 mouse(at, button, button, QEvent::MouseButtonPress, mods);
             } else if (action == "release") {
                 mouse(at, Qt::NoButton, button, QEvent::MouseButtonRelease, mods);
             } else if (action == "click" || action == "double_click") {
                 clickAt(at, button, mods, action == "double_click");
             } else if (action == "drag") {
                 const QPoint to((int)std::lround(a["x2"].toDouble()), (int)std::lround(a["y2"].toDouble()));
                 mouse(at, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, mods);
                 mouse(at, button, button, QEvent::MouseButtonPress, mods);
                 for (int k = 1; k <= 8; ++k) mouse(at + (to - at) * k / 8, button, Qt::NoButton, QEvent::MouseMove, mods);
                 mouse(to, Qt::NoButton, button, QEvent::MouseButtonRelease, mods);
             } else if (action == "wheel") {
                 const int delta = a["delta"].toInt(120);
                 deliver([=] {
                     QWidget* w = windowAt(at);
                     if (!w || !w->window()->windowHandle()) return;
                     QWindow* window = w->window()->windowHandle();
                     QWindowSystemInterface::handleWheelEvent(window, QPointF(window->mapFromGlobal(at)), QPointF(at), QPoint(0, 2 * delta), QPoint(0, delta), mods);  // as macOS sends a mouse wheel: a pixel delta too, no phase
                 });
             } else if (action == "key") {
                 const QKeySequence seq(a["key"].toString(), QKeySequence::PortableText);
                 if (seq.isEmpty()) refuse("no key '" + a["key"].toString() + "'");
                 const QKeyCombination combo = seq[0];
                 const int key = (int)combo.key();
                 const Qt::KeyboardModifiers m = combo.keyboardModifiers();
                 deliver([=] {
                     QWindow* target = keyWindow();
                     if (!target) return;
                     const QString text = m & ~Qt::ShiftModifier ? QString() : (key >= 0x20 && key < 0x7f ? QString(QChar(key)).toLower() : QString());
                     // In the order a platform plugin keeps: a shortcut first, the key itself only if none took it
                     if (QWindowSystemInterface::handleShortcutEvent(target, 0, key, m, 0, 0, 0, text)) return;
                     QWindowSystemInterface::handleKeyEvent(target, QEvent::KeyPress, key, m, text);
                     QWindowSystemInterface::handleKeyEvent(target, QEvent::KeyRelease, key, m, text);
                 });
             } else if (action == "type") {
                 for (const QChar c : a["text"].toString())
                     deliver([c] {
                         QWindow* target = keyWindow();
                         if (!target) return;
                         QWindowSystemInterface::handleKeyEvent(target, QEvent::KeyPress, c.toUpper().unicode(), c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier, QString(c));
                         QWindowSystemInterface::handleKeyEvent(target, QEvent::KeyRelease, c.toUpper().unicode(), c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier, QString(c));
                     });
             }
             return QJsonValue(g_pending);
         }});
    add({"ui_input_pending", "How much input has not been delivered yet: 0 when everything sent has been handled.", cmd::object({}),
         [](cmd::Context&, const QJsonObject&) { return QJsonValue(g_pending + (QWindowSystemInterface::windowSystemEventsQueued() ? 1 : 0)); }});
    add({"ui_input_dropped", "How many inputs found no window to go to since the app started.", cmd::object({}),
         [](cmd::Context&, const QJsonObject&) { return QJsonValue(g_dropped); }});
    add({"ui_pixel", "The colour on screen at a global point of the window, as #rrggbb: what was really drawn. offscreen platforms have no GPU canvas to read.", cmd::object({{"x", cmd::param("number", "global x")}, {"y", cmd::param("number", "global y")}}),
         [sh](cmd::Context&, const QJsonObject& a) {
             const QImage image = sh->grab().toImage();
             const QPoint local = sh->mapFromGlobal(QPoint((int)a["x"].toDouble(), (int)a["y"].toDouble()));
             const qreal ratio = image.devicePixelRatio();
             const QPoint at((int)(local.x() * ratio), (int)(local.y() * ratio));
             if (!image.rect().contains(at)) refuse("that point is outside the window");
             return QJsonValue(image.pixelColor(at).name());
         }});
    add({"ui_raise", "Brings the window to the front, for input from outside the app.", cmd::object({}),
         [sh](cmd::Context&, const QJsonObject&) {
             sh->show();
             sh->raise();
             sh->activateWindow();
             // Input from the queue does not activate a window as the platform's would: say it has the focus, as the platform does
             if (sh->windowHandle()) QWindowSystemInterface::handleFocusWindowChanged(sh->windowHandle(), Qt::ActiveWindowFocusReason);
             return QJsonValue(true);
         }});
}

}  // namespace app
