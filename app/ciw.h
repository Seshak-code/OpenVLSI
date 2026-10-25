#pragma once
// The command window (Virtuoso's CIW): the session's main window. Its log echoes every change any window makes (and
// anything typed) with its result; its input line runs `command {json arguments}` (Python joins it later). Its menus
// open the tools.
#include "app/session.h"

#include <QMainWindow>

class QLineEdit;
class QPlainTextEdit;

namespace app {

class LibraryManager;

class Ciw : public QMainWindow {
    Q_OBJECT
public:
    explicit Ciw(Session& session, QWidget* parent = nullptr);
    // `name` or `name {json object}`; false (and the reason in the log) when it does not parse or the command fails
    bool runLine(const QString& line);
    QString logText() const;
    LibraryManager* libraryManager();  // opened on first use

private:
    void append(const QString& html);
    Session& session_;
    QPlainTextEdit* log_;
    QLineEdit* input_;
    LibraryManager* libraryManager_ = nullptr;
    bool typed_ = false;  // the call in progress came from the input line (reads are echoed only then)
};

}  // namespace app
