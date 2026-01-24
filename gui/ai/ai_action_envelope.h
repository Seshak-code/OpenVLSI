#ifndef AI_ACTION_ENVELOPE_H
#define AI_ACTION_ENVELOPE_H

#include <QString>
#include <QVariant>
#include <QDateTime>

namespace acutesim {
namespace gui {
namespace ai {

/**
 * @brief Strict contract for AI <-> GUI interaction.
 * 
 * Prevents AI from directly mutating internal structures.
 * All AI suggestions must pass through this envelope to be rendered as Ghosts
 * or applied as transactions.
 */
struct AiActionEnvelope {
    enum class Type {
        PlaceComponent,   // Add new component (Ghost)
        ModifyProperty,   // Change parameter (Ghost)
        SuggestNetlist,   // Full replacement (Diff)
        Explain,          // Text explanation only
        HighlightNetwork  // Visual focus only
    };

    enum class ConfidenceLevel {
        High,    // > 0.8
        Medium,  // 0.5 - 0.8
        Low      // < 0.5
    };

    // Metadata
    QString actionId;           // Unique UUID
    Type type;
    QDateTime timestamp;
    float confidenceScore;      // 0.0 to 1.0

    // Content
    QString intent;             // Short user-facing description e.g. "Add Bias Resistor"
    QString rationale;          // "Why?" explanation
    QVariant payload;           // Structured data (ComponentParams, Diff, etc.)
    
    // Context Validation
    QString relatedSchematicHash; // Ensure action applies to current state
    QStringList contextChips;     // IDs of chips used to generate this

    bool isValid() const {
        return !actionId.isEmpty() && confidenceScore >= 0.0f;
    }

    ConfidenceLevel getConfidenceLevel() const {
        if (confidenceScore > 0.8f) return ConfidenceLevel::High;
        if (confidenceScore > 0.5f) return ConfidenceLevel::Medium;
        return ConfidenceLevel::Low;
    }
};

} // namespace ai
} // namespace gui
} // namespace acutesim

#endif // AI_ACTION_ENVELOPE_H
