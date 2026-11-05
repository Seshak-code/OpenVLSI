#include "layout/flatten.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace layout {

namespace {
double area2(const std::vector<oadb::Point>& p) {
    double a = 0;
    for (size_t k = 0, j = p.size() - 1; k < p.size(); j = k++) a += (double)p[j].x * (double)p[k].y - (double)p[k].x * (double)p[j].y;
    return a;
}
}  // namespace

Xform Xform::then(const Xform& o) const {
    Xform r;
    r.a = o.a * a + o.b * c;
    r.b = o.a * b + o.b * d;
    r.c = o.c * a + o.d * c;
    r.d = o.c * b + o.d * d;
    r.e = o.a * e + o.b * f + o.e;
    r.f = o.c * e + o.d * f + o.f;
    return r;
}

Xform Xform::of(const oadb::LayoutInstance& i, int col, int row) {
    // exact at the right angles a layout uses: no rounding in a vertex's way
    double cs = 1, sn = 0;
    const double turn = std::fmod(std::fmod(i.angle, 360.0) + 360.0, 360.0);
    if (turn == 90) { cs = 0; sn = 1; }
    else if (turn == 180) { cs = -1; sn = 0; }
    else if (turn == 270) { cs = 0; sn = -1; }
    else if (turn != 0) { cs = std::cos(turn * M_PI / 180); sn = std::sin(turn * M_PI / 180); }
    const double my = i.mirrorX ? -1.0 : 1.0;
    Xform t;
    t.a = i.mag * cs;
    t.b = -i.mag * sn * my;
    t.c = i.mag * sn;
    t.d = i.mag * cs * my;
    t.e = (double)i.at.x + (double)col * (double)i.colStep.x + (double)row * (double)i.rowStep.x;
    t.f = (double)i.at.y + (double)col * (double)i.colStep.y + (double)row * (double)i.rowStep.y;
    return t;
}

bool triangulate(const std::vector<oadb::Point>& polygon, std::vector<double>& xy) {
    std::vector<oadb::Point> p = polygon;
    if (p.size() > 1 && p.front() == p.back()) p.pop_back();
    if (p.size() < 3) return false;
    if (area2(p) < 0) std::reverse(p.begin(), p.end());
    auto cross = [](const oadb::Point& a, const oadb::Point& b, const oadb::Point& c) {
        return (double)(b.x - a.x) * (double)(c.y - a.y) - (double)(b.y - a.y) * (double)(c.x - a.x);
    };
    auto emit = [&](const oadb::Point& a, const oadb::Point& b, const oadb::Point& c) {
        xy.insert(xy.end(), {(double)a.x, (double)a.y, (double)b.x, (double)b.y, (double)c.x, (double)c.y});
    };
    // ponytail: ear clipping is quadratic in a polygon's points; a mask polygon has at most 8190, and each master's are
    // tessellated once. A sweep is the upgrade if a merged full-chip layer ever comes through as one polygon.
    std::vector<int> v(p.size());
    for (size_t k = 0; k < v.size(); ++k) v[k] = (int)k;
    size_t guard = 2 * v.size() * v.size() + 16, at = 0;
    while (v.size() > 3) {
        if (guard-- == 0) return false;
        const size_t n = v.size(), ia = (at + n - 1) % n, ib = at % n, ic = (at + 1) % n;
        const auto& a = p[v[ia]];
        const auto& b = p[v[ib]];
        const auto& c = p[v[ic]];
        const double turn = cross(a, b, c);
        bool ear = turn > 0;
        for (size_t k = 0; ear && k < n; ++k) {
            if (k == ia || k == ib || k == ic) continue;
            const auto& q = p[v[k]];
            if (q == a || q == b || q == c) continue;  // a keyhole's seam touches itself
            ear = !(cross(a, b, q) >= 0 && cross(b, c, q) >= 0 && cross(c, a, q) >= 0);
        }
        if (turn == 0) {  // a collinear vertex adds nothing
            v.erase(v.begin() + (long)ib);
            guard = 2 * v.size() * v.size() + 16;
            continue;
        }
        if (ear) {
            emit(a, b, c);
            v.erase(v.begin() + (long)ib);
            guard = 2 * v.size() * v.size() + 16;
            at = ia;
            continue;
        }
        ++at;
    }
    emit(p[v[0]], p[v[1]], p[v[2]]);
    return true;
}

