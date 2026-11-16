#pragma once
// The command palette (Cmd+K) and the form behind it. Every command in the registry can be found by name or by what it
// does, along with every view in the workspace. A command with parameters opens a form generated from its JSON schema,
// so a command added anywhere is reachable from the GUI the moment it exists.
#include "app/session.h"

#include <QDialog>
#include <QJsonObject>

#include <functional>
#include <vector>

class QLineEdit;
class QListWidget;

namespace app {

// Subsequence match of query in text, case-insensitive: higher is better, < 0 is no match. Runs of consecutive
// characters and starts of words score up.
int fuzzyScore(const QString& query, const QString& text);

// What a form's field can hold: the workspace's values for it (libraries, cells, runs, ...), or a file or folder to browse for
struct FieldChoices {
    QStringList values;
    bool file = false, folder = false;  // Browse…: a file, a folder
};
// The workspace's values for a command's parameter, given what the form holds now (the cell list follows the library field, ...)
using ChoiceSource = std::function<FieldChoices(const QString& command, const QString& param, const QJsonObject& schema, const QJsonObject& current)>;

class CommandForm : public QDialog {
    Q_OBJECT
public:
    // Where every form's fields find their choices: the shell sets it once (a form without it offers none)
    static void setChoiceSource(ChoiceSource source);
    // prefill: values for parameters of the same name (the selected library, cell, ...)
    CommandForm(const cmd::Command& c, const QJsonObject& prefill, QWidget* parent = nullptr);
    // The arguments as typed: empty optional fields left out, each field parsed per its schema. False and the reason
    // when a field does not parse or the result fails the command's schema (the form stays open).
    bool arguments(QJsonObject* out, QString* error) const;
    QWidget* field(const QString& name) const;  // QLineEdit, QComboBox or QCheckBox

private:
    QJsonObject current() const;  // what the fields hold now, leniently: the text of each
    void offer(QLineEdit* line, const QString& name, const QString& type, const QJsonObject& schema, const QPoint& at);
    struct Field { QString name, type; QWidget* widget; };
    const cmd::Command& command_;
    std::vector<Field> fields_;
};

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    struct Entry {
        QString title, detail, icon;
        std::function<void()> run;
    };
    CommandPalette(std::vector<Entry> entries, QWidget* parent = nullptr);
    void setQuery(const QString& q);
    QStringList shown() const;  // the titles listed now, best first
    void activateCurrent();

private:
    void filter();
    std::vector<Entry> entries_;
    QLineEdit* query_;
    QListWidget* list_;
    bool eventFilter(QObject* o, QEvent* e) override;
};

}  // namespace app
