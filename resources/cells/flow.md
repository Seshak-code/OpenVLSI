# Standard Cells Flow

The `resources/cells/` directory contains the **Schematic Definitions** for the Standard Cell Library. These are the building blocks of digital logic (AND, OR, NOT, DFF) implemented at the transistor level.

## Key Files

*   **`*.sch`**: JSON-based schematic files produced by the `SchematicEditor`.
*   **Examples**:
    *   `inverter.sch`: CMOS Inverter (1 P-MOS, 1 N-MOS).
    *   `dff.sch`: D-Flip-Flop implementation.
    *   `pdk_nfet.sch`: A wrapper around the primitive NFET for hierarchical use.

## Role in Simulation

Unlike the hardcoded primitives in `components/`, these cells are **Hierarchical Sub-Circuits**.

1.  **Definition**: They are drawn by the user (or developer) in the GUI and saved as `.sch` files.
2.  **Instantiation**: When used in a larger design, they appear as a "Black Box" (`ICPackage` component).
3.  **Netlisting**: During simulation prep, the `tensorizeBlock` process:
    *   Recursively loads the `.sch` file.
    *   Flattens the hierarchy (allocating offset node IDs).
    *   Injects the internal transistors into the global solver matrix.

## Connection to PDK

These files are referenced by the PDK definition (e.g., `resources/pdk/sky130.json`). The PDK says "There exists an Inverter", and this directory provides the "Implementation of that Inverter".

## Development Rules

1.  **Purity**: Standard cells should only contain PDK primitives (NFETs, PFETs) or other standard cells. Do not use ideal voltage sources or resistors inside a logic cell unless modeling specific parasitics.
2.  **Ports**: The Input/Output ports (pins) defined in the schematic MUST match the symbol definition in the PDK JSON.
