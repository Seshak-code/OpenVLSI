# Drivers Flow

The `compute/orchestration/drivers/` directory contains the **External Tool Adapters**. AcuteSim is not just a simulator; it is a full Digital/Analog Implementation Environment. To achieve this, it orchestrates industry-standard open-source EDA tools (Yosys, OpenRoad, Ngspice, etc.).

## Philosophy: Script Generation over Binding

Instead of linking against the C++ APIs of these tools (which are often unstable, GPL-infected, or poorly documented), we use a **Configuration-to-Script** pattern.
1.  **Capture Intent**: The user selects "Optimize for Speed" in the AcuteSim GUI.
2.  **Generate Script**: The Driver generates a precise TCL/Shell script (e.g., for Yosys).
3.  **Execute & Parse**: The Orchestrator runs the tool as a subprocess and parses the text output.

This ensures:
*   **Stability**: If Yosys crashes, AcuteSim stays alive.
*   **Decoupling**: We can upgrade/swap external tools without recompiling AcuteSim.
*   **Transparency**: The user can "Eject" the generated script and run it manually if they wish.

## Key Drivers

### 1. `yosys_task.h`: Logic Synthesis
Drives **Yosys**, converting Verilog/SystemVerilog into a technology-mapped Netlist (BLIF/Verilog).
*   **Input**: `SynthesisConfig` (Source files, Top module, Target Library).
*   **Goals**: `SynthesisGoal::AREA`, `SPEED`, `POWER`.
*   **Output**: A generated TCL string containing commands like `synth -top ...`, `abc -liberty ...`, `write_verilog ...`.

## Core Logic & Snippets

### Semantic TCL Generation (`yosys_task.cpp`)
We abstract valid ABC optimization flags behind simple enums.

```cpp
QString YosysTask::getGoalOptimizations(SynthesisGoal goal) {
    switch (goal) {
        case SynthesisGoal::AREA:
            // High-effort area mapping
            return " -script +strash;ifraig;retime,-D,{D};dch,-f;map,-a,-M,1,{D}";
        case SynthesisGoal::SPEED:
            // Delay optimization
            return " -script +strash;ifraig;retime,-D,{D};map,-p,-M,1,{D}";
        default:
            return ""; // Balanced
    }
}
```

## Development Rules

1.  **No System Calls Here**: Drivers should only *generate strings* (scripts/configs). They should not call `system()` or `QProcess`. The execution is handled by the `orchestration/` parent layer.
2.  **Explicit Versioning**: If a generated script depends on a specific version of a tool (e.g., Yosys 0.9+), document it.
3.  **Idempotency**: Generated scripts should be self-contained. They must set up their own library paths and clean up temporary files.
4.  **Human Readability**: Generated scripts are artifacts. Add comments (`# Goal: Speed`) so a human inspecting the `debug/` folder understands what AcuteSim tried to do.
