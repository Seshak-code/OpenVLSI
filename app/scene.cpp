#include "app/scene.h"

#include "sch/schematic.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <cmath>
#include <functional>

namespace app {

namespace {

QPointF pt(const QJsonValue& v) { return {v.toArray()[0].toDouble(), v.toArray()[1].toDouble()}; }

Role roleOf(const QString& layer) { return layer == "pin" ? Role::Pin : layer == "annotate" ? Role::Annotate : Role::Device; }

// The direction a text grows from its point: Left +x, Right -x; lower +y, upper -y
QPoint growth(const QString& j) {
    return {j.endsWith("Left") ? 1 : j.endsWith("Right") ? -1 : 0, j.startsWith("lower") ? 1 : j.startsWith("upper") ? -1 : 0};
}
QString justifyOf(QPoint g) {
    return QString(g.y() > 0 ? "lower" : g.y() < 0 ? "upper" : "center") + (g.x() > 0 ? "Left" : g.x() < 0 ? "Right" : "Center");
}

double dist(QPointF p, QPointF a, QPointF b) {
    const QPointF d = b - a;
    const double len2 = QPointF::dotProduct(d, d);
    const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, d) / len2, 0.0, 1.0) : 0.0;
    const QPointF q = a + t * d - p;
    return std::hypot(q.x(), q.y());
}

QRectF boxOf(const Display& g) {
    // by hand: QRectF treats a zero-width box (a vertical wire's) as null
    QRectF box;
    bool first = true;
    auto add = [&](const QRectF& r) {
        box = first ? r : QRectF(QPointF(std::min(box.left(), r.left()), std::min(box.top(), r.top())),
                                 QPointF(std::max(box.right(), r.right()), std::max(box.bottom(), r.bottom())));
        first = false;
    };
    for (const auto& s : g.segs) add(QRectF(s.a, s.b).normalized());
    for (const auto& d : g.dots) add(QRectF(d.at.x() - d.half, d.at.y() - d.half, 2 * d.half, 2 * d.half));
    for (const auto& t : g.texts) add(textBox(t));
    return box;
}

void square(Display& g, QPointF c, double h, Role role) {
    const QPointF a(c.x() - h, c.y() - h), b(c.x() + h, c.y() - h), d(c.x() + h, c.y() + h), e(c.x() - h, c.y() + h);
    g.segs.insert(g.segs.end(), {{a, b, role}, {b, d, role}, {d, e, role}, {e, a, role}});
}

// A shape's geometry; `map` places its points (an instance's transform, or none)
template <class Map>
void shapeGeometry(Display& g, const QJsonObject& s, Map map, const QString& orient, const std::function<QString(QString)>& subst) {
    std::vector<QPointF> p;
    for (const auto& v : s["points"].toArray()) p.push_back(pt(v));
    const QString kind = s["kind"].toString();
    const Role role = roleOf(s["layer"].toString());
    auto line = [&](QPointF a, QPointF b) { g.segs.push_back({map(a), map(b), role}); };
    if (kind == "line" || kind == "polygon") {
        for (size_t k = 1; k < p.size(); ++k) line(p[k - 1], p[k]);
        if (kind == "polygon" && p.size() > 2) line(p.back(), p.front());
    } else if (kind == "rect" && p.size() == 2) {
        const QPointF a = p[0], c = p[1], b(c.x(), a.y()), d(a.x(), c.y());
        line(a, b), line(b, c), line(c, d), line(d, a);
    } else if (kind == "ellipse" && p.size() == 2) {
        const QPointF c = (p[0] + p[1]) / 2;
        const double rx = std::abs(p[1].x() - p[0].x()) / 2, ry = std::abs(p[1].y() - p[0].y()) / 2;
        constexpr int n = 48;
        for (int k = 0; k < n; ++k) {
            const double a0 = 2 * M_PI * k / n, a1 = 2 * M_PI * (k + 1) / n;
            line(c + QPointF(rx * std::cos(a0), ry * std::sin(a0)), c + QPointF(rx * std::cos(a1), ry * std::sin(a1)));
        }
    } else if (kind == "label" && p.size() == 1) {
        const QPoint gr = growth(s["justify"].toString());
        const oadb::Point t = sch::transform(orient.toStdString(), {gr.x(), gr.y()});
        g.texts.push_back({map(p[0]), subst(s["text"].toString()), justifyOf({(int)t.x, (int)t.y}), role});
    }
}

}  // namespace

QRectF textBox(const Text& t) {
    const double h = kTextHeight, w = 0.6 * h * std::max<qsizetype>(1, t.text.size());
    const QPoint g = growth(t.justify);
    const double x = t.at.x() - (g.x() > 0 ? 0 : g.x() < 0 ? w : w / 2), y = t.at.y() - (g.y() > 0 ? 0 : g.y() < 0 ? h : h / 2);
    return {x, y, w, h};
}

