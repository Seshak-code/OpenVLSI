# GUI Editors Flow

The `gui/editors/` directory contains the **Content Creation** tools of the application. These widgets are the primary interface where users (and AI) design circuits.

## Key Files & Responsibilities

### 1. Schematic Editor (`schematic_editor.h`)
**Role**: The Canvas. A high-performance 2D vector editor for placing components and routing wires.
-   **GridScene**: Manages the component database, geometric indexing, and ERC validation.
-   **AI Hooks**: Exposes placement/routing APIs specifically for the `ai_api`.

```cpp
class SchematicEditor : public QWidget {
public:
    // Core I/O
    TensorNetlist toNetlist();
    QJsonObject getCircuitState();
    
    // AI Manipulation API
    QString placeComponent(const QString& type, QPointF pos, double rotation, const QJsonObject& params);
    void placeWirePath(const QVector<QPointF>& points);
    bool optimizeComponentRotation(const QString& id);

    // Automation
    void setTool(ComponentType t);
    QStringList runERC();

    // Hierarchical Support
    // Handles recursive loading of "sourceFile" for ICPackage components
    // Hierarchical Support
    // Handles recursive loading of "sourceFile" for ICPackage components
    // Emits requestOpenSubcircuit(path) for drill-down navigation
    // Handles Generic Component persistence (saves "source" property)
};
```

### 2. HDL Editor (`hdl_editor.h`)
**Role**: The Code Editor. A syntax-highlighting text editor for Verilog, VHDL, and Verilog-AMS.
-   **Syntax Highlighting**: Custom tokenizer for EDA languages.
-   **Bi-Directional**: Changes here can drive schematic updates (and vice-versa).

```cpp
class HDLSyntaxHighlighter : public QSyntaxHighlighter {
public:
    enum Language { VERILOG, VHDL, VERILOG_AMS };
    void setLanguage(Language lang);
};

class HDLEditor : public QPlainTextEdit {
    void setLanguage(HDLSyntaxHighlighter::Language lang);
};
```

### 3. Component Wizard (`component_wizard.h`)
**Role**: The Packaging Tool. Guides the user through converting a schematic into a reusable sub-circuit.
-   **Workflow**: Name -> Pin Mapping -> Package Selection -> Save.

```cpp
class ComponentWizard : public QWizard {
public:
    // Result access
    SubCircuitDefinition getResult() const;
    bool wasSuccessful() const;
};
```

## Data Flow
1.  **Interaction**: User clicks "Resistor" tool in `SchematicEditor`.
2.  **Scene Update**: `GridScene` handles the mouse events, showing a specific ghost.
3.  **Commit**: On click, a `VisualComponent` is added to the scene database.
4.  **Sync**: `SchematicEditor` triggers `generateVerilogSnippet()` to update the `HDLEditor` if linked.
5.  **Simulation**: When "Run" is pressed, `SchematicEditor::toNetlist()` compiles the scene into a `TensorNetlist`.

## Development Rules
1.  **AI Compatibility**: Any new interaction MUST have a programmatic equivalent in `SchematicEditor` so the AI can perform it.
2.  **Performance**: `GridScene` must handle 10,000+ items smoothly using BSP quantization (built-in to QGraphicsScene).
3.  **Model-View Separation**: The Editor is the View/Controller. The Model is the `TensorNetlist` (derived) or the Scene Graph (persistent).
