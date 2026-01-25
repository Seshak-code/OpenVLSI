#include "command_palette.h"
#include "../styling/theme_manager.h"
#include <QApplication>
#include <QScreen>
#include <algorithm>

namespace acutesim {
namespace gui {
namespace widgets {

CommandPalette::CommandPalette(QWidget* parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setModal(true);
    setAttribute(Qt::WA_TranslucentBackground);

    // Layout
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Search box
    searchBox_ = new QLineEdit(this);
    searchBox_->setPlaceholderText("Type a command...");
    searchBox_->setClearButtonEnabled(true);
    layout->addWidget(searchBox_);

    // Results list
    resultsList_ = new QListWidget(this);
    resultsList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    resultsList_->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(resultsList_);

    // Connections
    connect(searchBox_, &QLineEdit::textChanged, this, &CommandPalette::onSearchTextChanged);
    connect(resultsList_, &QListWidget::itemActivated, this, &CommandPalette::onItemActivated);
    connect(resultsList_, &QListWidget::itemClicked, this, &CommandPalette::onItemClicked);

    setupStyle();

    // Fixed width, variable height
    setFixedWidth(600);
    setMaximumHeight(400);
}

void CommandPalette::setupStyle() {
    auto& tm = styling::ThemeManager::instance();

    QString bgColor = tm.getColor("ui.background").name();
    QString borderColor = tm.getColor("ui.border").name();
    QString textColor = tm.getColor("ui.text").name();
    QString accentColor = tm.getColor("ui.accent").name();
    QString hoverColor = "#2a2d2e";
    QString selectedColor = "#094771";

    setStyleSheet(QString(R"(
        CommandPalette {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 6px;
        }
        QLineEdit {
            background-color: %1;
            color: %3;
            border: none;
            border-bottom: 1px solid %2;
            padding: 12px 16px;
            font-size: 14px;
        }
        QLineEdit:focus {
            outline: none;
        }
        QListWidget {
            background-color: %1;
            color: %3;
            border: none;
            outline: none;
        }
        QListWidget::item {
            padding: 8px 16px;
            border: none;
        }
        QListWidget::item:hover {
            background-color: %4;
        }
        QListWidget::item:selected {
            background-color: %5;
        }
    )").arg(bgColor, borderColor, textColor, hoverColor, selectedColor));
}

void CommandPalette::registerCommand(const Command& cmd) {
    commands_[cmd.id] = cmd;
}

void CommandPalette::registerAction(QAction* action, const QString& category) {
    if (!action) return;

    Command cmd;
    cmd.id = action->objectName().isEmpty() ? action->text() : action->objectName();
    cmd.label = action->text().remove('&'); // Remove mnemonics
    cmd.category = category;
    cmd.shortcut = action->shortcut().toString(QKeySequence::NativeText);
    cmd.action = action;

    registerCommand(cmd);
}

void CommandPalette::clearCommands() {
    commands_.clear();
}

void CommandPalette::addToRecent(const QString& commandId) {
    recentCommands_.removeAll(commandId);
    recentCommands_.prepend(commandId);
    if (recentCommands_.size() > 10) {
        recentCommands_ = recentCommands_.mid(0, 10);
    }
}

void CommandPalette::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);

    // Center horizontally at top of parent
    if (parentWidget()) {
        QPoint parentCenter = parentWidget()->mapToGlobal(
            QPoint(parentWidget()->width() / 2, 100));
        move(parentCenter.x() - width() / 2, parentCenter.y());
    }

    searchBox_->clear();
    searchBox_->setFocus();
    updateResults("");
}

void CommandPalette::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Escape:
            hide();
            break;
        case Qt::Key_Down:
            selectNextItem();
            break;
        case Qt::Key_Up:
            selectPreviousItem();
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (resultsList_->currentItem()) {
                onItemActivated(resultsList_->currentItem());
            }
            break;
        default:
            QDialog::keyPressEvent(event);
    }
}

void CommandPalette::onSearchTextChanged(const QString& text) {
    updateResults(text);
}

void CommandPalette::onItemActivated(QListWidgetItem* item) {
    if (!item) return;
    QString commandId = item->data(Qt::UserRole).toString();
    executeCommand(commandId);
    hide();
}

void CommandPalette::onItemClicked(QListWidgetItem* item) {
    onItemActivated(item);
}

