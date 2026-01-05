#pragma once
#include "schema_api.h"
#include "simulation_api.h"
#include <QJsonObject>

/**
 * AI API: Unified Interface for GenAI Chatbot
 * 
 * This is the main entry point for the AI assistant. All commands are
 * processed through this interface using JSON for maximum flexibility.
 * 
 * USAGE EXAMPLE (from Copilot Chat):
 * 
 * User: "Place a 10k resistor at (100, 200)"
 * AI calls: executeCommand({
 *   "action": "placeComponent",
 *   "params": {
 *     "type": "Resistor",
 *     "x": 100,
 *     "y": 200,
 *     "properties": {"resistance": "10k"}
 *   }
 * })
 * 
 * User: "Route a wire from R1 to C1"
 * AI calls: executeCommand({
 *   "action": "autoRoute",
 *   "params": {"start": "R1.terminal1", "end": "C1.terminal1"}
 * })
 */

namespace AIAPI {

/**
 * Main command dispatcher for AI agents.
 * 
 * Accepts JSON commands and returns JSON results.
 * All errors are returned as {"error": "message"}.
 */
QJsonObject executeCommand(const QJsonObject& command);

/**
 * Get API documentation (for LLM context injection).
 * Returns a structured description of all available commands.
 */
QJsonObject getAPIDocs();

/**
 * Validate a command before execution.
 */
bool validateCommand(const QJsonObject& command, QString& errorMsg);

// ============================================================================
// COMMAND EXAMPLES (for LLM training)
// ============================================================================

/**
 * Example commands that the AI can use:
 * 
 * 1. COMPONENT PLACEMENT:
 * {
 *   "action": "placeComponent",
 *   "params": {
 *     "type": "NMOS",
 *     "x": 500,
 *     "y": 300,
 *     "rotation": 0,
 *     "properties": {"W": "2u", "L": "180n"}
 *   }
 * }
 * 
 * 2. WIRE ROUTING:
 * {
 *   "action": "routeWire",
 *   "params": {
 *     "netName": "VDD",
 *     "waypoints": [[0,0], [100,0], [100,100]]
 *   }
 * }
 * 
 * 3. RUN SIMULATION:
 * {
 *   "action": "runTransient",
 *   "params": {
 *     "timeStep": 1e-9,
 *     "stopTime": 1e-6
 *   }
 * }
 * 
 * 4. PLACE PROBE:
 * {
 *   "action": "placeProbe",
 *   "params": {
 *     "type": "voltage",
 *     "node": "OUT",
 *     "label": "Vout"
 *   }
 * }
 * 
 * 5. GET CIRCUIT STATE:
 * {
 *   "action": "getCircuitState"
 * }
 * 
 * 6. RUN DRC:
 * {
 *   "action": "runDRC",
 *   "params": {
 *     "rules": [
 *       {"name": "Metal1.width", "minWidth": 0.14},
 *       {"name": "Metal1.spacing", "minSpacing": 0.14}
 *     ]
 *   }
 * }
 * 
 * 7. FLOORPLAN GENERATION:
 * {
 *   "action": "generateFloorplan",
 *   "params": {
 *     "constraints": [
 *       {"block": "ALU", "region": [0,0,1000,1000]},
 *       {"block": "RAM", "region": [1000,0,2000,1000]}
 *     ]
 *   }
 * }
 * 
 * 8. EXPORT GDS:
 * {
 *   "action": "exportGDS",
 *   "params": {
 *     "filename": "chip.gds",
 *     "layers": ["Metal1", "Metal2", "Via1"]
 *   }
 * }
 */

} // namespace AIAPI
