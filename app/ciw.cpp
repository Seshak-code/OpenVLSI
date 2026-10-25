#include "app/ciw.h"

#include "app/library_manager.h"
#include "app/ade_explorer.h"
#include "app/view_editor.h"
#include "app/waveform_viewer.h"

#include <QApplication>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace app {

namespace {
QString compact(const QJsonValue& v) {
    const QByteArray a = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(a.mid(1, a.size() - 2));
}
}  // namespace

Ciw::Ciw(Session& session, QWidget* parent) : QMainWindow(parent), session_(session) {
    setWindowTitle("OpenVLSI — " + session.workspaceFile());
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    log_ = new QPlainTextEdit(central);
    log_->setReadOnly(true);
    log_->setFont(QFont("Menlo"));
    input_ = new QLineEdit(central);
    input_->setPlaceholderText("command {json arguments}, e.g. cell_list {\"library\": \"analog\"}");
    layout->addWidget(log_);
    layout->addWidget(input_);
    setCentralWidget(central);
    connect(input_, &QLineEdit::returnPressed, this, [this] {
        if (runLine(input_->text())) input_->clear();
    });

    // every command any window runs, echoed with its outcome
    connect(&session_, &Session::called, this, [this](const QString& name, const QJsonObject& args, const cmd::Result& r, bool readOnly) {
        if (readOnly && !typed_) return;  // a window's browsing is not echoed; its changes and anything typed are
        append(name.toHtmlEscaped() + " " + (args.isEmpty() ? QString() : compact(args).toHtmlEscaped()));
        append(r.ok ? "<span style='color:gray'>⇒ " + compact(r.value).toHtmlEscaped() + "</span>"
                    : "<span style='color:#c0392b'>✗ " + r.error.toHtmlEscaped() + "</span>");
    });

    session_.addCommand({"editor_open", "Opens a view's tool (the GUI's command): the schematic or symbol editor, or ADE Explorer on a "
                                        "maestro view; returns the view's type.",
                         cmd::object({{"library", cmd::param("string", "library name")}, {"cell", cmd::param("string", "cell name")},
                                      {"view", cmd::param("string", "view name")}}),
                         [this](cmd::Context& c, const QJsonObject& a) {
                             const QString lib = a["library"].toString(), cell = a["cell"].toString(), view = a["view"].toString();
                             const std::string type = c.workspace.library(lib.toStdString()).viewType(cell.toStdString(), view.toStdString());
                             if (type != "schematic" && type != "symbol" && type != "maestro")
                                 throw std::runtime_error("no tool for a " + type + " view yet (schematic, symbol and maestro)");
                             QPointer<QWidget>& e = editors_[lib + "/" + cell + "/" + view];
                             if (!e && type == "maestro") e = new AdeExplorer(session_, lib, cell, view, this);
                             else if (!e) e = new ViewEditor(session_, lib, cell, view, this);
                             e->show();
                             e->raise();
                             return QJsonValue(QString::fromStdString(type));
                         },
                         true});

    const QJsonObject kRun = cmd::param("string", "a run directory (simulate's run, an ADE point's dir)");
    session_.addCommand({"waveform_open", "Opens the waveform viewer on a run (the GUI's command); returns the run's top schematic.",
                         cmd::object({{"run", kRun}}),
                         [this](cmd::Context& c, const QJsonObject& a) { return QJsonValue(openViewer(c, a["run"].toString())->design()); }, true});
    session_.addCommand({"waveform_add", "Plots a calculator expression's waveform in a run's viewer (opened if need be).",
                         cmd::object({{"run", kRun}, {"expression", cmd::param("string", "a waveform: VT(out), dB20(VF(out)), ...")}}),
                         [this](cmd::Context& c, const QJsonObject& a) {
                             QString error;
                             if (!openViewer(c, a["run"].toString())->add(a["expression"].toString(), &error)) throw std::runtime_error(error.toStdString());
                             return a["expression"];
                         },
                         true});
    session_.addCommand({"waveform_clear", "Removes every trace from a run's viewer.", cmd::object({{"run", kRun}}),
                         [this](cmd::Context& c, const QJsonObject& a) {
                             openViewer(c, a["run"].toString())->clear();
                             return QJsonValue(true);
                         },
                         true});
    session_.addCommand({"waveform_probe", "Plots a net of a schematic in the newest viewer showing a run of it (the schematic's Plot Net).",
                         cmd::object({{"library", cmd::param("string", "library name")}, {"cell", cmd::param("string", "cell name")},
                                      {"view", cmd::param("string", "the schematic view")}, {"net", cmd::param("string", "net name")}}),
                         [this](cmd::Context&, const QJsonObject& a) {
                             const QString design = a["library"].toString() + "/" + a["cell"].toString() + "/" + a["view"].toString();
                             for (auto it = viewers_.rbegin(); it != viewers_.rend(); ++it) {
                                 if (!*it || (*it)->design() != design) continue;
                                 const QString e = (*it)->defaultAccess() + "(" + a["net"].toString() + ")";
                                 QString error;
                                 if ((*it)->defaultAccess().isEmpty() || !(*it)->add(e, &error))
                                     throw std::runtime_error(error.isEmpty() ? "the run has no waveform analysis" : error.toStdString());
                                 (*it)->show();
                                 (*it)->raise();
                                 return QJsonValue(e);
                             }
                             throw std::runtime_error("no waveform viewer shows a run of " + design.toStdString() + "; open one (waveform_open)");
                         },
                         true});
    session_.addCommand({"editor_select_net", "Selects a net's wires and labels in a schematic's editor (opened if need be); returns how many.",
                         cmd::object({{"library", cmd::param("string", "library name")}, {"cell", cmd::param("string", "cell name")},
                                      {"view", cmd::param("string", "the schematic view")}, {"net", cmd::param("string", "net name")}}),
                         [this](cmd::Context&, const QJsonObject& a) {
                             const cmd::Result r = session_.call("editor_open", {{"library", a["library"]}, {"cell", a["cell"]}, {"view", a["view"]}});
                             ViewEditor* e = editor(a["library"].toString(), a["cell"].toString(), a["view"].toString());
                             if (!r.ok || !e || !e->isSchematic()) throw std::runtime_error(r.ok ? "not a schematic" : r.error.toStdString());
                             const int n = e->selectNet(a["net"].toString());
                             if (n == 0) throw std::runtime_error("no wire or label of net " + a["net"].toString().toStdString() + " in " +
                                                                  a["cell"].toString().toStdString());
                             return QJsonValue(n);
                         },
                         true});

    QMenu* file = menuBar()->addMenu("&File");
    file->addAction("&Quit", QKeySequence::Quit, qApp, &QApplication::quit);
    QMenu* tools = menuBar()->addMenu("&Tools");
    tools->addAction("&Library Manager", QKeySequence("Ctrl+L"), this, [this] {
        libraryManager()->show();
        libraryManager()->raise();
    });
    resize(820, 420);
}