void CommandPalette::updateResults(const QString& filter) {
    resultsList_->clear();

    // Collect and score matching commands
    QVector<QPair<int, QString>> scoredResults;

    for (auto it = commands_.begin(); it != commands_.end(); ++it) {
        const Command& cmd = it.value();
        QString searchText = cmd.category + " " + cmd.label;

        if (filter.isEmpty()) {
            // Show recent commands first when no filter
            int score = recentCommands_.indexOf(cmd.id);
            if (score >= 0) {
                scoredResults.append({1000 - score, cmd.id});
            } else {
                scoredResults.append({0, cmd.id});
            }
        } else if (fuzzyMatch(searchText, filter)) {
            int score = fuzzyScore(searchText, filter);
            scoredResults.append({score, cmd.id});
        }
    }

    // Sort by score (descending)
    std::sort(scoredResults.begin(), scoredResults.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Populate list (limit to 15 items)
    int count = 0;
    for (const auto& [score, cmdId] : scoredResults) {
        if (count >= 15) break;

        const Command& cmd = commands_[cmdId];

        QListWidgetItem* item = new QListWidgetItem(resultsList_);
        item->setData(Qt::UserRole, cmdId);

        // Format: "Category: Label    Shortcut"
        QString displayText = cmd.label;
        if (!cmd.category.isEmpty() && cmd.category != "General") {
            displayText = cmd.category + ": " + cmd.label;
        }
        if (!cmd.shortcut.isEmpty()) {
            displayText += QString("    %1").arg(cmd.shortcut);
        }
        item->setText(displayText);

        count++;
    }

    // Select first item
    if (resultsList_->count() > 0) {
        resultsList_->setCurrentRow(0);
    }

    // Adjust height based on content
    int itemHeight = 36;
    int headerHeight = 48;
    int contentHeight = qMin(resultsList_->count() * itemHeight, 300);
    setFixedHeight(headerHeight + contentHeight + 8);
}

void CommandPalette::executeCommand(const QString& commandId) {
    if (!commands_.contains(commandId)) return;

    const Command& cmd = commands_[commandId];
    addToRecent(commandId);

    try {
        if (cmd.action && cmd.action->isEnabled()) {
            cmd.action->trigger();
        } else if (cmd.callback) {
            cmd.callback();
        }
    } catch (const std::exception& e) {
        qWarning() << "Command execution failed:" << commandId << "-" << e.what();
    } catch (...) {
        qWarning() << "Command execution failed:" << commandId << "- unknown error";
    }

    emit commandExecuted(commandId);
}

void CommandPalette::selectNextItem() {
    int current = resultsList_->currentRow();
    if (current < resultsList_->count() - 1) {
        resultsList_->setCurrentRow(current + 1);
    }
}

void CommandPalette::selectPreviousItem() {
    int current = resultsList_->currentRow();
    if (current > 0) {
        resultsList_->setCurrentRow(current - 1);
    }
}

bool CommandPalette::fuzzyMatch(const QString& text, const QString& pattern) const {
    if (pattern.isEmpty()) return true;

    QString lowerText = text.toLower();
    QString lowerPattern = pattern.toLower();

    int textIdx = 0;
    for (int patIdx = 0; patIdx < lowerPattern.length(); ++patIdx) {
        QChar c = lowerPattern[patIdx];
        int found = lowerText.indexOf(c, textIdx);
        if (found < 0) return false;
        textIdx = found + 1;
    }
    return true;
}

int CommandPalette::fuzzyScore(const QString& text, const QString& pattern) const {
    if (pattern.isEmpty()) return 0;

    QString lowerText = text.toLower();
    QString lowerPattern = pattern.toLower();

    int score = 0;
    int textIdx = 0;
    int consecutiveBonus = 0;

    // Exact prefix match bonus
    if (lowerText.startsWith(lowerPattern)) {
        score += 100;
    }

    // Word start bonus
    QStringList words = lowerText.split(QRegularExpression("[\\s:_-]+"));
    for (const QString& word : words) {
        if (word.startsWith(lowerPattern)) {
            score += 50;
            break;
        }
    }

    // Character matching with consecutive bonus
    for (int patIdx = 0; patIdx < lowerPattern.length(); ++patIdx) {
        QChar c = lowerPattern[patIdx];
        int found = lowerText.indexOf(c, textIdx);
        if (found < 0) return 0;

        if (found == textIdx) {
            consecutiveBonus += 5;
            score += consecutiveBonus;
        } else {
            consecutiveBonus = 0;
            score += 1;
        }
        textIdx = found + 1;
    }

    return score;
}

} // namespace widgets
} // namespace gui
} // namespace acutesim
