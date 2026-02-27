# AcuteSim EDA Platform Root

AcuteSim is a modern, high-performance EDA platform designed for AI-assisted design and multi-physics simulation.

## Global Architecture: The Artificial Engineer
The project is organized into three major kernels:
1.  [**ai_api/**](file:///Users/seshak/Desktop/CuteApp/ai_api/flow.md): The Brain (Cognition & Intelligence).
2.  [**gui/**](file:///Users/seshak/Desktop/CuteApp/gui/flow.md): The Body (Interaction & Visualization).
3.  [**compute/**](file:///Users/seshak/Desktop/CuteApp/compute/flow.md): The Physics (Engine & Solvers).

## Critical Boundaries
-   [**engine_api/**](file:///Users/seshak/Desktop/CuteApp/engine_api/flow.md): The scientific contract for engine isolation.
-   [**connectivity/**](file:///Users/seshak/Desktop/CuteApp/connectivity/flow.md): The platform kernel (isolated orchestration).

## 🤖 Global Guardrails
-   **No Mixing**: GUI must never include compute internals. Engine must never include Qt.
-   **Pure Contracts**: Every platform feature must be defined via a DTO or abstract interface in `engine_api`.
-   **Verification-First**: All PRs must pass the isolation scripts in `tools/`.
