#pragma once

#include <QObject>
#include <QMap>
#include <QVariant>
#include <QVector>
#include <QRgb>
#include "../../components/design_db.h"

// Forward Declarations
class QGraphicsItem;
class AbstractPlotWidget; // Base for Waveform Viewer?

namespace acutesim {
namespace gui {

using namespace components;

/**
 * @brief CrossprobeManager - The "Paradigm Bridge"
 * Links Electrical Truth (DesignDb UUIDs) to Visual Representation (GraphicsItems)
 * and Simulation Reality (Waveform Traces).
 */
class CrossprobeManager : public QObject, public DesignDbObserver {
    Q_OBJECT
public:
    static CrossprobeManager& instance() {
        static CrossprobeManager inst;
        return inst;
    }
    
    // ========================================================================
    // MAPPING REGISTRY
    // ========================================================================
    
    // GUI -> UUID
    void registerComponent(InstUUID id, QGraphicsItem* item);
    void registerNet(NetUUID id, QGraphicsItem* item); // Wires
    
    // UUID -> Waveform
    struct TraceHandle {
        int traceIndex;
        QRgb color;
        QString name;
    };
    
    // Many-to-Many Map: UUID -> Traces
    void associateTrace(NetUUID netId, int traceIdx, QRgb color);
    void associateTrace(InstUUID instId, int traceIdx, QRgb color);
    
    // ========================================================================
    // SELECTION & HIGHLIGHTING
    // ========================================================================
    
    // Select via UUID (e.g. from AI or Waveform Click)
    void highlight(UUID<NetTag> id);
    void highlight(UUID<InstTag> id);
    
    // Select via GUI Item (e.g. Schematic Click)
    void onGuiSelectionChanged(QList<QGraphicsItem*> selectedItems);
    
    // Select via Trace (e.g. Waveform Click)
    void onTraceClicked(int traceIdx);

signals:
    // Signal for Waveform Viewer to highlight specific traces
    void requestTraceHighlight(QVector<int> traceIndices);
    
    // Signal for Schematic to highlight specific items
    void requestSchematicHighlight(QVector<QGraphicsItem*> items);
    
    // Signal for AI Explanation
    void lookupContext(QString uuidStr);

public: // DesignDbObserver Impl
    void onNetCreated(const Net& net) override {}
    void onNetModified(const Net& net) override {}
    void ofInstancePlaced(const Instance& inst) override {}
    void onInstanceMoved(const Instance& inst) override {}
    void onTopologyInvalidated() override {
        // Clear trace mappings on re-simulation?
        traceMapNet_.clear();
        traceMapInst_.clear();
    }

private:
    CrossprobeManager() = default;
    
    // Maps
    QMap<QGraphicsItem*, InstUUID> itemToInst_;
    QMap<InstUUID, QGraphicsItem*> instToItem_;
    
    // We might have multiple wire segments for one net
    QMap<QGraphicsItem*, NetUUID> itemToNet_; 
    QMap<NetUUID, QList<QGraphicsItem*>> netToItems_;
    
    // Trace Maps (Multi-map logic via inner struct)
    struct TraceRef { int idx; QRgb color; };
    QMap<QString, QVector<TraceRef>> traceMapNet_; // Keyed by UUID string
    QMap<QString, QVector<TraceRef>> traceMapInst_;
    
    // Reverse Map for Trace Click
    QMap<int, QString> traceToUuid_; // Index -> UUID String
};

} // namespace gui
} // namespace acutesim
