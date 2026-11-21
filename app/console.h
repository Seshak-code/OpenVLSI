#pragma once
// Script consoles (Innovus's Tcl prompt, Virtuoso's CIW for SKILL): Tcl inside the app, every command a Tcl command
// (`cell_list -library analog`, results as dicts and lists); Python as the user's python3, its `ov` the running app through
// its API socket (`ov.cell_list(library="analog")`). Either way each call goes through the Session, so the windows follow.
#include "app/session.h"

#include <QStringList>
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QProcess;
struct Tcl_Interp;

namespace app {

class Console : public QWidget {
    Q_OBJECT
public:
    Console(Session& session, const QString& language, QWidget* parent = nullptr);  // "tcl" or "python"; throws when it cannot start
    ~Console() override;
    QString language() const { return language_; }
    // Runs code as if typed (a block may span lines); returns what it printed and its value. Throws with the error.
    QString evaluate(const QString& code);
    QString logText() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;  // Up and Down walk the history

private:
    void submit();
    void show(const QString& text, const QString& color = {});
    QString evaluateTcl(const QString& code);
    QString evaluatePython(const QString& code);
    Session& session_;
    QString language_;
    QPlainTextEdit* log_;
    QLabel* prompt_;
    QLineEdit* input_;
    QString pending_;  // lines of a block not yet complete
    QStringList history_;
    int back_ = 0;  // how far up the history Up has gone
    Tcl_Interp* tcl_ = nullptr;
    QProcess* python_ = nullptr;
    QString out_;      // what the code printed (Tcl's puts, python's output) since the evaluation began
    int prompts_ = 0;
    bool failed_ = false;  // python's code raised (it printed \x03)  // python's prompts so far: one comes back as each line is done
    friend struct TclGlue;
};

}  // namespace app
