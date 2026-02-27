#include "veriloga_generator.h"
#include <sstream>
#include <set>
#include <cstdio>  // snprintf

namespace VerilogAGenerator {

// ── Helpers ─────────────────────────────────────────────────────────────────

// Format a double with full precision for FP determinism.
static std::string fp(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

// Map a solver node index to a Verilog-A net name.
// Node 0 is ground; all others are n<index>.
static std::string netName(int nodeIdx) {
    if (nodeIdx == 0) return "gnd";
    return "n" + std::to_string(nodeIdx);
}

// ── Port/Net collection ──────────────────────────────────────────────────────

static void collectNodes(const TensorBlock& blk, std::set<int>& nodes) {
    auto add = [&](int n){ if (n > 0) nodes.insert(n); };

    for (const auto& c : blk.resistors)    { add(c.nodeTerminal1); add(c.nodeTerminal2); }
    for (const auto& c : blk.capacitors)   { add(c.nodePlate1);    add(c.nodePlate2);    }
    for (const auto& c : blk.inductors)    { add(c.nodeCoil1);     add(c.nodeCoil2);     }
    for (const auto& c : blk.voltageSources){ add(c.nodePositive);  add(c.nodeNegative); }
    for (const auto& c : blk.currentSources){ add(c.nodePositive);  add(c.nodeNegative); }
    for (const auto& c : blk.diodes)       { add(c.anode);         add(c.cathode);       }
    for (const auto& c : blk.bjts)         { add(c.nodeCollector);  add(c.base); add(c.emitter); }
    for (const auto& c : blk.mosfets)      { add(c.drain); add(c.gate); add(c.source); add(c.body); }
    for (const auto& c : blk.probes)       { add(c.nodePositive);   add(c.nodeNegative); }
}

// ── Main Generator ───────────────────────────────────────────────────────────

// ── Flattening Logic (DTO -> TensorNetlist) ──────────────────────────────────

static void flattenDto(const acutesim::compute::orchestration::HierarchicalSchematicDTO& hier,
                       TensorNetlist& outNetlist) {
    // Current VerilogAGenerator only handles a single flattened module.
    // We map DTO instances to TensorNetlist primitives or call them recursively.
    
    auto& top = hier.topGraph;
    outNetlist.numGlobalNodes = top.nets.size(); // Simplified: assume nets map to indices
    
    auto getVal = [](const std::map<std::string, std::string>& params, const std::string& key, double def = 0.0) {
        auto it = params.find(key);
        if (it == params.end()) return def;
        try { return std::stod(it->second); } catch(...) { return def; }
    };
    
    auto getNetIdx = [&](const std::string& name) -> int {
        if (name == "0" || name == "GND" || name == "gnd") return 0;
        // Simple linear search or map for net names -> indices
        for (size_t i = 0; i < top.nets.size(); ++i) {
            if (top.nets[i].name == name) return (int)i + 1;
        }
        return 0; // fallback to ground
    };

    for (const auto& inst : top.instances) {
        if (inst.type == "Resistor") {
            Resistor r;
            r.name = inst.instanceName;
            r.nodeTerminal1 = inst.connectedNets.size() > 0 ? getNetIdx(inst.connectedNets[0]) : 0;
            r.nodeTerminal2 = inst.connectedNets.size() > 1 ? getNetIdx(inst.connectedNets[1]) : 0;
            r.resistance_ohms = getVal(inst.parameters, "R", getVal(inst.parameters, "val", 1000.0));
            outNetlist.globalBlock.resistors.push_back(r);
        } else if (inst.type == "Capacitor") {
            Capacitor c;
            c.name = inst.instanceName;
            c.nodePlate1 = inst.connectedNets.size() > 0 ? getNetIdx(inst.connectedNets[0]) : 0;
            c.nodePlate2 = inst.connectedNets.size() > 1 ? getNetIdx(inst.connectedNets[1]) : 0;
            c.capacitance_farads = getVal(inst.parameters, "C", getVal(inst.parameters, "val", 1e-12));
            outNetlist.globalBlock.capacitors.push_back(c);
        } else if (inst.type == "Inductor") {
            Inductor l;
            l.name = inst.instanceName;
            l.nodeCoil1 = inst.connectedNets.size() > 0 ? getNetIdx(inst.connectedNets[0]) : 0;
            l.nodeCoil2 = inst.connectedNets.size() > 1 ? getNetIdx(inst.connectedNets[1]) : 0;
            l.inductance_henries = getVal(inst.parameters, "L", getVal(inst.parameters, "val", 1e-9));
            outNetlist.globalBlock.inductors.push_back(l);
        } else if (inst.type == "VoltageSource") {
            VoltageSource v;
            v.nodePositive = inst.connectedNets.size() > 0 ? getNetIdx(inst.connectedNets[0]) : 0;
            v.nodeNegative = inst.connectedNets.size() > 1 ? getNetIdx(inst.connectedNets[1]) : 0;
            v.expression = inst.instanceName;
            v.voltage_V = getVal(inst.parameters, "V", getVal(inst.parameters, "val", 1.0));
            // Handle waveforms if present
            auto itType = inst.parameters.find("type");
            if (itType != inst.parameters.end()) v.type = itType->second;
            if (v.type == "PULSE") {
                v.pulse_v1 = getVal(inst.parameters, "v1", 0.0);
                v.pulse_v2 = getVal(inst.parameters, "v2", 1.8);
                v.pulse_td = getVal(inst.parameters, "td", 0.0);
                v.pulse_pw = getVal(inst.parameters, "pw", 10e-9);
                v.pulse_per = getVal(inst.parameters, "per", 20e-9);
            }
            outNetlist.globalBlock.voltageSources.push_back(v);
        } else if (inst.type == "Probe") {
            Probe p;
            p.name = inst.instanceName;
            p.nodePositive = inst.connectedNets.size() > 0 ? getNetIdx(inst.connectedNets[0]) : 0;
            p.nodeNegative = 0;
            outNetlist.globalBlock.probes.push_back(p);
        }
        // TODO: Add more primitive types as needed
    }
}

std::string generate(const acutesim::compute::orchestration::HierarchicalSchematicDTO& hierDto, const GeneratorOptions& opts) {
    TensorNetlist tempNl;
    flattenDto(hierDto, tempNl);
    return generate(tempNl, opts);
}

std::string generate(const TensorNetlist& netlist, const GeneratorOptions& opts) {
    std::ostringstream ss;
    TensorBlock blk = netlist.globalBlock; // Make a mutable copy to sort in-place

    // ── Enforce Determinism via Sorting ─────────────────────────────────────
    
    std::sort(blk.resistors.begin(), blk.resistors.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        if (a.nodeTerminal1 != b.nodeTerminal1) return a.nodeTerminal1 < b.nodeTerminal1;
        return a.nodeTerminal2 < b.nodeTerminal2;
    });

    std::sort(blk.capacitors.begin(), blk.capacitors.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        if (a.nodePlate1 != b.nodePlate1) return a.nodePlate1 < b.nodePlate1;
        return a.nodePlate2 < b.nodePlate2;
    });

