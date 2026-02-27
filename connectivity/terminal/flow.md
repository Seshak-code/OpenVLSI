# Terminal Connectivity Architecture

The `connectivity/terminal` directory provides the **Universal Control Protocol** for the EDA platform. It enables a unified command interface for human users (GUI Terminal), automated scripts (CLI), and AI agents.

## Architectural Goal: Shared Command Logic
By implementing the parser in the connectivity layer (pure STL), we ensure that all control surfaces interpret commands identically without duplicating logic.

## Data Flow: Tokenization to Execution
1.  **Tokenization**: Raw command strings are split into quote-aware tokens.
2.  **Parsing**: Tokens are classified into `TerminalCommandClass` (Simulation, Netlist, etc.).
3.  **Validation**: Verbs and flags are checked for semantic correctness.
4.  **Dispatch**: The `ParsedTerminalCommand` is handed to the appropriate bridge (e.g., `EngineTerminalBridge`).

## 🤖 SME Validation Checklist
- [ ] **Pure STL**: Is the parser free of GUI dependencies? (Must be reusable in a headless CLI).
- [ ] **Quote Awareness**: Does the tokenizer handle space-separated args within double quotes?
- [ ] **Monotonic Verbs**: Are you introducing new verbs that conflict with existing simulation commands?
