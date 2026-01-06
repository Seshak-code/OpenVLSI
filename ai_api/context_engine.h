#pragma once
#include <QString>
#include <QJsonObject>

namespace AIAPI {

class ContextEngine {
public:
    /**
     * Generates a comprehensive system prompt describing the current circuit state.
     * This includes:
     * - Top-level stats (e.g. "Circuit with 15 components")
     * - Textual Netlist (e.g. "R1 connects Node 1 to Node 2")
     * - Parameter details (e.g. "M1 is an NMOS with W=10u")
     * - Visual context (e.g. "Components are placed in a cascade structure")
     */
    static QString generateSystemPrompt();
    
    /**
     * Generates a compact JSON summary token-optimized for LLM context windows.
     */
    static QJsonObject getContextJson();

private:
    static QString describeComponent(const QJsonObject& comp);
    static QString inferTopologyIntent(const QJsonObject& state);
    static QString getSpatialContext();
};

} // namespace AIAPI
