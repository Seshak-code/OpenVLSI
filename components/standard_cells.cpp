#include "standard_cells.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <iostream>
#include <cmath>

namespace StandardCells {

// ── Engineering value parser (e.g. "10k" → 10000.0, "0.18u" → 0.18e-6) ──

static double parseEngValue(const std::string& str) {
    if (str.empty()) return 0.0;
    size_t idx = 0;
    double val = 0.0;
    try { val = std::stod(str, &idx); } catch (...) { return 0.0; }
    if (idx >= str.size()) return val;
    char suffix = str[idx];
    switch (suffix) {
        case 'k': case 'K': return val * 1e3;
        case 'M':           return val * 1e6;
        case 'G':           return val * 1e9;
        case 'm':           return val * 1e-3;
        case 'u':           return val * 1e-6;
        case 'n':           return val * 1e-9;
        case 'p':           return val * 1e-12;
        case 'f':           return val * 1e-15;
        default:            return val;
    }
}

// ── Pin offset lookup per component type ──
// Returns pin positions relative to component center (x,y) for a given type
// and rotation. Pins are returned as {name, dx, dy} tuples.

struct PinOffset { std::string name; int dx; int dy; };

static std::vector<PinOffset> getPinOffsets(const std::string& type, int rotation) {
    // Base offsets (rotation 0) per component type.
    // The GUI uses 20px grid offsets for pin spacing.
    if (type == "Resistor" || type == "Capacitor" || type == "Inductor") {
        // 2-terminal: n1 at top, n2 at bottom (rotation 0 = vertical)
        // rotation 90 = horizontal (n1 left, n2 right)
        if (rotation == 0 || rotation == 180) {
            return {{"n1", 0, -20}, {"n2", 0, 20}};
        } else { // 90, -90, 270
            return {{"n1", -20, 0}, {"n2", 20, 0}};
        }
    }
    if (type == "Diode") {
        // Anode → Cathode direction
        if (rotation == 0) return {{"anode", 0, -20}, {"cathode", 0, 20}};
        if (rotation == 90) return {{"anode", -20, 0}, {"cathode", 20, 0}};
        if (rotation == -90 || rotation == 270) return {{"anode", 20, 0}, {"cathode", -20, 0}};
        if (rotation == 180) return {{"anode", 0, 20}, {"cathode", 0, -20}};
    }
    if (type == "NPN" || type == "PNP") {
        // C at top, B at left, E at bottom
        return {{"C", 0, -20}, {"B", -20, 0}, {"E", 0, 20}};
    }
    if (type == "PDK_NFET" || type == "NMOS") {
        // D at top, G at left, S at bottom, B at right
        return {{"D", 0, -20}, {"G", -20, 0}, {"S", 0, 20}, {"B", 20, 0}};
    }
    if (type == "PDK_PFET" || type == "PMOS") {
        return {{"D", 0, -20}, {"G", -20, 0}, {"S", 0, 20}, {"B", 20, 0}};
    }
    if (type == "Pin") {
        // External pin: connects at its own position
        return {{"pin", 0, 0}};
    }
    return {};
}

// ── .sch JSON → SubCircuitDefinition parser ──

bool StandardCellLibrary::loadCellFromSchematic(const std::string& path,
                                                 SubCircuitDefinition& outDef)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;
    QJsonObject root = doc.object();

    // Extract metadata for cell name
    QJsonObject meta = root["metadata"].toObject();
    std::string cellName = meta["description"].toString().toStdString();
    if (cellName.empty()) {
        // Derive name from filename
        QString fname = QString::fromStdString(path);
        cellName = QFileInfo(fname).baseName().toStdString();
    }

    outDef.name = cellName;
    outDef.description = cellName;

    // Phase 1: Build a position → netName map from wires
    // Key: (x, y) → netName
    std::map<std::pair<int,int>, std::string> pointToNet;
    QJsonArray wires = root["wires"].toArray();
    for (const auto& w : wires) {
        QJsonObject wire = w.toObject();
        std::string netName = wire["netName"].toString().toStdString();
        QJsonArray points = wire["points"].toArray();
        for (const auto& p : points) {
            QJsonObject pt = p.toObject();
            int px = pt["x"].toInt();
            int py = pt["y"].toInt();
            pointToNet[{px, py}] = netName;
        }
    }

    // Phase 2: Assign node indices per unique net name
    // GND pin maps to node 0 by convention
    std::map<std::string, int> netToNode;
    int nextNode = 1;

    // Phase 3: Process components
    QJsonArray components = root["components"].toArray();
    std::vector<std::string> externalPinNames;
    std::map<std::string, int> pinNameToNodeIndex; // for pinToInternalNode

    // First pass: identify external pins and their nets to build the pin map
    for (const auto& comp : components) {
        QJsonObject c = comp.toObject();
        std::string type = c["type"].toString().toStdString();
        if (type != "Pin") continue;

        std::string pinName = c["name"].toString().toStdString();
        int cx = c["x"].toInt();
        int cy = c["y"].toInt();

        // Find the net this pin connects to
        auto it = pointToNet.find({cx, cy});
        std::string netName = (it != pointToNet.end()) ? it->second : ("__pin_" + pinName);

        // GND pins map to node 0
        int nodeIdx;
        if (pinName == "GND" || pinName == "VSS") {
            nodeIdx = 0;
            netToNode[netName] = 0;
        } else {
            if (netToNode.find(netName) == netToNode.end())
                netToNode[netName] = nextNode++;
            nodeIdx = netToNode[netName];
        }

        externalPinNames.push_back(pinName);
        pinNameToNodeIndex[pinName] = nodeIdx;
    }

