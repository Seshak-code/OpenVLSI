# Scripts Flow (Optional)

The `scripts/` directory contains **DevOps and Developer Utilities**. These are not part of the shipped application but facilitate the build, test, and deployment lifecycle.

## Key Files

-   **`setup_dependencies.sh`**: Installs build requirements (cmake, ninja, qt6) for the dev environment.
-   **`kill_web.sh/ps1`**: Helper to terminate lingering web server processes during Wasm development.
-   **`serve.py`**: A simple Python HTTP server for testing the WebAssembly build locally.

## Development Workflows

1.  **Setting Up**: `source scripts/setup_dependencies.sh`
2.  **Web Testing**: `python3 scripts/serve.py` -> Open `localhost:8000`
