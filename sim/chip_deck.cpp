#include "sim/chip_deck.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace sim {

std::string spiceNode(const std::string& net) {
    std::string out;
    for (char c : net) {
        if (c == '\\') continue;
        out += std::isalnum((unsigned char)c) || c == '_' || c == '.' ? c : '_';
    }
    return out;
}

namespace {

std::vector<std::string> words(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// A name without the backslashes SPEF and CDL put before characters their grammars use
std::string plainName(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\\'), s.end());
    return s;
}

// Lines with their + continuations joined
std::vector<std::string> logicalLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line[0] == '+' && !out.empty()) out.back() += " " + line.substr(1);
        else out.push_back(line);
    }
    return out;
}

const flow::VcdSignal* signalNamed(const flow::Vcd& v, const std::string& name) {
    for (const auto& s : v.vars)
        if (s.name == name) return &s;
    return nullptr;
}

const std::vector<flow::VcdChange>& changesOf(const flow::Vcd& v, const flow::VcdSignal& s) {
    static const std::vector<flow::VcdChange> none;
    const auto it = v.changes.find(s.code);
    return it == v.changes.end() ? none : it->second;
}

// One bit of a value, MSB first and padded as VCD allows: its bit, or x
char bitOf(const std::string& value, int width, int bit) {
    if (bit < 0) return value.empty() ? 'x' : value.back();
    const int at = (int)value.size() - 1 - bit;
    if (at >= 0) return value[(size_t)at];
    const char pad = value.empty() ? 'x' : (value[0] == '1' ? '0' : value[0]);  // VCD extends 1 with 0, and x and z with themselves
    (void)width;
    return pad;
}

}  // namespace

