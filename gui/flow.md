# GUI Architecture

The `gui` directory constitutes the **Body** of the Artificial Engineer. While `ai_api` provides the cognitive faculties, `gui` provides the sensory input (Editor/Canvas) and the means of expression (Chat/Actions).

It is built on **Qt 6 (C++)** and follows a strict Model-View-Controller (MVC) separation, where the "Model" is often the underlying physics engine or the AI context.

## Module Map

The GUI is organized into 9 specialized sub-modules, representing a layered architecture:

### 1. The Interaction Layer
Where the user communicates with the agent.
-   [**panels/**](file:///Users/seshak/Desktop/CuteApp/gui/panels/flow.md): The Chat Interface (`AiAssistantPanel`).
-   [**orchestration/**](file:///Users/seshak/Desktop/CuteApp/gui/orchestration/flow.md): The Bridge to the Compute Worker (`WorkerManager`).

### 2. The Creative Layer
Where the design happens.
-   [**editors/**](file:///Users/seshak/Desktop/CuteApp/gui/editors/flow.md): The Canvas (`SchematicEditor`) and Code Editor (`HDLEditor`).
-   [**ai/**](file:///Users/seshak/Desktop/CuteApp/gui/ai/flow.md): AI State Management (`AiActionEnvelope`, Undo/Redo).

### 3. The Scientific Layer
Where analysis and validation occur.
-   [**analysis/**](file:///Users/seshak/Desktop/CuteApp/gui/analysis/flow.md): Simulation & ERC Engines (`SimulationWorkspace`, `ERCEngine`).

### 4. The Foundation Layer
The building blocks of the application.
-   [**core/**](file:///Users/seshak/Desktop/CuteApp/gui/core/flow.md): App Skeleton (`MainWindow`, `DockingManager`).
-   [**widgets/**](file:///Users/seshak/Desktop/CuteApp/gui/widgets/flow.md): Reusable UI Library (`WaveformWidget`, `ToastManager`).
-   [**components/**](file:///Users/seshak/Desktop/CuteApp/gui/components/flow.md): Visual Primitives (`VisualComponent`).
-   [**styling/**](file:///Users/seshak/Desktop/CuteApp/gui/styling/flow.md): Theme Engine (`ThemeManager`).

## Data Flow: "The Artificial Engineer"

When a user asks the AI to modify the circuit, the data flows through the layers as follows:

1.  **Stimulus**: User types "Add a 10k resistor" in `gui/panels`.
2.  **Transport**: `gui/orchestration` ships this prompt to the external worker.
3.  **Cognition**: The worker uses `ai_api` logic to determine the action.
4.  **Response**: A JSON/Protobuf message returns, detailing a `SchematicChangeRequest`.
5.  **State**: `gui/ai` wraps this into an `AiActionEnvelope`.
6.  **Visualization**: `gui/panels` asks `gui/editors` to display a "Ghost" preview.
7.  **Actuation**: User clicks "Approve", and `gui/editors` commits the change to the `GridScene`.

## Development Principles

1.  **No Logic in Widgets**: Widgets should be "dumb" renderers. Business logic belongs in `core` managers or `analysis` engines.
2.  **Strict Threading**: The GUI thread (Main) must never block. Heavy compute (Simulation, AI) happens in background threads managed by `orchestration`.
3.  **Ground Truth**: The C++ headers in each specific folder are the ultimate source of truth for API signatures.

## 🤖 SME Validation Checklist
*(Consult this list before modifying `gui/`)*

- [ ] **No Direct Physics**: Are you calculating voltages or verifying connectivity in a widget? (Forbidden: Use `gui/analysis` or `compute/orchestration`).
- [ ] **Thread Safety**: Does this action trigger a long-running task (Sim/AI) on the main thread? (Forbidden: Use `WorkerManager`).
- [ ] **View/Logic Separation**: accessible via `AiAssistantPanel`? Logic belongs in `core/` or `analysis/`, visual representation in `widgets/` or `components/`.
- [ ] **Signal/Slots**: Are you manually passing pointers between deeply nested widgets? (Use Signals/Slots or `DockingManager`).
- [ ] **Mode Awareness**: Does the widget respect the current `AppMode` (Bench/Block/System)?
