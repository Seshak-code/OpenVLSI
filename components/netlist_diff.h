#pragma once
#include "circuit.h"
#include "graph_canonical.h"
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

/*
 * netlist_diff.h - Semantic Netlist Differ
 * Implement semantic comparison between two TensorBlocks.
 */

enum class DiffChangeType {
    ADDED,
    REMOVED,
    MODIFIED_PARAMETER,
    CONNECTIVITY_CHANGED
};

// Simulation Impact Bitmask
enum class SimulationImpact : uint32_t {
    NONE = 0,
    MATRIX_REBUILD = 1 << 0,     // Connectivity change -> sparse pattern change
    JACOBIAN_UPDATE = 1 << 1,    // Parameter change -> numeric update only
    STATE_RESET = 1 << 2         // History invalid -> clear transient history
};

inline SimulationImpact operator|(SimulationImpact a, SimulationImpact b) {
    return static_cast<SimulationImpact>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

struct ComponentChange {
    std::string name;
    std::string type;
    DiffChangeType changeType;
    std::string details;
    uint32_t impactMask;

    bool operator<(const ComponentChange& other) const {
        if (type != other.type) return type < other.type;
        return name < other.name;
    }
};

struct DiffReport {
    bool isSemanticallyIdentical = true;
    bool isTopologyChanged = false;
    std::vector<ComponentChange> changes;

    void addChange(const std::string& name, const std::string& type, 
                   DiffChangeType changeType, const std::string& details, SimulationImpact impact) {
        changes.push_back({name, type, changeType, details, static_cast<uint32_t>(impact)});
        isSemanticallyIdentical = false;
    }
    
    void sort() {
        std::sort(changes.begin(), changes.end());
    }
};

class SemanticNetlistDiffer {
public:
    static DiffReport diff(TensorBlock& oldBlock, TensorBlock& newBlock) {
        DiffReport report;
        
        // 1. Topology Hash Check (Fast Path)
        oldBlock.computeTopologyHash(); // ensure fresh
        newBlock.computeTopologyHash();
        
        if (oldBlock.topologyHash != newBlock.topologyHash) {
            report.isTopologyChanged = true;
        }

        // 2. Diff Passives (Resistor, Capacitor, Inductor)
        diffComponents<Resistor>(oldBlock.resistors, newBlock.resistors, "Resistor", report,
            [](const Resistor& a, const Resistor& b) {
                if (std::abs(a.resistance_ohms - b.resistance_ohms) > 1e-9)
                    return "R: " + std::to_string(a.resistance_ohms) + " -> " + std::to_string(b.resistance_ohms);
                return std::string("");
            },
            [](const Resistor& r) -> std::pair<int, int> { return {r.nodeTerminal1, r.nodeTerminal2}; }
        );

        diffComponents<Capacitor>(oldBlock.capacitors, newBlock.capacitors, "Capacitor", report,
            [](const Capacitor& a, const Capacitor& b) {
                if (std::abs(a.capacitance_farads - b.capacitance_farads) > 1e-15)
                    return "C: " + std::to_string(a.capacitance_farads) + " -> " + std::to_string(b.capacitance_farads);
                return std::string("");
            },
             [](const Capacitor& c) -> std::pair<int, int> { return {c.nodePlate1, c.nodePlate2}; }
        );

        // ... Add Inductor, Diode, BJT, Mosfet similarly ...

        report.sort();
        return report;
    }

private:
    // Generic Diff Logic
    template<typename T, typename ParamCheckFunc, typename NodeExtractFunc>
    static void diffComponents(const std::vector<T>& oldList, 
                               const std::vector<T>& newList,
                               const std::string& typeName,
                               DiffReport& report,
                               ParamCheckFunc checkParams,
                               NodeExtractFunc getNodes) 
    {
        // Map old components by Name
        std::map<std::string, const T*> oldMap;
        for (const auto& item : oldList) oldMap[item.name] = &item;

        std::map<std::string, const T*> newMap;
        for (const auto& item : newList) newMap[item.name] = &item;

        // Check for REMOVED
        for (const auto& kv : oldMap) {
            if (newMap.find(kv.first) == newMap.end()) {
                report.addChange(kv.first, typeName, DiffChangeType::REMOVED, "Component deleted", 
                                 SimulationImpact::MATRIX_REBUILD | SimulationImpact::STATE_RESET);
            }
        }

        // Check for ADDED and MODIFIED
        for (const auto& kv : newMap) {
            const std::string& name = kv.first;
            const T* newItem = kv.second;

            if (oldMap.find(name) == oldMap.end()) {
                report.addChange(name, typeName, DiffChangeType::ADDED, "Component added", 
                                 SimulationImpact::MATRIX_REBUILD | SimulationImpact::STATE_RESET);
            } else {
                const T* oldItem = oldMap.at(name);
                
                // Check Connectivity
                auto nodesOld = getNodes(*oldItem);
                auto nodesNew = getNodes(*newItem);
                
                // Simple connectivity check (Assumes non-swapped terminals for now, 
                // ideally we use GraphCanonical mappings here if we had them computed)
                if (nodesOld.first != nodesNew.first || nodesOld.second != nodesNew.second) {
                   // Try swapped check for non-polaized? (Resistor)
                   // For now, strict check.
                   report.addChange(name, typeName, DiffChangeType::CONNECTIVITY_CHANGED, 
                                    "Nodes changed", SimulationImpact::MATRIX_REBUILD);
                } else {
                    // Check Parameters
                    std::string details = checkParams(*oldItem, *newItem);
                    if (!details.empty()) {
                        report.addChange(name, typeName, DiffChangeType::MODIFIED_PARAMETER, 
                                         details, SimulationImpact::JACOBIAN_UPDATE);
                    }
                }
            }
        }
    }
};