ChipDeck buildChipDeck(const ChipDeckConfig& cfg, const flow::Vcd& recording, const std::string& clockPort) {
    ChipDeck deck;
    deck.clockPort = clockPort;
    deck.stopSeconds = cfg.stopSeconds > 0 ? std::min(cfg.stopSeconds, (double)recording.endTime * recording.secondsPerUnit) : (double)recording.endTime * recording.secondsPerUnit;
    if (!(deck.stopSeconds > 0)) throw std::invalid_argument("the recording holds no time to replay");

    // The ports' directions, from the routed netlist
    std::map<std::string, std::pair<bool, int>> direction;  // name -> output, width
    static const std::regex decl(R"(^\s*(input|output)\s*(?:\[\s*(\d+)\s*:\s*(\d+)\s*\])?\s*([A-Za-z_][\w$]*)\s*;)");
    {
        std::istringstream in(cfg.verilog);
        for (std::string line; std::getline(in, line);) {
            std::smatch m;
            if (std::regex_search(line, m, decl)) direction[m[4]] = {m[1] == "output", m[2].matched ? std::stoi(m[2]) - std::stoi(m[3]) + 1 : 1};
        }
    }
    if (direction.empty()) throw std::invalid_argument("the routed netlist declares no ports");

    // The netlist's cells and their pins, and which instance pins the top joins to each of its nets
    const std::vector<std::string> lines = logicalLines(cfg.cdl);
    std::map<std::string, std::vector<std::string>> cellPins;
    for (const std::string& raw : lines) {
        const auto w = words(raw);
        if (w.size() >= 2 && lower(w[0]) == ".subckt") cellPins[w[1]] = std::vector<std::string>(w.begin() + 2, w.end());
    }
    std::map<std::string, std::set<std::string>> attached;  // a top net's node -> what is on it, as instance:pin
    {
        bool top = false;
        for (const std::string& raw : lines) {
            const auto w = words(raw);
            if (w.empty()) continue;
            const std::string head = lower(w[0]);
            if (head == ".subckt") top = w.size() > 1 && w[1] == cfg.top;
            else if (head == ".ends") top = false;
            else if (top && head[0] == 'x') {
                const auto pins = cellPins.find(w.back());
                for (size_t k = 1; k + 1 < w.size(); ++k)
                    attached[spiceNode(w[k])].insert(pins != cellPins.end() && pins->second.size() == w.size() - 2 ? plainName(w[0].substr(1)) + ":" + pins->second[k - 1] : "?");
            } else if (top && head[0] != '.' && head[0] != '*') {
                for (size_t k = 1; k < w.size(); ++k) attached[spiceNode(w[k])].insert("?");  // a device in the top itself: the SPEF names no pin for it
            }
        }
    }

    // Parasitics, from the SPEF. A net's own RC network where it can stand in for the net: its pins are the netlist's, and its resistors
    // join every node of it. Any other net keeps its total capacitance at one node, to ground.
    // ponytail: coupling capacitance is grounded at its own net (no crosstalk); a resistor of 0 ohm becomes 1 milliohm.
    std::ostringstream parasitics;
    std::map<std::string, std::string> pinNode;  // instance:pin -> its own node, on a net with its RC network
    if (!cfg.spef.empty()) {
        struct Net {
            std::string node;
            double total = 0;
            bool port = false;
            std::map<std::string, std::string> pins;  // instance:pin -> node
            std::vector<std::tuple<std::string, std::string, double>> caps;  // a node (and for coupling the other net's, in either order), farads
            std::vector<std::tuple<std::string, std::string, double>> res;
        };
        std::map<std::string, std::string> names;
        std::vector<Net> nets;
        std::istringstream in(cfg.spef);
        std::string line;
        bool inMap = false;
        char section = 0;
        double cScale = 1e-12, rScale = 1;
        const auto mappedName = [&](const std::string& token) {
            const auto mapped = names.find(token);
            return mapped == names.end() ? token : mapped->second;
        };
        // A node of a net: a port by its name, instance:pin, or net:number
        const auto nodeOf = [&](const std::string& token) {
            const auto colon = token.find(':');
            if (token[0] != '*' || colon == std::string::npos) return spiceNode(mappedName(token));
            return spiceNode(mappedName(token.substr(0, colon))) + ":" + token.substr(colon + 1);
        };
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            const auto w = words(line);
            if (w.empty()) continue;
            if (w[0] == "*C_UNIT" && w.size() == 3) {
                cScale = std::stod(w[1]) * (w[2] == "PF" ? 1e-12 : w[2] == "FF" ? 1e-15 : w[2] == "NF" ? 1e-9 : 1.0);
            } else if (w[0] == "*R_UNIT" && w.size() == 3) {
                rScale = std::stod(w[1]) * (w[2] == "KOHM" ? 1e3 : 1.0);
            } else if (w[0] == "*NAME_MAP") {
                inMap = true;
            } else if (w[0] == "*D_NET") {
                inMap = false;
                section = 0;
                if (w.size() != 3) continue;
                nets.push_back({});
                nets.back().node = spiceNode(mappedName(w[1]));
                nets.back().total = std::stod(w[2]) * cScale;
            } else if (inMap && line[0] == '*' && line.size() > 1 && std::isdigit((unsigned char)line[1])) {
                if (w.size() == 2) names[w[0]] = w[1];
            } else if (w[0] == "*CONN" || w[0] == "*CAP" || w[0] == "*RES" || w[0] == "*END") {
                section = w[0][2];  // O, A, E or N
            } else if (nets.empty() || !section) {
                continue;
            } else if (section == 'O' && w.size() >= 3) {
                const auto colon = w[1].find(':');
                if (w[0] == "*P") nets.back().port = true;
                if (w[0] == "*P" && nodeOf(w[1]) != nets.back().node) nets.back().pins["?"] = "?";  // a port under another name: not understood
                if (w[0] == "*I" && colon != std::string::npos) nets.back().pins[plainName(mappedName(w[1].substr(0, colon))) + ":" + w[1].substr(colon + 1)] = nodeOf(w[1]);
            } else if (section == 'A' && (w.size() == 3 || w.size() == 4)) {
                if (std::stod(w.back()) > 0) nets.back().caps.push_back({nodeOf(w[1]), w.size() == 4 ? nodeOf(w[2]) : std::string(), std::stod(w.back()) * cScale});
            } else if (section == 'E' && w.size() >= 4) {
                nets.back().res.push_back({nodeOf(w[1]), nodeOf(w[2]), std::max(1e-3, std::stod(w[3]) * rScale)});
            }
        }
        long index = 0;
        for (const Net& net : nets) {
            if (lower(net.node) == "vdd" || lower(net.node) == "vss") continue;
            // Its network stands in for the net when its pins are exactly the netlist's and its resistors leave no node apart
            bool whole = cfg.wireResistance && !net.res.empty();
            if (whole) {
                std::set<std::string> pins;
                for (const auto& [pin, node] : net.pins) pins.insert(pin);
                const auto on = attached.find(net.node);
                whole = on != attached.end() ? pins == on->second : pins.empty();
            }
            if (whole) {
                std::map<std::string, std::string> parent;
                const std::function<std::string(const std::string&)> find = [&](const std::string& n) {
                    const auto it = parent.find(n);
                    if (it == parent.end() || it->second == n) return n;
                    return it->second = find(it->second);
                };
                for (const auto& [a, b, ohms] : net.res) parent[find(a)] = find(b);
                const std::string root = find(std::get<0>(net.res.front()));
                for (const auto& [pin, node] : net.pins) whole = whole && find(node) == root;
                for (const auto& [node, other, farads] : net.caps) whole = whole && (find(node) == root || (!other.empty() && find(other) == root));
                if (net.port) whole = whole && find(net.node) == root;
            }
            if (whole) {
                std::set<std::string> own;
                for (const auto& [a, b, ohms] : net.res) {
                    own.insert(a);
                    own.insert(b);
                    parasitics << "Rw" << ++index << " " << a << " " << b << " " << ohms << "\n";
                }
                for (const auto& [pin, node] : net.pins) pinNode[pin] = node;
                for (const auto& [node, other, farads] : net.caps) {
                    parasitics << "Cw" << ++index << " " << (own.count(node) ? node : other) << " 0 " << farads << "\n";
                    deck.capacitanceFarads += farads;
                }
                deck.resistors += (long)net.res.size();
                ++deck.nets;
            } else if (net.total > 0) {
                // Inside the top subcircuit, where these names are its nets
                parasitics << "Cnet" << ++index << " " << net.node << " 0 " << net.total << "\n";
                deck.capacitanceFarads += net.total;
                ++deck.nets;
                ++deck.lumpedNets;
            }
        }
    }

    std::ostringstream out;
    out << "* " << cfg.top << " at transistor level, its layout's netlist\n" << cfg.modelCards;
    std::vector<std::string> topPorts;
    std::set<std::string> others;
    bool inTop = false;
    std::string current;
    std::map<std::string, long> inCell, placed;
    for (const std::string& raw : lines) {
        const auto w = words(raw);
        if (w.empty() || w[0][0] == '*') continue;
        const std::string head = lower(w[0]);
        if (head == ".subckt") {
            if (w.size() < 2) throw std::invalid_argument("a .SUBCKT without a name");
            inTop = w[1] == cfg.top;
            current = w[1];
            out << ".subckt " << w[1];
            for (size_t k = 2; k < w.size(); ++k) {
                out << " " << spiceNode(w[k]);
                if (inTop) topPorts.push_back(w[k]);
            }
            out << "\n";
        } else if (head == ".ends") {
            if (inTop) out << parasitics.str();
            out << ".ends\n";
            inTop = false;
        } else if (head[0] == 'm') {
            // A transistor: on its model's name, through the device library's subcircuit
            if (w.size() < 6) throw std::invalid_argument("a malformed transistor: " + raw);
            out << "X" << w[0];
            for (size_t k = 1; k < 5; ++k) out << " " << spiceNode(w[k]);
            std::string params;
            for (size_t k = 6; k < w.size(); ++k) params += " " + lower(w[k]);
            out << " " << w[5] << params << " mm_ok={mc_mismatch}\n";
            ++inCell[current];
        } else if (head[0] == 'x') {
            out << w[0];
            const auto pins = cellPins.find(w.back());
            for (size_t k = 1; k + 1 < w.size(); ++k) {
                // On a net with its RC network, each pin is a node of its own
                const auto own = inTop && pins != cellPins.end() && pins->second.size() == w.size() - 2 ? pinNode.find(plainName(w[0].substr(1)) + ":" + pins->second[k - 1]) : pinNode.end();
                out << " " << (own != pinNode.end() ? own->second : spiceNode(w[k]));
            }
            out << " " << w.back() << "\n";
            if (inTop) ++placed[w.back()];
        } else if (head[0] == '.') {
            continue;
        } else if (head[0] == 'd') {
            others.insert(raw);  // an antenna diode: reverse biased, leaves only its small capacitance
        } else {
            throw std::invalid_argument("a device this cannot simulate at transistor level yet: " + raw);
        }
    }
    if (topPorts.empty()) throw std::invalid_argument("the netlist has no .SUBCKT " + cfg.top);
    for (const auto& [cell, count] : placed) {
        const auto devices = inCell.find(cell);
        if (devices != inCell.end()) deck.transistors += count * devices->second;
    }
    if (!deck.transistors) throw std::invalid_argument("the netlist has no transistors");

    // The chip, its supplies, its pins
    out << "Xchip";
    for (const auto& p : topPorts) {
        const std::string node = spiceNode(p);
        out << " " << (lower(p) == "vss" || lower(p) == "gnd" ? std::string("0") : node);
    }
    out << " " << cfg.top << "\n";
    bool hasSupply = false;
    for (const auto& p : topPorts)
        if (lower(p) == "vdd") {
            out << "Vsupply " << spiceNode(p) << " 0 dc " << cfg.supply << "\n";
            hasSupply = true;
        }
    if (!hasSupply) throw std::invalid_argument("the top has no VDD port");
    if (cfg.dut.empty()) throw std::invalid_argument("the design's instance in the recording is needed");

    for (const auto& p : topPorts) {
        const std::string l = lower(p);
        if (l == "vdd" || l == "vss" || l == "gnd") continue;
        const size_t open = p.find('[');
        const std::string name = p.substr(0, open);
        const int bit = open == std::string::npos ? -1 : std::stoi(p.substr(open + 1));
        const auto dir = direction.find(name);
        if (dir == direction.end()) throw std::invalid_argument("port " + name + " is not declared in the routed netlist");
        deck.ports.push_back({name, spiceNode(p), bit, dir->second.first});
    }

    // Inputs: what the recording did at each pin, as a voltage with an edge
    for (const ChipPort& port : deck.ports) {
        if (port.output) {
            out << "Cload_" << port.node << " " << port.node << " 0 " << cfg.outputLoadFarads << "\n";
            continue;
        }
        const flow::VcdSignal* sig = signalNamed(recording, cfg.dut + "." + port.name);
        if (!sig) throw std::invalid_argument("the recording has no signal " + cfg.dut + "." + port.name);
        const double volts0 = 0.0;
        double last = volts0;
        std::ostringstream pwl;
        pwl << "V_" << port.node << " " << port.node << " 0 pwl(0 " << volts0;
        for (const auto& c : changesOf(recording, *sig)) {
            const double t = (double)c.time * recording.secondsPerUnit;
            if (t > deck.stopSeconds) break;
            const char b = bitOf(c.value, sig->width, port.bit);
            const double v = b == '1' ? cfg.supply : 0.0;  // x and z drive low: a real pin has some value, and reset settles the rest
            if (v == last) continue;
            const double from = std::max(t, 1e-13);
            pwl << " " << from << " " << last << " " << from + cfg.edgeSeconds << " " << v;
            last = v;
        }
        pwl << ")\n";
        // A long PWL stays one word per point; the engine reads continuation lines, and needs no shorter a line
        out << pwl.str();
    }

    deck.text = out.str();
    return deck;
}

