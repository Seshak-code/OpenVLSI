import os

file_path = "/Users/seshak/Desktop/CuteApp/gui/editors/schematic_editor.cpp"
with open(file_path, 'r') as f:
    lines = f.readlines()

start_index = -1
end_index = -1

# Find start
for i, line in enumerate(lines):
    if "TensorNetlist GridScene::generateNetlist() {" in line:
        start_index = i
        break

if start_index == -1:
    print("Could not find generateNetlist start")
    exit(1)

# Find end by brace counting
brace_count = 0
found_start_brace = False
for i in range(start_index, len(lines)):
    line = lines[i]
    brace_count += line.count('{')
    brace_count -= line.count('}')
    
    if brace_count > 0:
        found_start_brace = True
    
    if found_start_brace and brace_count == 0:
        end_index = i
        break

if end_index == -1:
    print("Could not find generateNetlist end")
    exit(1)

print(f"Replacing lines {start_index+1} to {end_index+1}")

new_code = r'''TensorNetlist GridScene::generateNetlist() {
    // Phase 6.1: Paradigm Bridge
    // 1. Populate/Update DesignDb from Spatial State (Migration Strategy)
    // 2. Snapshot DesignDb to TensorBlock
    
    // --- Step 1: Connectivity Analysis (Spatial Approach) ---
    QMap<QPair<int,int>, int> pointToNetID;
    int nextNetID = 1; 
    
    // Function to get distinct net ID for a point, creating new if needed
    auto getNetID = [&](QPointF p) -> int {
        QPair<int,int> key = {(int)round(p.x()), (int)round(p.y())};
        if (!pointToNetID.contains(key)) {
            pointToNetID[key] = nextNetID++;
        }
        return pointToNetID[key];
    };
    
    // Temporary structure to merge sets: map old_id -> new_id
    QMap<int, int> merges;
    
    std::function<int(int)> findSet = [&](int id) {
        while (merges.contains(id)) id = merges[id];
        return id;
    };
    
    auto unionSets = [&](int id1, int id2) {
        int root1 = findSet(id1);
        int root2 = findSet(id2);
        if (root1 != root2) {
            if (root1 < root2) merges[root2] = root1;
            else merges[root1] = root2;
        }
    };
    
    QList<QGraphicsItem*> allItems = items();
    
    // 1.1 Traverse Wires -> Unify points
    for (auto* item : allItems) {
        if (auto* w = dynamic_cast<VisualWire*>(item)) {
            if (!w->isVisible()) continue;
            QLineF line = w->line();
            QPointF sceneP1 = w->mapToScene(line.p1());
            QPointF sceneP2 = w->mapToScene(line.p2());
            
            int x1 = (int)round(sceneP1.x()); int y1 = (int)round(sceneP1.y());
            int x2 = (int)round(sceneP2.x()); int y2 = (int)round(sceneP2.y());
            
            // Unify Start
            int startID = getNetID(QPointF(x1, y1));
            
            if (x1 == x2) { // Vertical
                int minY = std::min(y1, y2); int maxY = std::max(y1, y2);
                for (int y = minY; y <= maxY; y += GRID_STEP) unionSets(startID, getNetID(QPointF(x1, y)));
            } else if (y1 == y2) { // Horizontal
                int minX = std::min(x1, x2); int maxX = std::max(x1, x2);
                for (int x = minX; x <= maxX; x += GRID_STEP) unionSets(startID, getNetID(QPointF(x, y1)));
            } else {
                unionSets(startID, getNetID(QPointF(x2, y2)));
            }
        }
    }
    
    // 1.2 Unify Terminals
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->type == ComponentType::Probe) continue;
            for (const auto& localP : c->getTerminals()) {
                getNetID(c->mapToScene(localP));
            }
        }
    }
    
    // --- Step 2: Sync to DesignDb ---
    using namespace acutesim::components;
    
    // 2.1 Map Spatial Nets to DB Nets
    QMap<int, NetUUID> spatialToDbNet;
    
    auto getDbNet = [&](QPointF p) -> NetUUID {
        int rawID = getNetID(p);
        int rootID = findSet(rawID);
        if (!spatialToDbNet.contains(rootID)) {
             spatialToDbNet[rootID] = designDb.createNet("N" + std::to_string(rootID));
        }
        return spatialToDbNet[rootID];
    };
    
    // Check Ground
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->type == ComponentType::Ground) {
                QPointF termPos = c->mapToScene(c->getTerminals()[0]);
                int rootID = findSet(getNetID(termPos));
                if (!spatialToDbNet.contains(rootID)) {
                     // Create explicit GND net
                     // Note: We need a way to set name to "GND" if we want deterministic ground
                     // For now createNet("GND") works but unique ID matters.
                     spatialToDbNet[rootID] = designDb.createNet("GND");
                }
            }
        }
    }

    // 2.2 Populate Instances
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->type == ComponentType::Select || c->type == ComponentType::Wire || 
                c->type == ComponentType::Probe || c->type == ComponentType::Ground) continue;

             // Ensure Instance Exists
             if (c->dbId == InstUUID()) {
                  c->dbId = designDb.createInstance(componentTypeToString(c->type).toStdString(), 
                                                    c->name.toStdString());
             }
             
             // Sync Parameters
             double val = parseValue(c->value);
             
             std::string mainKey = "val";
             if (c->type == ComponentType::Resistor) { mainKey = "R"; designDb.setStringParam(c->dbId, "Model", c->params.value("Model", "poly_res").toStdString()); }
             else if (c->type == ComponentType::Capacitor) { mainKey = "C"; designDb.setStringParam(c->dbId, "Model", c->params.value("Model", "mim_cap").toStdString()); }
             else if (c->type == ComponentType::Inductor) { mainKey = "L"; designDb.setStringParam(c->dbId, "Model", c->params.value("Model", "spiral_ind").toStdString()); }
             else if (c->type == ComponentType::VoltageSource) mainKey = "V";
             else if (c->type == ComponentType::CurrentSource) mainKey = "I";
             else if (c->type == ComponentType::Diode) mainKey = "Is";
             
             if (!mainKey.empty()) {
                 designDb.setParam(c->dbId, mainKey, val);
             }
             
             // MOSFET Specifics
             if (c->type == ComponentType::Mosfet || c->type == ComponentType::PDK_NFET || c->type == ComponentType::PDK_PFET) {
                 designDb.setParam(c->dbId, "W", parseValue(c->params.value("W", "1u")));
                 designDb.setParam(c->dbId, "L", parseValue(c->params.value("L", "180n")));
                 designDb.setStringParam(c->dbId, "Model", c->params.value("Model", (c->type==ComponentType::PDK_PFET ? "pfet" : "nfet")).toStdString());
             }
             
             // VCVS/VCCS/CCVS/CCCS -> Gain
             if (c->type == ComponentType::VCVS || c->type == ComponentType::VCCS || 
                 c->type == ComponentType::CCVS || c->type == ComponentType::CCCS) {
                 designDb.setParam(c->dbId, "Gain", val);
             }

             // Wire up pins (Clear first to avoid dupes in this "refresh" logic)
             designDb.clearPins(c->dbId);
             
             QVector<QPointF> terms = c->getTerminals();
             for (int i = 0; i < terms.size(); ++i) {
                 QPointF p = c->mapToScene(terms[i]);
                 NetUUID netId = getDbNet(p);
                 designDb.connectPin(c->dbId, i, netId);
             }
        }
    }
    
    // 3. Snapshot
    TensorBlock block = designDb.snapshot();
    
    // Wrap in TensorNetlist
    TensorNetlist netlist;
    netlist.globalBlock = block;
    netlist.globalState.initialize(block); 
    
    return netlist;
}
'''

new_lines = lines[:start_index] + [new_code] + lines[end_index+1:]

with open(file_path, 'w') as f:
    f.writelines(new_lines)

print("Success")
