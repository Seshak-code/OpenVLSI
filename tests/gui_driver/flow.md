# GUI Driver Architecture

The `tests/gui_driver` directory contains the **Sensory Automators** for headless GUI testing.

## Logic: Interaction Emulation
It uses the `TclBridge` and native Qt events to simulate clicks, drags, and text entry in the `MainWindow`.

## 🤖 SME Validation Checklist
- [ ] **Main Thread Only**: Are you calling GUI events from a background thread? (Forbidden: Qt GUI calls must be on the main thread).
- [ ] **Deterministic Waits**: Are you using `sleep()`? (Forbidden: Use `QSignalSpy` or condition-wait loops).
