#include "app/shell.h"

#include "app/console.h"

#include "app/ade_explorer.h"
#include "app/icons.h"
#include "app/implement.h"
#include "app/layout_viewer.h"
#include "app/rtl_stage.h"
#include "app/test_stage.h"
#include "app/api_server.h"
#include "app/ui_commands.h"
#include "app/verify_stage.h"
#include "app/netlist_view.h"
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
#include <QRegularExpression>
#include <QFile>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace app {

static const QStringList kStageNames{"design", "rtl", "simulate", "implement", "verify", "test"};

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
    if (auto* n = qobject_cast<NetlistView*>(tool)) return n->cell() + " " + n->view();
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
    if (auto* n = qobject_cast<NetlistView*>(tool)) return n->library() + "  ›  " + n->cell() + "  ›  " + n->view();
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
        b->setAccessibleName(stages[k].second);
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
    consoleButton_->setAccessibleName("Command window");
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
    search_->setAccessibleName("Find a command");
    search_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(search_, &QToolButton::clicked, this, &Shell::openPalette);
    bar->addWidget(search_);

    // The menus every tool shares, in the order a Cadence tool has them: File, the tool's own, Tools, Options, Window, Help
    fileMenu_ = new QMenu("&File", this);
    fileMenu_->addAction("New Library…", this, [this] { runForm("library_create"); });
    fileMenu_->addAction("New Cell…", this, [this] { runForm("cell_create", context()); });
    fileMenu_->addAction("New View…", this, [this] { runForm("view_create", context()); });
    fileMenu_->addAction("Open Cellview…", QKeySequence::Open, this, [this] { runForm("editor_open", context()); });
    fileMenu_->addSeparator();
    fileMenu_->addAction("Import GDS…", this, [this] { runForm("layout_import_gds", context()); });
    fileMenu_->addAction("Export GDS…", this, [this] { runForm("layout_export_gds", context()); });
    fileMenu_->addSeparator();
    fileMenu_->addAction("Install PDK…", this, [this] { runForm("pdk_install"); });
    fileMenu_->addAction("Install PDK Cells…", this, [this] { runForm("pdk_install_cells"); });
    fileMenu_->addSeparator();
    fileMenu_->addAction("Close Tab", QKeySequence::Close, this, [this] { closeTab(tabs_->currentIndex()); });
    fileMenu_->addAction("Quit", QKeySequence::Quit, qApp, &QApplication::quit);
    toolsMenu_ = new QMenu("&Tools", this);
    toolsMenu_->addAction("Library Manager", this, [this] { showLibraryManager(); });
    toolsMenu_->addAction("Command Palette", QKeySequence("Ctrl+K"), this, &Shell::openPalette);
    toolsMenu_->addAction("Command Window", QKeySequence("Ctrl+J"), this, [this] { showConsole(!consoleVisible()); });
    toolsMenu_->addAction("Tcl Console", this, [this] { session_.call("console_open", {{"language", "tcl"}}); });
    toolsMenu_->addAction("Python Console", this, [this] { session_.call("console_open", {{"language", "python"}}); });
    toolsMenu_->addSeparator();
    toolsMenu_->addAction("Run DRC…", this, [this] { runForm("verify_drc", context()); });
    toolsMenu_->addAction("Run LVS…", this, [this] { runForm("verify_lvs", context()); });
    toolsMenu_->addAction("Netlist as Verilog…", this, [this] { runForm("netlist_verilog", context()); });
    optionsMenu_ = new QMenu("&Options", this);
    themeMenu_ = optionsMenu_->addMenu("Appearance");
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
    optionsMenu_->addAction("EDA Tools…", this, [this] { session_.call("ui_help", {{"topic", "tools"}}); });
    optionsMenu_->addAction("Keyboard Shortcuts…", this, [this] { session_.call("ui_shortcuts", {{"show", true}}); });
    viewMenu_ = new QMenu("&Window", this);
    viewMenu_->addAction("Side Panel", QKeySequence("Ctrl+B"), this, [this] { panels_->setVisible(!panels_->isVisible()); });
    viewMenu_->addSeparator();
    for (int k = 0; k < (int)kStageNames.size(); ++k) {
        const QString name = kStageNames[k];
        viewMenu_->addAction(k == 1 ? QString("RTL") : name.left(1).toUpper() + name.mid(1), QKeySequence(QString("Ctrl+%1").arg(k + 1)), this, [this, k] { showStage(k); });
    }
    helpMenu_ = new QMenu("&Help", this);
    helpMenu_->addAction("User Guide", this, [this] { session_.call("ui_help", {{"topic", "guide"}}); });
    helpMenu_->addAction("Command Reference", this, [this] { session_.call("ui_help", {{"topic", "api"}}); });
    helpMenu_->addAction("About OpenVLSI", this, [this] { session_.call("ui_help", {{"topic", "about"}}); });
    goMenu_ = nullptr;

    addUiCommands(session_, *this);
    api_ = new ApiServer(session_, this);
    if (!api_->listening()) qWarning("OpenVLSI API: %s", qPrintable(api_->error()));
    bindActions(this, {{"New Library…", "library_create"}, {"New Cell…", "cell_create"}, {"New View…", "view_create"}, {"Open Cellview…", "editor_open"},
                       {"Import GDS…", "layout_import_gds"}, {"Export GDS…", "layout_export_gds"}, {"Install PDK…", "pdk_install"}, {"Install PDK Cells…", "pdk_install_cells"},
                       {"Close Tab", "ui_tab_close"}, {"Quit", "ui_quit"}, {"Command Palette", "api_list"}, {"Command Window", "ui_console"}, {"Tcl Console", "console_open"}, {"Python Console", "console_open"}, {"Run DRC…", "verify_drc"},
                       {"Run LVS…", "verify_lvs"}, {"Netlist as Verilog…", "netlist_verilog"}, {"EDA Tools…", "ui_help"}, {"Keyboard Shortcuts…", "ui_shortcuts"},
                       {"User Guide", "ui_help"}, {"Command Reference", "ui_help"}, {"About OpenVLSI", "ui_help"}, {"Side Panel", "ui_panel"}, {"Design", "ui_stage"},
                       {"RTL", "ui_stage"}, {"Simulate", "ui_stage"}, {"Implement", "ui_stage"}, {"Verify", "ui_stage"}, {"Test", "ui_stage"}, {"Library Manager", "ui_library_manager"},
                       {"Match the system", "ui_theme"}, {"Light", "ui_theme"}, {"Dark", "ui_theme"}});
    // A document in a tab of its own (the guide, the command reference, ...): one tab a title, shown again when asked again
    const auto showDocument = [this](const QString& title, const QString& markdown) {
        for (int k = 0; k < tabs_->count(); ++k)
            if (tabs_->widget(k)->objectName() == "document:" + title) {
                static_cast<QTextBrowser*>(tabs_->widget(k))->setMarkdown(markdown);
                tabs_->setCurrentIndex(k);
                return;
            }
        auto* doc = new QTextBrowser(this);
        doc->setObjectName("document:" + title);
        doc->setOpenExternalLinks(true);
        doc->setMarkdown(markdown);
        tabs_->setCurrentIndex(tabs_->addTab(doc, title));
    };
    session_.addCommand({"ui_help", "Shows a document in a tab: the user guide, the command reference (every command and its parameters), about (the "
                                    "version and the EDA tools it found) or tools (the tools alone, where each was found). Returns its title.",
                         cmd::object({{"topic", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"guide", "api", "about", "tools"}}, {"description", "which"}}}}),
                         [this, showDocument](cmd::Context&, const QJsonObject& a) {
                             const QString topic = a["topic"].toString();
                             QString title, text;
                             if (topic == "guide") {
                                 QFile f(":/docs/guide.md");
                                 if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("this build carries no user guide (docs/guide.md)");
                                 title = "User Guide", text = QString::fromUtf8(f.readAll());
                             } else if (topic == "api") {
                                 title = "Command Reference", text = apiReference(session_);
                             } else {
                                 QString tools = "| Tool | Found at |\n|---|---|\n";
                                 for (const auto& t : session_.call("flow_tools").value.toArray()) {
                                     const QJsonObject o = t.toObject();
                                     tools += "| " + o["name"].toString() + " | " + (o["found"].toBool() ? "`" + o["path"].toString() + "`" : QString("not found")) + " |\n";
                                 }
                                 title = topic == "about" ? "About OpenVLSI" : "EDA Tools";
                                 text = topic == "about" ? "# OpenVLSI\n\nChip design from RTL to a tested chip, macOS first: TreeSPICE simulates, and Yosys, OpenROAD, "
                                                           "KLayout and Icarus are its backends.\n\nWorkspace: `" + QString::fromStdString(session_.workspace().file()) +
                                                           "`\n\n## EDA tools\n\n" + tools
                                                         : "# EDA tools\n\nWhere OpenVLSI found each tool (`scripts/setup_eda_tools.sh` installs them under ~/eda).\n\n" + tools;
                             }
                             showDocument(title, text);
                             return QJsonValue(title);
                         }});
    session_.addCommand({"ui_shortcuts", "Every menu item with a keyboard shortcut in the menus now shown (the shared ones and the active tool's): menu, item, keys. "
                                         "show puts them in a tab too.",
                         cmd::object({}, {{"show", cmd::param("boolean", "show them in a tab (default false)")}}),
                         [this, showDocument](cmd::Context&, const QJsonObject& a) {
                             QJsonArray out;
                             QString table = "# Keyboard shortcuts\n\n| Menu | Item | Keys |\n|---|---|---|\n";
                             std::function<void(QMenu*, const QString&)> walk = [&](QMenu* m, const QString& path) {
                                 for (QAction* act : m->actions()) {
                                     if (act->menu()) {
                                         walk(act->menu(), path + " > " + act->text().remove('&'));
                                     } else if (!act->shortcut().isEmpty()) {
                                         const QString keys = act->shortcut().toString(QKeySequence::NativeText);
                                         out.append(QJsonObject{{"menu", path}, {"item", act->text()}, {"keys", keys}});
                                         table += "| " + path + " | " + act->text() + " | " + keys + " |\n";
                                     }
                                 }
                             };
                             for (QAction* act : menuBar()->actions())
                                 if (act->menu()) walk(act->menu(), act->text().remove('&'));
                             if (a["show"].toBool(false)) showDocument("Keyboard Shortcuts", table);
                             return QJsonValue(out);
                         },
                         true});
    // Script consoles, a tab each: Tcl inside the app, Python as the user's python3 on the app's socket
    const auto console = [this](const QString& language) {
        QPointer<Console>& c = consoles_[language];
        if (!c) c = new Console(session_, language, this);  // throws when it cannot start
        host(c);
        return c.data();
    };
    const QJsonObject kLanguage{{"type", "string"}, {"enum", QJsonArray{"tcl", "python"}}, {"description", "tcl or python"}};
    session_.addCommand({"console_open", "Opens a script console as a tab: Tcl (every command as `name -param value`, results as dicts and lists) or "
                                         "Python (`ov`, this app, every command a method). Returns the language.",
                         QJsonObject{{"type", "object"}, {"properties", QJsonObject{{"language", kLanguage}}}, {"required", QJsonArray{"language"}}},
                         [console](cmd::Context&, const QJsonObject& a) {
                             console(a["language"].toString());
                             return a["language"];
                         },
                         true});
    session_.addCommand({"console_eval", "Runs code in a script console (opened if need be), as if typed: returns what it printed and, in Tcl, its result; "
                                         "an error is the command's error.",
                         QJsonObject{{"type", "object"},
                                     {"properties", QJsonObject{{"language", kLanguage}, {"code", cmd::param("string", "the code; it may span lines")}}},
                                     {"required", QJsonArray{"language", "code"}}},
                         [console](cmd::Context&, const QJsonObject& a) { return QJsonValue(console(a["language"].toString())->evaluate(a["code"].toString())); }});
    // Every form's fields offer the workspace's values for them (a right click, completion): by the parameter's name, and its description
    // for files and folders
    CommandForm::setChoiceSource([this](const QString& command, const QString& param, const QJsonObject& schema, const QJsonObject& cur) {
        FieldChoices out;
        const auto names = [](const cmd::Result& r, const char* key = nullptr) {
            QStringList v;
            if (r.value.isObject()) v = r.value.toObject().keys();
            for (const auto& x : r.value.toArray()) v << (key ? x.toObject()[key].toString() : x.toString());
            v.removeAll(QString());
            v.removeDuplicates();
            return v;
        };
        const auto lib = [&](std::initializer_list<const char*> keys) {
            for (const char* k : keys)
                if (!cur[k].toString().isEmpty()) return cur[k].toString();
            return QString();
        };
        static const QStringList libraries = {"library", "masterLibrary", "master_library", "mapLibrary"};
        if (libraries.contains(param)) {
            out.values = names(session_.call("workspace_libraries"));
        } else if (param == "cell" || param == "mapCell") {
            if (const QString l = lib({param == "mapCell" ? "mapLibrary" : "library"}); !l.isEmpty()) out.values = names(session_.call("library_views", {{"library", l}}));
        } else if (param == "master" || param == "master_cell") {
            if (const QString l = lib({"masterLibrary", "master_library", "library"}); !l.isEmpty()) out.values = names(session_.call("library_views", {{"library", l}}));
        } else if ((param == "view" || param == "masterView" || param == "master_view" || param == "schematic") && !cur["cell"].toString().isEmpty()) {
            const QString l = param.startsWith("master") ? lib({"masterLibrary", "master_library", "library"}) : lib({"library"});
            const QString c = param.startsWith("master") ? cur[param == "masterView" ? "master" : "master_cell"].toString() : cur["cell"].toString();
            for (const auto& v : session_.call("library_views", {{"library", l}}).value.toObject()[c].toArray()) out.values << v.toObject()["view"].toString();
        } else if (param == "flow_run") {
            out.values = names(session_.call("flow_list"), "run");
        } else if (param == "run") {
            out.values = names(session_.call(command.startsWith("test_") ? "test_list" : "verify_list"), "run");
        } else if (param == "platform") {
            out.values = names(session_.call("flow_platforms"), "name");
        } else if ((param == "layer" || param == "metal" || param == "from") && !cur["library"].toString().isEmpty()) {
            out.values = names(session_.call("layout_layers", {{"library", cur["library"].toString()}}), "name");
        }
        static const QRegularExpression fileish(R"(\b(file|files|path|paths|directory|folder|gds|hex)\b)", QRegularExpression::CaseInsensitiveOption);
        if (out.values.isEmpty() && fileish.match(schema["description"].toString()).hasMatch()) {
            out.folder = schema["description"].toString().contains(QRegularExpression(R"(\b(directory|folder)\b)", QRegularExpression::CaseInsensitiveOption));
            out.file = !out.folder;
        }
        return out;
    });
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
    close->setAccessibleName("Close " + tabTitle(tool));
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
    if (auto* n = qobject_cast<NetlistView*>(tool)) noteRecent(n->library() + "/" + n->cell() + "/" + n->view());
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
    menuBar()->addMenu(toolsMenu_);
    menuBar()->addMenu(optionsMenu_);
    menuBar()->addMenu(viewMenu_);
    menuBar()->addMenu(helpMenu_);
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
    if (auto* n = qobject_cast<NetlistView*>(current())) c = {{"library", n->library()}, {"cell", n->cell()}, {"view", n->view()}};
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
        const QJsonObject all = session_.call("library_views", {{"library", lib}}).value.toObject();
        for (auto c = all.begin(); c != all.end(); ++c) {
            const QString cell = c.key();
            for (const auto& v : c.value().toArray()) {
                const QString view = v.toObject()["view"].toString(), type = v.toObject()["type"].toString();
                if (type != "schematic" && type != "symbol" && type != "maestro" && type != "layout" && type != "verifier" && type != "config" && type != "extracted") continue;
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
