#include "app/shell.h"

#include "app/ade_explorer.h"
#include "app/icons.h"
#include "app/implement.h"
#include "app/layout_viewer.h"
#include "app/rtl_stage.h"
#include "app/test_stage.h"
#include "app/api_server.h"
#include "app/ui_commands.h"
#include "app/verify_stage.h"
#include "app/verifier.h"
#include "app/library_manager.h"
#include "app/navigator.h"
#include "app/palette.h"
#include "app/view_editor.h"
#include "app/waveform_viewer.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace app {

namespace {
QString iconOf(QWidget* tool) {
    if (auto* e = qobject_cast<ViewEditor*>(tool)) return e->isSchematic() ? "schematic" : "symbol";
    if (qobject_cast<AdeExplorer*>(tool)) return "maestro";
    if (qobject_cast<WaveformViewer*>(tool)) return "waveform";
    if (qobject_cast<LayoutViewer*>(tool)) return "layout";
    if (qobject_cast<Verifier*>(tool)) return "verifier";
    if (qobject_cast<FlowRunView*>(tool)) return "implement";
    if (qobject_cast<TestRunView*>(tool)) return "test";
    if (qobject_cast<SignoffView*>(tool)) return "verify";
    if (qobject_cast<RtlEditor*>(tool)) return "rtl";
    return "view";
}

QString tabTitle(QWidget* tool) {
    if (auto* e = qobject_cast<ViewEditor*>(tool)) return e->cell() + " " + e->view();
    if (auto* a = qobject_cast<AdeExplorer*>(tool)) return a->cell() + " " + a->view();
    if (auto* w = qobject_cast<WaveformViewer*>(tool)) return QFileInfo(w->run()).fileName();
    if (auto* l = qobject_cast<LayoutViewer*>(tool)) return l->cell() + " " + l->view();
    if (auto* r = qobject_cast<Verifier*>(tool)) return r->cell() + " " + r->view();
    if (auto* f = qobject_cast<FlowRunView*>(tool)) return f->run().replace('/', ' ');
    if (auto* v = qobject_cast<TestRunView*>(tool)) return v->run().replace('/', ' ');
    if (auto* e = qobject_cast<RtlEditor*>(tool)) return QFileInfo(e->file()).fileName();
    if (auto* o = qobject_cast<SignoffView*>(tool)) return o->run().section('/', 0, 0) + (o->kind() == SignoffView::Kind::Timing ? " timing" : " power");
    return tool->windowTitle();
}

QString crumbOf(QWidget* tool) {
    if (auto* e = qobject_cast<ViewEditor*>(tool)) return e->library() + "  ›  " + e->cell() + "  ›  " + e->view();
    if (auto* a = qobject_cast<AdeExplorer*>(tool)) return a->library() + "  ›  " + a->cell() + "  ›  " + a->view();
    if (auto* w = qobject_cast<WaveformViewer*>(tool)) return (w->design().isEmpty() ? QString() : w->design() + "  ›  ") + QFileInfo(w->run()).fileName();
    if (auto* l = qobject_cast<LayoutViewer*>(tool)) return l->library() + "  ›  " + l->cell() + "  ›  " + l->view();
    if (auto* r = qobject_cast<Verifier*>(tool)) return r->library() + "  ›  " + r->cell() + "  ›  " + r->view();
    if (auto* f = qobject_cast<FlowRunView*>(tool)) return "Implement  ›  " + f->design() + "  ›  " + f->run().section('/', 1);
    if (auto* v = qobject_cast<TestRunView*>(tool)) return "Test  ›  " + v->run().section('/', 0, 0) + "  ›  " + v->run().section('/', 1);
    if (auto* e = qobject_cast<RtlEditor*>(tool)) return "RTL  ›  " + (e->design().isEmpty() ? QString() : e->design() + "  ›  ") + QFileInfo(e->file()).fileName();
    if (auto* o = qobject_cast<SignoffView*>(tool)) return "Sign-off  ›  " + o->run().section('/', 0, 0) + "  ›  " + o->run().section('/', 1) + (o->kind() == SignoffView::Kind::Timing ? "  ›  timing" : "  ›  power");
    return {};
}
}  // namespace

