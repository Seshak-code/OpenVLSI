#include "design_db.h"
#include "acutesim_engine/netlist/circuit.h"
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

DesignDb::GraphSnapshotResult DesignDb::snapshotWithNodeMap() const {
    GraphSnapshotResult result;
    auto& graph = result.graph;
    auto& netToNode = result.netToNode;

    graph.name = "DesignDb_Snapshot";

    // 1. Connectivity Mapping
    int nextNode = 1;
    std::vector<const Net*> sortedNets;
    for (const auto& kv : nets_) sortedNets.push_back(&kv.second);
    std::sort(sortedNets.begin(), sortedNets.end(), [](const Net* a, const Net* b){
        return a->name < b->name;
    });

    for (const Net* net : sortedNets) {
        acutesim::compute::orchestration::SchematicNodeDTO nodeDto;
        nodeDto.name = net->name;
        
        if (net->name == "0" || net->name == "GND" || net->name == "gnd" || net->name == "VSS") {
            nodeDto.index = 0;
            netToNode[net->id] = 0;
        } else {
            nodeDto.index = nextNode++;
            netToNode[net->id] = nodeDto.index;
        }
        graph.nets.push_back(nodeDto);
    }

    // 2. Build Instance Connectivity Map (InstUUID -> Terminal -> NetName)
    std::map<InstUUID, std::map<int, std::string>> instConn;
    for (const auto& kv : pins_) {
        const Pin& pin = kv.second;
        auto netIt = nets_.find(pin.netId);
        std::string netName = (netIt != nets_.end()) ? netIt->second.name : "0";
        instConn[pin.instanceId][pin.terminalIndex] = netName;
    }

    // 3. Instantiate Components
    std::vector<const Instance*> sortedInsts;
    for (const auto& kv : instances_) sortedInsts.push_back(&kv.second);
    std::sort(sortedInsts.begin(), sortedInsts.end(), [](const Instance* a, const Instance* b){
        return a->name < b->name;
    });

    for (const Instance* instPtr : sortedInsts) {
        const Instance& inst = *instPtr;
        acutesim::compute::orchestration::SchematicInstanceDTO instDto;
        instDto.type = inst.masterName;
        instDto.instanceName = inst.name;

        for (const auto& p : inst.parameters) {
            instDto.parameters[p.first] = std::to_string(p.second);
        }
        for (const auto& p : inst.stringParameters) {
            instDto.parameters[p.first] = p.second;
        }

        const auto& nodes = instConn[inst.id];
        int maxTerm = -1;
        for (const auto& n : nodes) {
            if (n.first > maxTerm) maxTerm = n.first;
        }
        for (int i = 0; i <= maxTerm; ++i) {
            auto it = nodes.find(i);
            instDto.connectedNets.push_back((it != nodes.end()) ? it->second : "0");
        }

        graph.instances.push_back(instDto);
    }

    return result;
}

