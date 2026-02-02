#include "design_db.h"
#include "circuit.h"
#include <iostream>
#include <algorithm>
#include <map>
#include <set>

namespace acutesim {
namespace components {

NetUUID DesignDb::createNet(const std::string& name) {
    NetUUID id = NetUUID::generate();
    Net net;
    net.id = id;
    net.name = name;
    
    nets_[id] = net;
    
    for (auto* obs : observers_) obs->onNetCreated(net);
    return id;
}

InstUUID DesignDb::createInstance(const std::string& master, const std::string& name) {
    InstUUID id = InstUUID::generate();
    Instance inst;
    inst.id = id;
    inst.masterName = master;
    inst.name = name;
    
    // Default spatial (0,0)
    inst.spatial = {0.0, 0.0, 0.0, false};
    
    instances_[id] = inst;
    
    for (auto* obs : observers_) obs->ofInstancePlaced(inst);
    return id;
}

PinUUID DesignDb::connectPin(InstUUID instId, int terminal, NetUUID netId) {
    auto instIt = instances_.find(instId);
    auto netIt = nets_.find(netId);
    
    if (instIt == instances_.end() || netIt == nets_.end()) {
        // In a real system, might return invalid or throw
        return PinUUID(); 
    }
    
    PinUUID id = PinUUID::generate();
    Pin pin;
    pin.id = id;
    pin.instanceId = instId;
    pin.terminalIndex = terminal;
    pin.netId = netId;
    
    pins_[id] = pin;
    
    // Update Net connectivity
    netIt->second.connectedPins.insert(id);
    
    // Notify observers of modification to the NET (connectivity change)
    for (auto* obs : observers_) obs->onNetModified(netIt->second);
    
    return id;
}

void DesignDb::disconnectPin(PinUUID pinId) {
    auto it = pins_.find(pinId);
    if (it == pins_.end()) return;
    
    Pin pin = it->second;
    NetUUID netId = pin.netId;
    
    // Update Net
    auto netIt = nets_.find(netId);
    if (netIt != nets_.end()) {
        netIt->second.connectedPins.erase(pinId);
        for (auto* obs : observers_) obs->onNetModified(netIt->second);
    }
    
    pins_.erase(it);
}

void DesignDb::setParam(InstUUID instId, const std::string& key, double value) {
    auto it = instances_.find(instId);
    if (it != instances_.end()) {
        it->second.parameters[key] = value;
        // Notify?
    }
}

void DesignDb::setStringParam(InstUUID instId, const std::string& key, const std::string& value) {
    auto it = instances_.find(instId);
    if (it != instances_.end()) {
        it->second.stringParameters[key] = value;
    }
}

void DesignDb::clearPins(InstUUID instId) {
    // Inefficient linear scan, but better than map<Inst, vector<Pin>> maintenance for now
    // Actually we need to delete pins belonging to this instance.
    auto it = pins_.begin();
    while (it != pins_.end()) {
        if (it->second.instanceId == instId) {
             NetUUID netId = it->second.netId;
             auto netIt = nets_.find(netId);
             if (netIt != nets_.end()) {
                 netIt->second.connectedPins.erase(it->first);
             }
             it = pins_.erase(it);
        } else {
            ++it;
        }
    }
}


const Net* DesignDb::getNet(NetUUID id) const {
    auto it = nets_.find(id);
    return (it != nets_.end()) ? &it->second : nullptr;
}

const Instance* DesignDb::getInstance(InstUUID id) const {
    auto it = instances_.find(id);
    return (it != instances_.end()) ? &it->second : nullptr;
}

Instance* DesignDb::getInstanceMutable(InstUUID id) {
    auto it = instances_.find(id);
    return (it != instances_.end()) ? &it->second : nullptr;
}

const Pin* DesignDb::getPin(PinUUID id) const {
    auto it = pins_.find(id);
    return (it != pins_.end()) ? &it->second : nullptr;
}

void DesignDb::addObserver(DesignDbObserver* obs) {
    observers_.push_back(obs);
}

void DesignDb::removeObserver(DesignDbObserver* obs) {
    auto it = std::remove(observers_.begin(), observers_.end(), obs);
    observers_.erase(it, observers_.end());
}

// ============================================================================
// SNAPSHOT GENERATION (Paradigm Bridge)
// ============================================================================

TensorBlock DesignDb::snapshot() const {
    TensorBlock block;
    // Uses a distinct name to ensure cache uniqueness
    block.name = "DesignDb_Snapshot"; 

    // 1. Connectivity Mapping (Contextual Node IDs)
    // DesignDb NetUUID -> Solver int NodeIndex
    std::map<NetUUID, int> netToNode;
    int nextNode = 1;
    
    // Ensure "0", "GND", "gnd" map to 0
    std::vector<const Net*> sortedNets;
    for (const auto& kv : nets_) sortedNets.push_back(&kv.second);
    std::sort(sortedNets.begin(), sortedNets.end(), [](const Net* a, const Net* b){
        return a->name < b->name;
    });

    for (const Net* net : sortedNets) {
        if (net->name == "0" || net->name == "GND" || net->name == "gnd" || net->name == "VSS") {
            netToNode[net->id] = 0;
        } else {
            netToNode[net->id] = nextNode++;
        }
    }
    
    block.numInternalNodes = nextNode - 1;

    // 2. Build Instance Connectivity Map (InstUUID -> Terminal -> NodeIndex)
    std::map<InstUUID, std::map<int, int>> instConn;
    for (const auto& kv : pins_) {
        const Pin& pin = kv.second;
        auto it = netToNode.find(pin.netId);
        int nodeIdx = (it != netToNode.end()) ? it->second : 0; 
        instConn[pin.instanceId][pin.terminalIndex] = nodeIdx;
    }

    // Helper to safely get parameter
    auto getParam = [](const Instance& inst, const std::string& key, double def) {
        auto it = inst.parameters.find(key);
        return (it != inst.parameters.end()) ? it->second : def;
    };
    
    // Helper to safely get string parameter
    auto getStrParam = [](const Instance& inst, const std::string& key, const std::string& def) {
        auto it = inst.stringParameters.find(key);
        return (it != inst.stringParameters.end()) ? it->second : def;
    };

    // 3. Instantiate Components (Technology Mapping)
    std::vector<const Instance*> sortedInsts;
    for (const auto& kv : instances_) sortedInsts.push_back(&kv.second);
    std::sort(sortedInsts.begin(), sortedInsts.end(), [](const Instance* a, const Instance* b){
        return a->name < b->name;
    });

    for (const Instance* instPtr : sortedInsts) {
        const Instance& inst = *instPtr;
        const auto& nodes = instConn[inst.id];
        
        auto getN = [&](int t) { 
            auto it = nodes.find(t);
            return (it != nodes.end()) ? it->second : 0;
        };

        std::string m = inst.masterName;
        
        if (m == "R" || m == "res_generic" || m == "Resistor") {
            block.addResistor(inst.name, getN(0), getN(1), getParam(inst, "R", 1000.0));
            if (!inst.stringParameters.empty()) {
                // Populate model if present
            }
        }
        else if (m == "C" || m == "cap_generic" || m == "Capacitor") {
            block.addCapacitor(inst.name, getN(0), getN(1), getParam(inst, "C", 1e-12));
        }
        else if (m == "L" || m == "ind_generic" || m == "Inductor") {
            block.addInductor(inst.name, getN(0), getN(1), getParam(inst, "L", 1e-9));
        }
        else if (m == "V" || m == "vsource" || m == "vdc" || m == "VoltageSource") {
            block.addVoltageSource(getN(0), getN(1), getParam(inst, "V", 0.0), "DC");
        }
        else if (m == "I" || m == "isource" || m == "idc" || m == "CurrentSource") {
            block.addCurrentSource(getN(0), getN(1), getParam(inst, "I", 0.0));
        }
        else if (m == "D" || m == "diode" || m == "diode_generic" || m == "Diode") {
            block.addDiode(getN(0), getN(1), getParam(inst, "Is", 1e-14));
        }
        else if (m == "nfet" || m == "nfet_01v8" || m == "Mosfet" || m == "PDK_NFET") {
            // MOSFET D, G, S, B
            // Default generic MOSFET to nfet if not specified, but visual component tells us.
            // Check Model param?
            std::string model = getStrParam(inst, "Model", "nfet");
            block.addMosfet(inst.name, getN(0), getN(1), getN(2), getN(3),
                            model, getParam(inst, "W", 1e-6), getParam(inst, "L", 1e-6));
        }
        else if (m == "pfet" || m == "pfet_01v8" || m == "PDK_PFET") {
            std::string model = getStrParam(inst, "Model", "pfet");
            block.addMosfet(inst.name, getN(0), getN(1), getN(2), getN(3),
                            model, getParam(inst, "W", 1e-6), getParam(inst, "L", 1e-6));           
        }
        else if (m == "Bjt") {
             std::string model = getStrParam(inst, "Model", "2N2222");
             block.addBJT(inst.name, getN(0), getN(1), getN(2), model, true);
        }
        else if (m == "VCVS") {
             block.addVCVS(getN(2), getN(3), getN(0), getN(1), getParam(inst, "Gain", 1.0));
        }
        // ... (Other dependent sources)
    }
    
    block.freeze();
    return block;
}

} // namespace components
} // namespace acutesim