Shell::Shell(Session& session, QWidget* parent) : QMainWindow(parent), session_(session) {
    ciw_ = new Ciw(session_);
    ciw_->setWindowFlags(Qt::Widget);
    ciw_->menuBar()->setNativeMenuBar(false);
    ciw_->menuBar()->hide();
    ciw_->setHost([this](QWidget* tool) { host(tool); });

    // The rail: the stages of a design, in the order a chip goes through them
    auto* rail = new QWidget(this);
    rail->setObjectName("rail");
    rail->setAttribute(Qt::WA_StyledBackground);
    rail->setFixedWidth(46);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(0, 6, 0, 6);
    railLayout->setSpacing(0);
    const std::pair<QString, QString> stages[] = {{"design", "Design: libraries, cells and views"},
                                                  {"rtl", "RTL: digital designs and their Verilog"},
                                                  {"maestro", "Simulate: tests and their runs"},
                                                  {"implement", "Implement: Verilog to GDS"},
                                                  {"verify", "Verify: DRC and LVS"},
                                                  {"test", "Test: the chip under its testbenches and firmware"}};
    for (int k = 0; k < (int)std::size(stages); ++k) {
        auto* b = new QToolButton(rail);
        b->setObjectName("railButton");
        b->setCheckable(true);
        b->setIconSize({24, 24});
        b->setToolTip(stages[k].second);
        b->setProperty("glyph", stages[k].first);
        b->setFixedWidth(46);
        connect(b, &QToolButton::clicked, this, [this, k] { showStage(k); });
        railLayout->addWidget(b);
        stages_.push_back(b);
    }
    railLayout->addStretch(1);
    consoleButton_ = new QToolButton(rail);
    consoleButton_->setObjectName("railButton");
    consoleButton_->setCheckable(true);
    consoleButton_->setIconSize({24, 24});
    consoleButton_->setToolTip("Command window (Ctrl+J)");
    consoleButton_->setProperty("glyph", "console");
    consoleButton_->setFixedWidth(46);
    connect(consoleButton_, &QToolButton::clicked, this, [this](bool on) { showConsole(on); });
    railLayout->addWidget(consoleButton_);

    const auto open = [this](const QString& lib, const QString& cell, const QString& view, const QString& history) {
        const cmd::Result r = session_.call("editor_open", {{"library", lib}, {"cell", cell}, {"view", view}});
        if (!r.ok) {
            ciw_->runLine("editor_open " + QString::fromUtf8(QJsonDocument(QJsonObject{{"library", lib}, {"cell", cell}, {"view", view}}).toJson(QJsonDocument::Compact)));
            showConsole(true);
            return;
        }
        if (!history.isEmpty())
            if (auto* a = ciw_->explorer(lib, cell, view)) a->select(history);
    };
    const auto form = [this](const QString& c, const QJsonObject& p) { runForm(c, p); };
    panels_ = new QStackedWidget(this);
    design_ = new Navigator(session_, Navigator::Mode::Design, {open, form}, panels_);
    simulate_ = new Navigator(session_, Navigator::Mode::Simulate, {open, form}, panels_);
    panels_->addWidget(design_);
    rtl_ = new RtlPanel(session_, {[this](const QString& file, const QString& design) { session_.call("rtl_open", {{"file", file}, {"design", design}}); },
                                   [this] { runForm("rtl_design_set", {}); }}, panels_);
    panels_->addWidget(rtl_);
    panels_->addWidget(simulate_);
    implement_ = new ImplementPanel(session_, {[this](const QString& run) {
        session_.call("flow_view", {{"run", run}});
    }}, panels_);
    panels_->addWidget(implement_);
    verify_ = new VerifyPanel(session_, {[this](const QString& command) { runForm(command, context()); }}, panels_);
    panels_->addWidget(verify_);
    test_ = new TestPanel(session_, {[this](const QString& run) { session_.call("test_view", {{"run", run}}); }, [this] { runForm("test_shmoo", {}); }}, panels_);
    panels_->addWidget(test_);

    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setElideMode(Qt::ElideMiddle);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &Shell::closeTab);

    // The start page: where a session begins, and what an empty workspace needs next
    start_ = new QWidget(tabs_);
    auto* startOuter = new QHBoxLayout(start_);
    startOuter->setContentsMargins(56, 48, 56, 48);
    auto* startColumn = new QVBoxLayout;
    startColumn->setSpacing(4);
    auto heading = [&](const QString& text, const char* role) {
        auto* l = new QLabel(text, start_);
        l->setProperty("role", role);
        startColumn->addWidget(l);
        return l;
    };
    heading("OpenVLSI", "title");
    startWorkspace_ = heading(QString(), "muted");
    startWorkspace_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    startColumn->addSpacing(24);
    heading("Start", "heading");
    const std::pair<QString, std::function<void()>> starts[] = {
        {"Create a library", [this] { runForm("library_create"); }},
        {"Install a PDK", [this] { runForm("pdk_install"); }},
        {"Find a command", [this] { openPalette(); }}};
    for (const auto& [text, fn] : starts) {
        auto* b = new QPushButton(text, start_);
        b->setProperty("api", text == QLatin1String("Create a library") ? "library_create" : text == QLatin1String("Install a PDK") ? "pdk_install" : "api_list");
        b->setObjectName("linkButton");
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, fn);
        startColumn->addWidget(b, 0, Qt::AlignLeft);
    }
    startColumn->addSpacing(24);
    heading("Recent", "heading");
    recentList_ = new QVBoxLayout;
    recentList_->setSpacing(2);
    startColumn->addLayout(recentList_);
    startColumn->addStretch(1);
    startOuter->addLayout(startColumn);
    startOuter->addStretch(1);
    tabs_->addTab(start_, "Start");
    tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    tabs_->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);

    inner_ = new QSplitter(Qt::Vertical, this);
    inner_->addWidget(tabs_);
    inner_->addWidget(ciw_);
    inner_->setStretchFactor(0, 1);
    inner_->setStretchFactor(1, 0);
    inner_->setChildrenCollapsible(false);
    outer_ = new QSplitter(Qt::Horizontal, this);
    outer_->addWidget(panels_);
    outer_->addWidget(inner_);
    outer_->setStretchFactor(0, 0);
    outer_->setStretchFactor(1, 1);
    outer_->setSizes({250, 1000});
    inner_->setSizes({640, 170});

    auto* central = new QWidget(this);
    auto* centralLayout = new QHBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(rail);
    centralLayout->addWidget(outer_, 1);
    setCentralWidget(central);

    // The top bar: where you are, and the way to everything else
    auto* bar = addToolBar("Location");
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setContextMenuPolicy(Qt::PreventContextMenu);
    crumb_ = new QLabel(bar);
    crumb_->setContentsMargins(10, 0, 0, 0);
    bar->addWidget(crumb_);
    auto* spring = new QWidget(bar);
    spring->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spring);
    search_ = new QToolButton(bar);
    search_->setObjectName("searchButton");
    search_->setText("Find a command   ⌘K");
    search_->setProperty("api", "api_list");
    search_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(search_, &QToolButton::clicked, this, &Shell::openPalette);
    bar->addWidget(search_);

    // The menus every tool shares
    fileMenu_ = new QMenu("&File", this);
    fileMenu_->addAction("New Library…", this, [this] { runForm("library_create"); });
    fileMenu_->addAction("Install PDK…", this, [this] { runForm("pdk_install"); });
    fileMenu_->addSeparator();
    fileMenu_->addAction("Close Tab", QKeySequence::Close, this, [this] { closeTab(tabs_->currentIndex()); });
    fileMenu_->addAction("Quit", QKeySequence::Quit, qApp, &QApplication::quit);
    viewMenu_ = new QMenu("&Window", this);
    viewMenu_->addAction("Command Palette", QKeySequence("Ctrl+K"), this, &Shell::openPalette);
    viewMenu_->addAction("Command Window", QKeySequence("Ctrl+J"), this, [this] { showConsole(!consoleVisible()); });
    viewMenu_->addAction("Side Panel", QKeySequence("Ctrl+B"), this, [this] { panels_->setVisible(!panels_->isVisible()); });
    viewMenu_->addSeparator();
    viewMenu_->addAction("Library Manager", this, [this] { showLibraryManager(); });
    themeMenu_ = viewMenu_->addMenu("Appearance");
    auto* group = new QActionGroup(this);
    for (Theme::Mode m : {Theme::Mode::Auto, Theme::Mode::Light, Theme::Mode::Dark}) {
        const QString name = Theme::modeName(m);
        QAction* a = themeMenu_->addAction(name == "auto" ? "Match the system" : name == "light" ? "Light" : "Dark", this, [this, name] {
            session_.call("ui_theme", {{"mode", name}});
        });
        a->setCheckable(true);
        a->setData(name);
        a->setChecked(m == Theme::mode());
        group->addAction(a);
    }
    goMenu_ = nullptr;

    addUiCommands(session_, *this);
    api_ = new ApiServer(session_, this);
    if (!api_->listening()) qWarning("OpenVLSI API: %s", qPrintable(api_->error()));
    bindActions(this, {{"New Library…", "library_create"}, {"Install PDK…", "pdk_install"}, {"Close Tab", "ui_tab_close"}, {"Quit", "ui_quit"}, {"Command Palette", "api_list"},
                       {"Command Window", "ui_console"}, {"Side Panel", "ui_panel"}, {"Library Manager", "ui_library_manager"}, {"Match the system", "ui_theme"}, {"Light", "ui_theme"}, {"Dark", "ui_theme"}});
    session_.addCommand({"ui_theme", "Sets the app's appearance: auto follows the system, or light or dark; returns the mode now in effect.",
                         cmd::object({{"mode", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"auto", "light", "dark"}}, {"description", "the appearance"}}}}),
                         [this](cmd::Context&, const QJsonObject& a) {
                             Theme::Mode m = Theme::Mode::Auto;
                             Theme::parseMode(a["mode"].toString(), &m);
                             Theme::apply(*qApp, m);
                             QSettings().setValue("theme", Theme::modeName(m));
                             for (QAction* action : themeMenu_->actions()) action->setChecked(action->data() == a["mode"].toString());
                             return QJsonValue(Theme::modeName(m));
                         },
                         true});
    session_.addCommand({"ui_tabs", "Lists the shell's open tabs in order, with the active one's index.", cmd::object({}),
                         [this](cmd::Context&, const QJsonObject&) {
                             QJsonArray titles;
                             for (const QString& t : tabTitles()) titles.append(t);
                             return QJsonValue(QJsonObject{{"tabs", titles}, {"active", tabs_->currentIndex()}});
                         },
                         true});
    session_.addCommand({"ui_show", "Shows a part of the window: the design or simulate panel, or the command window.",
                         cmd::object({{"part", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"design", "simulate", "console"}}, {"description", "what to show"}}}}),
                         [this](cmd::Context&, const QJsonObject& a) {
                             const QString p = a["part"].toString();
                             if (p == "console") showConsole(true);
                             else showStage(stageIndex(p));
                             return a["part"];
                         },
                         true});

    session_.addCommand({"ui_grab", "Saves a picture of the window to a PNG file; returns its size in pixels.",
                         cmd::object({{"path", cmd::param("string", "the PNG file to write")}}),
                         [this](cmd::Context&, const QJsonObject& a) {
                             const QPixmap shot = grab();
                             if (!shot.save(a["path"].toString(), "PNG")) throw std::runtime_error("cannot write " + a["path"].toString().toStdString());
                             return QJsonValue(QJsonArray{shot.width(), shot.height()});
                         },
                         true});

    connect(Theme::notifier(), SIGNAL(changed()), this, SLOT(update()));
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] { refreshIcons(); });
    connect(&session_, &Session::changed, this, [this] { refreshStart(); });

    connect(tabs_, &QTabWidget::currentChanged, this, [this] {
        if (QWidget* tool = tabs_->currentWidget()) tool->setFocus(Qt::TabFocusReason);  // its keys work at once, without a click first
        rebuildMenus();
        updateCrumb();
        if (auto* m = qobject_cast<QMainWindow*>(current()); m && m->centralWidget()) m->centralWidget()->setFocus();
    });
    setWindowTitle("OpenVLSI");
    startWorkspace_->setText(session_.workspaceFile());
    const QSize avail = screen() ? screen()->availableSize() : QSize(1440, 900);
    resize(std::min(1360, avail.width()), std::min(860, avail.height()));
    showStage(0);
    showConsole(QSettings().value("console", true).toBool());
    refreshIcons();
    refreshStart();
    rebuildMenus();
    updateCrumb();
}

