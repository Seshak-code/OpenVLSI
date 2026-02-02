#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QGroupBox>
#include <QMap>
#include <QKeyEvent>
#include "../core/editor_manager.h" // For IEditor
#include "../core/hierarchy_path.h" // Phase 5: Hierarchical Navigation
#include "../components/visual_component.h"
#include "../../components/circuit.h"
#include "schematic_view.h"


#include <QJsonArray>
#include <QJsonObject>
#include "gui_types.h"
#include "../analysis/erc_engine.h"
#include "../analysis/erc_autofix.h"
#include "../analysis/erc_autofix.h"
#include "../../physics/simulation_profile.h"
#include "../../components/design_db.h"

// Forward declarations
namespace acutesim::gui::widgets {
    class BreadcrumbWidget;
}

// ============================================================================
// DATA STRUCTURES
// ============================================================================
struct TerminalSnapResult {
    bool snapped = false;
    QPointF point;
    VisualComponent* component = nullptr;
    int terminalIndex = -1;
    double distance = 1e9;
};

// ============================================================================
// SCENE (Grid Handling + Validation)
// ============================================================================
class GridScene : public QGraphicsScene, public acutesim::components::DesignDbObserver {
    Q_OBJECT
public:
    enum class Mode {
        Interactive,
        Headless
    };

    explicit GridScene(Mode mode = Mode::Interactive, QObject* parent = nullptr);
    ~GridScene() override;

    Mode mode() const { return m_mode; }

    void setTool(ComponentType t, const QString& subName = "");
    // setMode() removed - unified component library
    void setGridVisible(bool visible);
    void setSnappingEnabled(bool enabled);
    bool isSnappingEnabled() const { return snappingEnabled; }
    
    // Core Logic (moved from Editor to support Headless)
    QString generateUniqueId(ComponentType type);
    QString placeComponent(const QString& typeStr, QPointF pos, double rotation, const QJsonObject& params);
    void loadState(const QJsonObject& state);
    VisualComponent* findComponent(const QString& name) const;
    QString resolveSubcircuitPath(const QString& name) const;
    void setFilePath(const QString& path) { m_filePath = path; }
    QString getFilePath() const { return m_filePath; }
    
    // Quick Preview
    void setQuickPreviewEnabled(bool enabled);
    bool isQuickPreviewEnabled() const { return quickPreviewEnabled_; }
    TensorNetlist generateNetlist();
    QStringList runERC();
    
    // Automation API (GenAI Hook)
    // Returns full circuit state for AI context
    QJsonObject getCircuitState() const;
    
    // Check if a region is occupied by components or wires
    int getGridSnap() const { return GRID_STEP; }
    
    // Geometric Query
    bool isOccupied(const QRectF& rect, QGraphicsItem* exclude = nullptr);
    bool collidesWithAny(const QRectF& rect, const QGraphicsItem* ignore = nullptr) const;
    bool followsPDKRules(const QRectF& rect) const;
    bool isWireAllowed(const QLineF& line) const;
    
    // Contract-based Wire Validation
    bool isWireValid(VisualWire* wire); // Legacy wrapper
    bool isEndpointLegal(const QPointF& p, const VisualWire* context);
    bool isSegmentLegal(const QLineF& line, const VisualWire* context);
    
    TerminalSnapResult findNearestTerminal(const QPointF& pos, double threshold);
    
    void placeWirePath(const QVector<QPointF>& points);

    QPointF snapToGrid(const QPointF& pos) {
        if (!snappingEnabled) return pos;
        double x = std::round(pos.x() / GRID_STEP) * GRID_STEP;
        double y = std::round(pos.y() / GRID_STEP) * GRID_STEP;
        return QPointF(x, y);
    }
    
    // Paradigm Bridge: Access to the Unified Substrate
    acutesim::components::DesignDb& getDesignDb() { return designDb; }
    
    // Manually trigger updates (for API/UndoRedo)
    void triggerTopologyUpdate() {
        emit topologyChanged(QDateTime::currentMSecsSinceEpoch());
        updateERC();
    }
    
    // DesignDbObserver Implementation (Backward Sync)
    void onNetCreated(const acutesim::components::Net& net) override {}
    void onNetModified(const acutesim::components::Net& net) override; 
    void ofInstancePlaced(const acutesim::components::Instance& inst) override;
    void onInstanceMoved(const acutesim::components::Instance& inst) override;
    void onTopologyInvalidated() override;
    void onNetRenamed(acutesim::components::NetUUID id, const std::string& oldName, const std::string& newName) override;

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override; // Drill-Down
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override; // Right-Click Menu
    void keyPressEvent(QKeyEvent* event) override;

signals:
    void topologyChanged(size_t hash); // Emitted on component/wire add, remove, or property change
    void mouseMoved(int x, int y);
    void messageLogged(const QString& msg);
    void toolChanged(ComponentType newTool);
    void componentSelected(VisualComponent* component); 
    void wireSelected(VisualWire* wire);
    void violationsChanged(const QList<ERC::Violation>& violations);
    void requestOpenSubcircuit(const QString& filePath); // Drill-Down Event
    void requestAscend(); // Phase 5: Ascend Event

private:
    bool isPlacementValid(const QPointF& pos, QGraphicsItem* item);

