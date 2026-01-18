#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>
#include <string>

namespace AIAPI {

/**
 * @file topology_analyzer.h
 * @brief Phase 1.26 Step 5: Topology-Aware Prompt Compression
 * 
 * This module analyzes circuit topology to:
 * 1. Identify functional blocks (Amplifiers, Bias Networks, Current Mirrors, etc.)
 * 2. Extract and summarize signal pathways for token-efficient LLM prompts
 */

// ============================================================================
// 5.1: FUNCTIONAL BLOCK TAXONOMY
// ============================================================================

/**
 * Analog block types recognized by the topology analyzer.
 */
enum class FunctionalBlockType {
    UNKNOWN,
    
    // Amplification
    COMMON_EMITTER,      // BJT CE amplifier
    COMMON_COLLECTOR,    // Emitter follower
    COMMON_BASE,         // CB amplifier
    COMMON_SOURCE,       // MOSFET CS amplifier
    COMMON_DRAIN,        // Source follower
    COMMON_GATE,         // CG amplifier
    DIFFERENTIAL_PAIR,   // Diff pair (BJT or MOSFET)
    DARLINGTON_PAIR,     // Cascaded BJT
    CASCODE,             // Stacked transistors
    
    // Biasing
    RESISTOR_DIVIDER,    // Voltage divider
    CURRENT_MIRROR,      // BJT or MOSFET mirror
    BETA_MULTIPLIER,     // Self-biased reference
    BANDGAP_REFERENCE,   // Vbg circuit
    
    // Feedback & Regulation
    NEGATIVE_FEEDBACK,   // Generic NFB network
    EMITTER_DEGENERATION, // RE resistor
    SOURCE_DEGENERATION,  // RS resistor
    
    // Power
    POWER_STAGE,         // Output driver
    PUSH_PULL,           // Complementary output
    
    // Passive
    RC_FILTER,           // Low-pass/high-pass
    LC_TANK,             // Resonant circuit
    DECOUPLING_NETWORK   // Bypass caps
};

/**
 * Represents an identified functional block in the circuit.
 */
struct FunctionalBlock {
    FunctionalBlockType type;
    QString name;               // Human-readable name (e.g., "Q1-Q2 Diff Pair")
    std::vector<QString> componentIds;  // Components belonging to this block
    QString inputNode;          // Primary input node
    QString outputNode;         // Primary output node
    double estimatedGain;       // For amplifiers: voltage gain estimate
    QString description;        // Brief description for prompt
};

/**
 * Represents a signal pathway through the circuit.
 */
struct SignalPath {
    QString pathId;             // Unique identifier
    QString source;             // Starting point (e.g., "V1", "Vin")
    QString sink;               // Ending point (e.g., "GND", "Vout")
    std::vector<QString> blockSequence;  // Ordered list of FunctionalBlock names
    std::vector<QString> probePoints;    // Recommended measurement points
    QString compressedSummary;  // Token-efficient description
};

// ============================================================================
// 5.2: TOPOLOGY ANALYZER
// ============================================================================

class TopologyAnalyzer {
public:
    /**
     * Analyzes the current circuit and identifies all functional blocks.
     * Uses structural pattern matching on the netlist.
     */
    static std::vector<FunctionalBlock> identifyFunctionalBlocks();
    
    /**
     * Extracts signal pathways from sources to sinks through identified blocks.
     * Produces compressed summaries suitable for LLM context windows.
     */
    static std::vector<SignalPath> extractSignalPaths();
    
    /**
     * Generates a compressed topology summary for prompt injection.
     * This is the main entry point for Step 5.
     * 
     * Example output:
     * "Circuit: 2-stage amp. Stage1: CE(Q1,Av≈-40). Stage2: EF(Q2,Av≈0.99).
     *  Path: Vin→CE→EF→Vout. Bias: R1-R2 divider, Rb=10k."
     */
    static QString generateCompressedPrompt();
    
    /**
     * Returns structured JSON for programmatic access.
     */
    static QJsonObject toJson();

private:
    // Pattern matchers for specific block types
    static bool matchDifferentialPair(const QJsonArray& components, FunctionalBlock& out);
    static bool matchCurrentMirror(const QJsonArray& components, FunctionalBlock& out);
    static bool matchDarlington(const QJsonArray& components, FunctionalBlock& out);
    static bool matchCommonEmitter(const QJsonArray& components, FunctionalBlock& out);
    static bool matchResistorDivider(const QJsonArray& components, FunctionalBlock& out);
    static bool matchEmitterDegeneration(const QJsonArray& components, FunctionalBlock& out);
    
    // Helper to get estimated gain
    static double estimateGain(FunctionalBlockType type, const QJsonObject& params);
    
    // Compression utilities
    static QString blockToShorthand(const FunctionalBlock& block);
    static QString pathToShorthand(const SignalPath& path);
};

} // namespace AIAPI