const QJsonObject* Scene::master(Session& session, const QString& lib, const QString& cell, const QString& view) {
    const QString key = lib + "/" + cell + "/" + view;
    auto it = masters_.find(key);
    if (it == masters_.end()) {
        const cmd::Result r = session.call("view_get", {{"library", lib}, {"cell", cell}, {"view", view}});
        it = masters_.emplace(key, r.ok ? r.value.toObject() : QJsonObject()).first;
    }
    return it->second.isEmpty() ? nullptr : &it->second;
}

Item Scene::placed(Session& session, const QString& masterLib, const QString& masterCell, const QString& masterView,
                   QPointF at, const QString& orient, const QString& name, const QJsonObject& params) {
    Item item{"instance", name, {}, {}};
    const QJsonObject* m = master(session, masterLib, masterCell, masterView);
    if (!m) {  // drawn where it is, so it can be found, moved or deleted
        square(item.geometry, at, 50, Role::Marker);
        item.geometry.texts.push_back({at, name + ": no " + masterLib + "/" + masterCell + "/" + masterView, "centerCenter", Role::Marker});
    } else {
        const std::string o = orient.toStdString();
        auto map = [&](QPointF p) {
            const oadb::Point t = sch::transform(o, {(int64_t)std::llround(p.x()), (int64_t)std::llround(p.y())});
            return QPointF(at.x() + t.x, at.y() + t.y);
        };
        static const QRegularExpression field(R"(\[@(\w+)\])");
        auto subst = [&](QString text) {
            QRegularExpressionMatch k;
            for (qsizetype from = 0; (k = field.match(text, from)).hasMatch();) {
                const QString f = k.captured(1);
                const QString v = f == "instanceName" ? name : f == "cellName" ? masterCell : f == "libName" ? masterLib
                                : f == "viewName" ? masterView : params.contains(f) ? f + "=" + params[f].toString() : k.captured(0);
                text.replace(k.capturedStart(), k.capturedLength(), v);
                from = k.capturedStart() + v.size();
            }
            return text;
        };
        for (const auto& s : (*m)["shapes"].toArray()) shapeGeometry(item.geometry, s.toObject(), map, orient, subst);
        for (const auto& p : (*m)["pins"].toArray()) square(item.geometry, map(pt(p.toObject()["at"])), 8, Role::Pin);
    }
    item.box = boxOf(item.geometry);
    return item;
}

QString Scene::load(Session& session, const QString& lib, const QString& cell, const QString& view) {
    masters_.clear();
    items_.clear();
    junctions_.clear();
    pinPoints_.clear();
    wireSegs_.clear();
    const cmd::Result r = session.call("view_get", {{"library", lib}, {"cell", cell}, {"view", view}});
    view_ = r.ok ? r.value.toObject() : QJsonObject();
    if (!r.ok) return r.error;
    const bool schematic = type() == "schematic";
    auto id = [](const QJsonObject& o) { return QString::number((qint64)o["id"].toDouble()); };

    for (const auto& v : view_["shapes"].toArray()) {
        Item it{"shape", id(v.toObject()), {}, {}};
        shapeGeometry(it.geometry, v.toObject(), [](QPointF p) { return p; }, "R0", [](QString t) { return t; });
        it.box = boxOf(it.geometry);
        items_.push_back(std::move(it));
    }
    for (const auto& v : view_["pins"].toArray()) {
        const QJsonObject p = v.toObject();
        const QPointF at = pt(p["at"]);
        Item it{"pin", id(p), {}, {}};
        square(it.geometry, at, schematic ? 15 : 8, Role::Pin);
        if (schematic) it.geometry.texts.push_back({at + QPointF(0, 25), p["name"].toString(), "lowerCenter", Role::Pin});
        it.box = boxOf(it.geometry);
        items_.push_back(std::move(it));
        pinPoints_.push_back(at);
    }
    for (const auto& v : view_["instances"].toArray()) {
        const QJsonObject i = v.toObject();
        const QStringList m = i["master"].toString().split('/');
        items_.push_back(placed(session, m.value(0), m.value(1), m.value(2), pt(i["at"]), i["orient"].toString(),
                                i["name"].toString(), i["params"].toObject()));
        if (const QJsonObject* mo = master(session, m.value(0), m.value(1), m.value(2)))
            for (const auto& p : (*mo)["pins"].toArray()) {
                const QPointF q = pt(p.toObject()["at"]);
                const oadb::Point t = sch::transform(i["orient"].toString().toStdString(), {(int64_t)q.x(), (int64_t)q.y()});
                pinPoints_.push_back(pt(i["at"]) + QPointF(t.x, t.y));
            }
    }
    std::vector<QPointF> ends;
    for (const auto& v : view_["wires"].toArray()) {
        Item it{"wire", id(v.toObject()), {}, {}};
        std::vector<QPointF> p;
        for (const auto& q : v.toObject()["points"].toArray()) p.push_back(pt(q));
        for (size_t k = 1; k < p.size(); ++k) {
            it.geometry.segs.push_back({p[k - 1], p[k], Role::Wire});
            wireSegs_.emplace_back(p[k - 1], p[k]);
        }
        if (!p.empty()) ends.insert(ends.end(), {p.front(), p.back()});
        it.box = boxOf(it.geometry);
        items_.push_back(std::move(it));
    }
    for (const auto& v : view_["labels"].toArray()) {
        Item it{"label", id(v.toObject()), {}, {}};
        it.geometry.texts.push_back({pt(v.toObject()["at"]), v.toObject()["name"].toString(), "lowerLeft", Role::Label});
        it.box = boxOf(it.geometry);
        items_.push_back(std::move(it));
    }

    // a junction dot where three or more connections meet at a wire's end (a T counts the through wire twice)
    // ponytail: O(ends × (ends + pins + segments)); a spatial index when schematics reach ~10^4 wires
    std::set<std::pair<double, double>> done;
    for (const QPointF e : ends) {
        if (!done.insert({e.x(), e.y()}).second) continue;
        int n = 0;
        for (const QPointF f : ends) n += f == e;
        for (const QPointF p : pinPoints_) n += p == e;
        for (const auto& [a, b] : wireSegs_) n += (a != e && b != e && dist(e, a, b) < 0.5) ? 2 : 0;
        if (n >= 3) junctions_.push_back({e, 10, Role::Wire});
    }
    return "";
}