std::vector<double> pathQuads(const std::vector<oadb::Point>& p, int64_t width, int pathType) {
    std::vector<double> q;
    const double hw = (double)width / 2;
    const double extend = pathType == 0 ? 0.0 : hw;  // ponytail: type 1 (round ends) is drawn square; none of the PDKs' output uses it
    for (size_t k = 0; k + 1 < p.size(); ++k) {
        double dx = (double)(p[k + 1].x - p[k].x), dy = (double)(p[k + 1].y - p[k].y);
        const double len = std::hypot(dx, dy);
        if (len == 0) continue;
        dx /= len;
        dy /= len;
        const double e0 = k == 0 ? extend : hw, e1 = k + 2 == p.size() ? extend : hw;  // inner vertices meet by their squares
        const double x0 = (double)p[k].x - dx * e0, y0 = (double)p[k].y - dy * e0;
        const double x1 = (double)p[k + 1].x + dx * e1, y1 = (double)p[k + 1].y + dy * e1;
        const double nx = -dy * hw, ny = dx * hw;
        q.insert(q.end(), {x0 + nx, y0 + ny, x0 - nx, y0 - ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny});
    }
    return q;
}

struct Hierarchy::Master {
    std::string name;
    double metersPerDbu = 1e-9;
    struct Piece { uint32_t layerIndex; uint32_t triFirst, triCount, lineFirst, lineCount; Box box; int64_t id; };  // one shape's
    std::vector<LayerKey> keys;
    std::vector<std::vector<double>> tri, line;  // per layer in `keys`
    std::vector<Piece> pieces;
    struct Label { double x, y; uint32_t layerIndex; std::string text; int64_t id; };
    std::vector<Label> labels;
    struct Child { std::shared_ptr<Master> master; oadb::LayoutInstance placed; };
    std::vector<Child> children;
    uint64_t ownShapes = 0, unfilled = 0;
    Box box;
    uint32_t index = 0;
};

