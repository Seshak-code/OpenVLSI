# Test Stub Architecture

The `tests/stubs` directory contains **Mock Components** used to isolate units of logic during simulation testing.

## Logic: Controlled Inputs
Stubs provide predictable behavior for complex dependencies (e.g., a mock `ISimulationDriver` that always returns converged results).

## 🤖 SME Validation Checklist
- [ ] **Minimal Latency**: Do these stubs perform heavy compute? (Forbidden: Stubs should be instantaneous).
- [ ] **Interface Match**: Does the stub exactly match the public API of the component it replaces?