QRectF Scene::bounds() const {
    if (items_.empty()) return {};
    QRectF box = items_[0].box;
    for (const auto& it : items_)
        box = QRectF(QPointF(std::min(box.left(), it.box.left()), std::min(box.top(), it.box.top())),
                     QPointF(std::max(box.right(), it.box.right()), std::max(box.bottom(), it.box.bottom())));
    return box;
}

int Scene::find(const QString& kind, const QString& key) const {
    for (size_t k = 0; k < items_.size(); ++k)
        if (items_[k].kind == kind && items_[k].key == key) return (int)k;
    return -1;
}

int Scene::hit(QPointF p, double tol) const {
    // the nearest drawn object within tol; an instance (picked anywhere in its box) only when nothing else is
    int best = -1;
    double bestDist = 0;
    bool bestInstance = true;
    for (size_t k = 0; k < items_.size(); ++k) {
        const Item& it = items_[k];
        if (!it.box.adjusted(-tol, -tol, tol, tol).contains(p)) continue;
        const bool instance = it.kind == "instance";
        double d = instance ? it.box.width() * it.box.height() : HUGE_VAL;  // instances: the smallest box
        if (!instance) {
            for (const auto& s : it.geometry.segs) d = std::min(d, dist(p, s.a, s.b));
            for (const auto& g : it.geometry.dots)
                d = std::min(d, std::max({0.0, std::abs(p.x() - g.at.x()) - g.half, std::abs(p.y() - g.at.y()) - g.half}));
            for (const auto& t : it.geometry.texts) {
                const QRectF b = textBox(t);
                d = std::min(d, std::hypot(std::max({0.0, b.left() - p.x(), p.x() - b.right()}), std::max({0.0, b.top() - p.y(), p.y() - b.bottom()})));
            }
            if (d > tol) continue;
        }
        if (best < 0 || (bestInstance && !instance) || (bestInstance == instance && d < bestDist)) {
            best = (int)k;
            bestDist = d;
            bestInstance = instance;
        }
    }
    return best;
}

std::vector<int> Scene::within(const QRectF& r) const {
    std::vector<int> out;
    const QRectF n = r.normalized();
    for (size_t k = 0; k < items_.size(); ++k) {
        const QRectF& b = items_[k].box;  // edges compared: QRectF::contains refuses a zero-width box
        if (n.left() <= b.left() && b.right() <= n.right() && n.top() <= b.top() && b.bottom() <= n.bottom()) out.push_back((int)k);
    }
    return out;
}

bool Scene::connects(QPointF p) const {
    for (const QPointF q : pinPoints_) if (q == p) return true;
    for (const auto& [a, b] : wireSegs_) if (dist(p, a, b) < 0.5) return true;
    return false;
}

void Scene::draw(Display& out, const std::set<int>& selected) const {
    for (size_t k = 0; k < items_.size(); ++k) {
        const bool sel = selected.count((int)k);
        for (auto s : items_[k].geometry.segs) { if (sel) s.role = Role::Selected; out.segs.push_back(s); }
        for (auto d : items_[k].geometry.dots) { if (sel) d.role = Role::Selected; out.dots.push_back(d); }
        for (auto t : items_[k].geometry.texts) { if (sel) t.role = Role::Selected; out.texts.push_back(t); }
    }
    out.dots.insert(out.dots.end(), junctions_.begin(), junctions_.end());
}

}  // namespace app
