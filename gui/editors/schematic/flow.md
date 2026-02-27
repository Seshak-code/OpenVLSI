# Schematic Editor Architecture

The `gui/editors/schematic` directory provides the **Primary Design Canvas** for AcuteSim.

## Data Flow: Graph to Netlist
1.  **Interaction**: User places components and wires on the `GridScene`.
2.  **Topology**: The editor maintains a graph of connected pins.
3.  **Sync**: `schematic_editor_sync.cpp` ensures the visual state matches the underlying `SchematicGraphDTO`.
4.  **Handoff**: The graph is serialised and sent to the `SchematicEngineConnector` for simulation.

## Logic: Incremental Graph Updates
The editor detects minimal changes to topology or parameters to enable incremental simulation reuse (see `engine_api/schematic_sim_request.h`).

## 🤖 SME Validation Checklist
- [ ] **No Direct Simulation**: Is the editor calling the solver directly? (Forbidden: Use `SchematicEngineConnector`).
- [ ] **Thread Safety**: Are you updating the scene from a background thread? (Must use `QMetaObject::invokeMethod` or Signals/Slots).
- [ ] **Undo/Redo**: Does the change support the `AiActionEnvelope` for AI-assisted design?