QWidget* Shell::current() const { return tabs_->currentWidget() == start_ ? nullptr : tabs_->currentWidget(); }

QStringList Shell::tabTitles() const {
    QStringList out;
    for (int k = 0; k < tabs_->count(); ++k) out << tabs_->tabText(k);
    return out;
}

QStringList Shell::menuTitles() const {
    QStringList out;
    for (QAction* a : menuBar()->actions()) out << a->text();
    return out;
}

void Shell::adopt(QWidget* tool) {
    auto* m = qobject_cast<QMainWindow*>(tool);
    if (!m) return;
    m->menuBar()->setNativeMenuBar(false);
    m->menuBar()->hide();
    m->statusBar()->setSizeGripEnabled(false);
    // A tool's keys work while its tab is the active one (a key like W belongs to whichever schematic you are in)
    for (QAction* a : m->findChildren<QAction*>()) {
        if (a->shortcut().isEmpty()) continue;
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        tool->addAction(a);
    }
}

void Shell::host(QWidget* tool) {
    const int at = tabs_->indexOf(tool);
    if (at >= 0) {
        tabs_->setCurrentIndex(at);
        return;
    }
    if (qobject_cast<LibraryManager*>(tool)) {  // a floating palette of its own
        tool->show();
        tool->raise();
        return;
    }
    adopt(tool);
    tool->setProperty("glyph", iconOf(tool));
    const int index = tabs_->addTab(tool, icons::get(iconOf(tool)), tabTitle(tool));
    tabs_->setTabToolTip(index, crumbOf(tool));
    auto* close = new QToolButton(tabs_->tabBar());
    close->setObjectName("tabClose");
    close->setIcon(icons::get("close"));
    close->setIconSize({12, 12});
    close->setToolTip("Close this tab");
    connect(close, &QToolButton::clicked, this, [this, tool] { closeTab(tabs_->indexOf(tool)); });
    tabs_->tabBar()->setTabButton(index, QTabBar::RightSide, close);
    tabs_->setCurrentIndex(index);
    if (qobject_cast<FlowRunView*>(tool)) showStage(stageIndex("implement"));
    if (qobject_cast<TestRunView*>(tool)) showStage(stageIndex("test"));
    if (qobject_cast<RtlEditor*>(tool)) showStage(stageIndex("rtl"));
    if (auto* e = qobject_cast<ViewEditor*>(tool)) noteRecent(e->library() + "/" + e->cell() + "/" + e->view());
    if (auto* a = qobject_cast<AdeExplorer*>(tool)) noteRecent(a->library() + "/" + a->cell() + "/" + a->view());
    if (auto* l = qobject_cast<LayoutViewer*>(tool)) noteRecent(l->library() + "/" + l->cell() + "/" + l->view());
    if (auto* r = qobject_cast<Verifier*>(tool)) noteRecent(r->library() + "/" + r->cell() + "/" + r->view());
}