LibraryManager* Ciw::libraryManager() {
    if (!libraryManager_) libraryManager_ = new LibraryManager(session_, this);
    return libraryManager_;
}

void Ciw::append(const QString& html) { log_->appendHtml(html); }

QString Ciw::logText() const { return log_->toPlainText(); }

ViewEditor* Ciw::editor(const QString& lib, const QString& cell, const QString& view) const {
    const auto it = editors_.find(lib + "/" + cell + "/" + view);
    return it == editors_.end() ? nullptr : qobject_cast<ViewEditor*>(it->second.data());
}

WaveformViewer* Ciw::openViewer(cmd::Context& c, const QString& run) {
    const QString dir = QString::fromStdString(cmd::runDirectory(c, run.toStdString()));
    WaveformViewer* v = viewer(dir);
    if (!v) {
        v = new WaveformViewer(session_, dir, this);
        viewers_.push_back(v);
    }
    v->show();
    v->raise();
    return v;
}

WaveformViewer* Ciw::viewer(const QString& run) const {
    for (const auto& v : viewers_) if (v && v->run() == run) return v;
    return nullptr;
}

AdeExplorer* Ciw::explorer(const QString& lib, const QString& cell, const QString& view) const {
    const auto it = editors_.find(lib + "/" + cell + "/" + view);
    return it == editors_.end() ? nullptr : qobject_cast<AdeExplorer*>(it->second.data());
}

bool Ciw::runLine(const QString& line) {
    const QString t = line.trimmed();
    if (t.isEmpty()) return false;
    const int brace = t.indexOf('{');
    const QString name = (brace < 0 ? t : t.left(brace)).trimmed();
    QJsonObject args;
    if (brace >= 0) {
        QJsonParseError e;
        const QJsonDocument d = QJsonDocument::fromJson(t.mid(brace).toUtf8(), &e);
        if (e.error != QJsonParseError::NoError || !d.isObject()) {
            append("<span style='color:#c0392b'>✗ the arguments are not a JSON object: " + e.errorString().toHtmlEscaped() + "</span>");
            return false;
        }
        args = d.object();
    }
    typed_ = true;
    const bool ok = session_.call(name, args).ok;
    typed_ = false;
    return ok;
}

}  // namespace app
