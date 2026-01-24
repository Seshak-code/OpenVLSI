#include "ai_action_history.h"
#include <QDebug>

namespace acutesim {
namespace gui {
namespace ai {

AiActionHistory& AiActionHistory::instance() {
    static AiActionHistory instance;
    return instance;
}

AiActionHistory::AiActionHistory(QObject* parent) : QObject(parent) {}

void AiActionHistory::pushAction(const AiActionEnvelope& action) {
    if (!action.isValid()) {
        qWarning() << "AiActionHistory: Attempted to push invalid action";
        return;
    }

    HistoryEntry entry;
    entry.action = action;
    if (isGrouping_) {
        entry.groupName = currentGroupName_;
    }

    undoStack_.push(entry);
    redoStack_.clear();

    emit historyChanged();
}

bool AiActionHistory::canUndo() const {
    return !undoStack_.isEmpty();
}

bool AiActionHistory::canRedo() const {
    return !redoStack_.isEmpty();
}

AiActionEnvelope AiActionHistory::undo() {
    if (undoStack_.isEmpty()) return {};

    HistoryEntry entry = undoStack_.pop();
    redoStack_.push(entry);
    
    // Handle Grouping: If part of a group, undo all in that group
    if (!entry.groupName.isEmpty()) {
        while (!undoStack_.isEmpty() && undoStack_.top().groupName == entry.groupName) {
            redoStack_.push(undoStack_.pop());
        }
    }

    emit actionUndone(entry.action);
    emit historyChanged();
    return entry.action;
}

AiActionEnvelope AiActionHistory::redo() {
    if (redoStack_.isEmpty()) return {};

    HistoryEntry entry = redoStack_.pop();
    undoStack_.push(entry);

    // Handle Grouping
    if (!entry.groupName.isEmpty()) {
        while (!redoStack_.isEmpty() && redoStack_.top().groupName == entry.groupName) {
            undoStack_.push(redoStack_.pop());
        }
    }

    emit actionRedone(entry.action);
    emit historyChanged();
    return entry.action;
}

void AiActionHistory::beginGroup(const QString& groupName) {
    isGrouping_ = true;
    currentGroupName_ = groupName;
}

void AiActionHistory::endGroup() {
    isGrouping_ = false;
    currentGroupName_.clear();
}

QVector<AiActionEnvelope> AiActionHistory::getHistory() const {
    QVector<AiActionEnvelope> result;
    // Iterate stack (needs workaround or copy if not iterable directly, QStack inherits QVector)
    for (const auto& entry : undoStack_) {
        result.append(entry.action);
    }
    return result;
}

void AiActionHistory::clear() {
    undoStack_.clear();
    redoStack_.clear();
    emit historyChanged();
}

} // namespace ai
} // namespace gui
} // namespace acutesim
