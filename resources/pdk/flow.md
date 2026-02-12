# PDK Resources Flow

The `resources/pdk/` directory contains the **Process Design Kit (PDK)** definitions for the "Silicon Mode" of the application. These files define the physical and electrical characteristics of the semiconductor process being simulated (e.g., SkyWater 130nm).

## Key Files

*   **`sky130.json`**: The primary definition file for the SkyWater 130nm process node.

## File Structure (`.json`)

The PDK JSON file defines the "Palette" of standard cells and primitives available to the user when working in ASIC mode. It maps logical components to their physical implementations and simulation models.

```json
{
  "process_name": "SkyWater 130nm",
  "version": "1.0.0",
  "layers": [ ... ], // renderable layers like polysilicon, metal1
  "devices": [
    {
      "name": "nfet_01v8",
      "type": "Mosfet",
      "model": "sky130_fd_pr__nfet_01v8",
      "symbol": "nfet.svg"
    },
    ...
  ],
  "standard_cells": [
    {
      "name": "sky130_fd_sc_hd__inv_1",
      "function": "Inverter",
      "schematic": "resources/cells/inverter.sch", // Links to implementation
      "layout": "resources/cells/inverter.gds"
    }
  ]
}
```

## Integration Flow

1.  **Loading**: On startup, `gui/widgets/component_library.cpp` reads this JSON file.
2.  **Population**: It populates the "PDK" tab in the component library with the defined devices and cells.
3.  **Instantiation**: When a user places a PDK component (e.g., an Inverter):
    *   The **Visual Component** is created with the specified symbol.
    *   The **Simulation Model** is linked:
        *   Primitives (NFET/PFET) link to internal physics models (`MosfetLevel1`).
        *   Standard Cells link to hierarchical sub-circuits defined in `resources/cells/`.

## Development Rules

1.  **Immutability**: These files represent physical reality. Do not change parameter values (e.g., threshold voltage) unless you are correcting a mismatch with the actual foundry PDK.
2.  **Versioning**: If the process specs change, increment the version field.
