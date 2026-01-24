#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QStringList>

class TerminalWidget : public QWidget {
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget* parent = nullptr);

    // API for Copilot / External Agents
    Q_SLOT void executeCommand(const QString& fullCommand);

signals:
    // Emitted when a command is finalized (User Enter or Copilot)
    void commandRequested(const QString& command);

private slots:
    void onReturnPressed();

private:
    QPlainTextEdit* outputArea_;
    QLineEdit* inputBox_;
    QStringList history_;
    int historyIndex_ = 0;

    void appendOutput(const QString& text, bool isError = false);
    void processInternalCommands(const QString& cmd); // e.g. clear, help
};
