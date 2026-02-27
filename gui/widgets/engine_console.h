#pragma once
// ============================================================================
// engine_console.h — Interactive Engine Console Widget
// ============================================================================
// Routes all commands exclusively through ISimulationDriver.
// ARCHITECTURAL RULE: Never invokes solver directly. Never parses engine logs.
// ============================================================================

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QStringList>
#include <QCompleter>

namespace acutesim {
namespace gui {
namespace analysis { class SimulationDriverAdapter; }
namespace widgets {

class EngineConsole : public QWidget {
    Q_OBJECT
public:
    explicit EngineConsole(QWidget* parent = nullptr);

    // Set the adapter for command execution
    void setDriverAdapter(acutesim::gui::analysis::SimulationDriverAdapter* adapter);

private slots:
    void onCommandEntered();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUi();
    void appendOutput(const QString& text, const QColor& color = QColor(205, 214, 244));
    void appendPrompt();

    QPlainTextEdit* output_;
    QLineEdit* input_;
    QCompleter* completer_;

    acutesim::gui::analysis::SimulationDriverAdapter* adapter_ = nullptr;
    QStringList history_;
    int historyIndex_ = -1;
};

} // namespace widgets
} // namespace gui
} // namespace acutesim
