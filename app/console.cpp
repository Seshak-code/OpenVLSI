#include "app/console.h"

#include "mcp/endpoint.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <tcl.h>

namespace app {

// ── Tcl: every command a Tcl command, `name -param value ...`; values converted by the command's schema ──────────────
struct TclGlue {
    struct Bound {
        Console* console;
        const cmd::Command* command;
    };

    // a Tcl value as the JSON the schema asks for; false with the reason in error
    static bool toJson(Tcl_Interp* in, Tcl_Obj* o, const QJsonObject& schema, QJsonValue& out, QString& error) {
        const QString type = schema["type"].toString();
        if (type == "integer") {
            Tcl_WideInt v;
            if (Tcl_GetWideIntFromObj(in, o, &v) != TCL_OK) return error = Tcl_GetStringResult(in), false;
            out = QJsonValue(qint64(v));
        } else if (type == "number") {
            double v;
            if (Tcl_GetDoubleFromObj(in, o, &v) != TCL_OK) return error = Tcl_GetStringResult(in), false;
            out = v;
        } else if (type == "boolean") {
            int v;
            if (Tcl_GetBooleanFromObj(in, o, &v) != TCL_OK) return error = Tcl_GetStringResult(in), false;
            out = bool(v);
        } else if (type == "array") {
            Tcl_Size n;
            Tcl_Obj** items;
            if (Tcl_ListObjGetElements(in, o, &n, &items) != TCL_OK) return error = Tcl_GetStringResult(in), false;
            QJsonArray a;
            for (Tcl_Size k = 0; k < n; ++k) {
                QJsonValue v;
                if (!toJson(in, items[k], schema["items"].toObject(), v, error)) return false;
                a.append(v);
            }
            out = a;
        } else if (type == "object") {
            Tcl_DictSearch search;
            Tcl_Obj *key, *value;
            int done;
            if (Tcl_DictObjFirst(in, o, &search, &key, &value, &done) != TCL_OK) return error = Tcl_GetStringResult(in), false;
            QJsonObject m;
            const QJsonObject props = schema["properties"].toObject();
            for (; !done; Tcl_DictObjNext(&search, &key, &value, &done)) {
                const QString k = QString::fromUtf8(Tcl_GetString(key));
                QJsonValue v;
                if (!toJson(in, value, props[k].toObject(), v, error)) {
                    Tcl_DictObjDone(&search);
                    return false;
                }
                m[k] = v;
            }
            out = m;
        } else if (type.isEmpty()) {  // ponytail: an untyped value is a whole number, else a number, else a string, as Tcl cannot say which
            Tcl_WideInt i;
            double d;
            if (Tcl_GetWideIntFromObj(nullptr, o, &i) == TCL_OK) out = QJsonValue(qint64(i));
            else if (Tcl_GetDoubleFromObj(nullptr, o, &d) == TCL_OK) out = d;
            else out = QString::fromUtf8(Tcl_GetString(o));
        } else {
            out = QString::fromUtf8(Tcl_GetString(o));
        }
        return true;
    }

    static Tcl_Obj* toTcl(const QJsonValue& v) {
        if (v.isObject()) {
            Tcl_Obj* d = Tcl_NewDictObj();
            const QJsonObject o = v.toObject();
            for (auto it = o.begin(); it != o.end(); ++it) Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj(it.key().toUtf8().constData(), -1), toTcl(it.value()));
            return d;
        }
        if (v.isArray()) {
            Tcl_Obj* l = Tcl_NewListObj(0, nullptr);
            for (const QJsonValue& e : v.toArray()) Tcl_ListObjAppendElement(nullptr, l, toTcl(e));
            return l;
        }
        if (v.isBool()) return Tcl_NewBooleanObj(v.toBool());
        if (v.isDouble()) {
            const double d = v.toDouble();
            return d == double(qint64(d)) && std::abs(d) < 9e15 ? Tcl_NewWideIntObj(qint64(d)) : Tcl_NewDoubleObj(d);
        }
        if (v.isString()) return Tcl_NewStringObj(v.toString().toUtf8().constData(), -1);
        return Tcl_NewObj();  // null
    }