void Shell::closeTab(int index) {
    QWidget* w = tabs_->widget(index);
    if (!w || w == start_) return;
    tabs_->removeTab(index);
    w->deleteLater();
}

void Shell::rebuildMenus() {
    menuBar()->clear();
    menuBar()->addMenu(fileMenu_);
    if (auto* m = qobject_cast<QMainWindow*>(current()))
        for (QAction* a : m->menuBar()->actions()) menuBar()->addAction(a);
    menuBar()->addMenu(viewMenu_);
}

void Shell::refreshIcons() {
    for (int k = 0; k < (int)stages_.size(); ++k)
        stages_[k]->setIcon(icons::get(stages_[k]->property("glyph").toString(), stages_[k]->isChecked()));
    consoleButton_->setIcon(icons::get("console", consoleButton_->isChecked()));
    search_->setIcon(icons::get("command"));
    for (int k = 0; k < tabs_->count(); ++k)
        if (tabs_->widget(k) != start_) {
            tabs_->setTabIcon(k, icons::get(iconOf(tabs_->widget(k))));
            if (auto* b = qobject_cast<QToolButton*>(tabs_->tabBar()->tabButton(k, QTabBar::RightSide))) b->setIcon(icons::get("close"));
        }
    for (QWidget* w : findChildren<QWidget*>())  // what the stylesheet role changes on labels needs
        if (w->property("role").isValid()) { w->style()->unpolish(w); w->style()->polish(w); }
}

