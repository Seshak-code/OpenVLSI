#include "sim/netlist.h"

#include "sch/schematic.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace sim {

namespace {

using oadb::Error;

QJsonValue parsed(const std::string& text) {  // a stored JSON value (scalars included)
    return QJsonDocument::fromJson(QByteArray("[") + QByteArray::fromStdString(text) + "]").array().at(0);
}
std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
std::string str(const QJsonValue& v) {
    if (v.isString()) return v.toString().toStdString();
    if (v.isDouble()) {
        std::ostringstream o;
        o.precision(17);
        o << v.toDouble();
        return o.str();
    }
    return "";
}
bool startsWith(const std::string& s, const std::string& p) { return upper(s).rfind(upper(p), 0) == 0; }

struct Netlister {
    oadb::Workspace& ws;
    std::vector<std::string> viewList{"spice", "schematic"};
    std::map<std::string, std::string> bindings;
    std::set<std::string> globals;
    std::string subckts;                       // definitions, each after the ones it uses
    std::map<std::string, std::string> names;  // SUBCKT NAME -> lib/cell/view
    std::vector<std::string> stack;            // the schematics being netlisted, outermost first

    // "spice" (a primitive) or the schematic view an instance of lib/cell descends into
    std::string switchView(const std::string& lib, const std::string& cell) {
        oadb::Library& l = ws.library(lib);
        const auto b = bindings.find(lib + "/" + cell);
        const std::vector<std::string> tries = b == bindings.end() ? viewList : std::vector<std::string>{b->second};
        for (const auto& v : tries) {
            if (v == "spice") {
                if (l.cellProp(cell, "spice")) return v;
                continue;
            }
            for (const auto& info : l.views(cell))
                if (info.name == v && info.type == "schematic") return v;
        }
        std::string list;
        for (const auto& v : tries) list += (list.empty() ? "" : " ") + v;
        throw Error("netlist: " + lib + "/" + cell + (b != bindings.end() ? " is bound to '" + list + "', which is not" : " has none of the views")
                    + " (" + list + ") a netlist can use: a schematic view, or spice with a spice cell property");
    }

    // a net's SPICE node
    std::string node(const std::string& net, const std::string& where) {
        if (net.find_first_of("(),=\"'") != std::string::npos) throw Error("netlist: " + where + ": net " + net + " has a character SPICE cannot take");
        if (!net.empty() && net.back() == '!') {
            if (net == "gnd!") return "0";
            globals.insert(net);
            return net;
        }
        if (upper(net) == "0" || upper(net) == "GND")
            throw Error("netlist: " + where + ": the local net " + net + " would be SPICE's ground; name it gnd! for ground, or rename it");
        return net;
    }

    std::string expand(const std::string& tmpl, const oadb::Instance& i, const std::map<std::string, std::string>& nets,
                       const QJsonObject& defaults, const std::string& where) {
        std::string out;
        for (size_t k = 0; k < tmpl.size(); ++k) {
            const size_t close = tmpl[k] == '[' ? tmpl.find(']', k) : std::string::npos;
            if (close == std::string::npos || close == k + 1 || (tmpl[k + 1] != '|' && tmpl[k + 1] != '@')) {
                out += tmpl[k];
                continue;
            }
            const char kind = tmpl[k + 1];
            const std::string field = tmpl.substr(k + 2, close - k - 2);
            k = close;
            if (kind == '|') {
                const auto n = nets.find(field);
                if (n == nets.end()) throw Error("netlist: " + where + ": the spice template names pin " + field + ", which the symbol lacks");
                out += n->second;
                continue;
            }
            const size_t colon = field.find(':');
            const std::string p = field.substr(0, colon);
            std::string v;
            bool has = true;
            if (p == "instanceName") v = i.name;
            else if (p == "cellName") v = i.cell;
            else if (const auto it = i.params.find(p); it != i.params.end()) v = it->second;
            else if (defaults.contains(QString::fromStdString(p))) v = str(defaults[QString::fromStdString(p)]);
            else has = false;
            if (has && (v.empty() || std::any_of(v.begin(), v.end(), [](unsigned char c) { return std::isspace(c) || c < 0x20; })))
                throw Error("netlist: " + where + ": parameter " + p + " = '" + v + "' is empty or holds spaces");
            if (colon == std::string::npos) {
                if (!has) throw Error("netlist: " + where + ": no value for parameter " + p + " (the instance's, or the cell's params default)");
                out += v;
            } else if (has) {
                std::string text = field.substr(colon + 1);
                for (size_t at; (at = text.find('%')) != std::string::npos;) text.replace(at, 1, v);
                out += text;
            }
        }
        std::istringstream words(out);  // single spaces
        std::string w, line;
        while (words >> w) line += (line.empty() ? "" : " ") + w;
        return line;
    }

    std::vector<std::string> ports(const std::string& lib, const std::string& cell, const std::string& view) {
        std::vector<std::string> out;
        for (const auto& p : ws.library(lib).pins(cell, view)) out.push_back(p.name);  // sorted by name
        return out;
    }

