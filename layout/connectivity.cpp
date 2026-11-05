#include "layout/connectivity.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace layout {

namespace {

struct Piece { int metal; Rect r; };

bool touching(const Rect& a, const Rect& b) { return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1; }

Rect boundsOf(const std::vector<oadb::Point>& points) {
    Rect r{INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
    for (const auto& p : points) {
        r.x0 = std::min(r.x0, p.x);
        r.y0 = std::min(r.y0, p.y);
        r.x1 = std::max(r.x1, p.x);
        r.y1 = std::max(r.y1, p.y);
    }
    return r;
}

Rect placed(const oadb::LayoutInstance& i, const Rect& r) {
    const Xform t = Xform::of(i);
    Rect out{INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
    for (const auto& [px, py] : {std::pair{r.x0, r.y0}, {r.x1, r.y0}, {r.x1, r.y1}, {r.x0, r.y1}}) {
        double x, y;
        t.apply((double)px, (double)py, x, y);
        out.x0 = std::min(out.x0, (int64_t)std::llround(x));
        out.y0 = std::min(out.y0, (int64_t)std::llround(y));
        out.x1 = std::max(out.x1, (int64_t)std::llround(x));
        out.y1 = std::max(out.y1, (int64_t)std::llround(y));
    }
    return out;
}

}  // namespace

bool ConnectivityReport::clean() const {
    if (!shorts.empty()) return false;
    return std::all_of(nets.begin(), nets.end(), [](const NetStatus& n) { return n.groups <= 1; });
}

ConnectivityReport checkConnectivity(oadb::Library& lib, const Tech& tech, const std::string& cell, const std::string& view) {
    const auto prop = lib.prop(cell, view, "connectivity");
    if (!prop) throw std::runtime_error(cell + "/" + view + " has no connectivity: it was not generated from a schematic (layout_generate)");
    const QJsonObject con = QJsonDocument::fromJson(QByteArray::fromStdString(*prop)).object();

    struct Terminal { std::string net; Piece piece; };
    std::vector<Terminal> terminals;
    const QJsonObject instanceNets = con["instances"].toObject();
    for (const auto& i : lib.layoutInstances(cell, view)) {
        const QJsonObject entry = instanceNets[QString::number(i.id)].toObject();
        if (entry.isEmpty()) continue;  // placed by hand: nothing the schematic says about it
        const auto declared = lib.prop(i.cell, i.view.empty() ? "layout" : i.view, "terminals");
        if (!declared) throw std::runtime_error("master " + i.cell + " declares no terminals");
        const QJsonObject masterTerminals = QJsonDocument::fromJson(QByteArray::fromStdString(*declared)).object();
        const QJsonObject nets = entry["nets"].toObject();
        for (auto it = nets.begin(); it != nets.end(); ++it)
            for (const auto& v : masterTerminals[it.key()].toArray()) {
                const QJsonArray a = v.toArray();
                terminals.push_back({it.value().toString().toStdString(),
                                     {a[0].toInt(), placed(i, {(int64_t)a[1].toDouble(), (int64_t)a[2].toDouble(), (int64_t)a[3].toDouble(), (int64_t)a[4].toDouble()})}});
            }
    }
    // The view's own wiring, and the cuts that join one metal to the next. A pin is a label on a metal's pin rectangle,
    // read here rather than recorded: pins move
    std::map<int, int> metalOfLayer, viaOfLayer;
    for (size_t m = 0; m < tech.metals.size(); ++m) metalOfLayer[tech.metals[m].layer] = (int)m;
    for (size_t v = 0; v < tech.vias.size(); ++v) viaOfLayer[tech.vias[v].cutLayer] = (int)v;
    std::vector<Piece> wires;
    std::vector<std::pair<int, Rect>> cuts;
    const std::vector<oadb::LayoutShape> shapes = lib.layoutShapes(cell, view);
    for (const auto& label : shapes) {
        const auto metal = metalOfLayer.find(label.layer);
        if (label.kind != "text" || label.datatype != tech.textDatatype || metal == metalOfLayer.end() || label.points.empty()) continue;
        const oadb::Point at = label.points[0];
        for (const auto& s : shapes) {
            if (s.kind == "text" || s.layer != label.layer || s.datatype != tech.pinDatatype) continue;
            const Rect r = boundsOf(s.points);
            if (at.x >= r.x0 && at.x <= r.x1 && at.y >= r.y0 && at.y <= r.y1) {
                terminals.push_back({label.text, {metal->second, r}});
                break;
            }
        }
    }
    for (const auto& s : shapes) {
        if (s.kind == "text" || s.datatype != 0) continue;
        Rect r = boundsOf(s.points);
        if (s.kind == "path") {
            r.x0 -= s.width / 2;
            r.y0 -= s.width / 2;
            r.x1 += s.width / 2;
            r.y1 += s.width / 2;
        }
        if (const auto m = metalOfLayer.find(s.layer); m != metalOfLayer.end()) wires.push_back({m->second, r});
        else if (const auto v = viaOfLayer.find(s.layer); v != viaOfLayer.end()) cuts.push_back({v->second, r});
    }

    // Union-find over the terminals and the wires; n^2, for a cell a person routes by hand
    const size_t nt = terminals.size(), n = nt + wires.size();
    std::vector<size_t> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    const std::function<size_t(size_t)> find = [&](size_t x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
    const auto pieceAt = [&](size_t k) -> const Piece& { return k < nt ? terminals[k].piece : wires[k - nt]; };
    for (size_t a = 0; a < n; ++a)
        for (size_t b = a + 1; b < n; ++b) {
            const Piece& pa = pieceAt(a);
            const Piece& pb = pieceAt(b);
            if (pa.metal == pb.metal) {
                if (touching(pa.r, pb.r)) parent[find(a)] = find(b);
            } else {
                for (const auto& [via, r] : cuts) {
                    const TechVia& v = tech.vias[via];
                    const bool joins = (v.lower == pa.metal && v.upper == pb.metal) || (v.lower == pb.metal && v.upper == pa.metal);
                    if (joins && touching(r, pa.r) && touching(r, pb.r)) {
                        parent[find(a)] = find(b);
                        break;
                    }
                }
            }
        }

    ConnectivityReport out;
    std::map<std::string, std::map<size_t, std::vector<size_t>>> byNet;
    std::map<size_t, std::set<std::string>> netsOfGroup;
    for (size_t k = 0; k < nt; ++k) {
        byNet[terminals[k].net][find(k)].push_back(k);
        netsOfGroup[find(k)].insert(terminals[k].net);
    }
    for (const auto& [group, names] : netsOfGroup) {
        (void)group;
        if (names.size() > 1)
            for (auto a = names.begin(); std::next(a) != names.end(); ++a) out.shorts.push_back({*a, *std::next(a)});
    }
    for (const auto& [name, groups] : byNet) {
        NetStatus st;
        st.name = name;
        st.groups = (int)groups.size();
        const size_t* previous = nullptr;
        for (const auto& [root, members] : groups) {
            (void)root;
            st.terminals += (int)members.size();
            if (previous) {
                const Rect& a = terminals[*previous].piece.r;
                const Rect& b = terminals[members.front()].piece.r;
                st.flightLines.push_back({(a.x0 + a.x1) / 2, (a.y0 + a.y1) / 2, (b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2});
            }
            previous = &members.front();
        }
        out.nets.push_back(std::move(st));
    }
    return out;
}

}  // namespace layout
