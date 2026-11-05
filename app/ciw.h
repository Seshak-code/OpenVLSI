#pragma once
// The command window (Virtuoso's CIW): the session's main window. Its log echoes every change any window makes (and
// anything typed) with its result; its input line runs `command {json arguments}` (Python joins it later). Its menus
// open the tools; it owns the editor windows (editor_open, a command of the GUI session only).
#include "app/session.h"

#include <QMainWindow>
#include "app/signoff.h"

#include <QPointer>

#include <functional>
#include <map>

class QLineEdit;
class QPlainTextEdit;

namespace app {

class LibraryManager;
class ViewEditor;
class AdeExplorer;
class WaveformViewer;
class LayoutViewer;
class Verifier;
class FlowRunView;
class TestRunView;
class RtlEditor;

class Ciw : public QMainWindow {
    Q_OBJECT
public:
    explicit Ciw(Session& session, QWidget* parent = nullptr);
    // `name` or `name {json object}`; false (and the reason in the log) when it does not parse or the command fails
    bool runLine(const QString& line);
    // Where tools open: the shell's tabs. Unset, each is a window of its own.
    void setHost(std::function<void(QWidget*)> host) { host_ = std::move(host); }
    QString logText() const;
    LibraryManager* libraryManager();  // opened on first use
    ViewEditor* editor(const QString& lib, const QString& cell, const QString& view) const;  // open, or null
    AdeExplorer* explorer(const QString& lib, const QString& cell, const QString& view) const;
    LayoutViewer* layout(const QString& lib, const QString& cell, const QString& view) const;
    Verifier* verifier(const QString& lib, const QString& cell, const QString& view) const;
    FlowRunView* flowView(const QString& run) const;
    TestRunView* testView(const QString& run) const;
    RtlEditor* rtlEditor(const QString& file, const QString& design);  // opened, and shown, if need be
    SignoffView* signoffView(const QString& run, SignoffView::Kind kind);  // opened, and shown, if need be
    WaveformViewer* viewer(const QString& run) const;  // the open viewer on a run directory, or null

private:
    void append(const QString& html);
    void present(QWidget* tool);
    std::function<void(QWidget*)> host_;
    Session& session_;
    QPlainTextEdit* log_;
    QLineEdit* input_;
    LibraryManager* libraryManager_ = nullptr;
    std::map<QString, QPointer<QWidget>> editors_;  // "lib/cell/view": a ViewEditor or an AdeExplorer
    std::map<QString, QPointer<FlowRunView>> flowViews_;
    std::map<QString, QPointer<TestRunView>> testViews_;
    std::map<QString, QPointer<RtlEditor>> rtlEditors_;
    std::map<QString, QPointer<SignoffView>> signoffViews_;
    std::vector<QPointer<WaveformViewer>> viewers_;  // in the order they were opened (a probe goes to the newest)
    WaveformViewer* openViewer(cmd::Context& c, const QString& run);
    bool typed_ = false;  // the call in progress came from the input line (reads are echoed only then)
};

}  // namespace app
