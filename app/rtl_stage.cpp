#include "app/rtl_stage.h"

#include "app/icons.h"
#include "app/theme.h"
#include "app/ui_commands.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace app {

namespace {

class VerilogHighlighter : public QSyntaxHighlighter {
public:
    explicit VerilogHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {}

protected:
    void highlightBlock(const QString& text) override {
        const Theme& t = Theme::current();
        static const QRegularExpression keywords(
            "\\b(module|endmodule|input|output|inout|wire|reg|logic|assign|always|always_ff|always_comb|initial|begin|end|if|else|case|casez|casex|endcase|default|for|while|repeat|forever|function|endfunction|task|endtask|"
            "parameter|localparam|posedge|negedge|or|and|not|generate|endgenerate|genvar|integer|real|time|typedef|enum|struct|packed|signed|unsigned|interface|endinterface|package|endpackage|import)\\b");
        static const QRegularExpression numbers("(\\b\\d*'[sS]?[bBoOdDhH][0-9a-fA-FxXzZ_?]+\\b|\\b\\d[\\d_]*(\\.\\d+)?\\b)");
        static const QRegularExpression system("(?<![A-Za-z0-9_$])[$`][A-Za-z_][A-Za-z0-9_]*");
        static const QRegularExpression strings("\"[^\"]*\"");
        const auto paint = [&](const QRegularExpression& re, const QColor& colour, bool bold = false) {
            QTextCharFormat f;
            f.setForeground(colour);
            if (bold) f.setFontWeight(QFont::DemiBold);
            auto it = re.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                setFormat((int)m.capturedStart(), (int)m.capturedLength(), f);
            }
        };
        paint(keywords, t.via, true);
        paint(numbers, t.warn);
        paint(system, t.pass);
        paint(strings, t.warn);
        // Comments last: they cover what is inside them. A block comment runs across lines, by the block's state.
        QTextCharFormat comment;
        comment.setForeground(t.muted);
        setCurrentBlockState(0);
        int start = previousBlockState() == 1 ? 0 : -1;
        int from = 0;
        while (true) {
            if (start < 0) {
                const int line = (int)text.indexOf("//", from), block = (int)text.indexOf("/*", from);
                if (line >= 0 && (block < 0 || line < block)) {
                    setFormat(line, (int)text.size() - line, comment);
                    return;
                }
                if (block < 0) return;
                start = block;
                from = block + 2;
            }
            const int end = (int)text.indexOf("*/", start == 0 && previousBlockState() == 1 ? 0 : from);
            if (end < 0) {
                setFormat(start, (int)text.size() - start, comment);
                setCurrentBlockState(1);
                return;
            }
            setFormat(start, end + 2 - start, comment);
            from = end + 2;
            start = -1;
        }
    }
};

}  // namespace

RtlEditor::RtlEditor(Session& session, const QString& file, const QString& design, QWidget* parent)
    : QMainWindow(parent, Qt::Window), session_(session), file_(file), design_(design) {
    setWindowTitle(file);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* top = new QHBoxLayout();
    top->setContentsMargins(10, 6, 10, 6);
    status_ = new QLabel(central);
    status_->setAccessibleName("Status");
    auto* lintButton = new QPushButton("Check", central);
    auto* simulate = new QPushButton("Simulate…", central);
    auto* implement = new QPushButton("Implement", central);
    top->addWidget(status_, 1);
    top->addWidget(lintButton);
    top->addWidget(simulate);
    top->addWidget(implement);
    simulate->setEnabled(!design.isEmpty());
    implement->setEnabled(!design.isEmpty());
    layout->addLayout(top);
    auto* split = new QSplitter(Qt::Vertical, central);
    editor_ = new QPlainTextEdit(split);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(12);
    editor_->setFont(mono);
    editor_->setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(' ') * 4);
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor_->setAccessibleName("Verilog source");
    new VerilogHighlighter(editor_->document());
    problems_ = new QListWidget(split);
    problems_->setAccessibleName("Problems");
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
    setCentralWidget(central);
    setFocusProxy(editor_);

    QMenu* source = menuBar()->addMenu("&Source");
    source->addAction("Save", QKeySequence::Save, this, [this] { save(); });
    source->addAction("Check Syntax", QKeySequence("Ctrl+Shift+L"), this, [this] { lint(); });
    bindActions(this, {{"Save", "rtl_write"}, {"Check Syntax", "rtl_lint"}, {"Check", "rtl_lint"}, {"Simulate…", "test_run"}, {"Implement", "flow_run"}});

    connect(lintButton, &QPushButton::clicked, this, [this] { lint(); });
    connect(implement, &QPushButton::clicked, this, [this] { if (save()) emit implementRequested(design_); });
    connect(simulate, &QPushButton::clicked, this, [this] { if (save()) emit simulateRequested(design_); });
    connect(problems_, &QListWidget::itemActivated, this, [this](QListWidgetItem* i) { goToLine(i->data(Qt::UserRole).toInt()); });
    connect(problems_, &QListWidget::itemClicked, this, [this](QListWidgetItem* i) { goToLine(i->data(Qt::UserRole).toInt()); });
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] { editor_->document()->markContentsDirty(0, editor_->document()->characterCount()); });
    const cmd::Result r = session_.call("rtl_read", {{"file", file}});
    if (r.ok) {
        editor_->setPlainText(r.value.toString());
        editor_->document()->setModified(false);
        status_->setText(QFileInfo(file).fileName() + (design.isEmpty() ? QString() : ", in " + design));
    } else {
        status_->setText(r.error);
    }
}

