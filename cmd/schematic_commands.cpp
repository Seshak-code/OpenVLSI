// The schematic and symbol editors' commands: what their tools do, callable from the GUI, scripts and MCP alike
#include "cmd/registry.h"
#include "oadb/oadb.h"
#include "sch/schematic.h"

#include <QJsonDocument>

#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace cmd {

namespace {

std::string s(const QJsonObject& a, const char* key) { return a[key].toString().toStdString(); }
QString q(const std::string& v) { return QString::fromStdString(v); }
oadb::Library& lib(Context& c, const QJsonObject& a) { return c.workspace.library(s(a, "library")); }

const QJsonObject kLib = param("string", "library name"), kCell = param("string", "cell name"),
                  kView = param("string", "view name");
const QJsonObject kCoord = param("integer", "coordinate, in database units");
const QJsonObject kPoints = {{"type", "array"}, {"description", "points [[x, y], ...], in database units"},
                             {"items", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}}}}};
const QJsonObject kOrient = {{"type", "string"}, {"description", "orientation"},
                             {"enum", QJsonArray{"R0", "R90", "R180", "R270", "MX", "MY", "MXR90", "MYR90"}}};

oadb::Point point(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    if (a.size() != 2) throw std::invalid_argument("a point is [x, y]");
    return {(int64_t)a[0].toDouble(), (int64_t)a[1].toDouble()};
}
std::vector<oadb::Point> points(const QJsonValue& v) {
    std::vector<oadb::Point> out;
    for (const auto& p : v.toArray()) out.push_back(point(p));
    return out;
}
QJsonArray json(const std::vector<oadb::Point>& ps) {
    QJsonArray a;
    for (const auto& p : ps) a.append(QJsonArray{(double)p.x, (double)p.y});
    return a;
}
QJsonValue parsed(const std::string& text) {  // stored JSON text -> value (scalars included)
    return QJsonDocument::fromJson(QByteArray("[") + QByteArray::fromStdString(text) + "]").array().at(0);
}
std::string text(const QJsonValue& v) {
    const QByteArray a = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return a.mid(1, a.size() - 2).toStdString();
}

// The view's objects as JSON: what an editor (or an agent) reads back
QJsonObject viewJson(oadb::Library& l, const std::string& cell, const std::string& view) {
    QJsonArray pins, insts, wires, labels, shapes;
    for (const auto& p : l.pins(cell, view))
        pins.append(QJsonObject{{"id", (double)p.id}, {"name", q(p.name)}, {"direction", q(p.direction)}, {"at", json({p.at})[0]}});
    for (const auto& i : l.instances(cell, view)) {
        QJsonObject params;
        for (const auto& [k, v] : i.params) params[q(k)] = q(v);
        insts.append(QJsonObject{{"name", q(i.name)}, {"master", q(i.lib + "/" + i.cell + "/" + i.view)}, {"at", json({i.at})[0]},
                                 {"orient", q(i.orient)}, {"params", params}});
    }
    for (const auto& w : l.wires(cell, view)) wires.append(QJsonObject{{"id", (double)w.id}, {"points", json(w.points)}});
    for (const auto& b : l.labels(cell, view))
        labels.append(QJsonObject{{"id", (double)b.id}, {"name", q(b.name)}, {"at", json({b.at})[0]}});
    for (const auto& h : l.shapes(cell, view))
        shapes.append(QJsonObject{{"id", (double)h.id}, {"kind", q(h.kind)}, {"layer", q(h.layer)}, {"points", json(h.points)}, {"text", q(h.text)}});
    return {{"type", q(l.viewType(cell, view))}, {"pins", pins}, {"instances", insts}, {"wires", wires}, {"labels", labels}, {"shapes", shapes}};
}

}  // namespace