acutesim::components::MutationResult acutesim::components::DesignDb::renameNet(NetUUID id, const std::string& newName) {
    if (nets_.find(id) == nets_.end()) return MutationResult::NotFound;
    
    // Check uniqueness (optional, but good practice)
    for (const auto& pair : nets_) {
        if (pair.second.name == newName && pair.first != id) {
            return MutationResult::NameCollision; // Name collision
        }
    }
    
    std::string oldName = nets_[id].name;
    if (oldName == newName) return MutationResult::NoChange;

    nets_[id].name = newName;
    
    // Notify observers
    for (auto* obs : observers_) {
        obs->onNetRenamed(id, oldName, newName);
    }
    return MutationResult::Success;
}

void acutesim::components::DesignDb::clear() {
    nets_.clear();
    instances_.clear();
    pins_.clear();
    // Keep observers? Yes.
}

acutesim::components::MutationResult acutesim::components::DesignDb::renameNet(const std::string& oldName, const std::string& newName) {
    // Linear search for now (not optimized for huge netlists, but fine for GUI interaction)
    for (const auto& pair : nets_) {
        if (pair.second.name == oldName) {
            return renameNet(pair.first, newName);
        }
    }
    return MutationResult::NotFound;
}

// ============================================================================
// HIERARCHICAL SNAPSHOT (Phase 5)
// ============================================================================

