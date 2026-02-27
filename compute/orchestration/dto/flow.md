# Orchestration DTO Architecture

The `compute/orchestration/dto` directory defines the **Unified Data Contract** for the platform's distributed services.

## Logic: Protocol-First
These headers contain Plain Old Data (POD) structures that are serialised into JSON or Protobuf for cross-process communication. They are shared by the GUI, the Compute Worker, and the AI Engine.

## 🤖 SME Validation Checklist
- [ ] **No Logic**: Does this DTO contain anything other than data and helper methods? (Forbidden: Business logic belongs in solvers).
- [ ] **ABI Safety**: Did you change a field without updating the schema version?
