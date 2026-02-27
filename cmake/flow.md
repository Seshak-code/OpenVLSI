# Build System Architecture

The `cmake` directory contains the **Construction Blueprint** for the platform.

## Logic: Layered Compilation
The build system is designed to enforce architectural boundaries via `INTERFACE` targets and visibility rules.
-   **acutesim_engine_api**: Header-only interface.
-   **acutesim_math_core**: Isolated mathematical primitives.

## 🤖 SME Validation Checklist
- [ ] **Boundary Enforcement**: Does a change in `cmake/` allow the GUI to see engine internals? (Forbidden).
- [ ] **Cross-Platform**: Does the build support macOS (native) and WASM (emscripten)?
