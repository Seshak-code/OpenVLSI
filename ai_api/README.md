# AI API Integration Guide

## Overview

The AI API provides **complete programmatic access** to all simulator functionality, enabling GenAI models to perform full **RTL-to-GDS automation**.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    GenAI Model (LLM)                        │
│              (GPT-4, Claude, Gemini, etc.)                  │
└────────────────────┬────────────────────────────────────────┘
                     │ JSON Commands
                     ▼
┌─────────────────────────────────────────────────────────────┐
│              ai_interface.h (Unified Dispatcher)            │
│                  executeCommand(json) → json                │
└───┬─────────┬─────────┬──────────┬──────────┬──────────────┘
    │         │         │          │          │
    ▼         ▼         ▼          ▼          ▼
┌────────┐ ┌──────┐ ┌──────┐ ┌────────┐ ┌──────────┐
│schema_ │ │simul-│ │analy-│ │design_ │ │validation│
│api.h   │ │ation_│ │sis_  │ │api.h   │ │_api.h    │
│        │ │api.h │ │api.h │ │        │ │          │
└────┬───┘ └───┬──┘ └───┬──┘ └────┬───┘ └─────┬────┘
     │         │        │         │           │
     ▼         ▼        ▼         ▼           ▼
┌─────────────────────────────────────────────────────┐
│         Core Simulator (circuitsim.h)               │
│         GUI (SchematicEditor, MainWindow)           │
└─────────────────────────────────────────────────────┘
```

## Key Features for RTL-to-GDS

### 1. **Geometric Precision**
All coordinates are grid-snapped and DRC-aware:
```cpp
QPointF snapToGrid(QPointF(123.7, 456.2));  // → (120, 460)
```

### 2. **Hierarchical Design**
Create and instantiate blocks:
```json
{
  "action": "createBlock",
  "params": {
    "name": "Inverter",
    "definition": {
      "components": [...],
      "ports": {"IN": 1, "OUT": 2, "VDD": 3, "GND": 0}
    }
  }
}
```

### 3. **3D Layout Support** (Future)
Multi-layer routing with vertical DRC:
```cpp
addShape("Metal1", polygon);
checkVerticalIntersection("shape1", "shape2");
```

### 4. **Complete State Access**
Get full circuit state for AI context:
```cpp
QJsonObject state = getFullCircuitState();
// Returns:
// {
//   "components": [...],
//   "wires": [...],
//   "hierarchy": {...},
//   "bounds": {"xMin": 0, "xMax": 1000, ...}
// }
```

## Usage in Copilot Chat

The AI assistant (`AiAssistantPanel`) connects to this API:

```cpp
// In ai_assistant.cpp
void AiAssistantPanel::processCommand(const QString& cmd) {
    QJsonObject command = parseUserIntent(cmd);
    QJsonObject result = AIAPI::executeCommand(command);
    displayResult(result);
}
```

### Example Conversation

**User**: "Create a voltage divider with 10k and 20k resistors"

**AI generates**:
```json
[
  {"action": "placeComponent", "params": {"type": "Resistor", "x": 0, "y": 0, "properties": {"resistance": "10k"}}},
  {"action": "placeComponent", "params": {"type": "Resistor", "x": 0, "y": 100, "properties": {"resistance": "20k"}}},
  {"action": "routeWire", "params": {"waypoints": [[0,0], [0,100]]}}
]
```

**User**: "Run DC analysis and measure output voltage"

**AI generates**:
```json
[
  {"action": "placeProbe", "params": {"type": "voltage", "node": "OUT"}},
  {"action": "runDCAnalysis"}
]
```

## RTL-to-GDS Workflow

1. **RTL Input** → Parse Verilog/VHDL
2. **Synthesis** → Map to standard cells
3. **Floorplanning** → `generateFloorplan()`
4. **Placement** → `placeComponent()` for each cell
5. **Routing** → `autoRoute()` for each net
6. **DRC** → `runDRC()` to validate
7. **Export** → `exportGDS()`

## API Documentation

See individual header files:
- `schema_api.h` - Placement, routing, geometry
- `simulation_api.h` - Simulation control
- `analysis_api.h` - DC/AC/Transient/Monte Carlo
- `design_api.h` - Optimization, auto-routing
- `validation_api.h` - ERC, DRC, compliance
- `ai_interface.h` - Unified JSON command interface

## Probe Component API

The `Probe` component allows marking specific nodes for measurement. Probes are essential for:
- Defining output observation points
- Debugging intermediate signal values
- AI-suggested measurement locations

### Place a Probe
```json
{
  "action": "placeComponent",
  "params": {
    "type": "Probe",
    "x": 300,
    "y": 100,
    "id": "P1"
  }
}
```

### AI Commands (Natural Language)
The following phrases are recognized by the NL parser:
- "add a probe at 300, 100"
- "place probe next to R1"
- "suggest probes" → AI analyzes and recommends probe locations
- "find signal path" → AI traces input-to-output chains

### Signal Path Analysis API
```cpp
// Returns paths from sources to sinks with recommended probe points
QJsonArray AIAPI::ContextEngine::analyzeSignalPaths();

// Returns debug suggestions with priority levels
QJsonArray AIAPI::ContextEngine::getDebugSuggestions();
```

### Response Format (Signal Path)
```json
{
  "source": "V1",
  "sink": "GND",
  "path": ["V1", "Q1", "Q2", "GND"],
  "probePoints": [
    {"node": "V1.+", "label": "Vin"},
    {"node": "Q1.C", "label": "Vout_Q1"},
    {"node": "Q2.C", "label": "Vout_Q2"}
  ]
}
```

## Future Enhancements

- **Machine Learning Integration**: Train models on successful placements
- **Natural Language Processing**: Direct English → GDS
- **Multi-Objective Optimization**: Area vs Power vs Speed
- **Real-time Collaboration**: Multiple AI agents working together