    static int call(void* data, Tcl_Interp* in, Tcl_Size objc, Tcl_Obj* const objv[]) {
        const auto* b = static_cast<Bound*>(data);
        const QJsonObject props = b->command->params["properties"].toObject();
        QJsonObject args;
        for (Tcl_Size k = 1; k < objc; k += 2) {
            const QString flag = QString::fromUtf8(Tcl_GetString(objv[k]));
            if (!flag.startsWith('-') || k + 1 >= objc) {
                Tcl_SetObjResult(in, Tcl_NewStringObj(("usage: " + b->command->name + " -param value ... (" + flag + " has no value or no -)").toUtf8().constData(), -1));
                return TCL_ERROR;
            }
            QJsonValue v;
            QString error;
            if (!toJson(in, objv[k + 1], props[flag.mid(1)].toObject(), v, error)) {
                Tcl_SetObjResult(in, Tcl_NewStringObj((flag + ": " + error).toUtf8().constData(), -1));
                return TCL_ERROR;
            }
            args[flag.mid(1)] = v;
        }
        const cmd::Result r = b->console->session_.call(b->command->name, args);
        if (!r.ok) {
            Tcl_SetObjResult(in, Tcl_NewStringObj(r.error.toUtf8().constData(), -1));
            return TCL_ERROR;
        }
        Tcl_SetObjResult(in, toTcl(r.value));
        return TCL_OK;
    }

    static int out(void* data, Tcl_Interp*, Tcl_Size objc, Tcl_Obj* const objv[]) {
        if (objc == 2) static_cast<Console*>(data)->out_ += QString::fromUtf8(Tcl_GetString(objv[1]));
        return TCL_OK;
    }

    static int help(void* data, Tcl_Interp* in, Tcl_Size objc, Tcl_Obj* const objv[]) {
        const cmd::Registry& reg = static_cast<Console*>(data)->session_.registry();
        QString text;
        if (objc < 2) {
            for (const cmd::Command* c : reg.commands()) text += c->name + "\n";
            text += "help <command> describes one; each takes -param value ...";
        } else {
            const cmd::Command* c = reg.find(QString::fromUtf8(Tcl_GetString(objv[1])));
            if (!c) {
                Tcl_SetObjResult(in, Tcl_NewStringObj("no such command", -1));
                return TCL_ERROR;
            }
            const QJsonArray required = c->params["required"].toArray();
            text = c->name + ": " + c->description;
            const QJsonObject props = c->params["properties"].toObject();
            for (auto it = props.begin(); it != props.end(); ++it)
                text += "\n  -" + it.key() + " " + it.value().toObject()["type"].toString("any") + (required.contains(it.key()) ? "" : " (optional)") + ": " +
                        it.value().toObject()["description"].toString();
        }
        Tcl_SetObjResult(in, Tcl_NewStringObj(text.toUtf8().constData(), -1));
        return TCL_OK;
    }
};

namespace {
// puts to stdout or stderr shows in the console; to any other channel, as Tcl's own
const char* kPuts = R"(
rename puts ::ov::_puts
proc puts {args} {
    set nl "\n"
    if {[lindex $args 0] eq "-nonewline"} { set nl ""; set args [lrange $args 1 end] }
    if {[llength $args] == 1 || ([llength $args] == 2 && [lindex $args 0] in {stdout stderr})} {
        ::ov::_out "[lindex $args end]$nl"
    } elseif {$nl eq ""} { ::ov::_puts -nonewline {*}$args } else { ::ov::_puts {*}$args }
}
)";
}  // namespace

