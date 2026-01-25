# GUI Components Flow

The `gui/components/` directory defines the **Visual Primitives** of the schematic editor. These classes inherit from `QGraphicsObject` and handle rendering, user interaction, and data storage for the circuit elements.

## Key Files & Responsibilities

### 1. Visual Component (`visual_component.h`)
**Role**: The Base Class. Represents any renderable device on the canvas (Resistor, MOSFET, IC).
-   **Terminals**: Defines connection points for wires.
-   **Ghosting**: Used for AI suggestions (preview state).

```cpp
// gui/components/visual_component.h
enum class ComponentType {
    Resistor, Capacitor, Inductor,
    VoltageSource, Ground,
    Mosfet, Bjt, JFET_N,
    PowerPort, RailDriver, // Mode-specific
    ICPackage // Hierarchical
};

class VisualComponent : public QGraphicsObject {
public:
    ComponentType type;
    QString name;
    QString value; // e.g., "10k"
    
    // Phase 11.6: Ghosting Support
    enum class GhostState {
        None, Preview, Focused, Accepted, Rejected
    };
    GhostState ghostState = GhostState::None;
    
    QVector<QPointF> getTerminals() const;
};
```

### 2. Visual Wire (`visual_component.h`)
**Role**: The Connective Tissue. Drawn as orthogonal lines between component terminals.
-   **Net Association**: Stores the name of the net it belongs to (e.g., "N001").
-   **Physics**: Can visualize current flow by changing width/color.

```cpp
// gui/components/visual_component.h
class VisualWire : public QGraphicsObject {
public:
    void setLine(const QLineF& line);
    QString netName;
    
    // Physics Properties
    void setWidth(double width);
};
```

## Data Flow
1.  **Creation**: User or AI instantiates `VisualComponent(ComponentType::Resistor)`.
2.  **Placement**: The component is added to the `QGraphicsScene` at a grid-snapped location.
3.  **Wiring**: `VisualWire` objects connect the Terminals.
4.  **Netlisting**: The `SchematicEditor` traverses these visual objects to build the `TensorNetlist` for simulation.

## Development Rules
1.  **Grid Alignment**: All terminals MUST align to the `GRID_STEP` (20px). Off-grid terminals make wiring impossible.
2.  **Snapping**: `itemChange()` handles snapping logic.
3.  **Paint Performance**: `paint()` methods must be highly optimized as they run at 60Hz during panning/zooming.