void addSchematicCommands(Registry& r) {
    r.add({"cell_prop_set", "Sets a cell property (CDF parameters, simulator info, instNamePrefix, ...) to a JSON value.",
           object({{"library", kLib}, {"cell", kCell}, {"key", param("string", "property name")}, {"value", param("", "any JSON value")}}),
           [](Context& c, const QJsonObject& a) {
               lib(c, a).setCellProp(s(a, "cell"), s(a, "key"), text(a["value"]));
               return a["value"];
           }});
    r.add({"cell_prop_get", "Returns a cell property.", object({{"library", kLib}, {"cell", kCell}, {"key", param("string", "property name")}}),
           [](Context& c, const QJsonObject& a) {
               const auto v = lib(c, a).cellProp(s(a, "cell"), s(a, "key"));
               if (!v) throw std::runtime_error("no property '" + s(a, "key") + "' on cell " + s(a, "cell"));
               return parsed(*v);
           }, true});

    r.add({"view_get", "Returns a schematic's or symbol's objects: pins, instances, wires, labels, shapes.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}}),
           [](Context& c, const QJsonObject& a) { return QJsonValue(viewJson(lib(c, a), s(a, "cell"), s(a, "view"))); }, true});
    r.add({"shape_add", "Adds a drawing shape (line, rect, polygon, ellipse, label) to a symbol or schematic; returns its id.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView},
                   {"kind", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"line", "rect", "polygon", "ellipse", "label"}}, {"description", "shape kind"}}},
                   {"points", kPoints}},
                  {{"layer", param("string", "drawing layer (default device)")}, {"text", param("string", "a label's text")}}),
           [](Context& c, const QJsonObject& a) {
               const std::string layer = a.contains("layer") ? s(a, "layer") : "device";
               return QJsonValue((double)lib(c, a).addShape(s(a, "cell"), s(a, "view"), {0, s(a, "kind"), layer, points(a["points"]), s(a, "text")}));
           }});
    r.add({"pin_add", "Adds a pin (a symbol's terminal or a schematic's port); returns its id.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"name", param("string", "pin name")},
                   {"direction", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"input", "output", "inputOutput"}}, {"description", "direction"}}},
                   {"x", kCoord}, {"y", kCoord}}),
           [](Context& c, const QJsonObject& a) {
               return QJsonValue((double)lib(c, a).addPin(s(a, "cell"), s(a, "view"),
                                                          {0, s(a, "name"), s(a, "direction"), {(int64_t)a["x"].toDouble(), (int64_t)a["y"].toDouble()}}));
           }});
    r.add({"instance_add", "Places an instance of a master symbol in a schematic; returns its name (by the master's "
                           "instNamePrefix, else I, and the next free number, unless given).",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"master_library", kLib}, {"master_cell", kCell},
                   {"x", kCoord}, {"y", kCoord}},
                  {{"master_view", param("string", "the master's view (default symbol)")}, {"orient", kOrient},
                   {"name", param("string", "instance name")}, {"params", param("object", "parameter values (strings)")}}),
           [](Context& c, const QJsonObject& a) {
               auto& l = lib(c, a);
               const std::string ml = s(a, "master_library"), mc = s(a, "master_cell"),
                                 mv = a.contains("master_view") ? s(a, "master_view") : "symbol";
               if (c.workspace.library(ml).viewType(mc, mv) != "symbol")
                   throw std::runtime_error(ml + "/" + mc + "/" + mv + " is not a symbol");
               std::string name = s(a, "name");
               if (name.empty()) {
                   std::string prefix = "I";
                   if (const auto p = c.workspace.library(ml).cellProp(mc, "instNamePrefix")) prefix = parsed(*p).toString().toStdString();
                   std::set<std::string> taken;
                   for (const auto& i : l.instances(s(a, "cell"), s(a, "view"))) taken.insert(i.name);
                   int k = 0;
                   do name = prefix + std::to_string(k++); while (taken.count(name));
               }
               std::map<std::string, std::string> params;
               const QJsonObject po = a["params"].toObject();
               for (auto it = po.begin(); it != po.end(); ++it) {
                   if (!it.value().isString()) throw std::invalid_argument("parameter " + it.key().toStdString() + ": a value is a string");
                   params[it.key().toStdString()] = it.value().toString().toStdString();
               }
               l.addInstance(s(a, "cell"), s(a, "view"), {0, name, ml, mc, mv, {(int64_t)a["x"].toDouble(), (int64_t)a["y"].toDouble()},
                                                           a.contains("orient") ? s(a, "orient") : "R0", params});
               return QJsonValue(q(name));
           }});
    r.add({"instance_move", "Moves (and optionally reorients) an instance.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"instance", param("string", "instance name")}, {"x", kCoord}, {"y", kCoord}},
                  {{"orient", kOrient}}),
           [](Context& c, const QJsonObject& a) {
               auto& l = lib(c, a);
               std::string orient = s(a, "orient");
               if (orient.empty())
                   for (const auto& i : l.instances(s(a, "cell"), s(a, "view"))) if (i.name == s(a, "instance")) orient = i.orient;
               if (orient.empty()) throw std::runtime_error("no instance " + s(a, "instance"));
               l.moveInstance(s(a, "cell"), s(a, "view"), s(a, "instance"), {(int64_t)a["x"].toDouble(), (int64_t)a["y"].toDouble()}, orient);
               return a["instance"];
           }});
    r.add({"instance_set_params", "Sets instance parameters (strings; \"\" removes one).",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"instance", param("string", "instance name")},
                   {"params", param("object", "parameter values (strings)")}}),
           [](Context& c, const QJsonObject& a) {
               std::map<std::string, std::string> params;
               const QJsonObject po = a["params"].toObject();
               for (auto it = po.begin(); it != po.end(); ++it) {
                   if (!it.value().isString()) throw std::invalid_argument("parameter " + it.key().toStdString() + ": a value is a string");
                   params[it.key().toStdString()] = it.value().toString().toStdString();
               }
               lib(c, a).setInstanceParams(s(a, "cell"), s(a, "view"), s(a, "instance"), params);
               return a["instance"];
           }});
    r.add({"wire_add", "Draws a wire (a polyline) in a schematic; returns its id.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"points", kPoints}}),
           [](Context& c, const QJsonObject& a) {
               return QJsonValue((double)lib(c, a).addWire(s(a, "cell"), s(a, "view"), {0, points(a["points"])}));
           }});
    r.add({"label_add", "Places a net label on a wire (or pin): it names that net; a name ending in ! is global. Returns its id.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"name", param("string", "net name")}, {"x", kCoord}, {"y", kCoord}}),
           [](Context& c, const QJsonObject& a) {
               return QJsonValue((double)lib(c, a).addLabel(s(a, "cell"), s(a, "view"),
                                                            {0, s(a, "name"), {(int64_t)a["x"].toDouble(), (int64_t)a["y"].toDouble()}}));
           }});
    r.add({"object_delete", "Deletes a schematic/symbol object: an instance by name, a shape, pin, wire or label by id.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView},
                   {"kind", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"shape", "pin", "instance", "wire", "label"}}, {"description", "object kind"}}},
                   {"key", param("", "the instance's name, or the object's id")}}),
           [](Context& c, const QJsonObject& a) {
               const QJsonValue k = a["key"];
               const std::string key = k.isString() ? k.toString().toStdString() : std::to_string((long long)k.toDouble());
               lib(c, a).deleteObject(s(a, "cell"), s(a, "view"), s(a, "kind"), key);
               return k;
           }});

    r.add({"schematic_check", "Checks a schematic's connectivity: returns its nets (with each instance pin and port), errors "
                              "(shorts, labels on nothing, missing masters) and warnings (floating pins, dangling wires, "
                              "a symbol whose pins differ).",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}}), [](Context& c, const QJsonObject& a) {
               const sch::Connectivity k = sch::extract(c.workspace, s(a, "library"), s(a, "cell"), s(a, "view"));
               QJsonArray nets, errors, warnings;
               for (const auto& n : k.nets) {
                   QJsonArray terms;
                   for (const auto& t : n.terminals)
                       terms.append(t.instance.empty() ? QJsonObject{{"pin", q(t.pin)}} : QJsonObject{{"instance", q(t.instance)}, {"pin", q(t.pin)}});
                   nets.append(QJsonObject{{"name", q(n.name)}, {"global", n.global}, {"terminals", terms}});
               }
               for (const auto& e : k.errors) errors.append(q(e));
               for (const auto& w : k.warnings) warnings.append(q(w));
               return QJsonValue(QJsonObject{{"ok", k.ok()}, {"nets", nets}, {"errors", errors}, {"warnings", warnings}});
           }, true});
    r.add({"symbol_generate", "Creates a symbol view from a schematic's pins (inputs left, outputs right, inout on top).",
           object({{"library", kLib}, {"cell", kCell}},
                  {{"from", param("string", "schematic view (default schematic)")}, {"to", param("string", "new symbol view (default symbol)")}}),
           [](Context& c, const QJsonObject& a) {
               const std::string from = a.contains("from") ? s(a, "from") : "schematic", to = a.contains("to") ? s(a, "to") : "symbol";
               sch::generateSymbol(lib(c, a), s(a, "cell"), from, to);
               return QJsonValue(q(to));
           }});
}

}  // namespace cmd