void Shell::noteRecent(const QString& path) {
    QSettings s;
    QStringList r = s.value("recent").toStringList();
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > 8) r.removeLast();
    s.setValue("recent", r);
    refreshStart();
}

QStringList Shell::recentViews() const { return QSettings().value("recent").toStringList(); }

void Shell::refreshStart() {
    while (QLayoutItem* i = recentList_->takeAt(0)) {
        delete i->widget();
        delete i;
    }
    int shown = 0;
    for (const QString& path : recentViews()) {
        const QStringList p = path.split('/');
        if (p.size() != 3) continue;
        if (!session_.call("view_list", {{"library", p[0]}, {"cell", p[1]}}).ok) continue;  // gone from this workspace
        auto* b = new QPushButton(p[1] + " " + p[2] + "   " + p[0], start_);
        b->setProperty("api", "editor_open");
        b->setObjectName("linkButton");
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, p] {
            session_.call("editor_open", {{"library", p[0]}, {"cell", p[1]}, {"view", p[2]}});
        });
        recentList_->addWidget(b, 0, Qt::AlignLeft);
        ++shown;
    }
    if (!shown) {
        auto* none = new QLabel("The views you open appear here.", start_);
        none->setProperty("role", "muted");
        recentList_->addWidget(none);
    }
}

