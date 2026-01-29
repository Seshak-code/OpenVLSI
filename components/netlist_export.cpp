#include "netlist_export.h"
#include <sstream>
#include <set>
#include <vector>
#include <algorithm>
#include <map>

namespace NetlistExporter {

    // Helper: Canonical Net Name
    static std::string getNetName(int nodeID, const NetlistExporter::ExportOptions& options) {
        if (nodeID == 0) return "n0"; // Standardizing to n0 for ground to match sync.cpp
        return "n" + std::to_string(nodeID);
    }

    struct PortInfo {
        std::string name;
        int nodeID;
        // Inferred direction: 0=Unknown/BiDir, 1=Input, 2=Output
        int direction = 0; 
    };

    std::string toVerilog(const TensorNetlist& netlist, const ExportOptions& options) {
        std::stringstream ss;
        const auto& block = netlist.globalBlock;

        // 1. Module Header
        ss << "// Auto-Generated from Schematic via NetlistExporter\n";
        ss << "module " << options.topModuleName << " ();\n\n";

        // 2. Wire Declarations
        std::set<int> allNodes;
        auto collect = [&](int n) { if (n >= 0) allNodes.insert(n); };

        for (const auto& r : block.resistors) { collect(r.nodeTerminal1); collect(r.nodeTerminal2); }
        for (const auto& c : block.capacitors) { collect(c.nodePlate1); collect(c.nodePlate2); }
        for (const auto& l : block.inductors) { collect(l.nodeCoil1); collect(l.nodeCoil2); }
        for (const auto& v : block.voltageSources) { collect(v.nodePositive); collect(v.nodeNegative); }
        for (const auto& i : block.currentSources) { collect(i.nodePositive); collect(i.nodeNegative); }
        for (const auto& d : block.diodes) { collect(d.anode); collect(d.cathode); }
        for (const auto& m : block.mosfets) { collect(m.drain); collect(m.gate); collect(m.source); collect(m.body); }
        for (const auto& b : block.bjts) { collect(b.nodeCollector); collect(b.base); collect(b.emitter); }

        ss << "  // Nets\n";
        for (int n : allNodes) {
             std::string name = getNetName(n, options);
             if (options.flavor == ExportFlavor::AnalogMixedSignal) {
                 ss << "  electrical " << name << ";\n";
             } else {
                 ss << "  wire " << name << ";\n";
             }
        }
        ss << "\n";

        // 3. Component Instantiations
        ss << "  // Component Instantiations\n";

        // Resistors
        for (size_t i = 0; i < block.resistors.size(); ++i) {
            const auto& comp = block.resistors[i];
            std::string name = comp.name.empty() ? ("R" + std::to_string(i)) : comp.name;
            ss << "  resistor #(.r(" << comp.resistance_ohms << ")) " << name 
               << " (.p(" << getNetName(comp.nodeTerminal1, options) << "), .n(" << getNetName(comp.nodeTerminal2, options) << "));\n";
        }

        // Capacitors
        for (size_t i = 0; i < block.capacitors.size(); ++i) {
            const auto& comp = block.capacitors[i];
            std::string name = comp.name.empty() ? ("C" + std::to_string(i)) : comp.name;
            ss << "  capacitor #(.c(" << comp.capacitance_farads << ")) " << name 
               << " (.p(" << getNetName(comp.nodePlate1, options) << "), .n(" << getNetName(comp.nodePlate2, options) << "));\n";
        }

        // Inductors
        for (size_t i = 0; i < block.inductors.size(); ++i) {
            const auto& comp = block.inductors[i];
            std::string name = comp.name.empty() ? ("L" + std::to_string(i)) : comp.name;
            ss << "  inductor #(.l(" << comp.inductance_henries << ")) " << name 
               << " (.p(" << getNetName(comp.nodeCoil1, options) << "), .n(" << getNetName(comp.nodeCoil2, options) << "));\n";
        }

        // Voltage Sources
        for (size_t i = 0; i < block.voltageSources.size(); ++i) {
            const auto& comp = block.voltageSources[i];
            std::string name = "V" + std::to_string(i); 
            ss << "  vsource #(.v(" << comp.voltage_V << ")) " << name 
               << " (.p(" << getNetName(comp.nodePositive, options) << "), .n(" << getNetName(comp.nodeNegative, options) << "));\n";
        }

        // Current Sources
        for (size_t i = 0; i < block.currentSources.size(); ++i) {
            const auto& comp = block.currentSources[i];
            std::string name = "I" + std::to_string(i);
            ss << "  isource #(.i(" << comp.current_A << ")) " << name 
               << " (.p(" << getNetName(comp.nodePositive, options) << "), .n(" << getNetName(comp.nodeNegative, options) << "));\n";
        }

        // Diodes
        for (size_t i = 0; i < block.diodes.size(); ++i) {
            const auto& comp = block.diodes[i];
            std::string name = "D" + std::to_string(i);
            ss << "  diode #(.is(" << comp.saturationCurrent_I_S_A << "), .n(" << comp.emissionCoefficient_N << ")) " << name 
               << " (.a(" << getNetName(comp.anode, options) << "), .c(" << getNetName(comp.cathode, options) << "));\n";
        }

        // MOSFETs
        for (size_t i = 0; i < block.mosfets.size(); ++i) {
            const auto& comp = block.mosfets[i];
            std::string name = comp.instanceName.empty() ? ("M" + std::to_string(i)) : comp.instanceName;
            std::string type = comp.modelName.find("PMOS") != std::string::npos ? "pmos" : "nmos";
            
            ss << "  " << type << " #(.w(" << comp.w << "), .l(" << comp.l << ")) " << name 
               << " (.d(" << getNetName(comp.drain, options) << "), .g(" << getNetName(comp.gate, options) << "), "
               << ".s(" << getNetName(comp.source, options) << "), .b(" << getNetName(comp.body, options) << "));\n";
        }

        // BJTs
        for (size_t i = 0; i < block.bjts.size(); ++i) {
            const auto& comp = block.bjts[i];
            std::string name = comp.instanceName.empty() ? ("Q" + std::to_string(i)) : comp.instanceName;
            std::string type = comp.isNPN ? "npn" : "pnp";
            
            ss << "  " << type << " #(.bf(" << comp.betaF << "), .is(" << comp.saturationCurrent_I_S_A << ")) " << name 
               << " (.c(" << getNetName(comp.nodeCollector, options) << "), .b(" << getNetName(comp.base, options) << "), "
               << ".e(" << getNetName(comp.emitter, options) << "));\n";
        }

        // Hierarchical Instances
        for (size_t i = 0; i < netlist.instances.size(); ++i) {
            const auto& inst = netlist.instances[i];
            std::string name = "U" + std::to_string(i);
            std::string type = inst.blockName;
            
            ss << "  " << type << " " << name << " (";
            for (size_t j = 0; j < inst.nodeMapping.size(); ++j) {
                ss << ".p" << (j+1) << "(" << getNetName(inst.nodeMapping[j], options) << ")";
                if (j < inst.nodeMapping.size() - 1) ss << ", ";
            }
            ss << ");\n";
        }

        ss << "\nendmodule\n";

        return ss.str();
    }

