# Scripts Flow (Optional)

The `scripts/` directory contains **DevOps and Developer Utilities**. These are not part of the shipped application but facilitate the build, test, and deployment lifecycle.

## Key Files

-   **`setup_dependencies.sh`**: Installs build requirements (cmake, ninja, qt6) for the dev environment.
-   **`kill_web.sh/ps1`**: Helper to terminate lingering web server processes during Wasm development.
-   **`serve.py`**: A simple Python HTTP server for testing the WebAssembly build locally.
-   **`tmp_patch.py`**: A regex-based post-compilation patcher that modifies `AcuteSim.js` to inject `EmscriptenEH` polyfills and intercept Embind `null`/`undefined` type conversion errors (essential for Qt 6.9 WebAssembly rendering).

## Development Workflows

1.  **Setting Up**: `source scripts/setup_dependencies.sh`
2.  **Web Testing**: `python3 scripts/serve.py` -> Open `localhost:8000`

## 🤖 SME Validation Checklist
*(Consult this list before modifying `scripts/`)*

- [ ] **Portability**: Does the script work on macOS (BSD utils) and Linux (GNU utils)? Prefer Python for complex logic.
- [ ] **Paths**: Are you using relative paths from the project root? (Forbidden: `cd /Users/seshak/...`).
- [ ] **Idempotency**: can the script be run twice without error?
- [ ] **Shell Injection**: Are user inputs properly quoted?