void Shell::updateCrumb() {
    QWidget* tool = current();
    crumb_->setText(tool ? crumbOf(tool) : QString());
    setWindowTitle(tool ? tabTitle(tool) + " — OpenVLSI" : "OpenVLSI");
}

void Shell::showStage(int stage) {
    panels_->setCurrentIndex(stage);
    panels_->show();
    if (stage == stageIndex("rtl")) rtl_->refresh();
    if (stage == stageIndex("implement")) implement_->refresh();
    if (stage == stageIndex("verify")) verify_->refresh();
    if (stage == stageIndex("test")) test_->refresh();
    for (int k = 0; k < (int)stages_.size(); ++k) stages_[k]->setChecked(k == stage);
    refreshIcons();
}

static const QStringList kStageNames{"design", "rtl", "simulate", "implement", "verify", "test"};

int Shell::stageIndex(const QString& name) const { return (int)kStageNames.indexOf(name); }

QString Shell::stageName() const { return kStageNames.value(panels_->currentIndex()); }

bool Shell::sidePanelVisible() const { return !panels_->isHidden(); }

void Shell::setSidePanelVisible(bool on) { panels_->setVisible(on); }

void Shell::showLibraryManager() {
    ciw_->libraryManager()->show();
    ciw_->libraryManager()->raise();
}