    std::string toSPEF(const TensorNetlist& netlist, const ExportOptions& options) {
        std::stringstream ss;
        const auto& block = netlist.globalBlock;

        // Header
        ss << "*SPEF \"IEEE 1481-1998\"\n";
        ss << "*DESIGN \"" << options.topModuleName << "\"\n";
        ss << "*DATE \"Tue Jan 28 14:00:00 2026\"\n";
        ss << "*VENDOR \"AcuteSim\"\n";
        ss << "*PROGRAM \"NetlistExporter\"\n";
        ss << "*VERSION \"0.1\"\n";
        ss << "*DESIGN_FLOW \"ANALOG_MIXED_SIGNAL\"\n";
        ss << "*DIVIDER /\n";
        ss << "*DELIMITER :\n";
        ss << "*BUS_BIT []\n";
        ss << "*T_UNIT 1.00000 NS\n";
        ss << "*C_UNIT 1.00000 FF\n";
        ss << "*R_UNIT 1.00000 OHM\n";
        ss << "*L_UNIT 1.00000 HENRY\n\n";

        // Ports Section (Match Verilog)
        ss << "*PORTS\n";
        ss << "in I\n";
        ss << "out O\n";
        ss << options.powerNetNames.at("VDD") << " B\n"; // Bidirectional Power
        ss << options.powerNetNames.at("VSS") << " B\n";
        ss << "*END\n\n";

        // Parasitics (D_NET)
        for(const auto& seg : block.wireSegments) {
            int netID = seg.node1; 
             ss << "*D_NET *" << getNetName(netID, options) << " 1.5\n"; 
             ss << "*CONN\n";
             ss << "*CAP\n";
             ss << "1 *" << getNetName(netID, options) << ":1 " << (seg.c_per_um * seg.length_um) << "\n";
             ss << "*RES\n";
             ss << "1 *" << getNetName(seg.node1, options) << ":1 *" << getNetName(seg.node2, options) << ":1 " 
                << (seg.r_per_um * seg.length_um) << "\n";
             ss << "*END\n\n";
        }
        
        return ss.str();
    }

} // namespace NetlistExporter
