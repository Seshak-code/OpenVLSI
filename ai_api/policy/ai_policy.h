#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QRegularExpression>

namespace AIAPI {

/**
 * AIPolicy - Enforces "Raw Evidence First" and citation requirements.
 * 
 * Phase 1.7.10: AI Integration Hooks
 * 
 * Policy 1: "Raw Evidence First" - AI explanations MUST be paired with solver proof.
 * Policy 2: AI suggestions MUST cite specific log lines or data points.
 */

struct AIPolicy {
    // Policy flags
    bool rawEvidenceFirst = true;      // AI explains *after* solver proof shown
    bool requireCitations = true;       // AI must cite log lines or data
    int minCitationsPerResponse = 1;    // Minimum citations required
    
    // Evidence requirements
    QStringList requiredEvidenceFields = {
        "convergence_status",
        "max_residual",
        "solver_method"
    };
    
    static AIPolicy defaultPolicy() {
        return AIPolicy();
    }
    
    static AIPolicy relaxedPolicy() {
        AIPolicy p;
        p.requireCitations = false;
        p.minCitationsPerResponse = 0;
        return p;
    }
};

/**
 * CitationValidator - Validates that AI responses contain proper citations.
 */
class CitationValidator {
public:
    struct ValidationResult {
        bool valid = false;
        int citationCount = 0;
        QStringList citedLines;
        QString errorMessage;
    };
    
    /**
     * Validates an AI response against the policy.
     * Looks for citation patterns like:
     * - [Line 42]
     * - (Log: "Converged in 5 iterations")
     * - Residual: 1.23e-12 A
     */
    static ValidationResult validate(const QString& aiResponse, const AIPolicy& policy) {
        ValidationResult result;
        
        // Pattern 1: [Line N] or [L:N]
        QRegularExpression linePattern(R"(\[(?:Line|L)[:\s]*(\d+)\])");
        auto lineMatches = linePattern.globalMatch(aiResponse);
        while (lineMatches.hasNext()) {
            auto match = lineMatches.next();
            result.citedLines.append("Line " + match.captured(1));
            result.citationCount++;
        }
        
        // Pattern 2: Quoted log text
        QRegularExpression logPattern(R"raw(\(Log:\s*"([^"]+)"\))raw");
        auto logMatches = logPattern.globalMatch(aiResponse);
        while (logMatches.hasNext()) {
            auto match = logMatches.next();
            result.citedLines.append("Log: " + match.captured(1));
            result.citationCount++;
        }
        
        // Pattern 3: Data citation (e.g., "Residual: 1.23e-12 A")
        QRegularExpression dataPattern(R"((Residual|Iterations|Convergence):\s*[\d.eE+-]+)");
        auto dataMatches = dataPattern.globalMatch(aiResponse);
        while (dataMatches.hasNext()) {
            auto match = dataMatches.next();
            result.citedLines.append(match.captured(0));
            result.citationCount++;
        }
        
        // Validate against policy
        if (policy.requireCitations) {
            if (result.citationCount < policy.minCitationsPerResponse) {
                result.valid = false;
                result.errorMessage = QString(
                    "Insufficient citations: found %1, required %2. "
                    "AI responses must cite specific log lines or data points.")
                    .arg(result.citationCount)
                    .arg(policy.minCitationsPerResponse);
            } else {
                result.valid = true;
            }
        } else {
            result.valid = true;
        }
        
        return result;
    }
    
    /**
     * Wraps an AI response with evidence header if policy requires.
     * Ensures solver proof is shown BEFORE AI explanation.
     */
    static QString wrapWithEvidence(const QString& aiResponse, 
                                    const QJsonObject& solverEvidence,
                                    const AIPolicy& policy) {
        if (!policy.rawEvidenceFirst) {
            return aiResponse;
        }
        
        QString evidenceBlock = "## Solver Evidence\n";
        evidenceBlock += QString("- **Status**: %1\n").arg(
            solverEvidence["converged"].toBool() ? "✅ Converged" : "❌ Failed"
        );
        evidenceBlock += QString("- **Method**: %1\n").arg(
            solverEvidence["method"].toString("Newton-Raphson")
        );
        evidenceBlock += QString("- **Iterations**: %1\n").arg(
            solverEvidence["iterations"].toInt()
        );
        evidenceBlock += QString("- **Max Residual**: %1 A\n").arg(
            solverEvidence["max_residual"].toDouble(), 0, 'e', 2
        );
        
        if (solverEvidence["gmin_stepping"].toBool()) {
            evidenceBlock += "- ⚠️ GMIN stepping was required\n";
        }
        
        evidenceBlock += "\n---\n\n## AI Analysis\n";
        evidenceBlock += aiResponse;
        
        return evidenceBlock;
    }
};

} // namespace AIAPI