std::shared_ptr<Hierarchy::Master> Hierarchy::load(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view,
                                                   std::vector<std::string>& stack) {
    const std::string id = lib + "/" + cell + "/" + view;
    if (std::find(stack.begin(), stack.end(), id) != stack.end()) throw std::runtime_error("layout: " + id + " places itself");
    if (auto it = masters_.find(id); it != masters_.end()) return it->second;
    oadb::Library& library = ws.library(lib);
    if (library.viewType(cell, view) != "layout") throw std::runtime_error("layout: " + id + " is not a layout view");
    auto m = std::make_shared<Master>();
    m->name = cell;
    m->index = (uint32_t)masters_.size();
    if (const auto u = library.prop(cell, view, "metersPerDbu")) m->metersPerDbu = std::stod(*u);
    std::map<LayerKey, uint32_t> indexOf;
    auto layerIndex = [&](LayerKey k) {
        const auto [it, fresh] = indexOf.try_emplace(k, (uint32_t)m->keys.size());
        if (fresh) {
            m->keys.push_back(k);
            m->tri.emplace_back();
            m->line.emplace_back();
        }
        return it->second;
    };
    for (const auto& s : library.layoutShapes(cell, view)) {
        const uint32_t li = layerIndex({s.layer, s.datatype});
        if (s.kind == "text") {
            if (!s.points.empty()) m->labels.push_back({(double)s.points[0].x, (double)s.points[0].y, li, s.text, s.id});
            continue;
        }
        Master::Piece piece{li, (uint32_t)(m->tri[li].size() / 2), 0, (uint32_t)(m->line[li].size() / 2), 0, {}, s.id};
        auto& tri = m->tri[li];
        auto& line = m->line[li];
        auto edge = [&](double x0, double y0, double x1, double y1) { line.insert(line.end(), {x0, y0, x1, y1}); };
        if (s.kind == "path") {
            const std::vector<double> q = pathQuads(s.points, s.width, s.pathType);
            for (size_t k = 0; k + 8 <= q.size(); k += 8) {
                tri.insert(tri.end(), {q[k], q[k + 1], q[k + 2], q[k + 3], q[k + 4], q[k + 5], q[k], q[k + 1], q[k + 4], q[k + 5], q[k + 6], q[k + 7]});
                for (int e = 0; e < 4; ++e) {
                    const int f = (e + 1) % 4;
                    edge(q[k + 2 * e], q[k + 2 * e + 1], q[k + 2 * f], q[k + 2 * f + 1]);
                }
                for (int e = 0; e < 4; ++e) piece.box.add(q[k + 2 * e], q[k + 2 * e + 1]);
            }
        } else {
            std::vector<double> xy;
            if (!triangulate(s.points, xy)) ++m->unfilled;
            tri.insert(tri.end(), xy.begin(), xy.end());
            const auto& p = s.points;
            for (size_t k = 0; k + 1 < p.size(); ++k) edge((double)p[k].x, (double)p[k].y, (double)p[k + 1].x, (double)p[k + 1].y);
            if (p.size() > 2 && !(p.front() == p.back())) edge((double)p.back().x, (double)p.back().y, (double)p[0].x, (double)p[0].y);
            for (const auto& q : p) piece.box.add((double)q.x, (double)q.y);
        }
        piece.triCount = (uint32_t)(tri.size() / 2) - piece.triFirst;
        piece.lineCount = (uint32_t)(line.size() / 2) - piece.lineFirst;
        m->box.add(piece.box);
        m->pieces.push_back(piece);
        ++m->ownShapes;
    }
    masters_[id] = m;
    stack.push_back(id);
    for (auto& placed : library.layoutInstances(cell, view)) {
        const std::string mlib = placed.lib.empty() ? lib : placed.lib, mview = placed.view.empty() ? "layout" : placed.view;
        auto child = load(ws, mlib, placed.cell, mview, stack);
        for (int c = 0; c < placed.cols; ++c)
            for (int r = 0; r < placed.rows; ++r) {
                const Xform t = Xform::of(placed, c, r);
                const Box& b = child->box;
                if (b.empty()) continue;
                double x, y;
                for (const auto& [px, py] : {std::pair{b.x0, b.y0}, {b.x1, b.y0}, {b.x1, b.y1}, {b.x0, b.y1}}) {
                    t.apply(px, py, x, y);
                    m->box.add(x, y);
                }
            }
        m->children.push_back({child, placed});
    }
    stack.pop_back();
    return m;
}

Hierarchy::Hierarchy(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view) {
    std::vector<std::string> stack;
    top_ = load(ws, lib, cell, view, stack);
}

Hierarchy::~Hierarchy() = default;

Box Hierarchy::box() const { return top_->box; }
uint64_t Hierarchy::shapeCount() const { return top_->ownShapes; }
double Hierarchy::metersPerDbu() const { return top_->metersPerDbu; }

std::vector<std::string> Hierarchy::children() const {
    std::vector<std::string> out;
    for (const auto& c : top_->children)
        if (std::find(out.begin(), out.end(), c.master->name) == out.end()) out.push_back(c.master->name);
    return out;
}

