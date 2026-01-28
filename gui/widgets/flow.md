# GUI Widgets Flow

The `gui/widgets/` directory contains the building blocks of the AcuteSim user interface. These reusable components handle specific visualization or interaction tasks.

## 1. Core Utilities

### Command Palette (`command_palette.h`)
**Role**: VS Code-style "Ctrl+Shift+P" action launcher with fuzzy search.
-   **Registry**: Central place for all app actions.

```cpp
struct Command {
    QString id;           // Unique identifier
    QString label;        // Display name
    QString category;     // Grouping
    QAction* action;      // Associated QAction
    std::function<void()> callback;
};

void registerCommand(const Command& cmd);
void addToRecent(const QString& commandId);
```

### Keybindings Dialog (`keybindings_dialog.h`)
**Role**: Shortcut editor with conflict detection.

```cpp
class KeybindingsDialog : public QDialog {
    // Manages the visual table of shortcuts
    void populateTableFiltered(const QString& filter);
    void onKeySequenceChanged(const QKeySequence& seq);
};
```

### Toast Manager (`toast_manager.h`)
**Role**: Non-intrusive notification system (replaces modal message boxes).

```cpp
enum class ToastLevel { Info, Success, Warning, Error, Critical };

/**
 * Show a transient message at bottom-center of root widget.
 */
void showToast(const QString& message, ToastLevel level = ToastLevel::Info);
```

void showToast(const QString& message, ToastLevel level = ToastLevel::Info);
```

### Project Explorer (`project_explorer.h`)
**Role**: VS Code-style file browser for the active project.
- **Model-View**: Uses `ProjectModel` to provide a tree view of project assets.
- **Organization**: Automatically categorizes files (Schematics, HDL, Scripts, etc.).

```cpp
class ProjectExplorer : public QWidget {
    // Signals when a file is opened or created
    void fileOpenRequested(const QString& category, const QString& fileName, const QString& filePath);
    void newFileRequested(const QString& category);
};
```

### Project Model (`project_model.h`)
**Role**: The Virtual File System. Manages the project structure in RAM (session persistence).
- **Structure**: Root -> Categories (Folders) -> Items (Files).
- **Persistence**: Can be serialized to/from JSON.

```cpp
class ProjectModel : public QAbstractItemModel {
    // Add file to the project structure
    bool addFile(const QString& category, const QString& fileName, const QString& filePath);
    
    // Serialization
    QJsonObject toJson() const;
};
```

## 2. Engineering Displays

### Waveform Widget (`waveform_widget.h`)
**Role**: Oscilloscope-style data visualizer. Supports docking and multi-trace rendering.
-   **Gestures**: Supports pinch-to-zoom and pan.

```cpp
// Append real-time data
void appendDataPoint(double time, double value, int traceIndex = 0);

// Toggle between Dock and Central Tab mode
void setFloatingMode(bool isFloatingTab);

// DesignDb Integration
void onNetRenamed(NetUUID id, const std::string& oldName, const std::string& newName);

class WaveformView : public QGraphicsView {
    void handlePinch(class QNativeGestureEvent* event);
};
```

### Problems Panel (`problems_panel.h`)
**Role**: List view for ERC/DRC violations.
-   **Navigation**: Double-clicking an error jumps to the component.

```cpp
// Set list of violations validation engine
void setViolations(const QList<ERC::Violation>& violations);

signals:
    void navigateToLocation(QPoint gridLocation);
    void requestAutoFix(int violationIndex);
```

### Terminal Widget (`terminal_widget.h`)
**Role**: Interactive shell for REPL and Copilot chat.

```cpp
// Execute command programmatically (e.g. from Copilot)
Q_SLOT void executeCommand(const QString& fullCommand);

signals:
    void commandRequested(const QString& command);
```

### Output Dock (`output_dock_widget.h`)
**Role**: Read-only log viewer for simulation stdout/stderr.

```cpp
void appendText(const QString& text);
void clear();
```

## 3. AI & Lifecycle Components

### Design Flow Manager (`design_flow.h`)
**Role**: Tracks the ASIC design lifecycle.

```cpp
enum class DesignStage {
    RTL_DESIGN,
    SYNTHESIS,
    PHYSICAL_DESIGN,
    VERIFICATION
};

void setStage(DesignStage stage);
```

### Ghost Diff Viewer (`ghost_diff_viewer.h`)
**Role**: Visualizes AI suggestions (Code or Netlist) with Red/Green diffs.

```cpp
// Render a side-by-side or inline diff
void setDiff(const QString& original, const QString& proposed, const QString& contextHeader = "");

signals:
    void accepted();
    void rejected();
```

### Trust Badge (`trust_badge_widget.h`)
**Role**: Explains *why* the user should trust the simulation results (Provenance).

```cpp
enum class TrustLevel { HIGH, MEDIUM, LOW, UNKNOWN };

void setTrustData(TrustLevel level,
                  const QString& method,    // e.g. "Newton-Raphson"
                  int iterations, 
                  double maxResidual,       // e.g. 1e-13
                  const QString& provenance);
```

### Component Library (`component_library.h`)
**Role**: The "Palette". Filters components based on the current AppMode (Bench/Block/System).

```cpp
// Filter visible components by mode
void setMode(AppMode mode);

signals:
    void componentSelected(ComponentType type);
```

### Debug Suggestions Panel (`debug_suggestions_panel.h`)
**Role**: AI-driven probe recommender.

```cpp
// Refresh list based on current circuit state
void refreshSuggestions();

signals:
    void probePlaced(const QString& node, const QString& reason);
```

## Development Rules
1.  **Separation of Concerns**: Widgets should be "dumb" viewers. Logic belongs in `gui/core` or `ai_api`.
2.  **Reparenting**: Widgets like `WaveformWidget` must handle being moved between Docks and Tabs without losing data.
3.  **Signals**: Use signals for all user interactions (clicks, commands). Never call parent methods directly.