    std::sort(blk.inductors.begin(), blk.inductors.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        if (a.nodeCoil1 != b.nodeCoil1) return a.nodeCoil1 < b.nodeCoil1;
        return a.nodeCoil2 < b.nodeCoil2;
    });

    std::sort(blk.voltageSources.begin(), blk.voltageSources.end(), [](const auto& a, const auto& b) {
        if (a.expression != b.expression) return a.expression < b.expression;
        if (a.nodePositive != b.nodePositive) return a.nodePositive < b.nodePositive;
        return a.nodeNegative < b.nodeNegative;
    });

    std::sort(blk.currentSources.begin(), blk.currentSources.end(), [](const auto& a, const auto& b) {
        if (a.expression != b.expression) return a.expression < b.expression;
        if (a.nodePositive != b.nodePositive) return a.nodePositive < b.nodePositive;
        return a.nodeNegative < b.nodeNegative;
    });

    std::sort(blk.diodes.begin(), blk.diodes.end(), [](const auto& a, const auto& b) {
        if (a.instanceName != b.instanceName) return a.instanceName < b.instanceName;
        if (a.anode != b.anode) return a.anode < b.anode;
        return a.cathode < b.cathode;
    });

    std::sort(blk.bjts.begin(), blk.bjts.end(), [](const auto& a, const auto& b) {
        if (a.instanceName != b.instanceName) return a.instanceName < b.instanceName;
        if (a.nodeCollector != b.nodeCollector) return a.nodeCollector < b.nodeCollector;
        return a.base < b.base;
    });

    std::sort(blk.mosfets.begin(), blk.mosfets.end(), [](const auto& a, const auto& b) {
        if (a.instanceName != b.instanceName) return a.instanceName < b.instanceName;
        if (a.drain != b.drain) return a.drain < b.drain;
        return a.gate < b.gate;
    });

    // Collect all non-ground nodes
    std::set<int> nonGroundNodes;
    collectNodes(blk, nonGroundNodes);

    // ── File header ─────────────────────────────────────────────────────────
    ss << "// Verilog-A — generated by VerilogAGenerator\n";
    ss << "// Module: " << opts.topModuleName << "\n";
    ss << "// Component types: R=" << blk.resistors.size()
       << " C=" << blk.capacitors.size()
       << " L=" << blk.inductors.size()
       << " V=" << blk.voltageSources.size()
       << " I=" << blk.currentSources.size()
       << " D=" << blk.diodes.size()
       << " Q=" << blk.bjts.size()
       << " M=" << blk.mosfets.size()
       << " Probes=" << blk.probes.size() << "\n\n";

    ss << "`include \"disciplines.vams\"\n\n";

    // ── Module declaration ───────────────────────────────────────────────────
    // Ports: all non-ground nets (ground is implicit via `ground gnd`)
    ss << "module " << opts.topModuleName << "(";
    bool first = true;
    for (int n : nonGroundNodes) {
        if (!first) ss << ", ";
        ss << netName(n);
        first = false;
    }
    ss << ");\n";

    // Electrical discipline declarations for ports
    for (int n : nonGroundNodes) {
        ss << "    electrical " << netName(n) << ";\n";
    }

    // Ground declaration
    ss << "    ground gnd;\n\n";

    // ── Parameter declarations ───────────────────────────────────────────────
    // Each component value gets a named parameter so the parser can
    // reconstruct it by matching the prefix before "_val".

    // Resistors
    for (const auto& r : blk.resistors) {
        std::string nm = r.name.empty() ? "R_anon" : r.name;
        ss << "    parameter real " << nm << "_val = " << fp(r.resistance_ohms) << ";\n";
    }
    // Capacitors
    for (const auto& c : blk.capacitors) {
        std::string nm = c.name.empty() ? "C_anon" : c.name;
        ss << "    parameter real " << nm << "_val = " << fp(c.capacitance_farads) << ";\n";
    }
    // Inductors
    for (const auto& l : blk.inductors) {
        std::string nm = l.name.empty() ? "L_anon" : l.name;
        ss << "    parameter real " << nm << "_val = " << fp(l.inductance_henries) << ";\n";
    }
    // Voltage Sources — DC value + waveform parameters
    {
        int idx = 0;
        for (const auto& v : blk.voltageSources) {
            std::string nm = v.expression.empty()
                ? ("V" + std::to_string(idx + 1))
                : v.expression;
            if (nm.empty()) nm = "V" + std::to_string(idx + 1);
            ss << "    parameter real " << nm << "_val = " << fp(v.voltage_V) << ";\n";
            if (v.type == "PULSE") {
                ss << "    parameter real " << nm << "_pw  = " << fp(v.pulse_pw)  << ";\n";
                ss << "    parameter real " << nm << "_per = " << fp(v.pulse_per) << ";\n";
                ss << "    parameter real " << nm << "_td  = " << fp(v.pulse_td)  << ";\n";
                ss << "    parameter real " << nm << "_v1  = " << fp(v.pulse_v1)  << ";\n";
                ss << "    parameter real " << nm << "_v2  = " << fp(v.pulse_v2)  << ";\n";
            } else if (v.type == "SINE") {
                ss << "    parameter real " << nm << "_va   = " << fp(v.sine_va)   << ";\n";
                ss << "    parameter real " << nm << "_freq = " << fp(v.sine_freq) << ";\n";
            }
            ++idx;
        }
    }
    // Current Sources
    {
        int idx = 0;
        for (const auto& i : blk.currentSources) {
            std::string nm = "I" + std::to_string(idx + 1);
            ss << "    parameter real " << nm << "_val = " << fp(i.current_A) << ";\n";
            ++idx;
        }
    }
    // Diodes
    for (const auto& d : blk.diodes) {
        std::string nm = d.instanceName.empty() ? "D_anon" : d.instanceName;
        ss << "    parameter real " << nm << "_Is = " << fp(d.saturationCurrent_I_S_A) << ";\n";
        ss << "    parameter real " << nm << "_N  = " << fp(d.emissionCoefficient_N)   << ";\n";
        ss << "    parameter real " << nm << "_Vt = " << fp(d.thermalVoltage_V_T_V)    << ";\n";
    }
    // BJTs
    for (const auto& q : blk.bjts) {
        std::string nm = q.instanceName.empty() ? "Q_anon" : q.instanceName;
        ss << "    parameter real " << nm << "_Is   = " << fp(q.saturationCurrent_I_S_A) << ";\n";
        ss << "    parameter real " << nm << "_Vt   = " << fp(q.thermalVoltage_V_T_V)    << ";\n";
        ss << "    parameter real " << nm << "_betaF = " << fp(q.betaF)                  << ";\n";
        ss << "    parameter real " << nm << "_betaR = " << fp(q.betaR)                  << ";\n";
        ss << "    parameter integer " << nm << "_isNPN = " << (q.isNPN ? 1 : 0) << ";\n";
    }
    // MOSFETs
    for (const auto& m : blk.mosfets) {
        std::string nm = m.instanceName.empty() ? "M_anon" : m.instanceName;
        // Determine PMOS flag from model name
        bool isPMOS = (m.modelName.find("pfet") != std::string::npos ||
                       m.modelName.find("PMOS") != std::string::npos ||
                       m.modelName.find("pmos") != std::string::npos);
        ss << "    parameter real    " << nm << "_W    = " << fp(m.w)  << ";\n";
        ss << "    parameter real    " << nm << "_L    = " << fp(m.l)  << ";\n";
        ss << "    parameter integer " << nm << "_isPMOS = " << (isPMOS ? 1 : 0) << ";\n";
    }

    ss << "\n";

    // ── Analog block ─────────────────────────────────────────────────────────
    ss << "    analog begin\n\n";

    // --- Resistors ---
    for (const auto& r : blk.resistors) {
        std::string nm = r.name.empty() ? "R_anon" : r.name;
        ss << "        // " << nm << "\n";
        ss << "        I(" << netName(r.nodeTerminal1) << "," << netName(r.nodeTerminal2) << ")"
           << " <+ V(" << netName(r.nodeTerminal1) << "," << netName(r.nodeTerminal2) << ")"
           << " / " << nm << "_val;\n\n";
    }

    // --- Capacitors ---
    for (const auto& c : blk.capacitors) {
        std::string nm = c.name.empty() ? "C_anon" : c.name;
        ss << "        // " << nm << "\n";
        ss << "        I(" << netName(c.nodePlate1) << "," << netName(c.nodePlate2) << ")"
           << " <+ " << nm << "_val"
           << " * ddt(V(" << netName(c.nodePlate1) << "," << netName(c.nodePlate2) << "));\n\n";
    }

    // --- Inductors ---
    for (const auto& l : blk.inductors) {
        std::string nm = l.name.empty() ? "L_anon" : l.name;
        ss << "        // " << nm << "\n";
        ss << "        V(" << netName(l.nodeCoil1) << "," << netName(l.nodeCoil2) << ")"
           << " <+ " << nm << "_val"
           << " * ddt(I(" << netName(l.nodeCoil1) << "," << netName(l.nodeCoil2) << "));\n\n";
    }

    // --- Voltage Sources ---
    {
        int idx = 0;
        for (const auto& v : blk.voltageSources) {
            std::string nm = "V" + std::to_string(idx + 1);
            ss << "        // " << nm << "\n";
            ss << "        V(" << netName(v.nodePositive) << "," << netName(v.nodeNegative) << ")"
               << " <+ " << nm << "_val;\n\n";
            ++idx;
        }
    }

    // --- Current Sources ---
    {
        int idx = 0;
        for (const auto& i : blk.currentSources) {
            std::string nm = "I" + std::to_string(idx + 1);
            ss << "        // " << nm << "\n";
            ss << "        I(" << netName(i.nodePositive) << "," << netName(i.nodeNegative) << ")"
               << " <+ " << nm << "_val;\n\n";
            ++idx;
        }
    }

    // --- Diodes (Shockley: I = Is*(exp(V/(N*Vt)) - 1)) ---
    for (const auto& d : blk.diodes) {
        std::string nm = d.instanceName.empty() ? "D_anon" : d.instanceName;
        ss << "        // " << nm << " (Diode — Shockley)\n";
        ss << "        I(" << netName(d.anode) << "," << netName(d.cathode) << ")"
           << " <+ " << nm << "_Is"
           << " * (exp(V(" << netName(d.anode) << "," << netName(d.cathode) << ")"
           << " / (" << nm << "_N * " << nm << "_Vt)) - 1.0);\n\n";
    }

    // --- BJTs (Ebers-Moll) ---
    for (const auto& q : blk.bjts) {
        std::string nm = q.instanceName.empty() ? "Q_anon" : q.instanceName;
        std::string nc = netName(q.nodeCollector);
        std::string nb = netName(q.base);
        std::string ne = netName(q.emitter);
        // Ebers-Moll forward/reverse currents
        // IF = Is * (exp(Vbe / Vt) - 1)
        // IR = Is * (exp(Vbc / Vt) - 1)
        // Ic = alphaF * IF - IR
        // Ib = (1-alphaF)*IF + (1-alphaR)*IR
        ss << "        // " << nm << " (BJT — Ebers-Moll, isNPN=" << (q.isNPN ? 1 : 0) << ")\n";
        ss << "        begin : " << nm << "_em\n";
        ss << "            real " << nm << "_Vbe, " << nm << "_Vbc;\n";
        ss << "            real " << nm << "_IF,  " << nm << "_IR;\n";
        ss << "            real " << nm << "_alphaF, " << nm << "_alphaR;\n";
        if (q.isNPN) {
            ss << "            " << nm << "_Vbe = V(" << nb << "," << ne << ");\n";
            ss << "            " << nm << "_Vbc = V(" << nb << "," << nc << ");\n";
        } else {
            // PNP: flip Vbe and Vbc signs
            ss << "            " << nm << "_Vbe = V(" << ne << "," << nb << ");\n";
            ss << "            " << nm << "_Vbc = V(" << nc << "," << nb << ");\n";
        }
        ss << "            " << nm << "_alphaF = " << nm << "_betaF / (1.0 + " << nm << "_betaF);\n";
        ss << "            " << nm << "_alphaR = " << nm << "_betaR / (1.0 + " << nm << "_betaR);\n";
        ss << "            " << nm << "_IF = " << nm << "_Is * (exp(" << nm << "_Vbe / " << nm << "_Vt) - 1.0);\n";
        ss << "            " << nm << "_IR = " << nm << "_Is * (exp(" << nm << "_Vbc / " << nm << "_Vt) - 1.0);\n";
        if (q.isNPN) {
            ss << "            I(" << nc << "," << ne << ") <+ " << nm << "_alphaF * " << nm << "_IF - " << nm << "_IR;\n";
            ss << "            I(" << nb << "," << ne << ") <+ (1.0 - " << nm << "_alphaF) * " << nm << "_IF + (1.0 - " << nm << "_alphaR) * " << nm << "_IR;\n";
        } else {
            ss << "            I(" << ne << "," << nc << ") <+ " << nm << "_alphaF * " << nm << "_IF - " << nm << "_IR;\n";
            ss << "            I(" << ne << "," << nb << ") <+ (1.0 - " << nm << "_alphaF) * " << nm << "_IF + (1.0 - " << nm << "_alphaR) * " << nm << "_IR;\n";
        }
        ss << "        end\n\n";
    }

    // --- MOSFETs (Shichman-Hodges: level 1) ---
    // Using standard parameters: Kp = 200e-6 A/V^2, Vth = 0.7V for NMOS
    //                             Kp = 60e-6 A/V^2, Vth = -0.9V for PMOS
    for (const auto& m : blk.mosfets) {
        std::string nm = m.instanceName.empty() ? "M_anon" : m.instanceName;
        bool isPMOS = (m.modelName.find("pfet") != std::string::npos ||
                       m.modelName.find("PMOS") != std::string::npos ||
                       m.modelName.find("pmos") != std::string::npos);
        std::string nd = netName(m.drain);
        std::string ng = netName(m.gate);
        std::string ns = netName(m.source);
        ss << "        // " << nm << " (MOSFET — Shichman-Hodges, isPMOS=" << (isPMOS ? 1 : 0) << ")\n";
        ss << "        begin : " << nm << "_sh\n";
        ss << "            real " << nm << "_Vgs, " << nm << "_Vds, " << nm << "_Vgst;\n";
        ss << "            real " << nm << "_Kp, " << nm << "_Vth;\n";
        ss << "            real " << nm << "_Id;\n";
        if (!isPMOS) {
            // NMOS
            ss << "            " << nm << "_Kp  = 200e-6;\n";
            ss << "            " << nm << "_Vth = 0.7;\n";
            ss << "            " << nm << "_Vgs = V(" << ng << "," << ns << ");\n";
            ss << "            " << nm << "_Vds = V(" << nd << "," << ns << ");\n";
        } else {
            // PMOS — use magnitudes (Vgs, Vds are negative for on-state)
            ss << "            " << nm << "_Kp  = 60e-6;\n";
            ss << "            " << nm << "_Vth = -0.9;\n";
            ss << "            " << nm << "_Vgs = V(" << ns << "," << ng << ");\n";
            ss << "            " << nm << "_Vds = V(" << ns << "," << nd << ");\n";
        }
        ss << "            " << nm << "_Vgst = " << nm << "_Vgs - " << nm << "_Vth;\n";
        ss << "            if (" << nm << "_Vgst <= 0.0) begin\n";
        ss << "                " << nm << "_Id = 0.0;\n";
        ss << "            end else if (" << nm << "_Vds < " << nm << "_Vgst) begin\n";
        ss << "                // Linear\n";
        ss << "                " << nm << "_Id = " << nm << "_Kp * (" << nm << "_W / " << nm << "_L)"
           << " * ((" << nm << "_Vgst * " << nm << "_Vds)"
           << " - (0.5 * " << nm << "_Vds * " << nm << "_Vds));\n";
        ss << "            end else begin\n";
        ss << "                // Saturation\n";
        ss << "                " << nm << "_Id = 0.5 * " << nm << "_Kp * (" << nm << "_W / " << nm << "_L)"
           << " * " << nm << "_Vgst * " << nm << "_Vgst;\n";
        ss << "            end\n";
        if (!isPMOS) {
            ss << "            I(" << nd << "," << ns << ") <+ " << nm << "_Id;\n";
        } else {
            ss << "            I(" << ns << "," << nd << ") <+ " << nm << "_Id;\n";
        }
        ss << "        end\n\n";
    }

    // --- Probes (comment annotations, no physics) ---
    if (opts.includeProbes) {
        for (const auto& p : blk.probes) {
            ss << "        // .probe V(" << netName(p.nodePositive) << ","
               << netName(p.nodeNegative) << ") \"" << p.name << "\"\n";
        }
        if (!blk.probes.empty()) ss << "\n";
    }

    ss << "    end // analog\n\n";
    ss << "endmodule\n";

    return ss.str();
}

} // namespace VerilogAGenerator