ChipCompare compareOutputs(const ChipDeck& deck, const flow::Vcd& recording, const std::string& dut, const std::map<std::string, std::vector<std::pair<double, double>>>& voltages, double supply) {
    ChipCompare out;
    const flow::VcdSignal* clock = signalNamed(recording, dut + "." + deck.clockPort);
    if (!clock) throw std::invalid_argument("the recording has no clock " + dut + "." + deck.clockPort);
    const auto& edges = changesOf(recording, *clock);
    std::vector<double> samples;
    for (size_t k = 1; k < edges.size(); ++k)
        if (edges[k].value == "1") samples.push_back((double)edges[k].time * recording.secondsPerUnit);
    for (const ChipPort& port : deck.ports) {
        if (!port.output) continue;
        const flow::VcdSignal* sig = signalNamed(recording, dut + "." + port.name);
        if (!sig) throw std::invalid_argument("the recording has no output " + dut + "." + port.name);
        const auto wave = voltages.find(port.node);
        if (wave == voltages.end() || wave->second.empty()) throw std::invalid_argument("the simulation has no waveform of output " + port.node);
        const auto& v = wave->second;
        for (const double edge : samples) {
            const double t = edge * (1 - 1e-6) - 1e-12;  // just before it: the end of the cycle
            if (t > v.back().first) break;
            const char want = bitOf(flow::valueAt(changesOf(recording, *sig), (int64_t)std::floor(t / recording.secondsPerUnit)), sig->width, port.bit);
            if (want != '0' && want != '1') continue;
            const auto after = std::lower_bound(v.begin(), v.end(), t, [](const std::pair<double, double>& p, double at) { return p.first < at; });
            double volts = after == v.end() ? v.back().second : after->second;
            if (after != v.begin() && after != v.end() && after->first > std::prev(after)->first) {
                const auto before = std::prev(after);
                volts = before->second + (after->second - before->second) * (t - before->first) / (after->first - before->first);
            }
            ++out.samples;
            const bool high = volts > 0.7 * supply, low = volts < 0.3 * supply;
            if ((want == '1' && high) || (want == '0' && low)) continue;
            ++out.mismatches;
            if (!high && !low) ++out.undecided;
            if (out.first.empty()) {
                std::ostringstream why;
                why << port.node << " at " << t * 1e9 << " ns: " << volts << " V, the recording says " << want;
                out.first = why.str();
            }
        }
    }
    return out;
}

}  // namespace sim
