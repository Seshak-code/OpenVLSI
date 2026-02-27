# Tools & Automation Architecture

The `tools` directory contains the **System Sentinels** — scripts and utilities that enforce the architectural purity of the EDA platform.

## Architectural Goal: Static Enforcement
These scripts are designed to run in CI/CD and pre-commit hooks to prevent architectural regression (e.g., Qt leaking into the engine).

## Logic: Isolation Checks
-   [**check_engine_isolation.py**](file:///Users/seshak/Desktop/CuteApp/tools/check_engine_isolation.py): Enforces the firewall between the GUI and the Engine.
-   [**check_connectivity_isolation.py**](file:///Users/seshak/Desktop/CuteApp/tools/check_connectivity_isolation.py): Enforces the "Engine-Pure" requirement for the connectivity layer.

## 🤖 SME Validation Checklist
- [ ] **No Side Effects**: Do these scripts modify code? (Forbidden: Must only report violations unless explicitly named as a refactoring tool).
- [ ] **Fast Execution**: Do these scripts take more than a few seconds? (Should be optimized for pre-commit loops).
- [ ] **Non-Interactive**: Do these scripts require user input? (Must run headlessly in CI).