TensorNetlist DesignDb::toNetlist() const {
    // Node indices come from the same mapping the DTO snapshot uses; parameters are
    // read straight from instances_ to skip the DTO's double->string->double round trip.
    const auto netToNode = snapshotWithNodeMap().netToNode;
    TensorNetlist netlist;
    TensorBlock& block = netlist.globalBlock;

    std::map<InstUUID, std::map<int, int>> instConn;
    for (const auto& kv : pins_) {
        auto it = netToNode.find(kv.second.netId);
        instConn[kv.second.instanceId][kv.second.terminalIndex] = (it != netToNode.end()) ? it->second : 0;
    }

    auto getParam = [](const Instance& inst, const std::string& key, double def) {
        auto it = inst.parameters.find(key);
        return (it != inst.parameters.end()) ? it->second : def;
    };
    auto getStrParam = [](const Instance& inst, const std::string& key, const std::string& def) {
        auto it = inst.stringParameters.find(key);
        return (it != inst.stringParameters.end()) ? it->second : def;
    };

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
        const std::string& m = inst.masterName;

        if (m == "R" || m == "res_generic" || m == "Resistor" || m == "PDK_Resistor") {
            block.addResistor(inst.name, getN(0), getN(1), getParam(inst, "R", 1000.0));
            auto& r = block.resistors.back();
            r.model_name = getStrParam(inst, "Model", "poly_res");
            r.width_m = getParam(inst, "W", 0.0);
        }
        else if (m == "C" || m == "cap_generic" || m == "Capacitor" || m == "PDK_Capacitor") {
            block.addCapacitor(inst.name, getN(0), getN(1), getParam(inst, "C", 1e-12));
            block.capacitors.back().model_name = getStrParam(inst, "Model", "mim_cap");
        }
        else if (m == "L" || m == "ind_generic" || m == "Inductor") {
            block.addInductor(inst.name, getN(0), getN(1), getParam(inst, "L", 1e-9));
            auto& l = block.inductors.back();
            l.model_name = getStrParam(inst, "Model", "ind_generic");
            l.width_m = getParam(inst, "W", 0.0);
            l.area_m2 = getParam(inst, "Area", 0.0);
        }
        else if (m == "V" || m == "vsource" || m == "vdc" || m == "VoltageSource") {
            block.addVoltageSource(getN(0), getN(1), getParam(inst, "V", 0.0), "DC");
        }
        else if (m == "I" || m == "isource" || m == "idc" || m == "CurrentSource") {
            block.addCurrentSource(getN(0), getN(1), getParam(inst, "I", 0.0));
        }
        else if (m == "D" || m == "diode" || m == "diode_generic" || m == "Diode") {
            block.addDiode(inst.name, getN(0), getN(1), getParam(inst, "Is", 1e-14));
        }
        else if (m == "nfet" || m == "nfet_01v8" || m == "Mosfet" || m == "PDK_NFET") {
            block.addMosfet(inst.name, getN(0), getN(1), getN(2), getN(3),
                            getStrParam(inst, "Model", "nfet"), getParam(inst, "W", 1e-6), getParam(inst, "L", 1e-6));
        }
        else if (m == "pfet" || m == "pfet_01v8" || m == "PDK_PFET") {
            block.addMosfet(inst.name, getN(0), getN(1), getN(2), getN(3),
                            getStrParam(inst, "Model", "pfet"), getParam(inst, "W", 1e-6), getParam(inst, "L", 1e-6));
        }
        else if (m == "Bjt") {
            block.addBJT(inst.name, getN(0), getN(1), getN(2), getStrParam(inst, "Model", "2N2222"), true);
        }
        // Controlled sources: terminals 0,1 = control, 2,3 = output
        else if (m == "VCVS") block.addVCVS(getN(2), getN(3), getN(0), getN(1), getParam(inst, "Gain", 1.0));
        else if (m == "VCCS") block.addVCCS(getN(2), getN(3), getN(0), getN(1), getParam(inst, "Gain", 1.0));
        else if (m == "CCVS") block.addCCVS(getN(2), getN(3), getN(0), getN(1), getParam(inst, "Gain", 1.0));
        else if (m == "CCCS") block.addCCCS(getN(2), getN(3), getN(0), getN(1), getParam(inst, "Gain", 1.0));
    }

    int maxNode = 0;
    for (const auto& kv : netToNode) maxNode = std::max(maxNode, kv.second);
    block.numInternalNodes = std::max(block.numInternalNodes, maxNode);
    netlist.numGlobalNodes = block.numInternalNodes;
    netlist.globalState.initialize(block);
    return netlist;
}

acutesim::compute::orchestration::SchematicGraphDTO DesignDb::buildGraphDTO() const {
    return snapshotWithNodeMap().graph;
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

acutesim::compute::orchestration::HierarchicalSchematicDTO acutesim::components::DesignDb::recursiveBuild(
    std::function<std::optional<acutesim::compute::orchestration::HierarchicalSchematicDTO>(
        const std::string& masterName, const std::map<std::string, double>& params)> resolver,
    int maxDepth
) const {
    acutesim::compute::orchestration::HierarchicalSchematicDTO result;

    result.topGraph = buildGraphDTO();

    if (maxDepth <= 0) {
        return result;
    }

    // Cache to avoid resolving the same subcircuit type/params multiple times
    // Key is just masterName for basic compatibility; in a real scenario you might
    // key by a hash of param values too, but SchematicGraphDTO natively represents
    // instantiated graphs anyway.
    std::set<std::string> resolvedSubcircuits;

    std::function<void(const DesignDb&, int)> processDb = [&](const DesignDb& db, int depth) {
        if (depth > maxDepth) return;

        for (const auto& [id, inst] : db.instances_) {
            const std::string& masterName = inst.masterName;

            // Skip common primitives (Optimization)
            if (masterName == "R" || masterName == "C" || masterName == "L" ||
                masterName == "V" || masterName == "I" || masterName == "Mosfet" ||
                masterName == "Bjt" || masterName == "Diode" ||
                masterName == "Resistor" || masterName == "Capacitor" || masterName == "Inductor" ||
                masterName == "VoltageSource" || masterName == "CurrentSource" ||
                masterName == "Pin" || masterName == "VCVS" || masterName == "VCCS" ||
                masterName == "CCVS" || masterName == "CCCS" ||
                masterName == "nfet" || masterName == "pfet" || masterName == "PDK_NFET" || masterName == "PDK_PFET" ||
                masterName == "res_generic" || masterName == "cap_generic" || masterName == "ind_generic") {
                continue; // Primitive
            }

            if (resolvedSubcircuits.count(masterName)) {
                continue; // Already resolved
            }

            // Try to resolve
            auto subHierOpt = resolver(masterName, inst.parameters);
            if (subHierOpt) {
                resolvedSubcircuits.insert(masterName);
                result.subcircuits[masterName] = subHierOpt->topGraph;
                
                // Merge nested subcircuits into the global map
                for (const auto& [name, subGraph] : subHierOpt->subcircuits) {
                    if (!resolvedSubcircuits.count(name)) {
                        result.subcircuits[name] = subGraph;
                        resolvedSubcircuits.insert(name);
                    }
                }
            }
        }
    };

    processDb(*this, 1);

    return result;
}
