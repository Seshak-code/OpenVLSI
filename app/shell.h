#pragma once
// The one window: a rail of the design's stages, their navigators, the open tools in tabs, and the command window below.
// Tools are the same widgets the rest of the app uses; the shell hosts them through Ciw::setHost, and merges the active
// tool's menus into the menu bar, so only that tool's keys are live. It adds a command palette over the registry and
// keeps the commands a script needs to drive it (ui_theme, ui_tabs, ui_show).
#include "app/ciw.h"
#include "app/theme.h"

#include <QMainWindow>

class QLabel;
class QMenu;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QVBoxLayout;

namespace app {

class CommandForm;
class CommandPalette;
class ImplementPanel;
class TestPanel;
class VerifyPanel;
class Navigator;

class Shell : public QMainWindow {
    Q_OBJECT
public:
    explicit Shell(Session& session, QWidget* parent = nullptr);
    Ciw* ciw() const { return ciw_; }
    QTabWidget* tabs() const { return tabs_; }
    Navigator* navigator() const { return design_; }
    Navigator* simulations() const { return simulate_; }
    ImplementPanel* implementPanel() const { return implement_; }
    TestPanel* testPanel() const { return test_; }
    VerifyPanel* verifyPanel() const { return verify_; }
    QWidget* current() const;              // the active tool; null on the start page
    QStringList tabTitles() const;
    QMenu* themeMenu() const { return themeMenu_; }
    CommandPalette* newPalette();          // what Cmd+K shows
    bool runForm(const QString& command, const QJsonObject& prefill = {});  // no parameters: runs it; else its form
    void showConsole(bool on);
    bool consoleVisible() const;
    void showStage(int stage);             // 0 design, 1 simulate, 2 implement, 3 verify, 4 test
    QStringList recentViews() const;
    QStringList menuTitles() const;

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void host(QWidget* tool);
    void adopt(QWidget* tool);
    void closeTab(int index);
    void rebuildMenus();
    void refreshIcons();
    void refreshStart();
    void updateCrumb();
    void openPalette();
    void noteRecent(const QString& path);
    QJsonObject context() const;  // the selection, for prefilling a form

    Session& session_;
    Ciw* ciw_;
    QTabWidget* tabs_;
    QStackedWidget* panels_;
    Navigator *design_, *simulate_;
    ImplementPanel* implement_ = nullptr;
    TestPanel* test_ = nullptr;
    VerifyPanel* verify_ = nullptr;
    QSplitter *outer_, *inner_;
    QWidget* start_;
    QVBoxLayout* recentList_;
    QLabel *crumb_, *startWorkspace_;
    std::vector<QToolButton*> stages_;
    QToolButton *consoleButton_, *search_;
    QMenu *fileMenu_, *viewMenu_, *goMenu_, *themeMenu_;
};

}  // namespace app
