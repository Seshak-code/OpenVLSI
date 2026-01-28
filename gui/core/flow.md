# GUI Core Flow

The `gui/core/` directory contains the **Application Skeleton**. It manages the main window, the docking system, global state persistence, and input routing. It is the "Controller" that glues the Widgets (View) to the AI/Physics engines (Model).

## Key Files & Responsibilities

### 1. Main Window (`mainwindow.h`)
**Role**: The Shell. Orchestrates the high-level application lifecycle.
-   **Mode Management**: Switches between Bench Test, Block Subsystem, and System Integration modes.
-   **Central Logic**: Holds the `SchematicEditor` and `SimulationWorkspace`.

```cpp
class MainWindow : public QMainWindow {
public:
    AppMode getMode() const;
    void setMode(AppMode mode);

    // AI/API Access
    // Opens schematic with robust path search (Direct -> resources/cells -> user_libs)
    void openSchematicFile(const QString& filePath, const QString& fileName);
    void openSchematicTab(const QString& title, const QJsonObject& content);
    
    // Core Components
    SchematicEditor* getSchematicEditor() const;
};
```

### 2. Docking Manager (`docking_manager.h`)
**Role**: The Layout Engine. Manages the VS Code-style panels (Output, Terminal, AI Assistant).
-   **Factory**: Lazily creates docks on startup.
-   **State**: Handles promoting docks to central tabs and back.

```cpp
class DockingManager : public QObject {
public:
    void createCoreDocks();
    
    // Accessors for global singletons
    TerminalWidget* terminal() const;
    WaveformWidget* waveform() const;
    ProblemsPanel* problems() const;
};
```

### 3. Layout Manager (`layout_manager.h`)
**Role**: The Persister. Saves and restores window geometry and dock arrangement.
-   **Presets**: Supports switching between "Debug Layout", "Coding Layout", etc.

```cpp
struct WorkspacePreset {
    QString id;
    QString name;
    bool isBuiltIn;
};

class LayoutManager : public QObject {
    void saveWindowLayout(const QString& key, QMainWindow* window);
    bool restoreWindowLayout(const QString& key, QMainWindow* window);
};
```

### 4. Keybinding Manager (`keybinding_manager.h`)
**Role**: The Input Router. Centralizes keyboard shortcuts to prevent conflicts and enable customization.

```cpp
struct KeyBinding {
    QString id;              // "file.save"
    QKeySequence defaultKey; // "Ctrl+S"
    QKeySequence currentKey; // User override
    QString context;         // "editor" vs "global"
};

class KeybindingManager : public QObject {
    void registerAction(const QString& id, ..., QAction* action);
    QString findConflict(const QKeySequence& key, const QString& context) const;
};
```

## Data Flow
1.  **Startup**: `MainWindow` initializes.
2.  **Layout**: `DockingManager` creates empty docks. `LayoutManager` restores their positions from `QSettings`.
3.  **Action**: User presses "Ctrl+R". `KeybindingManager` routes this to `MainWindow::runSimulation`.
4.  **Display**: `simulationWorkspace` updates, and `DockingManager` ensures the Output panel is visible.

## Development Rules
1.  **Singleton Access**: `DockingManager` is the *only* valid way to access global panels like the Terminal. Do not instantiate widgets manually.
2.  **Context Awareness**: Shortcuts must specify a context ("editor", "global") to avoid collision (e.g., "Del" component vs "Del" file).
3.  **State Isolation**: `MainWindow` should only coordinate. Business logic belongs in the respective Managers or the API layer.
