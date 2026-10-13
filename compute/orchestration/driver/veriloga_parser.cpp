#include "veriloga_parser.h"

#include <sstream>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdlib>
#include <cctype>

namespace acutesim {
namespace compute {
namespace orchestration {

// ============================================================================
// Internal tokenizer helpers
// ============================================================================

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Split a string by delimiters (any char in delims), collapse empty tokens.
static std::vector<std::string> split(const std::string& s,
                                       const std::string& delims) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (delims.find(c) != std::string::npos) {
            cur = trim(cur);
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    cur = trim(cur);
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

// Extract the substring inside the first matching pair of (...)
static std::string extractParens(const std::string& s, size_t start = 0) {
    size_t open = s.find('(', start);
    if (open == std::string::npos) return "";
    size_t close = s.find(')', open);
    if (close == std::string::npos) return "";
    return s.substr(open + 1, close - open - 1);
}

// Parse "n<index>" or "gnd" → node index. Returns -1 on failure.
static int parseNetName(const std::string& name) {
    std::string nm = trim(name);
    if (nm == "gnd") return 0;
    if (!nm.empty() && nm[0] == 'n') {
        try { return std::stoi(nm.substr(1)); } catch (...) {}
    }
    return -1;
}

// Parse a double literal (handles scientific notation).
static double parseDouble(const std::string& s) {
    try { return std::stod(s); } catch (...) { return 0.0; }
}

// ============================================================================
// Main parser
// ============================================================================

VerilogAParseResult parseVerilogA(const std::string& source) {
    VerilogAParseResult result;
    TensorNetlist& netlist = result.netlist;

    // ── Pass 1: Split into lines and collect parameters ──────────────────────
    // Parameters are declared as:
    //   parameter real   <name>_val = <value>;
    //   parameter integer <name>_isNPN = <value>;
    // We parse ALL parameter lines before processing contribution statements.

    std::unordered_map<std::string, double> params;    // name → value

    std::istringstream stream(source);
    std::string line;
    int lineNo = 0;
    std::vector<std::pair<int,std::string>> lines; // (lineNo, trimmed)

    while (std::getline(stream, line)) {
        ++lineNo;
        std::string trimmed = trim(line);
        lines.push_back({lineNo, trimmed});

        // parameter real/integer <ident> = <value>;
        if (startsWith(trimmed, "parameter ")) {
            // Remove "parameter " prefix
            std::string rest = trimmed.substr(10); // "parameter " = 10 chars
            rest = trim(rest);
            // Remove type keyword (real/integer)
            if (startsWith(rest, "real "))    rest = trim(rest.substr(5));
            else if (startsWith(rest, "integer ")) rest = trim(rest.substr(8));

            // rest is now "<ident> = <value>;"
            size_t eq = rest.find('=');
            if (eq == std::string::npos) continue;
            std::string ident = trim(rest.substr(0, eq));
            std::string valStr = trim(rest.substr(eq + 1));
            // Strip trailing semicolon
            if (!valStr.empty() && valStr.back() == ';') valStr.pop_back();
            valStr = trim(valStr);
            params[ident] = parseDouble(valStr);
        }
    }

    // ── Helper: look up a parameter by name, default to 0.0 ──────────────────
    auto param = [&](const std::string& name, double def = 0.0) -> double {
        auto it = params.find(name);
        return (it != params.end()) ? it->second : def;
    };

    // ── Helper: waveform + AC fields of a V/I source (see sourceParams) ──────
    auto readSource = [&](auto& s, const std::string& nm) {
        if (params.count(nm + "_per")) {
            s.type      = "PULSE";
            s.pulse_v1  = param(nm + "_v1");
            s.pulse_v2  = param(nm + "_v2");
            s.pulse_td  = param(nm + "_td");
            s.pulse_tr  = param(nm + "_tr");
            s.pulse_tf  = param(nm + "_tf");
            s.pulse_pw  = param(nm + "_pw");
            s.pulse_per = param(nm + "_per");
        } else if (params.count(nm + "_freq")) {
            s.type       = "SINE";
            s.sine_vo    = param(nm + "_vo");
            s.sine_va    = param(nm + "_va");
            s.sine_freq  = param(nm + "_freq");
            s.sine_td    = param(nm + "_td");
            s.sine_theta = param(nm + "_theta");
            s.sine_phase = param(nm + "_phase");
        }
        s.ac_mag   = param(nm + "_acmag");
        s.ac_phase = param(nm + "_acphase");
    };

    // ── Determine max node index from parameter names ─────────────────────────
    // We'll update numGlobalNodes as we parse contribution statements.
    auto touchNode = [&](int n) {
        if (n > netlist.numGlobalNodes) netlist.numGlobalNodes = n;
    };

    // ── Pass 2: Parse contribution statements ────────────────────────────────
    // Each contribution line has one of:
    //   I(a,b) <+ expr;
    //   V(a,b) <+ expr;
    //   // .probe V(a,b) "name"
    //   // <ComponentName> (BJT/MOSFET block begin comments)

    // State for multi-line BJT/MOSFET blocks (tracked by comment annotation)
    enum class BlockType { NONE, BJT, MOSFET };
    struct ComponentAnnotation {
        std::string name;
        BlockType   type  = BlockType::NONE;
    };
    ComponentAnnotation pending;

    // We use a two-pass approach: parse // <Name> comments before each block,
    // then parse the I(<a>,<b>) <+ ... lines to identify component type.

    // Strategy: iterate lines, detect annotation comments, then match patterns.
    for (size_t li = 0; li < lines.size(); ++li) {
        const auto& [ln, trimmed] = lines[li];

        // ── Probe annotation ──────────────────────────────────────────────────
        // // .probe V(a,b) "name"
        if (startsWith(trimmed, "// .probe V(") ||
            startsWith(trimmed, "//  .probe V(")) {
            // Extract inner content after ".probe "
            size_t pos = trimmed.find(".probe ");
            if (pos != std::string::npos) {
                std::string rest = trimmed.substr(pos + 7); // after ".probe "
                std::string nodes = extractParens(rest);
                auto parts = split(nodes, ",");
                if (parts.size() >= 2) {
                    int np = parseNetName(parts[0]);
                    int nn = parseNetName(parts[1]);
                    // Extract name from quotes
                    std::string probeName;
                    size_t q1 = rest.find('"');
                    size_t q2 = (q1 != std::string::npos) ? rest.find('"', q1+1) : std::string::npos;
                    if (q1 != std::string::npos && q2 != std::string::npos)
                        probeName = rest.substr(q1+1, q2-q1-1);
                    if (np >= 0 && nn >= 0) {
                        Probe probe;
                        probe.name         = probeName;
                        probe.nodePositive = np;
                        probe.nodeNegative = nn;
                        netlist.globalBlock.probes.push_back(probe);
                        touchNode(np); touchNode(nn);
                    }
                }
            }
            continue;
        }

        // ── Component-type annotation comment ─────────────────────────────────
        // // R1   or  // C1  or  // L1  etc. (followed by the contribution)
        // // Q1 (BJT — Ebers-Moll, ...)
        // // M1 (MOSFET — Shichman-Hodges, ...)
        if (startsWith(trimmed, "// ") && trimmed.size() > 3) {
            std::string rest = trim(trimmed.substr(3));
            if (rest.find("(BJT") != std::string::npos) {
                pending.name = rest.substr(0, rest.find(' '));
                pending.type = BlockType::BJT;
                continue;
            }
            if (rest.find("(MOSFET") != std::string::npos) {
                pending.name = rest.substr(0, rest.find(' '));
                pending.type = BlockType::MOSFET;
                continue;
            }
            // Other component annotations — just note the name
            pending.name = rest.substr(0, rest.find(' '));
            if (pending.name.empty()) pending.name = rest;
            pending.type = BlockType::NONE;
            continue;
        }

        // ── BJT block parsing: begin : <name>_em  ────────────────────────────
        if (pending.type == BlockType::BJT && startsWith(trimmed, "begin : ")) {
            // We already know the component name from the annotation.
            // Parse the BJT by looking at the two I(<>) <+ contribution lines
            // inside this block. We'll accumulate them.
            std::string bjt_nm = pending.name;
            bool isNPN = (static_cast<int>(param(bjt_nm + "_isNPN", 1)) != 0);

            // Find the collector, base, emitter from the I(<c>,<e>) lines
            // We know from the generator:
            // NPN: I(nc,ne) <+ alphaF*IF - IR
            //      I(nb,ne) <+ (1-alphaF)*IF + (1-alphaR)*IR
            // PNP: I(ne,nc) <+ alphaF*IF - IR
            //      I(ne,nb) <+ ...
            // Scan forward to "end"
            int nc = -1, nb = -1, ne = -1;
            int depth = 1;  // started inside "begin : Q1_em"
            for (size_t bj = li + 1; bj < lines.size(); ++bj) {
                const auto& [bln, bl] = lines[bj];
                // Track nested begin/end depth
                if (bl.find("end")   != std::string::npos) depth--;
                if (bl.find("begin") != std::string::npos) depth++;
                if (depth == 0) { li = bj; break; }

                // I(a,b) <+ ...
                if (startsWith(bl, "I(")) {
                    std::string inner = extractParens(bl);
                    auto pts = split(inner, ",");
                    if (pts.size() >= 2) {
                        int na = parseNetName(pts[0]);
                        int nbb = parseNetName(pts[1]);
                        // This is either the Ic or Ib line
                        if (nc < 0) {
                            // First I(...) line: depends on isNPN
                            if (isNPN) { nc = na; ne = nbb; }
                            else       { ne = na; nc = nbb; }
                        } else {
                            // Second I(...) line
                            if (isNPN) { nb = na; }  // I(nb,ne)
                            else       { nb = nbb; } // I(ne,nb)
                        }
                    }
                }
            }

            if (nc >= 0 && nb >= 0 && ne >= 0) {
                BJT bjt;
                bjt.instanceName             = bjt_nm;
                bjt.isNPN                    = isNPN;
                bjt.nodeCollector            = nc;
                bjt.base                     = nb;
                bjt.emitter                  = ne;
                bjt.saturationCurrent_I_S_A  = param(bjt_nm + "_Is",  1e-14);
                bjt.thermalVoltage_V_T_V     = param(bjt_nm + "_Vt",  0.02585);
                bjt.betaF                    = param(bjt_nm + "_betaF", 100.0);
                bjt.betaR                    = param(bjt_nm + "_betaR", 1.0);
                netlist.globalBlock.bjts.push_back(bjt);
                touchNode(nc); touchNode(nb); touchNode(ne);
            }
            pending.type = BlockType::NONE;
            continue;
        }

        // ── MOSFET block parsing: begin : <name>_sh  ─────────────────────────
        if (pending.type == BlockType::MOSFET && startsWith(trimmed, "begin : ")) {
            std::string mos_nm = pending.name;
            bool isPMOS = (static_cast<int>(param(mos_nm + "_isPMOS", 0)) != 0);

            // Scan for I(nd,ns) or I(ns,nd) contribution line
            int nd = -1, ng2 = -1, ns = -1;
            int depth = 1;  // started inside "begin : M1_sh"
            for (size_t mj = li + 1; mj < lines.size(); ++mj) {
                const auto& [mln, ml] = lines[mj];
                // Track nested begin/end depth
                if (ml.find("end")   != std::string::npos) depth--;
                if (ml.find("begin") != std::string::npos) depth++;
                if (depth == 0) { li = mj; break; }

                // <nm>_Vgs = V(ng,ns) or V(ns,ng) for PMOS
                if (ml.find("_Vgs = V(") != std::string::npos) {
                    std::string inner = extractParens(ml, ml.find("_Vgs = V(") + 8);
                    auto pts = split(inner, ",");
                    if (pts.size() >= 2) {
                        if (!isPMOS) { ng2 = parseNetName(pts[0]); ns = parseNetName(pts[1]); }
                        else         { ns  = parseNetName(pts[0]); ng2 = parseNetName(pts[1]); }
                    }
                }
                // I(nd,ns) or I(ns,nd)
                if (startsWith(ml, "I(")) {
                    std::string inner = extractParens(ml);
                    auto pts = split(inner, ",");
                    if (pts.size() >= 2) {
                        if (!isPMOS) { nd = parseNetName(pts[0]); /* ns already known */ }
                        else         { /* ns,nd */  nd = parseNetName(pts[1]); }
                    }
                }
            }

            // body defaults to source for simple models
            if (nd >= 0 && ng2 >= 0 && ns >= 0) {
                Mosfet m;
                m.instanceName = mos_nm;
                m.drain  = nd;
                m.gate   = ng2;
                m.source = ns;
                m.body   = ns; // default body = source
                m.w      = param(mos_nm + "_W", 1e-6);
                m.l      = param(mos_nm + "_L", 1e-6);
                m.modelName = isPMOS ? "pfet_01v8" : "nfet_01v8";
                netlist.globalBlock.mosfets.push_back(m);
                touchNode(nd); touchNode(ng2); touchNode(ns);
            }
            pending.type = BlockType::NONE;
            continue;
        }

        // ── Contribution statement: I(a,b) <+ expr; ──────────────────────────
        if (startsWith(trimmed, "I(") && trimmed.find("<+") != std::string::npos) {
            std::string inner = extractParens(trimmed);
            auto pts = split(inner, ",");
            if (pts.size() < 2) continue;
            int na = parseNetName(pts[0]);
            int nb2 = parseNetName(pts[1]);
            if (na < 0 || nb2 < 0) continue;

            // Get the RHS expression
            size_t arrow = trimmed.find("<+");
            std::string rhs = trim(trimmed.substr(arrow + 2));
            if (!rhs.empty() && rhs.back() == ';') rhs.pop_back();
            rhs = trim(rhs);

            // Identify pattern from component annotation name
            std::string cn = pending.name;

            // Resistor: V(a,b) / <name>_val
            if (rhs.find("/ ") != std::string::npos &&
                rhs.find("ddt") == std::string::npos &&
                rhs.find("exp") == std::string::npos) {
                // e.g. V(n1,gnd) / R1_val
                // name before "_val"
                std::string valParam = cn + "_val";
                double R = param(valParam, 1000.0);
                Resistor r;
                r.name          = cn;
                r.nodeTerminal1 = na;
                r.nodeTerminal2 = nb2;
                r.resistance_ohms = R;
                netlist.globalBlock.resistors.push_back(r);
                touchNode(na); touchNode(nb2);
            }
            // Capacitor: <name>_val * ddt(V(a,b))
            else if (rhs.find("ddt(V(") != std::string::npos &&
                     rhs.find("exp") == std::string::npos) {
                double C = param(cn + "_val", 1e-12);
                Capacitor cap;
                cap.name              = cn;
                cap.nodePlate1        = na;
                cap.nodePlate2        = nb2;
                cap.capacitance_farads = C;
                netlist.globalBlock.capacitors.push_back(cap);
                touchNode(na); touchNode(nb2);
            }
            // Current Source: simple scalar (not ddt, not exp, no /)
            else if (rhs.find("ddt") == std::string::npos &&
                     rhs.find("exp") == std::string::npos &&
                     rhs.find("/")   == std::string::npos) {
                // e.g. I1_val
                double I = param(cn + "_val", 0.0);
                CurrentSource cs;
                cs.nodePositive = na;
                cs.nodeNegative = nb2;
                cs.current_A    = I;
                cs.name         = cn;
                readSource(cs, cn);
                netlist.globalBlock.currentSources.push_back(cs);
                touchNode(na); touchNode(nb2);
            }
            // Diode: Is*(exp(...) - 1)
            else if (rhs.find("exp") != std::string::npos &&
                     rhs.find("Is") != std::string::npos) {
                Diode d;
                d.instanceName           = cn;
                d.anode                  = na;
                d.cathode                = nb2;
                d.saturationCurrent_I_S_A= param(cn + "_Is",  1e-14);
                d.emissionCoefficient_N  = param(cn + "_N",   1.0);
                d.thermalVoltage_V_T_V   = param(cn + "_Vt",  0.02585);
                netlist.globalBlock.diodes.push_back(d);
                touchNode(na); touchNode(nb2);
            }

            pending.type = BlockType::NONE;
            continue;
        }

        // ── Contribution statement: V(a,b) <+ expr; ──────────────────────────
        if (startsWith(trimmed, "V(") && trimmed.find("<+") != std::string::npos) {
            std::string inner = extractParens(trimmed);
            auto pts = split(inner, ",");
            if (pts.size() < 2) continue;
            int na = parseNetName(pts[0]);
            int nb2 = parseNetName(pts[1]);
            if (na < 0 || nb2 < 0) continue;

            size_t arrow = trimmed.find("<+");
            std::string rhs = trim(trimmed.substr(arrow + 2));
            if (!rhs.empty() && rhs.back() == ';') rhs.pop_back();
            rhs = trim(rhs);

            std::string cn = pending.name;

            // Inductor: <name>_val * ddt(I(a,b))
            if (rhs.find("ddt(I(") != std::string::npos) {
                double L = param(cn + "_val", 1e-6);
                Inductor ind;
                ind.name             = cn;
                ind.nodeCoil1        = na;
                ind.nodeCoil2        = nb2;
                ind.inductance_henries = L;
                netlist.globalBlock.inductors.push_back(ind);
                touchNode(na); touchNode(nb2);
            }
            // Voltage Source: scalar
            else {
                double V = param(cn + "_val", 0.0);
                VoltageSource vs;
                vs.nodePositive = na;
                vs.nodeNegative = nb2;
                vs.voltage_V    = V;
                vs.name         = cn;
                readSource(vs, cn);
                netlist.globalBlock.voltageSources.push_back(vs);
                touchNode(na); touchNode(nb2);
            }

            pending.type = BlockType::NONE;
            continue;
        }
    }

    // ── Finalize ──────────────────────────────────────────────────────────────
    netlist.globalBlock.computeTopologyHash();

    // Initialize state arrays for transient solver compatibility
    netlist.globalState.capacitorState.resize(netlist.globalBlock.capacitors.size());
    netlist.globalState.inductorState.resize(netlist.globalBlock.inductors.size());
    netlist.globalState.powerRailState.resize(netlist.globalBlock.powerRails.size());

    result.success = true;
    return result;
}

} // namespace orchestration
} // namespace compute
} // namespace acutesim
