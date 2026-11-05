#include "app/palette.h"

#include "app/icons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace app {

int fuzzyScore(const QString& query, const QString& text) {
    if (query.isEmpty()) return 0;
    int score = 0, at = 0, run = 0;
    for (const QChar q : query) {
        if (q.isSpace()) continue;
        const int hit = text.indexOf(q, at, Qt::CaseInsensitive);
        if (hit < 0) return -1;
        run = hit == at ? run + 1 : 0;
        score += 1 + 3 * run;
        if (hit == 0 || !text[hit - 1].isLetterOrNumber()) score += 6;
        at = hit + 1;
    }
    return score;
}

CommandForm::CommandForm(const cmd::Command& c, const QJsonObject& prefill, QWidget* parent) : QDialog(parent), command_(c) {
    setWindowTitle(c.name);
    auto* layout = new QVBoxLayout(this);
    auto* about = new QLabel(c.description, this);
    about->setWordWrap(true);
    about->setProperty("role", "muted");
    layout->addWidget(about);
    auto* form = new QFormLayout;
    const QJsonObject props = c.params["properties"].toObject();
    const QJsonArray required = c.params["required"].toArray();
    QStringList names = props.keys();
    std::stable_sort(names.begin(), names.end(), [&](const QString& a, const QString& b) {
        return required.contains(a) && !required.contains(b);
    });
    for (const QString& name : names) {
        const QJsonObject schema = props[name].toObject();
        const QString type = schema["type"].toString();
        QWidget* w;
        if (schema.contains("enum")) {
            auto* box = new QComboBox(this);
            if (!required.contains(name)) box->addItem(QString());
            for (const auto& v : schema["enum"].toArray()) box->addItem(v.toString());
            if (prefill.contains(name)) box->setCurrentText(prefill[name].toString());
            w = box;
        } else if (type == "boolean") {
            auto* box = new QCheckBox(this);
            box->setChecked(prefill[name].toBool());
            w = box;
        } else {
            auto* line = new QLineEdit(this);
            if (prefill[name].isString()) line->setText(prefill[name].toString());
            else if (prefill[name].isDouble()) line->setText(QString::number(prefill[name].toDouble()));
            line->setPlaceholderText(required.contains(name) ? QString() : "optional");
            w = line;
        }
        w->setToolTip(schema["description"].toString());
        w->setMinimumWidth(320);
        form->addRow(name, w);
        if (const QString d = schema["description"].toString(); !d.isEmpty()) {
            auto* hint = new QLabel(d, this);
            hint->setWordWrap(true);
            hint->setProperty("role", "muted");
            form->addRow(QString(), hint);
        }
        fields_.push_back({name, type, w});
    }
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Run");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QJsonObject args;
        QString error;
        if (arguments(&args, &error)) accept();
        else QMessageBox::warning(this, command_.name, error);
    });
}

QWidget* CommandForm::field(const QString& name) const {
    for (const auto& f : fields_) if (f.name == name) return f.widget;
    return nullptr;
}

bool CommandForm::arguments(QJsonObject* out, QString* error) const {
    QJsonObject args;
    for (const auto& f : fields_) {
        if (auto* box = qobject_cast<QCheckBox*>(f.widget)) {
            if (box->isChecked()) args[f.name] = true;
            continue;
        }
        if (auto* combo = qobject_cast<QComboBox*>(f.widget)) {
            if (!combo->currentText().isEmpty()) args[f.name] = combo->currentText();
            continue;
        }
        const QString text = static_cast<QLineEdit*>(f.widget)->text().trimmed();
        if (text.isEmpty()) continue;
        if (f.type == "integer" || f.type == "number") {
            bool ok = false;
            const double v = text.toDouble(&ok);
            if (!ok) { *error = f.name + ": \"" + text + "\" is not a number"; return false; }
            args[f.name] = v;
        } else if (f.type == "array" || f.type == "object") {
            QJsonParseError e;
            const QJsonDocument d = QJsonDocument::fromJson(text.toUtf8(), &e);
            if (e.error != QJsonParseError::NoError) { *error = f.name + ": not JSON: " + e.errorString(); return false; }
            if (d.isArray()) args[f.name] = d.array(); else args[f.name] = d.object();
        } else {
            args[f.name] = text;
        }
    }
    const QString bad = cmd::validate(command_.params, args);
    if (!bad.isEmpty()) { *error = bad; return false; }
    *out = args;
    return true;
}

CommandPalette::CommandPalette(std::vector<Entry> entries, QWidget* parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint), entries_(std::move(entries)) {
    setAttribute(Qt::WA_TranslucentBackground);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* frame = new QFrame(this);
    frame->setObjectName("palette");
    outer->addWidget(frame);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(1, 1, 1, 6);
    layout->setSpacing(0);
    query_ = new QLineEdit(frame);
    query_->setPlaceholderText("Find a command or open a view");
    list_ = new QListWidget(frame);
    list_->setIconSize({16, 16});
    list_->setUniformItemSizes(true);
    layout->addWidget(query_);
    layout->addWidget(list_);
    resize(640, 400);
    query_->installEventFilter(this);
    connect(query_, &QLineEdit::textChanged, this, [this] { filter(); });
    connect(list_, &QListWidget::itemActivated, this, [this] { activateCurrent(); });
    connect(list_, &QListWidget::itemClicked, this, [this] { activateCurrent(); });
    filter();
}

bool CommandPalette::eventFilter(QObject* o, QEvent* e) {
    if (o == query_ && e->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(e)->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up) {
            const int row = std::clamp(list_->currentRow() + (key == Qt::Key_Down ? 1 : -1), 0, std::max(0, list_->count() - 1));
            list_->setCurrentRow(row);
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            activateCurrent();
            return true;
        }
    }
    return QDialog::eventFilter(o, e);
}

void CommandPalette::setQuery(const QString& q) { query_->setText(q); }

void CommandPalette::filter() {
    std::vector<std::pair<int, size_t>> hits;
    const QString q = query_->text();
    for (size_t k = 0; k < entries_.size(); ++k) {
        const int title = fuzzyScore(q, entries_[k].title);
        const int score = title >= 0 ? title + 100 : fuzzyScore(q, entries_[k].detail);  // the name counts above the description
        if (score >= 0) hits.push_back({score, k});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    list_->clear();
    for (const auto& [score, k] : hits) {
        auto* item = new QListWidgetItem(icons::get(entries_[k].icon), entries_[k].title, list_);
        item->setToolTip(entries_[k].detail);
        item->setData(Qt::UserRole, (qulonglong)k);
    }
    if (list_->count()) list_->setCurrentRow(0);
}

QStringList CommandPalette::shown() const {
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) out << list_->item(i)->text();
    return out;
}

void CommandPalette::activateCurrent() {
    if (!list_->currentItem()) return;
    const auto run = entries_[list_->currentItem()->data(Qt::UserRole).toULongLong()].run;
    accept();
    run();
}

}  // namespace app