Flat Hierarchy::flatten(int levels) const {
    Flat out;
    out.box = top_->box;
    out.originX = out.box.empty() ? 0 : std::floor((out.box.x0 + out.box.x1) / 2);
    out.originY = out.box.empty() ? 0 : std::floor((out.box.y0 + out.box.y1) / 2);
    out.masters.resize(masters_.size());
    for (const auto& [id, m] : masters_) {
        (void)id;
        out.masters[m->index] = m->name;
        out.unfilled += m->unfilled;
    }

    // First the layers, so shapes can index them in sorted order
    std::map<LayerKey, uint32_t> indexOf;
    for (const auto& [id, m] : masters_) {
        (void)id;
        for (const LayerKey& k : m->keys) indexOf.try_emplace(k, 0);
    }
    uint32_t n = 0;
    for (auto& [k, i] : indexOf) {
        i = n++;
        out.layers.emplace_back();
        out.layers.back().key = k;
    }

    const auto put = [&](const Master& m, const Xform& t, int64_t placedBy) {
        std::vector<uint32_t> map(m.keys.size());
        for (size_t k = 0; k < m.keys.size(); ++k) map[k] = indexOf[m.keys[k]];
        double x, y;
        auto relative = [&](double px, double py, std::vector<float>& to) {
            t.apply(px, py, x, y);
            to.push_back((float)(x - out.originX));
            to.push_back((float)(y - out.originY));
        };
        for (const auto& piece : m.pieces) {
            FlatLayer& l = out.layers[map[piece.layerIndex]];
            const auto& tri = m.tri[piece.layerIndex];
            const auto& line = m.line[piece.layerIndex];
            FlatShape s{};
            s.layer = map[piece.layerIndex];
            s.tri = (uint32_t)(l.tri.size() / 2);
            s.triCount = piece.triCount;
            s.master = m.index;
            s.object = placedBy ? -placedBy : piece.id;
            Box b;
            for (uint32_t v = 0; v < piece.triCount; ++v) {
                relative(tri[2 * (piece.triFirst + v)], tri[2 * (piece.triFirst + v) + 1], l.tri);
                b.add(l.tri[l.tri.size() - 2], l.tri.back());
            }
            for (uint32_t v = 0; v < piece.lineCount; ++v) {
                relative(line[2 * (piece.lineFirst + v)], line[2 * (piece.lineFirst + v) + 1], l.line);
                b.add(l.line[l.line.size() - 2], l.line.back());
            }
            s.x0 = (float)b.x0; s.y0 = (float)b.y0; s.x1 = (float)b.x1; s.y1 = (float)b.y1;
            out.shapes.push_back(s);
            ++l.shapes;
        }
        for (const auto& lb : m.labels) {
            t.apply(lb.x, lb.y, x, y);
            out.labels.push_back({(float)(x - out.originX), (float)(y - out.originY), map[lb.layerIndex], lb.text, placedBy ? -placedBy : lb.id});
        }
    };
    const std::function<void(const Master&, const Xform&, int, int64_t)> walk = [&](const Master& m, const Xform& t, int depth, int64_t placedBy) {
        put(m, t, placedBy);
        for (const auto& c : m.children)
            for (int col = 0; col < c.placed.cols; ++col)
                for (int row = 0; row < c.placed.rows; ++row) {
                    const Xform placed = Xform::of(c.placed, col, row).then(t);
                    ++out.instances;
                    if (depth < levels) {
                        walk(*c.master, placed, depth + 1, placedBy ? placedBy : c.placed.id);
                    } else if (!c.master->box.empty()) {
                        const Box& b = c.master->box;
                        Box w;
                        double x, y;
                        for (const auto& [px, py] : {std::pair{b.x0, b.y0}, {b.x1, b.y0}, {b.x1, b.y1}, {b.x0, b.y1}}) {
                            placed.apply(px, py, x, y);
                            w.add(x - out.originX, y - out.originY);
                        }
                        out.boxes.insert(out.boxes.end(), {(float)w.x0, (float)w.y0, (float)w.x1, (float)w.y1});
                    }
                }
    };
    walk(*top_, Xform{}, 0, 0);
    return out;
}

}  // namespace layout