    // the element lines of a schematic
    std::string body(const std::string& lib, const std::string& cell, const std::string& view) {
        const std::string self = lib + "/" + cell + "/" + view;
        const sch::Connectivity k = sch::extract(ws, lib, cell, view);
        if (!k.ok()) {
            std::string all;
            for (const auto& e : k.errors) all += "; " + e;
            throw Error("netlist: " + self + " does not check:" + all.substr(1));
        }
        std::map<std::string, std::string> nodes;  // NODE -> net, for nets SPICE would merge
        for (const auto& n : k.nets) {
            const std::string s = node(n.name, self);
            if (const auto [it, fresh] = nodes.emplace(upper(s), n.name); !fresh && it->second != n.name)
                throw Error("netlist: " + self + ": nets " + it->second + " and " + n.name + " are one node to SPICE (it ignores case)");
        }
        std::ostringstream o;
        std::map<std::string, std::string> elements;  // ELEMENT -> instance
        for (const auto& i : ws.library(lib).instances(cell, view)) {
            const std::string where = self + " instance " + i.name;
            std::map<std::string, std::string> nets;
            if (const auto it = k.instanceNets.find(i.name); it != k.instanceNets.end())
                for (const auto& [pin, net] : it->second) nets[pin] = node(net, where);
            const std::string sw = switchView(i.lib, i.cell);
            std::string element;
            if (sw == "spice") {
                const QJsonObject spice = parsed(*ws.library(i.lib).cellProp(i.cell, "spice")).toObject();
                const std::string prefix = spice["prefix"].toString().toStdString(), tmpl = spice["template"].toString().toStdString();
                if (prefix.empty() || !std::isalpha((unsigned char)prefix[0]) || tmpl.empty())
                    throw Error("netlist: " + i.lib + "/" + i.cell + ": its spice property needs a prefix (a letter) and a template");
                const auto d = ws.library(i.lib).cellProp(i.cell, "params");
                element = startsWith(i.name, prefix) ? i.name : prefix + i.name;
                o << element << " " << expand(tmpl, i, nets, d ? parsed(*d).toObject() : QJsonObject(), where) << "\n";
            } else {
                if (!i.params.empty())
                    throw Error("netlist: " + where + ": parameters on a subcircuit instance are not supported yet");
                const std::vector<std::string> sub = ports(i.lib, i.cell, sw), sym = ports(i.lib, i.cell, i.view);
                if (sub != sym)
                    throw Error("netlist: " + where + ": the pins of " + i.lib + "/" + i.cell + "/" + i.view + " differ from its " + sw + "'s");
                const std::string name = define(i.lib, i.cell, sw);
                element = startsWith(i.name, "X") ? i.name : "X" + i.name;
                o << element;
                for (const auto& p : sub) o << " " << nets.at(p);
                o << " " << name << "\n";
            }
            if (const auto [it, fresh] = elements.emplace(upper(element), i.name); !fresh)
                throw Error("netlist: " + self + ": instances " + it->second + " and " + i.name + " are one element to SPICE (" + element + ")");
        }
        return o.str();
    }

    // a schematic as a .subckt (once); its name
    std::string define(const std::string& lib, const std::string& cell, const std::string& view) {
        const std::string key = lib + "/" + cell + "/" + view;
        if (std::find(stack.begin(), stack.end(), key) != stack.end()) throw Error("netlist: " + key + " contains itself");
        if (const auto it = names.find(upper(cell)); it != names.end()) {
            if (it->second != key) throw Error("netlist: " + it->second + " and " + key + " would both be subcircuit " + cell);
            return cell;
        }
        stack.push_back(key);
        const std::string lines = body(lib, cell, view);
        stack.pop_back();
        names[upper(cell)] = key;
        std::string head = ".subckt " + cell;
        for (const auto& p : ports(lib, cell, view)) head += " " + node(p, key);
        subckts += head + "\n" + lines + ".ends " + cell + "\n";
        return cell;
    }
};

}  // namespace

std::string netlist(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view) {
    Netlister n{ws};
    std::string tl = lib, tc = cell, tv = view;
    const std::string type = ws.library(lib).viewType(cell, view);
    if (type == "config") {
        oadb::Library& l = ws.library(lib);
        const auto top = l.prop(cell, view, "top");
        const QString t = top ? parsed(*top).toString() : QString();
        const QStringList parts = t.split('/');
        if (parts.size() != 3) throw Error("netlist: config " + lib + "/" + cell + "/" + view + " needs a top: \"lib/cell/view\"");
        tl = parts[0].toStdString(), tc = parts[1].toStdString(), tv = parts[2].toStdString();
        if (const auto vl = l.prop(cell, view, "viewList")) {
            n.viewList.clear();
            for (const auto& v : parsed(*vl).toArray()) n.viewList.push_back(v.toString().toStdString());
            if (n.viewList.empty()) throw Error("netlist: config " + cell + "/" + view + ": an empty viewList");
        }
        if (const auto b = l.prop(cell, view, "bindings")) {
            const QJsonObject o = parsed(*b).toObject();
            for (auto it = o.begin(); it != o.end(); ++it) n.bindings[it.key().toStdString()] = it.value().toString().toStdString();
        }
    } else if (type != "schematic") {
        throw Error("netlist: " + lib + "/" + cell + "/" + view + " is a " + type + " view, not a schematic or config");
    }
    if (ws.library(tl).viewType(tc, tv) != "schematic") throw Error("netlist: the top " + tl + "/" + tc + "/" + tv + " is not a schematic");
    n.stack.push_back(tl + "/" + tc + "/" + tv);
    const std::string top = n.body(tl, tc, tv);
    std::string out = "* " + tl + "/" + tc + "/" + tv + " (OpenVLSI)\n";
    if (!n.globals.empty()) {
        out += ".global";
        for (const auto& g : n.globals) out += " " + g;
        out += "\n";
    }
    return out + n.subckts + top;
}

}  // namespace sim
