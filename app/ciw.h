#pragma once
// The command window (Virtuoso's CIW): the session's main window. Its log echoes every change any window makes (and
// anything typed) with its result; its input line runs `command {json arguments}` (Python joins it later). Its menus
// open the tools; it owns the editor windows (editor_open, a command of the GUI session only).
#include "app/session.h"

#include <QMainWindow>
#include <QPointer>

#include <map>

class QLineEdit;
class QPlainTextEdit;

namespace app {

class LibraryManager;
class ViewEditor;

class Ciw : public QMainWindow {
    Q_OBJECT
public:
    explicit Ciw(Session& session, QWidget* parent = nullptr);
    // `name` or `name {json object}`; false (and the reason in the log) when it does not parse or the command fails
    bool runLine(const QString& line);
    QString logText() const;
    LibraryManager* libraryManager();  // opened on first use
    ViewEditor* editor(const QString& lib, const QString& cell, const QString& view) const;  // open, or null

private:
    void append(const QString& html);
    Session& session_;
    QPlainTextEdit* log_;
    QLineEdit* input_;
    LibraryManager* libraryManager_ = nullptr;
    std::map<QString, QPointer<ViewEditor>> editors_;  // "lib/cell/view"
    bool typed_ = false;  // the call in progress came from the input line (reads are echoed only then)
};

}  // namespace app
