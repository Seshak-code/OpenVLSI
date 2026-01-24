#ifndef AI_ACTION_HISTORY_H
#define AI_ACTION_HISTORY_H

#include <QObject>
#include <QStack>
#include <QVector>
#include "ai_action_envelope.h"

namespace acutesim {
namespace gui {
namespace ai {

/**
 * @brief Manages the history of AI actions for Undo/Redo and grouping.
 * 
 * Ensures all AI modifications are reversible and auditable.
 */
class AiActionHistory : public QObject {
    Q_OBJECT

public:
    static AiActionHistory& instance();

    // Push a new action (clears redo stack)
    void pushAction(const AiActionEnvelope& action);

    // Navigation
    bool canUndo() const;
    bool canRedo() const;
    
    AiActionEnvelope undo();
    AiActionEnvelope redo();

    // Grouping (for Multi-step suggestions)
    void beginGroup(const QString& groupName);
    void endGroup();

    // Audit
    QVector<AiActionEnvelope> getHistory() const;
    void clear();

signals:
    void historyChanged();
    void actionUndone(const AiActionEnvelope& action);
    void actionRedone(const AiActionEnvelope& action);

private:
    AiActionHistory(QObject* parent = nullptr);
    ~AiActionHistory() = default;
    Q_DISABLE_COPY(AiActionHistory)

    struct HistoryEntry {
        AiActionEnvelope action;
        QString groupName; // Empty if single
    };

    QStack<HistoryEntry> undoStack_;
    QStack<HistoryEntry> redoStack_;
    
    QString currentGroupName_;
    bool isGrouping_ = false;
};

} // namespace ai
} // namespace gui
} // namespace acutesim

#endif // AI_ACTION_HISTORY_H