QWidget* Shell::railButton(const QString& stage) const {
    const int at = stageIndex(stage);
    return at < 0 || at >= (int)stages_.size() ? nullptr : stages_[at];
}

QWidget* Shell::consoleRailButton() const { return consoleButton_; }

QString Shell::apiEndpoint() const { return api_ && api_->listening() ? api_->endpoint() : QString(); }

void Shell::showConsole(bool on) {
    ciw_->setVisible(on);
    consoleButton_->setChecked(on);
    consoleButton_->setIcon(icons::get("console", on));
    QSettings().setValue("console", on);
}

bool Shell::consoleVisible() const { return !ciw_->isHidden(); }

QJsonObject Shell::context() const {
    QJsonObject c = design_->selection();
    if (auto* e = qobject_cast<ViewEditor*>(current())) c = {{"library", e->library()}, {"cell", e->cell()}, {"view", e->view()}};
    if (auto* a = qobject_cast<AdeExplorer*>(current())) c = {{"library", a->library()}, {"cell", a->cell()}, {"view", a->view()}};
    if (auto* l = qobject_cast<LayoutViewer*>(current())) c = {{"library", l->library()}, {"cell", l->cell()}, {"view", l->view()}};
    if (auto* r = qobject_cast<Verifier*>(current())) c = {{"library", r->library()}, {"cell", r->cell()}, {"view", r->view()}};
    return c;
}

bool Shell::runForm(const QString& command, const QJsonObject& prefill) {
    const cmd::Command* c = session_.registry().find(command);
    if (!c) return false;
    QJsonObject args;
    if (!c->params["properties"].toObject().isEmpty()) {
        CommandForm form(*c, prefill, this);
        if (form.exec() != QDialog::Accepted) return false;
        QString error;
        if (!form.arguments(&args, &error)) return false;
    }
    const bool ok = ciw_->runLine(command + " " + QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
    if (!ok) showConsole(true);
    return ok;
}

CommandPalette* Shell::newPalette() {
    std::vector<CommandPalette::Entry> entries;
    for (const auto& l : session_.call("workspace_libraries").value.toArray()) {
        const QString lib = l.toString();
        for (const auto& c : session_.call("cell_list", {{"library", lib}}).value.toArray()) {
            const QString cell = c.toString();
            for (const auto& v : session_.call("view_list", {{"library", lib}, {"cell", cell}}).value.toArray()) {
                const QString view = v.toObject()["view"].toString(), type = v.toObject()["type"].toString();
                if (type != "schematic" && type != "symbol" && type != "maestro" && type != "layout" && type != "verifier") continue;
                entries.push_back({cell + " " + view, lib + "/" + cell + "/" + view + " (" + type + ")", type, [this, lib, cell, view] {
                                       session_.call("editor_open", {{"library", lib}, {"cell", cell}, {"view", view}});
                                   }});
            }
        }
    }
    for (const cmd::Command* c : session_.registry().commands()) {
        const QString name = c->name;
        entries.push_back({name, c->description, "console", [this, name] { runForm(name, context()); }});
    }
    return new CommandPalette(std::move(entries), this);
}

void Shell::openPalette() {
    CommandPalette* p = newPalette();
    p->setAttribute(Qt::WA_DeleteOnClose);
    const QPoint top = mapToGlobal(QPoint((width() - p->width()) / 2, 60));
    p->move(top);
    p->show();
}

void Shell::closeEvent(QCloseEvent* e) {
    QMainWindow::closeEvent(e);
    qApp->quit();
}

}  // namespace app