    bool gridVisible = true;
    bool snappingEnabled = true; // State
    Mode m_mode;
    QString m_filePath;
    ComponentType currentTool = ComponentType::Select;
    QString currentSubTool; // Name for SubCircuit
    
    // Wire drawing
    QPointF wireStart;
    bool isWiring = false; // State flag
    QList<VisualWire*> ghostWires; // Multiple ghost segments
    
    void clearGhostWires();
    
    // Ghost for placement
    VisualComponent* ghostItem = nullptr;
    
    // ====== GEOMETRIC CONSTRAINTS: Alignment Guides ======
    QLineF alignmentGuideH;  // Horizontal alignment guide
    QLineF alignmentGuideV;  // Vertical alignment guide
    bool showAlignH = false;
    bool showAlignV = false;
    
    // Compute alignment with existing components
    void updateAlignmentGuides(const QPointF& ghostPos);
    
    // ====== ERC (Electrical Rule Checking) ======
    ERC::ERCEngine ercEngine_;
    ERC::ERCAutoFix ercAutoFix_;
    QList<ERC::Violation> currentViolations_;
    bool ercEnabled_ = true;
    bool quickPreviewEnabled_ = true;
    
    // Re-run ERC after schematic changes
    void updateERC();
    
    // Draw violation markers
    void drawViolationMarkers(QPainter* painter);

public:
    // Real-time ERC access
    bool canSimulate() const { return ercEngine_.canSimulate(); }
    QString getERCSummary() const { return ercEngine_.getSummary(); }
    const QList<ERC::Violation>& getViolations() const { return currentViolations_; }
    void setERCEnabled(bool enabled) { ercEnabled_ = enabled; if (enabled) updateERC(); }
    
    // Auto-fix hint API
    void showRoutingHint(const ERC::Violation& v);
    void acceptRoutingHint();
    void dismissRoutingHint();
    
protected:
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    
    // Unified Data Substrate
    acutesim::components::DesignDb designDb;
};

// ============================================================================
// MAIN EDITOR WIDGET
// ============================================================================
// ============================================================================
// MAIN EDITOR WIDGET
// ============================================================================
class SchematicEditor : public acutesim::gui::core::IEditor {
    Q_OBJECT
public:
    explicit SchematicEditor(const QString& filePath, QWidget* parent = nullptr);

    // IEditor Interface
    QString filePath() const override { return filePath_; }
    QString title() const override;
    bool isDirty() const override { return modified_; }
    acutesim::gui::core::EditorCapabilities capabilities() const override;

    // Quick Preview
    void setQuickPreviewEnabled(bool enabled);
    bool isQuickPreviewEnabled() const { return quickPreviewEnabled_; }
    
    // Per-file simulation profile management
    const QMap<QString, Simulation::SimulationProfile>& getProfiles() const { return profiles_; }
    void setProfiles(const QMap<QString, Simulation::SimulationProfile>& profiles) { profiles_ = profiles; }
    QString getCurrentProfileId() const { return currentProfileId_; }
    void setCurrentProfileId(const QString& id) { currentProfileId_ = id; }
    
    // Topology hash (for simulation state tracking)
    size_t getTopologyHash() const { return topologyHash_; }

    TensorNetlist toNetlist() { return scene->generateNetlist(); }
    
    GridScene* getScene() const { return scene; }
    SchematicView* getView() const { return view; }

    // Navigation helpers
    void centerOn(qreal x, qreal y) { if (view) view->centerOn(x, y); }
    void highlightComponent(const QString& name);
    
    // Automation API (GenAI Hook)
    // Returns a JSON-like string description of the circuit
    QString dumpSceneState();
    
    // Returns full circuit state as JSON object
    QJsonObject getCircuitState();
    void loadState(const QJsonObject& state);
    
    // Returns a list of all available component types
    QStringList getAvailableComponents();
    
    // Wire naming API
    void setWireName(VisualWire* wire, const QString& name); // Renaming
    
    // Geometric API
    enum class Direction { North, South, East, West };
    Q_ENUM(Direction)

    QPointF findClearSpace(QPointF idealPos, QSizeF size, int searchRadius = 5) const;
    void placeNextTo(const QString& refId, const QString& newType, Direction dir);

signals:
    void topologyChanged(size_t hash); // Forwarded from GridScene
    // modeChanged() signal removed - unified component library
    void violationsChanged(const QList<ERC::Violation>& violations); // Forwarded from GridScene
    
public:
    // Wire placement API (for AI auto-routing)
    void placeWirePath(const QVector<QPointF>& points);
    void placePhysicallyAwareWire(const QVector<QPointF>& points, float width, double current);