Console::Console(Session& session, const QString& language, QWidget* parent) : QWidget(parent), session_(session), language_(language) {
    setWindowTitle(language == "tcl" ? "Tcl" : "Python");
    auto* layout = new QVBoxLayout(this);
    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setFont(QFont("Menlo"));
    log_->setAccessibleName(windowTitle() + " output");
    auto* line = new QHBoxLayout;
    prompt_ = new QLabel(language == "tcl" ? "%" : ">>>", this);
    prompt_->setFont(QFont("Menlo"));
    input_ = new QLineEdit(this);
    input_->setFont(QFont("Menlo"));
    input_->setAccessibleName(windowTitle() + " input");
    input_->installEventFilter(this);
    line->addWidget(prompt_);
    line->addWidget(input_);
    layout->addWidget(log_);
    layout->addLayout(line);
    connect(input_, &QLineEdit::returnPressed, this, &Console::submit);

    if (language == "tcl") {
        static const bool found = (Tcl_FindExecutable(nullptr), true);
        (void)found;
        tcl_ = Tcl_CreateInterp();
        // Tcl's own scripts (init.tcl, clock, ...) come from the library zipped into libtcl; without them its core commands still run
        if (Tcl_Init(tcl_) != TCL_OK) show(QString("Tcl's library did not load (%1): core commands only\n").arg(Tcl_GetStringResult(tcl_)), "#c0392b");
        Tcl_Eval(tcl_, "namespace eval ::ov {}");
        Tcl_CreateObjCommand2(tcl_, "::ov::_out", &TclGlue::out, this, nullptr);
        if (Tcl_Eval(tcl_, kPuts) != TCL_OK) throw std::runtime_error(std::string("Tcl: ") + Tcl_GetStringResult(tcl_));
        Tcl_CreateObjCommand2(tcl_, "help", &TclGlue::help, this, nullptr);
        for (const cmd::Command* c : session_.registry().commands()) {
            auto* b = new TclGlue::Bound{this, c};
            const QByteArray name = c->name.toUtf8();
            Tcl_CmdInfo info;
            // a name Tcl already has (none today) stays Tcl's; ::ov:: always has every command
            if (!Tcl_GetCommandInfo(tcl_, name.constData(), &info)) Tcl_CreateObjCommand2(tcl_, name.constData(), &TclGlue::call, b, nullptr);
            Tcl_CreateObjCommand2(tcl_, ("::ov::" + name).constData(), &TclGlue::call, b,
                                  [](void* d) { delete static_cast<TclGlue::Bound*>(d); });
        }
        show("Tcl " + QString(Tcl_GetVar(tcl_, "tcl_patchLevel", TCL_GLOBAL_ONLY)) +
             ": every command as `name -param value ...`, results as dicts and lists; `help` lists them.\n", "gray");
        return;
    }
    if (language != "python") throw std::runtime_error("no console for '" + language.toStdString() + "' (tcl or python)");
    QString exe = qEnvironmentVariable("OPENVLSI_PYTHON");
    if (exe.isEmpty()) exe = QStandardPaths::findExecutable("python3", {"/opt/homebrew/bin", "/usr/local/bin"});
    if (exe.isEmpty()) exe = QStandardPaths::findExecutable("python3");
    if (exe.isEmpty()) throw std::runtime_error("no python3 found (set OPENVLSI_PYTHON)");
    // the client (tools/openvlsi.py, a resource of the app) where python finds it
    const QString dir = QDir::tempPath() + "/openvlsi-client";
    QDir().mkpath(dir);
    QFile client(":/python/openvlsi.py"), copy(dir + "/openvlsi.py");
    if (!client.open(QIODevice::ReadOnly) || !copy.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error("cannot place the Python client in " + dir.toStdString());
    copy.write(client.readAll());
    copy.close();
    const QString endpoint = mcp::endpointFor(session_.workspaceFile());
    const QString startup = "import sys, openvlsi\n"
                            "ov = openvlsi.Client(endpoint=" + QString::fromUtf8(QJsonDocument(QJsonArray{endpoint}).toJson(QJsonDocument::Compact)) + "[0])\n"
                            // console_eval's code: as the REPL would run it (the last expression's value shown); an error's traceback
                            // from the code's own frames, then \x03 so the app knows it failed
                            "def _ov_run(src):\n"
                            "    import ast, traceback\n"
                            "    try:\n"
                            "        tree = ast.parse(src, '<console>')\n"
                            "        last = tree.body.pop() if tree.body and isinstance(tree.body[-1], ast.Expr) else None\n"
                            "        exec(compile(tree, '<console>', 'exec'), globals())\n"
                            "        if last is not None:\n"
                            "            v = eval(compile(ast.Expression(last.value), '<console>', 'eval'), globals())\n"
                            "            if v is not None: print(repr(v))\n"
                            "    except BaseException as e:\n"
                            "        tb = e.__traceback__\n"
                            "        while tb and tb.tb_frame.f_code.co_filename != '<console>': tb = tb.tb_next\n"
                            "        traceback.print_exception(type(e), e, tb)\n"
                            "        print('\\x03', end='')\n"
                            "sys.ps1, sys.ps2 = '\\x01', '\\x02'\n";
    python_ = new QProcess(this);
    python_->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONPATH", dir + (env.contains("PYTHONPATH") ? ":" + env.value("PYTHONPATH") : ""));
    env.insert("PYTHONIOENCODING", "utf-8");
    python_->setProcessEnvironment(env);
    connect(python_, &QProcess::readyRead, this, [this] {
        QString chunk = QString::fromUtf8(python_->readAll());
        for (int k = 0; k < chunk.size(); ++k) {
            if (chunk[k] == QChar(3)) failed_ = true;
            if (chunk[k] != QChar(1) && chunk[k] != QChar(2) && chunk[k] != QChar(3)) continue;
            prompt_->setText(chunk[k] == QChar(1) ? ">>>" : "...");
            if (chunk[k] == QChar(1)) ++prompts_;
            chunk.remove(k--, 1);
        }
        out_ += chunk;
        show(chunk);
    });
    connect(python_, &QProcess::finished, this, [this] { show("python3 ended\n", "#c0392b"); });
    python_->start(exe, {"-i", "-u", "-q", "-c", startup});
    if (!python_->waitForStarted(10000)) throw std::runtime_error("python3 did not start: " + python_->errorString().toStdString());
    QEventLoop wait;  // its first prompt: the client is connected
    connect(python_, &QProcess::readyRead, &wait, [&] { if (prompts_ > 0) wait.quit(); });
    connect(python_, &QProcess::finished, &wait, &QEventLoop::quit);
    QTimer::singleShot(20000, &wait, &QEventLoop::quit);
    if (prompts_ == 0) wait.exec();
    if (prompts_ == 0 || out_.contains("Traceback")) {
        python_->kill();
        throw std::runtime_error("Python console did not start in 20 s: " + out_.toStdString());
    }
    out_.clear();
    show("Python: `ov` is this app, every command a method (ov.cell_list(library=\"analog\")); ov.commands() lists them.\n", "gray");
}

Console::~Console() {
    if (tcl_) Tcl_DeleteInterp(tcl_);
    if (python_) {
        python_->closeWriteChannel();
        if (!python_->waitForFinished(2000)) python_->kill();
    }
}

QString Console::logText() const { return log_->toPlainText(); }

void Console::show(const QString& text, const QString& color) {
    QTextCursor c = log_->textCursor();
    c.movePosition(QTextCursor::End);
    QTextCharFormat f;
    if (!color.isEmpty()) f.setForeground(QColor(color));
    c.insertText(text, f);
    log_->setTextCursor(c);
    log_->ensureCursorVisible();
}

QString Console::evaluate(const QString& code) {
    show(prompt_->text() + " " + code + "\n", "gray");
    return language_ == "tcl" ? evaluateTcl(code) : evaluatePython(code);
}

QString Console::evaluateTcl(const QString& code) {
    out_.clear();
    const int status = Tcl_EvalEx(tcl_, code.toUtf8().constData(), -1, TCL_EVAL_GLOBAL);
    const QString result = QString::fromUtf8(Tcl_GetStringResult(tcl_)), printed = out_;
    show(printed);
    if (status == TCL_ERROR) {
        show(result + "\n", "#c0392b");
        throw std::runtime_error(result.toStdString());
    }
    if (!result.isEmpty()) show(result + "\n");
    return printed + result;
}

QString Console::evaluatePython(const QString& code) {
    if (python_->state() != QProcess::Running) throw std::runtime_error("the Python console has ended");
    out_.clear();
    failed_ = false;
    const int want = prompts_ + 1;
    // one line whatever the code: its own prompt comes back when it is done
    python_->write("_ov_run(" + QJsonDocument(QJsonArray{code}).toJson(QJsonDocument::Compact) + "[0])\n");
    QEventLoop wait;  // ponytail: no time limit; the calls python makes are served meanwhile
    connect(python_, &QProcess::readyRead, &wait, [&] { if (prompts_ >= want) wait.quit(); });
    connect(python_, &QProcess::finished, &wait, &QEventLoop::quit);
    if (prompts_ < want) wait.exec();
    if (prompts_ < want) throw std::runtime_error("python3 ended: " + out_.toStdString());
    if (failed_) throw std::runtime_error(out_.trimmed().toStdString());
    return out_;
}

void Console::submit() {
    const QString line = input_->text();
    input_->clear();
    if (!line.isEmpty()) history_.append(line);
    back_ = 0;
    if (python_) {  // typed lines go to the REPL itself: its blocks continue on "..."
        show(prompt_->text() + " " + line + "\n", "gray");
        python_->write((line + "\n").toUtf8());
        return;
    }
    show(prompt_->text() + " " + line + "\n", "gray");
    pending_ += line + "\n";
    if (!Tcl_CommandComplete(pending_.toUtf8().constData())) {
        prompt_->setText(">");
        return;
    }
    const QString code = pending_;
    pending_.clear();
    prompt_->setText("%");
    try {
        evaluateTcl(code);  // its echo is the lines already shown
    } catch (const std::exception&) {  // shown in red already
    }
}

bool Console::eventFilter(QObject* watched, QEvent* event) {
    if (watched == input_ && event->type() == QEvent::KeyPress && !history_.isEmpty()) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            back_ = std::clamp(back_ + (key == Qt::Key_Up ? 1 : -1), 0, int(history_.size()));
            input_->setText(back_ ? history_[history_.size() - back_] : QString());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace app