QString RtlEditor::text() const { return editor_->toPlainText(); }
void RtlEditor::setText(const QString& text) { editor_->setPlainText(text); editor_->document()->setModified(true); }
bool RtlEditor::modified() const { return editor_->document()->isModified(); }
QString RtlEditor::status() const { return status_->text(); }
int RtlEditor::diagnosticCount() const { return problems_->count(); }
int RtlEditor::currentLine() const { return editor_->textCursor().blockNumber() + 1; }

void RtlEditor::goToLine(int line) {
    const QTextBlock block = editor_->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) return;
    QTextCursor c(block);
    editor_->setTextCursor(c);
    editor_->centerCursor();
    editor_->setFocus();
}

bool RtlEditor::save() {
    if (!modified()) return true;
    const cmd::Result r = session_.call("rtl_write", {{"file", file_}, {"text", text()}});
    if (!r.ok) {
        status_->setText(r.error);
        return false;
    }
    editor_->document()->setModified(false);
    status_->setText("Saved");
    return true;
}

QJsonObject RtlEditor::lint() {
    if (!save()) return {};
    const QJsonObject args = design_.isEmpty() ? QJsonObject{{"verilog", QJsonArray{file_}}} : QJsonObject{{"design", design_}};
    const cmd::Result r = session_.call("rtl_lint", args);
    problems_->clear();
    const Theme& t = Theme::current();
    if (!r.ok) {
        status_->setText(r.error);
        status_->setStyleSheet(QString("color: %1;").arg(t.fail.name()));
        return {};
    }
    const QJsonObject o = r.value.toObject();
    for (const auto& v : o["diagnostics"].toArray()) {
        const QJsonObject d = v.toObject();
        auto* item = new QListWidgetItem(QString("%1:%2  %3").arg(QFileInfo(d["file"].toString()).fileName()).arg(d["line"].toInt()).arg(d["message"].toString()), problems_);
        item->setData(Qt::UserRole, d["line"].toInt());
        item->setForeground(d["severity"] == "error" ? t.fail : t.warn);
    }
    const int errors = o["errors"].toInt(), warnings = o["warnings"].toInt();
    status_->setText(o["clean"].toBool() ? (warnings ? QString("No errors, %1 warnings").arg(warnings) : QString("No errors")) : QString("%1 errors, %2 warnings").arg(errors).arg(warnings));
    status_->setStyleSheet(QString("color: %1;").arg((o["clean"].toBool() ? t.pass : t.fail).name()));
    return o;
}

RtlPanel::RtlPanel(Session& session, Actions actions, QWidget* parent) : QWidget(parent), session_(session), actions_(std::move(actions)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* add = new QPushButton("New design…", this);
    add->setIcon(icons::get("add"));
    add->setProperty("api", "rtl_design_set");
    connect(add, &QPushButton::clicked, this, [this] { if (actions_.addDesign) actions_.addDesign(); });
    layout->addWidget(add);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setAccessibleName("Digital designs");
    layout->addWidget(tree_, 1);
    const auto open = [this](QTreeWidgetItem* i) {
        const QString file = i->data(0, Qt::UserRole).toString();
        if (!file.isEmpty() && actions_.open) actions_.open(file, i->data(0, Qt::UserRole + 1).toString());
    };
    connect(tree_, &QTreeWidget::itemActivated, this, open);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, open);
    connect(&session_, &Session::called, this, [this](const QString& name, const QJsonObject&, const cmd::Result&, bool) { if (name == "rtl_design_set" || name == "rtl_design_remove") refresh(); });
    refresh();
}

void RtlPanel::refresh() {
    const cmd::Result r = session_.call("rtl_design_list");
    if (!r.ok) return;
    tree_->clear();
    for (const auto& v : r.value.toArray()) {
        const QJsonObject d = v.toObject();
        auto* top = new QTreeWidgetItem(tree_, {d["design"].toString() == d["top"].toString() ? d["design"].toString() : d["design"].toString() + "  top " + d["top"].toString()});
        top->setExpanded(true);
        const auto files = [&](const char* key, const QString& suffix) {
            for (const auto& f : d[key].toArray()) {
                auto* item = new QTreeWidgetItem(top, {QFileInfo(f.toString()).fileName() + suffix});
                item->setToolTip(0, f.toString());
                item->setData(0, Qt::UserRole, f.toString());
                item->setData(0, Qt::UserRole + 1, d["design"].toString());
            }
        };
        files("verilog", QString());
        files("testbenches", "  testbench");
    }
}

}  // namespace app
