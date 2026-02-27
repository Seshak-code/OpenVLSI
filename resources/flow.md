# Static Resources Architecture

The `resources` directory contains the **Sensory Definitions** of the platform — static assets including themes, icons, component cells, and Process Design Kits (PDKs).

## Logic: Modular Assets
Resources are organized by type to support themes and skinning.
-   [**themes/**](file:///Users/seshak/Desktop/CuteApp/resources/themes/flow.md): JSON-based styling for the GUI.
-   [**icons/**]: Visual representation of actions and components.
-   [**cells/**](file:///Users/seshak/Desktop/CuteApp/resources/cells/flow.md): Standard cell definitions for the schematic editor.
-   [**pdk/**](file:///Users/seshak/Desktop/CuteApp/resources/pdk/flow.md): Process-specific rules and models.

## 🤖 SME Validation Checklist
- [ ] **Git-Friendly**: Are these assets binary? (Prefer JSON/SVG for diffability).
- [ ] **PDK Integrity**: Are PDK models being modified directly? (Forbidden: Use symbolic links or external clones to original PDK repos).
- [ ] **Relative Paths**: Does any resource use absolute paths? (Forbidden: Must use relative or Qt Resource System `: /` paths).