    outDef.pinCount = static_cast<int>(externalPinNames.size());
    outDef.pinNames = externalPinNames;
    for (int i = 0; i < (int)externalPinNames.size(); ++i) {
        outDef.pinToInternalNode[i] = pinNameToNodeIndex[externalPinNames[i]];
    }

    TensorBlock& block = outDef.internalBlock;
    block.name = cellName + "_SCH";
    block.pinNames = externalPinNames;

    // Helper: resolve a component pin position to a node index
    auto resolvePin = [&](int px, int py) -> int {
        auto it = pointToNet.find({px, py});
        if (it == pointToNet.end()) return 0; // unconnected → ground
        const std::string& net = it->second;
        if (netToNode.find(net) == netToNode.end())
            netToNode[net] = nextNode++;
        return netToNode[net];
    };

    // Second pass: create device instances
    for (const auto& comp : components) {
        QJsonObject c = comp.toObject();
        std::string type = c["type"].toString().toStdString();
        if (type == "Pin") continue; // already handled

        std::string name = c["name"].toString().toStdString();
        int cx = c["x"].toInt();
        int cy = c["y"].toInt();
        int rot = c["rotation"].toInt();
        QJsonObject props = c["properties"].toObject();

        auto offsets = getPinOffsets(type, rot);
        // Build pin-position → node map for this component
        std::map<std::string, int> pinNodes;
        for (const auto& po : offsets) {
            pinNodes[po.name] = resolvePin(cx + po.dx, cy + po.dy);
        }

        if (type == "Resistor") {
            double R = 1000.0;
            if (props.contains("R")) R = parseEngValue(props["R"].toString().toStdString());
            block.addResistor(name, pinNodes["n1"], pinNodes["n2"], R);
        }
        else if (type == "Capacitor") {
            double C = 1e-12;
            if (props.contains("C")) C = parseEngValue(props["C"].toString().toStdString());
            block.addCapacitor(name, pinNodes["n1"], pinNodes["n2"], C);
        }
        else if (type == "Inductor") {
            double L = 1e-6;
            if (props.contains("L")) L = parseEngValue(props["L"].toString().toStdString());
            block.addInductor(name, pinNodes["n1"], pinNodes["n2"], L);
        }
        else if (type == "Diode") {
            double Is = 1e-12;
            if (props.contains("Is")) Is = parseEngValue(props["Is"].toString().toStdString());
            block.addDiode(pinNodes["anode"], pinNodes["cathode"], Is);
        }
        else if (type == "NPN") {
            std::string model = props.contains("Model")
                ? props["Model"].toString().toStdString() : "npn_generic";
            double beta = 100.0;
            if (props.contains("Beta")) beta = parseEngValue(props["Beta"].toString().toStdString());
            block.addBJT(name, pinNodes["C"], pinNodes["B"], pinNodes["E"],
                         model, true, beta);
        }
        else if (type == "PNP") {
            std::string model = props.contains("Model")
                ? props["Model"].toString().toStdString() : "pnp_generic";
            double beta = 100.0;
            if (props.contains("Beta")) beta = parseEngValue(props["Beta"].toString().toStdString());
            block.addBJT(name, pinNodes["C"], pinNodes["B"], pinNodes["E"],
                         model, false, beta);
        }
        else if (type == "PDK_NFET" || type == "NMOS" ||
                 type == "PDK_PFET" || type == "PMOS") {
            std::string model = props.contains("Model")
                ? props["Model"].toString().toStdString() : "nmos_generic";
            double W = 1e-6, L = 0.18e-6;
            if (props.contains("W")) W = parseEngValue(props["W"].toString().toStdString());
            if (props.contains("L")) L = parseEngValue(props["L"].toString().toStdString());

            int bodyNode = pinNodes.count("B") ? pinNodes["B"] : pinNodes["S"];
            block.addMosfet(name, pinNodes["D"], pinNodes["G"], pinNodes["S"],
                            bodyNode, model, W, L);
        }
    }

    block.numInternalNodes = nextNode - 1;
    return true;
}

int StandardCellLibrary::loadFromDirectory(const std::string& dir) {
    int count = 0;
    QDirIterator it(QString::fromStdString(dir), {"*.sch"},
                    QDir::Files, QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        QString filePath = it.next();
        SubCircuitDefinition def;
        if (loadCellFromSchematic(filePath.toStdString(), def)) {
            dataDrivenDefs_[def.name] = std::move(def);
            ++count;
        } else {
            std::cerr << "[WARN] StandardCells: Failed to load "
                      << filePath.toStdString() << std::endl;
        }
    }
    if (count > 0) {
        std::cout << "[INFO] StandardCells: Loaded " << count
                  << " data-driven cells from " << dir << std::endl;
    }
    return count;
}

} // namespace StandardCells
