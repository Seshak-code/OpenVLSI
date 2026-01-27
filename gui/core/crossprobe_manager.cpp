#include "crossprobe_manager.h"
#include <QGraphicsItem>
#include <QDebug>

namespace acutesim {
namespace gui {

void CrossprobeManager::registerComponent(InstUUID id, QGraphicsItem* item) {
    if (!item) return;
    itemToInst_[item] = id;
    instToItem_[id] = item;
}

void CrossprobeManager::registerNet(NetUUID id, QGraphicsItem* item) {
    if (!item) return;
    itemToNet_[item] = id;
    netToItems_[id].append(item);
}

void CrossprobeManager::associateTrace(NetUUID netId, int traceIdx, QRgb color) {
    TraceRef ref = {traceIdx, color};
    QString uuidStr = QString::fromStdString(netId.toString());
    traceMapNet_[uuidStr].append(ref);
    traceToUuid_[traceIdx] = uuidStr;
}

void CrossprobeManager::associateTrace(InstUUID instId, int traceIdx, QRgb color) {
    TraceRef ref = {traceIdx, color};
    QString uuidStr = QString::fromStdString(instId.toString());
    traceMapInst_[uuidStr].append(ref);
    traceToUuid_[traceIdx] = uuidStr;
}

void CrossprobeManager::highlight(UUID<NetTag> id) {
    // 1. Highlight Traces
    QString uuidStr = QString::fromStdString(id.toString());
    if (traceMapNet_.contains(uuidStr)) {
        QVector<int> indices;
        for (const auto& ref : traceMapNet_[uuidStr]) indices.push_back(ref.idx);
        emit requestTraceHighlight(indices);
    }
    
    // 2. Highlight Schematic Items
    if (netToItems_.contains(id)) {
        QVector<QGraphicsItem*> items;
        for (auto* item : netToItems_[id]) items.push_back(item);
        emit requestSchematicHighlight(items);
    }
}

void CrossprobeManager::highlight(UUID<InstTag> id) {
    // 1. Highlight Traces
    QString uuidStr = QString::fromStdString(id.toString());
    if (traceMapInst_.contains(uuidStr)) {
        QVector<int> indices;
        for (const auto& ref : traceMapInst_[uuidStr]) indices.push_back(ref.idx);
        emit requestTraceHighlight(indices);
    }
    
    // 2. Highlight Schematic Items
    if (instToItem_.contains(id)) {
        QVector<QGraphicsItem*> items;
        items.push_back(instToItem_[id]);
        emit requestSchematicHighlight(items);
    }
}

void CrossprobeManager::onGuiSelectionChanged(QList<QGraphicsItem*> selectedItems) {
    // Map selection to UUIDs and highlight traces
    // Note: Multiple selections?
    
    QVector<int> allTraceIndices;
    
    for (auto* item : selectedItems) {
        if (itemToInst_.contains(item)) {
            InstUUID id = itemToInst_[item];
            QString str = QString::fromStdString(id.toString());
            if (traceMapInst_.contains(str)) {
                for(auto& ref : traceMapInst_[str]) allTraceIndices.push_back(ref.idx);
            }
        }
        if (itemToNet_.contains(item)) {
            NetUUID id = itemToNet_[item];
            QString str = QString::fromStdString(id.toString());
            if (traceMapNet_.contains(str)) {
                for(auto& ref : traceMapNet_[str]) allTraceIndices.push_back(ref.idx);
            }
        }
    }
    
    emit requestTraceHighlight(allTraceIndices);
}

void CrossprobeManager::onTraceClicked(int traceIdx) {
    if (traceToUuid_.contains(traceIdx)) {
        QString uuidStr = traceToUuid_[traceIdx];
        
        // Is it Net or Inst? Try to resolve.
        // Or store type in map.
        // Simple search:
        
        // Check Net Map
        bool found = false;
        // Inefficient linear search for UUID from string if we don't store map string->UUID
        // But we store NetToItems keyed by UUID.
        // We need String -> UUID map or similar.
        // Wait, Map keys are NetUUID struct. Strings are just helper.
        // If I use string in traceToUuid, I need string in netToItems?
        // Or implement string parsing for UUID.
        // Let's assume linear scan over keys for now or add string map.
        // Better: store QVariant or type enum in traceToUuid.
        
        // For Phase 6.1 MVP:
        emit lookupContext(uuidStr); // Let AI explain it
        
        // Highlight Scheme?
        // Need to find UUID from string.
        // ...
    }
}

} // namespace gui
} // namespace acutesim
