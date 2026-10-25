#include "app/ciw.h"

#include "app/library_manager.h"

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