acutesim::components::HierarchicalSnapshot acutesim::components::DesignDb::recursiveSnapshot(
    std::function<std::shared_ptr<TensorBlock>(const std::string& masterName, const std::map<std::string, double>& params)> resolver,
    int maxDepth
) const {
    HierarchicalSnapshot result;

    // 1. Snapshot the top-level block
    result.topBlock = std::make_shared<TensorBlock>(snapshot());
    result.maxDepthReached = 0;

    if (maxDepth <= 0) {
        return result;
    }

    // 2. Resolve sub-blocks recursively
    std::function<void(const DesignDb&, int)> processDb = [&](const DesignDb& db, int depth) {
        if (depth > maxDepth) return;
        result.maxDepthReached = std::max(result.maxDepthReached, depth);

        for (const auto& [id, inst] : db.instances_) {
            const std::string& masterName = inst.masterName;

            // Skip common primitives (Optimization)
            if (masterName == "R" || masterName == "C" || masterName == "L" || 
                masterName == "V" || masterName == "I" || masterName == "Mosfet" ||
                masterName == "Bjt" || masterName == "Diode") {
                continue;
            }

            // Check Cache
            HierarchicalSnapshot::CacheKey key = {masterName, inst.parameters};
            if (result.subBlockCache.count(key)) {
                continue;
            }

            // Try to resolve
            auto subBlock = resolver(masterName, inst.parameters);
            if (subBlock) {
                result.subBlockCache[key] = subBlock;
                result.subBlocks[masterName] = subBlock; // For convenience
                result.loadOrder.push_back(masterName);
                
                // Note: The resolver should handle the recursion for the sub-schematic's internal instances
                // by calling its own generateNetlist() -> recursiveSnapshot().
            }
        }
    };

    processDb(*this, 1);

    return result;
}