    // AI Optimization API
    bool optimizeComponentRotation(const QString& id);
    
    // Tool Selection API
    void setTool(ComponentType t, const QString& subName = "");
    
    // Validation
    QStringList runERC();
    
    // Geometric Query for AI
    bool collidesWithAny(int x, int y, int w, int h, const QString& excludeId = "") const;
    
    // AI API Implementation
    QString placeComponent(const QString& type, QPointF pos, double rotation, const QJsonObject& params);
    bool moveComponent(const QString& id, double x, double y);
    bool rotateComponent(const QString& id, double degrees);
    bool deleteComponent(const QString& id);
    VisualComponent* findComponent(const QString& id) const;
    
    // Helper to generate unique ID
    QString generateUniqueId(ComponentType type);
    
    // Get scene bounding box (for auto-placement)
    QRectF getSceneBounds();
    
    // --- Phase 1.3: Relational Positioning API ---
    enum class RelativeDirection { Left, Right, Above, Below };
    
    // Place a component relative to an existing one
    bool placeNextTo(const QString& type, const QString& referenceId, RelativeDirection dir, 
                     int margin = 60, const QString& params = "");
    
    // Align a component with another (horizontal or vertical center)
    bool alignWith(const QString& targetId, const QString& referenceId, bool horizontal = true);
    
    // Find nearest clear space of given size
    QPoint findClearSpace(int width = 60, int height = 60);
    
    // Find center coordinate of a net/node (for smart probe placement)
    QPointF findNodeCenter(const QString& nodeName);
    
    // Get absolute scene coordinates of a component's terminals
    QVector<QPointF> getComponentTerminals(const QString& id);
    
    // Get spatial relationships for AI context
    QString getSpatialContext();
    
    // Overloaded placement for AI assistant (string-based)
    bool placeComponent(const QString& type, int x, int y, const QString& id = "", const QString& params = "");
    
    // Explicit Probe Placement with Custom Tip Locations
    QString placeProbe(const QString& id, QPointF bodyPos, QPointF posTip, QPointF negTip);

    // --- Phase 14.2: Bi-Directional Parsing ---
    // Schematic -> Verilog-AMS Code
    QString generateVerilogSnippet();
    
    // Verilog-AMS Code -> Schematic (Ghost Placement)
    void parseVerilogToSchematic(const QString& code);

    // --- Phase 2: Hierarchical Schematics ---
    bool saveToFile(const QString& filePath);
    bool loadFromFile(const QString& filePath);
    void createSubcircuitFromSelection();

    // --- Phase 5: Hierarchical Drill-Down Navigation ---
public:
    acutesim::gui::core::HierarchyPath* hierarchyPath() { return hierarchyPath_; }
    const acutesim::gui::core::HierarchyPath* hierarchyPath() const { return hierarchyPath_; }

    void descendInto(const QString& filePath, const QString& instanceName);
    void ascend();
    void navigateToDepth(int depth);
    bool canAscend() const;
    QString rootFilePath() const { return rootFilePath_; }

signals:
    void itemSelected(VisualComponent* item);
    void wireSelected(VisualWire* wire);
    void requestOpenSubcircuit(const QString& filePath); // Drill-Down Event
    void hierarchyChanged();  // Emitted when hierarchy navigation occurs

private slots:
    void onGridToggled(bool checked);
    void onSnappingToggled(bool checked);
    void onBreadcrumbNavigation(int depth);


private:
    // currentMode member removed - unified component library
    GridScene* scene;
    SchematicView* view;
    QLabel* statusLabel;

    // Per-file simulation profiles
    // State
    QMap<QString, Simulation::SimulationProfile> profiles_;
    QString currentProfileId_;
    Simulation::SimulationProfile* currentProfile_ = nullptr;
    size_t topologyHash_ = 0;

    // Flags
    bool ercEnabled_ = true;
    bool quickPreviewEnabled_ = true; // Default: on
    bool modified_ = false;

    void setupUi();
    QWidget* createRibbon();
    QPushButton* createToolButton(const QString& text, ComponentType type);
    QMap<ComponentType, QPushButton*> toolButtons;

    QString filePath_;
    QString rootFilePath_;  // Original file path (for hierarchy root tracking)

    // --- Phase 5: Hierarchy Navigation State ---
    acutesim::gui::core::HierarchyPath* hierarchyPath_ = nullptr;
    acutesim::gui::widgets::BreadcrumbWidget* breadcrumbWidget_ = nullptr;

    // Editor state stack for navigation restoration
    struct EditorState {
        QJsonObject sceneState;
        QPointF viewportCenter;
        qreal zoomLevel;
        QString currentProfileId;
    };
    QVector<EditorState> stateStack_;

    void saveCurrentState();
    void restoreState(const EditorState& state);
    void initializeHierarchy();
};
