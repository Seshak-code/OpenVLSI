# Scenario Testing Architecture

The `tests/scenarios` directory contains **End-to-End Workflows** representing real-world user designs.

## Logic: Holistic Validation
Scenarios combine schematics, netlists, and expected measurements to verify that the entire platform (GUI -> Worker -> Engine) works as a system.

## 🤖 SME Validation Checklist
- [ ] **Full Path**: Does this scenario exercise the entire data flow (Input -> Cognition -> Output)?
- [ ] **Regression Entry**: Is this scenario hooked into `regression_main.cpp`?
