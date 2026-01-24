#include "terminal_widget.h"
#include <QDateTime>
#include <QScrollBar>
#include "../../gui/styling/theme_manager.h"

TerminalWidget::TerminalWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Output Area
    outputArea_ = new QPlainTextEdit(this);
    outputArea_->setReadOnly(true);
    outputArea_->setReadOnly(true);
    // Style handled by ThemeManager generally, but we enforce monospace here
    auto& tm = acutesim::gui::styling::ThemeManager::instance();
    outputArea_->setStyleSheet(QString("font-family: Consolas, 'Courier New', monospace; font-size: 10pt; border: none; background-color: %1; color: %2;")
                               .arg(tm.getColor("ui.background").name())
                               .arg(tm.getColor("ui.text").name()));
    layout->addWidget(outputArea_);

    // Input Line
    inputBox_ = new QLineEdit(this);
    inputBox_->setPlaceholderText("Type command or ask Copilot (e.g., 'sync -to-hdl', 'run script.py')...");
    inputBox_->setStyleSheet(QString("border-top: 1px solid %1; padding: 4px; background-color: %2; color: %3;")
                             .arg(tm.getColor("ui.border").name())
                             .arg(tm.getColor("input.background").name())
                             .arg(tm.getColor("input.text").name()));
    layout->addWidget(inputBox_);

    connect(inputBox_, &QLineEdit::returnPressed, this, &TerminalWidget::onReturnPressed);

    // Initial greeting
    appendOutput("AcuteSim Terminal [Version 1.0]");
    appendOutput("Type 'help' for available commands.");
}

void TerminalWidget::executeCommand(const QString& fullCommand) {
    if (fullCommand.trimmed().isEmpty()) return;

    // 1. Echo Command
    appendOutput(QString("> %1").arg(fullCommand));

    // 2. Internal Handling (Clear, Local Echo)
    QString cmd = fullCommand.trimmed();
    if (cmd == "clear" || cmd == "cls") {
        outputArea_->clear();
        return;
    }
    
    // 3. Emit for System Execution (MainWindow / DockingManager)
    emit commandRequested(cmd);
}

void TerminalWidget::onReturnPressed() {
    QString cmd = inputBox_->text();
    inputBox_->clear();
    executeCommand(cmd);
}

void TerminalWidget::appendOutput(const QString& text, bool isError) {
    // Basic formatting
    // Feature idea: Use HTML for color if needed
    outputArea_->appendPlainText(text);
    outputArea_->verticalScrollBar()->setValue(outputArea_->verticalScrollBar()->maximum());
}

void TerminalWidget::processInternalCommands(const QString& cmd) {
    // Reserved for future local logic
}
