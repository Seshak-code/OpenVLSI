#include "engine_console.h"
#include "../analysis/simulation_driver_adapter.h"
#include <QKeyEvent>
#include <QScrollBar>

namespace acutesim {
namespace gui {
namespace widgets {

EngineConsole::EngineConsole(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void EngineConsole::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Output area (read-only)
    output_ = new QPlainTextEdit();
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(5000);
    output_->setStyleSheet(R"(
        QPlainTextEdit {
            background-color: #11111b;
            color: #cdd6f4;
            border: none;
            font-family: 'JetBrains Mono', 'Fira Code', 'SF Mono', monospace;
            font-size: 12px;
            padding: 8px;
        }
    )");
    layout->addWidget(output_, 1);

    // Input line
    input_ = new QLineEdit();
    input_->setPlaceholderText("Type a command (try 'help')...");
    input_->setStyleSheet(R"(
        QLineEdit {
            background-color: #1e1e2e;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 0px;
            font-family: 'JetBrains Mono', 'Fira Code', 'SF Mono', monospace;
            font-size: 12px;
            padding: 6px 8px;
        }
        QLineEdit:focus {
            border-color: #89b4fa;
        }
    )");
    connect(input_, &QLineEdit::returnPressed, this, &EngineConsole::onCommandEntered);
    input_->installEventFilter(this);
    layout->addWidget(input_);

    // Autocomplete
    QStringList commands = {"run dc", "run tran", "run ac", "sweep", "mc", "help", "status"};
    completer_ = new QCompleter(commands, this);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    input_->setCompleter(completer_);

    // Welcome
    appendOutput("AcuteSim Engine Console v1.0", QColor(166, 227, 161));
    appendOutput("Type 'help' for available commands.\n", QColor(166, 173, 200));
    appendPrompt();
}

void EngineConsole::setDriverAdapter(analysis::SimulationDriverAdapter* adapter) {
    adapter_ = adapter;
}

void EngineConsole::onCommandEntered() {
    QString cmdText = input_->text().trimmed();
    input_->clear();

    if (cmdText.isEmpty()) {
        appendPrompt();
        return;
    }

    // Record history
    history_.prepend(cmdText);
    if (history_.size() > 100) history_.removeLast();
    historyIndex_ = -1;

    // Echo command
    appendOutput("> " + cmdText, QColor(137, 180, 250));

    if (!adapter_) {
        appendOutput("Error: No simulation driver connected.", QColor(255, 120, 120));
        appendPrompt();
        return;
    }

    // Execute via adapter (through ISimulationDriver)
    compute::orchestration::TerminalCommandDTO dto;
    dto.rawCommand = cmdText.toStdString();
    dto.sessionToken = "console-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());

    auto resp = adapter_->executeCommand(dto);

    if (resp.success) {
        if (!resp.output.empty()) {
            appendOutput(QString::fromStdString(resp.output), QColor(166, 227, 161));
        }
    } else {
        appendOutput("Error: " + QString::fromStdString(resp.errorDetail), QColor(255, 120, 120));
    }

    appendPrompt();
}

bool EngineConsole::eventFilter(QObject* obj, QEvent* event) {
    if (obj == input_ && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        if (keyEvent->key() == Qt::Key_Up) {
            if (!history_.isEmpty() && historyIndex_ < history_.size() - 1) {
                historyIndex_++;
                input_->setText(history_[historyIndex_]);
            }
            return true;
        }
        if (keyEvent->key() == Qt::Key_Down) {
            if (historyIndex_ > 0) {
                historyIndex_--;
                input_->setText(history_[historyIndex_]);
            } else {
                historyIndex_ = -1;
                input_->clear();
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void EngineConsole::appendOutput(const QString& text, const QColor& color) {
    output_->appendHtml(
        "<span style='color: " + color.name() + ";'>" +
        text.toHtmlEscaped().replace("\n", "<br>") +
        "</span>"
    );
    output_->verticalScrollBar()->setValue(output_->verticalScrollBar()->maximum());
}

void EngineConsole::appendPrompt() {
    output_->appendHtml(
        "<span style='color: #a6adc8;'>acutesim&gt;</span>"
    );
}

} // namespace widgets
} // namespace gui
} // namespace acutesim
