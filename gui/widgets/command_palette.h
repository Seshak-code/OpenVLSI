#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QAction>
#include <QMap>
#include <QStringList>

namespace acutesim {
namespace gui {
namespace widgets {

/**
 * CommandPalette - VS Code-style searchable command launcher
 *
 * Triggered by Ctrl+Shift+P, provides quick access to all application
 * commands with fuzzy search filtering.
 */
class CommandPalette : public QDialog {
    Q_OBJECT

public:
    struct Command {
        QString id;           // Unique identifier
        QString label;        // Display name
        QString category;     // Category for grouping (File, Edit, View, etc.)
        QString shortcut;     // Keyboard shortcut hint
        QAction* action;      // Associated QAction (if any)
        std::function<void()> callback; // Direct callback (if no action)
    };

    explicit CommandPalette(QWidget* parent = nullptr);

    // Register commands
    void registerCommand(const Command& cmd);
    void registerAction(QAction* action, const QString& category = "General");
    void clearCommands();

    // Recent commands tracking
    void addToRecent(const QString& commandId);
    QStringList recentCommands() const { return recentCommands_; }

signals:
    void commandExecuted(const QString& commandId);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onItemActivated(QListWidgetItem* item);
    void onItemClicked(QListWidgetItem* item);

private:
    QLineEdit* searchBox_;
    QListWidget* resultsList_;
    QMap<QString, Command> commands_;
    QStringList recentCommands_;

    void updateResults(const QString& filter);
    void executeCommand(const QString& commandId);
    void selectNextItem();
    void selectPreviousItem();
    bool fuzzyMatch(const QString& text, const QString& pattern) const;
    int fuzzyScore(const QString& text, const QString& pattern) const;
    void setupStyle();
};

} // namespace widgets
} // namespace gui
} // namespace acutesim
