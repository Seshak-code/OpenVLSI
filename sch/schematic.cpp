#include "sch/schematic.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <numeric>
#include <set>

namespace sch {

oadb::Point transform(const std::string& o, oadb::Point p) {
    auto r90 = [](oadb::Point q) { return oadb::Point{-q.y, q.x}; };
    if (o == "R0") return p;
    if (o == "R90") return r90(p);
    if (o == "R180") return {-p.x, -p.y};
    if (o == "R270") return {p.y, -p.x};
    if (o == "MY") return {-p.x, p.y};
    if (o == "MX") return {p.x, -p.y};
    if (o == "MYR90") return r90({-p.x, p.y});
    if (o == "MXR90") return r90({p.x, -p.y});
    throw oadb::Error("sch: orientation '" + o + "' is not one of R0 R90 R180 R270 MX MY MXR90 MYR90");
}

namespace {

bool onSegment(oadb::Point p, oadb::Point a, oadb::Point b) {
    // collinear and inside the bounding box; exact in integers
    const __int128 cross = (__int128)(b.x - a.x) * (p.y - a.y) - (__int128)(b.y - a.y) * (p.x - a.x);
    return cross == 0 && std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y &&
           p.y <= std::max(a.y, b.y);
}

bool onWire(oadb::Point p, const oadb::Wire& w) {
    for (size_t k = 1; k < w.points.size(); ++k)
        if (onSegment(p, w.points[k - 1], w.points[k])) return true;
    return false;
}

std::string at(oadb::Point p) { return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")"; }

struct UnionFind {
    std::vector<int> up;
    explicit UnionFind(int n) : up(n) { std::iota(up.begin(), up.end(), 0); }
    int find(int x) { return up[x] == x ? x : up[x] = find(up[x]); }
    void join(int a, int b) { up[find(a)] = find(b); }
};

}  // namespace

Connectivity extract(oadb::Workspace& ws, const std::string& libName, const std::string& cell, const std::string& view) {
    Connectivity c;
    oadb::Library& lib = ws.library(libName);
    if (lib.viewType(cell, view) != "schematic") throw oadb::Error("sch: " + cell + "/" + view + " is not a schematic");
    const auto wires = lib.wires(cell, view);
    const auto labels = lib.labels(cell, view);
    const auto ports = lib.pins(cell, view);

    // Items: the wires, then every pin (each instance's, placed, then the schematic's own)
    struct PinItem { Terminal t; oadb::Point at; };
    std::vector<PinItem> pins;
    for (const auto& i : lib.instances(cell, view)) {
        std::vector<oadb::Pin> mp;
        try {
            if (ws.library(i.lib).viewType(i.cell, i.view) != "symbol")
                throw oadb::Error(i.lib + "/" + i.cell + "/" + i.view + " is not a symbol");
            mp = ws.library(i.lib).pins(i.cell, i.view);
        } catch (const oadb::Error& e) {
            c.errors.push_back("instance " + i.name + ": master " + i.lib + "/" + i.cell + "/" + i.view + ": " + e.what());
            continue;
        }
        for (const auto& p : mp) pins.push_back({{i.name, p.name}, place(i, p.at)});
    }
    for (const auto& p : ports) pins.push_back({{"", p.name}, p.at});
    const int W = (int)wires.size(), N = W + (int)pins.size();
    UnionFind uf(N);

    // ponytail: every end against every wire and pin, O(n^2); a spatial index when schematics reach thousands of wires
    std::vector<std::array<bool, 2>> endUsed(W, {false, false});
    for (int a = 0; a < W; ++a)
        for (int e = 0; e < 2; ++e) {
            const oadb::Point p = e ? wires[a].points.back() : wires[a].points.front();
            for (int b = 0; b < W; ++b)
                if (b != a && onWire(p, wires[b])) { uf.join(a, b); endUsed[a][e] = true; }  // an end on b: joined
            for (size_t k = 0; k < pins.size(); ++k)
                if (pins[k].at == p) { uf.join(a, W + (int)k); endUsed[a][e] = true; }
        }
    for (size_t k = 0; k < pins.size(); ++k)
        for (size_t m = k + 1; m < pins.size(); ++m)
            if (pins[k].at == pins[m].at) uf.join(W + (int)k, W + (int)m);

    // Names: labels on the item they touch; schematic pins by their own name
    std::map<int, std::set<std::string>> names;
    std::vector<bool> labelled(W, false);
    for (const auto& l : labels) {
        int item = -1;
        for (int a = 0; a < W && item < 0; ++a) if (onWire(l.at, wires[a])) item = a;
        for (size_t k = 0; k < pins.size() && item < 0; ++k) if (pins[k].at == l.at) item = W + (int)k;
        if (item < 0) { c.errors.push_back("label " + l.name + " at " + at(l.at) + " is on no wire or pin"); continue; }
        names[item].insert(l.name);
        if (item < W) labelled[item] = true;
    }
    for (int a = 0; a < W; ++a)  // a labelled stub connects by name: its free end is not dangling
        for (int e = 0; e < 2; ++e)
            if (!endUsed[a][e] && !labelled[a]) c.warnings.push_back("dangling wire end at " + at(e ? wires[a].points.back() : wires[a].points.front()));
    for (size_t k = 0; k < pins.size(); ++k)
        if (pins[k].t.instance.empty()) names[W + (int)k].insert(pins[k].t.pin);
    // the same name anywhere is the same net
    std::map<std::string, int> byName;
    for (const auto& [item, ns] : names)
        for (const auto& n : ns) {
            if (auto it = byName.find(n); it != byName.end()) uf.join(item, it->second);
            else byName[n] = item;
        }

    // Groups -> nets
    std::map<int, std::set<std::string>> groupNames;
    std::map<int, std::vector<Terminal>> groupTerms;
    std::map<int, oadb::Point> groupAt;  // the group's least point: a stable order for unnamed nets
    for (const auto& [item, ns] : names) groupNames[uf.find(item)].insert(ns.begin(), ns.end());
    auto least = [&](int g, oadb::Point p) {
        if (!groupAt.count(g) || p < groupAt[g]) groupAt[g] = p;
    };
    for (int a = 0; a < W; ++a) for (const auto& p : wires[a].points) least(uf.find(a), p);
    for (size_t k = 0; k < pins.size(); ++k) {
        const int g = uf.find(W + (int)k);
        groupTerms[g].push_back(pins[k].t);
        least(g, pins[k].at);
    }
    std::vector<std::pair<oadb::Point, int>> unnamed;
    std::map<int, std::string> netName;
    for (const auto& [g, p] : groupAt) {
        const auto ns = groupNames.find(g);
        if (ns == groupNames.end()) { unnamed.push_back({p, g}); continue; }
        if (ns->second.size() > 1) {
            std::string all;
            for (const auto& n : ns->second) all += (all.empty() ? "" : ", ") + n;
            c.errors.push_back("short: one net is named " + all);
        }
        netName[g] = *ns->second.begin();
    }
    std::sort(unnamed.begin(), unnamed.end());
    int serial = 0;
    for (const auto& [p, g] : unnamed) {
        std::string n;
        do n = "net" + std::to_string(++serial); while (byName.count(n));
        netName[g] = n;
    }

    for (const auto& [g, name] : netName) {
        Net net{name, !name.empty() && name.back() == '!', groupTerms[g]};
        std::sort(net.terminals.begin(), net.terminals.end(),
                  [](const Terminal& a, const Terminal& b) { return std::tie(a.instance, a.pin) < std::tie(b.instance, b.pin); });
        const bool named = groupNames.count(g) > 0;
        if (net.terminals.empty() && !named) c.warnings.push_back("wires at " + at(groupAt[g]) + " connect to nothing");
        if (net.terminals.size() == 1 && !named && !net.terminals[0].instance.empty())
            c.warnings.push_back("instance " + net.terminals[0].instance + " pin " + net.terminals[0].pin + " is not connected");
        for (const auto& t : net.terminals) if (!t.instance.empty()) c.instanceNets[t.instance][t.pin] = name;
        c.nets.push_back(std::move(net));
    }
    std::sort(c.nets.begin(), c.nets.end(), [](const Net& a, const Net& b) { return a.name < b.name; });

    // the cell's symbol must show the schematic's pins
    for (const auto& v : lib.views(cell)) {
        if (v.type != "symbol") continue;
        std::set<std::pair<std::string, std::string>> sp, cp;
        for (const auto& p : lib.pins(cell, v.name)) sp.insert({p.name, p.direction});
        for (const auto& p : ports) cp.insert({p.name, p.direction});
        if (sp != cp) c.warnings.push_back("the pins of " + cell + "/" + v.name + " differ from the schematic's (name or direction)");
    }
    return c;
}

void generateSymbol(oadb::Library& lib, const std::string& cell, const std::string& from, const std::string& to) {
    if (lib.viewType(cell, from) != "schematic") throw oadb::Error("sch: " + cell + "/" + from + " is not a schematic");
    const auto ports = lib.pins(cell, from);
    if (ports.empty()) throw oadb::Error("sch: " + cell + "/" + from + " has no pins to make a symbol from");
    std::vector<oadb::Pin> left, right, top;
    for (const auto& p : ports) (p.direction == "input" ? left : p.direction == "output" ? right : top).push_back(p);
    // ponytail: a fixed 100-unit pitch and width; the symbol editor restyles it
    const int64_t G = 100;
    const int64_t rows = (int64_t)std::max(left.size(), right.size());
    const int64_t width = std::max<int64_t>(4, 2 * (int64_t)top.size() + 2) * G, height = (2 * std::max<int64_t>(rows, 1) + 2) * G;
    lib.change("generate " + cell + "/" + to + " from " + from, [&] {
        lib.createView(cell, to, "symbol");
        lib.addShape(cell, to, {0, "rect", "device", {{0, 0}, {width, height}}, ""});
        auto side = [&](const std::vector<oadb::Pin>& ps, const char* justify, auto pinAt, auto stubEnd, auto textAt) {
            for (size_t k = 0; k < ps.size(); ++k) {
                const oadb::Point p = pinAt((int64_t)k), e = stubEnd((int64_t)k);
                lib.addShape(cell, to, {0, "line", "pin", {p, e}, ""});
                lib.addShape(cell, to, {0, "label", "pin", {textAt((int64_t)k)}, ps[k].name, justify});
                lib.addPin(cell, to, {0, ps[k].name, ps[k].direction, p});
            }
        };
        side(left, "centerLeft", [&](int64_t k) { return oadb::Point{-G, height - (2 * k + 2) * G}; },
             [&](int64_t k) { return oadb::Point{0, height - (2 * k + 2) * G}; },
             [&](int64_t k) { return oadb::Point{G / 4, height - (2 * k + 2) * G}; });
        side(right, "centerRight", [&](int64_t k) { return oadb::Point{width + G, height - (2 * k + 2) * G}; },
             [&](int64_t k) { return oadb::Point{width, height - (2 * k + 2) * G}; },
             [&](int64_t k) { return oadb::Point{width - G / 4, height - (2 * k + 2) * G}; });
        side(top, "upperCenter", [&](int64_t k) { return oadb::Point{(2 * k + 2) * G, height + G}; },
             [&](int64_t k) { return oadb::Point{(2 * k + 2) * G, height}; },
             [&](int64_t k) { return oadb::Point{(2 * k + 2) * G, height - G / 4}; });
        lib.addShape(cell, to, {0, "label", "annotate", {{0, height + 2 * G}}, "[@instanceName]", "lowerLeft"});
        lib.addShape(cell, to, {0, "label", "annotate", {{width / 2, height / 2}}, "[@cellName]", "centerCenter"});
    });
}

}  // namespace sch
