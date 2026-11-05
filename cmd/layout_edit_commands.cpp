// Layout editing as commands: shapes, instances, vias, pins and stretching; transistors and ties drawn from the process's
// rules; a layout generated from its schematic, and how much of it is wired.
#include "cmd/registry.h"
#include "layout/connectivity.h"
#include "layout/pcell.h"
#include "oadb/oadb.h"
#include "sch/schematic.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace cmd {

namespace {

const QJsonObject kLib = param("string", "library name"), kCell = param("string", "cell name"), kView = param("string", "the layout view (default \"layout\")");
const QJsonObject kLayerParam = param("string", "a layer: Metal1, Metal1.pin, or numbers 8/0");

std::string str(const QJsonObject& a, const char* k) { return a[k].toString().toStdString(); }
std::string viewOf(const QJsonObject& a) { return a.contains("view") ? str(a, "view") : "layout"; }
oadb::Library& libOf(Context& c, const QJsonObject& a) { return c.workspace.library(str(a, "library")); }
QString q(const std::string& s) { return QString::fromStdString(s); }

const layout::Tech* attached(const oadb::Library& l) {
    const auto name = l.libraryProp("tech");
    if (!name) return nullptr;
    const layout::Tech* t = layout::findTech(QJsonDocument::fromJson(QByteArray::fromStdString("[" + *name + "]")).array()[0].toString().toStdString());
    if (!t) throw std::runtime_error("library " + l.name() + " attaches an unknown process " + *name);
    return t;
}
const layout::Tech& needTech(const oadb::Library& l) {
    const layout::Tech* t = attached(l);
    if (!t) throw std::runtime_error("library " + l.name() + " has no process attached (layout_tech_set)");
    return *t;
}
layout::LayerKey layerOf(const oadb::Library& l, const std::string& name) {
    const auto k = layout::layerNamed(attached(l), name);
    if (!k) throw std::invalid_argument("no layer '" + name + "'" + (attached(l) ? " in " + attached(l)->name : ": without a process, give numbers, as 8/0"));
    return *k;
}
void needLayoutView(const oadb::Library& l, const std::string& cell, const std::string& view) {
    if (l.viewType(cell, view) != "layout") throw std::invalid_argument(cell + "/" + view + " is not a layout view");
}
int64_t whole(const QJsonValue& v, const char* what) {
    const double d = v.toDouble();
    if (d != std::floor(d) || std::abs(d) > 2e9) throw std::invalid_argument(std::string(what) + " must be a whole number of database units that fits GDS");
    return (int64_t)d;
}
oadb::Point pointOf(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    if (a.size() != 2) throw std::invalid_argument("a point is [x, y]");
    return {whole(a[0], "x"), whole(a[1], "y")};
}
std::vector<oadb::Point> pointsOf(const QJsonValue& v) {
    std::vector<oadb::Point> out;
    for (const auto& p : v.toArray()) out.push_back(pointOf(p));
    return out;
}
QJsonArray json(const std::vector<oadb::Point>& ps) {
    QJsonArray a;
    for (const auto& p : ps) a.append(QJsonArray{(double)p.x, (double)p.y});
    return a;
}
std::vector<int64_t> idsOf(const QJsonObject& a, const char* key) {
    std::vector<int64_t> out;
    for (const auto& v : a[key].toArray()) out.push_back(whole(v, "an object's id"));
    return out;
}
const QJsonObject kPointList = {{"type", "array"}, {"description", "points [[x, y], ...], in database units"},
                                {"items", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "number"}}}}}};
const QJsonObject kIdList = {{"type", "array"}, {"description", "object ids (layout_objects_get)"}, {"items", QJsonObject{{"type", "number"}}}};

// A master cell's layout view, generated once: the name holds every parameter, so the same size is the same master
std::string ensureMaster(oadb::Library& l, const layout::Generated& g, const std::string& name, const layout::Tech& t) {
    bool have = false;
    for (const auto& c : l.cells()) have = have || c == name;
    if (have) {
        for (const auto& v : l.views(name))
            if (v.name == "layout") return name;
    } else {
        l.createCell(name);
    }
    l.createView(name, "layout", "layout");
    l.setProp(name, "layout", "metersPerDbu", QString::number(t.metersPerDbu, 'g', 17).toStdString());
    QJsonObject terminals;
    for (const auto& [term, rects] : g.terminals) {
        QJsonArray list;
        for (const auto& [metal, r] : rects) list.append(QJsonArray{metal, (double)r.x0, (double)r.y0, (double)r.x1, (double)r.y1});
        terminals[q(term)] = list;
    }
    l.setProp(name, "layout", "terminals", QJsonDocument(terminals).toJson(QJsonDocument::Compact).toStdString());
    l.addLayoutObjects(name, "layout", g.shapes, {});
    return name;
}

std::string mosName(bool pmos, int64_t w, int64_t l, int ng) {
    return std::string(pmos ? "pmos" : "nmos") + "_w" + std::to_string(w) + "_l" + std::to_string(l) + "_ng" + std::to_string(ng);
}

// A SPICE value to nanometres on the grid: "1u", "0.13u", "740n", "1e-6"
int64_t nanometres(const std::string& text, const std::string& what, int64_t grid) {
    char* end = nullptr;
    double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) throw std::invalid_argument(what + " '" + text + "' is not a number (a parameter's value must be plain to draw it)");
    std::string unit(end);
    for (auto& ch : unit) ch = (char)std::tolower((unsigned char)ch);
    static const std::map<std::string, double> scale = {{"", 1}, {"m", 1e-3}, {"u", 1e-6}, {"n", 1e-9}, {"p", 1e-12}, {"um", 1e-6}, {"nm", 1e-9}};
    const auto it = scale.find(unit);
    if (it == scale.end()) throw std::invalid_argument(what + " '" + text + "' has an unknown unit");
    const double nm = v * it->second * 1e9;
    const auto rounded = (int64_t)std::llround(nm);
    if (std::abs(nm - (double)rounded) > 1e-6 || rounded % grid) throw std::invalid_argument(what + " " + text + " is off the " + std::to_string(grid) + " nm grid");
    return rounded;
}

}  // namespace

void addLayoutEditCommands(Registry& r) {
    r.add({"layout_tech_set", "Attaches a process to a library: its layers, metals, vias and generators become available to its layouts.",
           object({{"library", kLib}, {"tech", param("string", "the process (layout_tech_get lists them)")}}),
           [](Context& c, const QJsonObject& a) {
               if (!layout::findTech(str(a, "tech"))) throw std::invalid_argument("no process '" + str(a, "tech") + "'");
               libOf(c, a).setLibraryProp("tech", QJsonDocument(QJsonArray{a["tech"]}).toJson(QJsonDocument::Compact).mid(1).chopped(1).toStdString());
               return a["tech"].toVariant().toJsonValue();
           }});
    r.add({"layout_tech_get", "A library's process with its metals, vias and layers; without a library, the processes known.",
           object({}, {{"library", kLib}}),
           [](Context& c, const QJsonObject& a) {
               if (!a.contains("library")) {
                   QJsonArray names;
                   for (const auto& n : layout::techNames()) names.append(q(n));
                   return QJsonValue(names);
               }
               const layout::Tech& t = needTech(libOf(c, a));
               QJsonArray layers, metals, vias;
               for (const auto& l : t.layers) layers.append(QJsonObject{{"name", q(l.name)}, {"layer", l.layer}, {"datatype", l.datatype}, {"color", q(l.color)}});
               for (const auto& m : t.metals) metals.append(QJsonObject{{"name", q(m.name)}, {"minWidth", (double)m.minWidth}, {"minSpace", (double)m.minSpace}});
               for (const auto& v : t.vias) vias.append(QJsonObject{{"name", q(v.name)}, {"lower", q(t.metals[v.lower].name)}, {"upper", q(t.metals[v.upper].name)}, {"cut", (double)v.cut}});
               return QJsonValue(QJsonObject{{"name", q(t.name)}, {"grid", (double)t.grid}, {"layers", layers}, {"metals", metals}, {"vias", vias}});
           },
           true});

    r.add({"layout_shape_add", "Adds a shape to a layout view: rect (two corners), polygon, path (with width) or text; returns its id. Points are in database units.",
           object({{"library", kLib}, {"cell", kCell}, {"layer", kLayerParam}, {"kind", param("string", "rect, polygon, path or text")}, {"points", kPointList}},
                  {{"view", kView}, {"width", param("number", "a path's width")}, {"pathType", param("integer", "a path's ends: 0 flush, 1 round, 2 extended")},
                   {"text", param("string", "a text's string")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const std::string cell = str(a, "cell"), view = viewOf(a), kind = str(a, "kind");
               needLayoutView(l, cell, view);
               const layout::LayerKey key = layerOf(l, str(a, "layer"));
               std::vector<oadb::Point> p = pointsOf(a["points"]);
               oadb::LayoutShape s;
               if (kind == "rect") {
                   if (p.size() != 2 || p[0].x == p[1].x || p[0].y == p[1].y) throw std::invalid_argument("a rect is two corners with an area");
                   s = layout::rectShape(key, {std::min(p[0].x, p[1].x), std::min(p[0].y, p[1].y), std::max(p[0].x, p[1].x), std::max(p[0].y, p[1].y)});
               } else {
                   s.layer = key.first;
                   s.datatype = key.second;
                   s.kind = kind;
                   if (kind == "polygon") {
                       if (p.size() < 3) throw std::invalid_argument("a polygon needs three points");
                       if (!(p.front() == p.back())) p.push_back(p.front());
                   } else if (kind == "path") {
                       if (p.size() < 2) throw std::invalid_argument("a path needs two points");
                       s.width = whole(a["width"], "a path's width");
                       if (s.width <= 0) throw std::invalid_argument("a path needs a width");
                       s.pathType = a["pathType"].toInt(0);
                       if (s.pathType != 0 && s.pathType != 1 && s.pathType != 2) throw std::invalid_argument("pathType is 0, 1 or 2");
                   } else if (kind == "text") {
                       if (p.size() != 1) throw std::invalid_argument("a text has one point");
                       if (a["text"].toString().isEmpty()) throw std::invalid_argument("a text needs its string");
                       s.text = str(a, "text");
                   } else {
                       throw std::invalid_argument("no shape kind '" + kind + "' (rect, polygon, path, text)");
                   }
                   s.points = p;
               }
               return QJsonValue((double)l.addLayoutShape(cell, view, s));
           }});

    r.add({"layout_objects_get", "The objects a layout view holds itself (not through its instances): shapes and instances with their ids.",
           object({{"library", kLib}, {"cell", kCell}}, {{"view", kView}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const std::string cell = str(a, "cell"), view = viewOf(a);
               needLayoutView(l, cell, view);
               QJsonArray shapes, instances;
               for (const auto& s : l.layoutShapes(cell, view))
                   shapes.append(QJsonObject{{"id", (double)s.id}, {"layer", s.layer}, {"datatype", s.datatype}, {"kind", q(s.kind)}, {"points", json(s.points)},
                                             {"width", (double)s.width}, {"pathType", s.pathType}, {"text", q(s.text)}});
               for (const auto& i : l.layoutInstances(cell, view))
                   instances.append(QJsonObject{{"id", (double)i.id}, {"library", q(i.lib)}, {"cell", q(i.cell)}, {"view", q(i.view)}, {"at", QJsonArray{(double)i.at.x, (double)i.at.y}},
                                                {"angle", i.angle}, {"mirrorX", i.mirrorX}, {"cols", i.cols}, {"rows", i.rows}});
               return QJsonValue(QJsonObject{{"shapes", shapes}, {"instances", instances}});
           },
           true});

    r.add({"layout_instance_add", "Places a master in a layout view; returns the instance's id. A placement that would make a cell contain itself is refused.",
           object({{"library", kLib}, {"cell", kCell}, {"master", param("string", "the master cell")}, {"at", param("array", "where, [x, y] in database units")}},
                  {{"view", kView}, {"masterLibrary", param("string", "the master's library, when not this one")}, {"masterView", param("string", "the master's view")},
                   {"angle", param("number", "degrees counter-clockwise: 0, 90, 180 or 270")}, {"mirrorX", param("boolean", "mirrored about the x axis first")},
                   {"cols", param("integer", "array columns")}, {"rows", param("integer", "array rows")},
                   {"colStep", param("array", "column pitch, [dx, dy]")}, {"rowStep", param("array", "row pitch, [dx, dy]")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const std::string cell = str(a, "cell"), view = viewOf(a);
               needLayoutView(l, cell, view);
               oadb::LayoutInstance i;
               i.lib = a.contains("masterLibrary") ? str(a, "masterLibrary") : "";
               i.cell = str(a, "master");
               i.view = a.contains("masterView") ? str(a, "masterView") : "";
               i.at = pointOf(a["at"]);
               i.angle = a["angle"].toDouble(0);
               if (i.angle != 0 && i.angle != 90 && i.angle != 180 && i.angle != 270) throw std::invalid_argument("an instance's angle is 0, 90, 180 or 270");
               i.mirrorX = a["mirrorX"].toBool(false);
               i.cols = a["cols"].toInt(1);
               i.rows = a["rows"].toInt(1);
               if (i.cols < 1 || i.rows < 1) throw std::invalid_argument("an array has at least one column and row");
               if (a.contains("colStep")) i.colStep = pointOf(a["colStep"]);
               if (a.contains("rowStep")) i.rowStep = pointOf(a["rowStep"]);
               int64_t id = 0;
               // Loaded inside the change: a missing master or a cycle throws and rolls the placement back
               l.change("place " + i.cell + " in " + cell, [&] {
                   id = l.addLayoutInstance(cell, view, i);
                   layout::Hierarchy check(c.workspace, str(a, "library"), cell, view);
               });
               return QJsonValue((double)id);
           }});

    r.add({"layout_objects_move", "Moves shapes and instances of a layout view by dx, dy (one undo step).",
           object({{"library", kLib}, {"cell", kCell}, {"dx", param("number", "database units")}, {"dy", param("number", "database units")}},
                  {{"view", kView}, {"shapes", kIdList}, {"instances", kIdList}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const std::string cell = str(a, "cell"), view = viewOf(a);
               needLayoutView(l, cell, view);
               const auto shapes = idsOf(a, "shapes"), instances = idsOf(a, "instances");
               if (shapes.empty() && instances.empty()) throw std::invalid_argument("no objects given");
               l.moveLayoutObjects(cell, view, shapes, instances, whole(a["dx"], "dx"), whole(a["dy"], "dy"));
               return QJsonValue((int)(shapes.size() + instances.size()));
           }});
    r.add({"layout_objects_copy", "Copies shapes and instances of a layout view, offset by dx, dy; returns the new ids (one undo step).",
           object({{"library", kLib}, {"cell", kCell}, {"dx", param("number", "database units")}, {"dy", param("number", "database units")}},
                  {{"view", kView}, {"shapes", kIdList}, {"instances", kIdList}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const std::string cell = str(a, "cell"), view = viewOf(a);
               needLayoutView(l, cell, view);
               const auto ws = idsOf(a, "shapes"), wi = idsOf(a, "instances");
               if (ws.empty() && wi.empty()) throw std::invalid_argument("no objects given");
               const std::set<int64_t> sset(ws.begin(), ws.end()), iset(wi.begin(), wi.end());
               const int64_t dx = whole(a["dx"], "dx"), dy = whole(a["dy"], "dy");
               QJsonArray newShapes, newInstances;
               size_t found = 0;
               l.change("copy in " + cell, [&] {
                   for (auto s : l.layoutShapes(cell, view))
                       if (sset.count(s.id)) {
                           ++found;
                           for (auto& p : s.points) p = {p.x + dx, p.y + dy};
                           newShapes.append((double)l.addLayoutShape(cell, view, s));
                       }
                   for (auto i : l.layoutInstances(cell, view))
                       if (iset.count(i.id)) {
                           ++found;
                           i.at = {i.at.x + dx, i.at.y + dy};
                           newInstances.append((double)l.addLayoutInstance(cell, view, i));
                       }
                   if (found != sset.size() + iset.size()) throw std::invalid_argument("an id is not an object of this view");
               });
               return QJsonValue(QJsonObject{{"shapes", newShapes}, {"instances", newInstances}});
           }});
    r.add({"layout_objects_delete", "Deletes shapes and instances of a layout view (one undo step).",
           object({{"library", kLib}, {"cell", kCell}}, {{"view", kView}, {"shapes", kIdList}, {"instances", kIdList}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const std::string cell = str(a, "cell"), view = viewOf(a);
               needLayoutView(l, cell, view);
               const auto shapes = idsOf(a, "shapes"), instances = idsOf(a, "instances");
               if (shapes.empty() && instances.empty()) throw std::invalid_argument("no objects given");
               l.deleteLayoutObjects(cell, view, shapes, instances);
               return QJsonValue((int)(shapes.size() + instances.size()));
           }});
    r.add({"layout_stretch", "Moves the vertices of the given shapes that lie in a window by dx, dy: an edge or a corner, as Stretch does. A shape with no vertex in the window is an error.",
           object({{"library", kLib}, {"cell", kCell}, {"shapes", kIdList}, {"window", param("array", "x0, y0, x1, y1, database units")}, {"dx", param("number", "database units")}, {"dy", param("number", "database units")}},
                  {{"view", kView}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const std::string cell = str(a, "cell"), view = viewOf(a);
               needLayoutView(l, cell, view);
               const QJsonArray w = a["window"].toArray();
               if (w.size() != 4) throw std::invalid_argument("the window is x0, y0, x1, y1");
               const int64_t x0 = whole(w[0], "x0"), y0 = whole(w[1], "y0"), x1 = whole(w[2], "x1"), y1 = whole(w[3], "y1");
               const int64_t dx = whole(a["dx"], "dx"), dy = whole(a["dy"], "dy");
               const auto ids = idsOf(a, "shapes");
               const std::set<int64_t> wanted(ids.begin(), ids.end());
               if (wanted.empty()) throw std::invalid_argument("no shapes given");
               size_t done = 0;
               l.change("stretch in " + cell, [&] {
                   for (auto s : l.layoutShapes(cell, view)) {
                       if (!wanted.count(s.id)) continue;
                       if (s.kind == "text") throw std::invalid_argument("a text has no edges to stretch");
                       int moved = 0;
                       for (auto& p : s.points)
                           if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) {
                               p = {p.x + dx, p.y + dy};
                               ++moved;
                           }
                       if (!moved) throw std::invalid_argument("shape " + std::to_string(s.id) + " has no vertex in the window");
                       l.setLayoutShapePoints(cell, view, s.id, s.points);
                       ++done;
                   }
                   if (done != wanted.size()) throw std::invalid_argument("an id is not a shape of this view");
               });
               return QJsonValue((int)done);
           }});

    r.add({"layout_via_add", "Adds a via between a metal and the next one up at a point: its cuts and both metals' enclosures from the process's rules, as plain shapes. Returns their ids.",
           object({{"library", kLib}, {"cell", kCell}, {"from", param("string", "the lower metal, as Metal1")}, {"at", param("array", "the centre, [x, y]")}},
                  {{"view", kView}, {"cols", param("integer", "cut columns")}, {"rows", param("integer", "cut rows")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const std::string cell = str(a, "cell"), view = viewOf(a);
               needLayoutView(l, cell, view);
               const layout::Tech& t = needTech(l);
               const int metal = layout::metalIndex(t, str(a, "from"));
               if (metal < 0 || metal >= (int)t.vias.size()) throw std::invalid_argument(str(a, "from") + " has no via above it in " + t.name);
               const oadb::Point at = pointOf(a["at"]);
               QJsonArray ids;
               l.change("via in " + cell, [&] {
                   for (const auto& s : layout::viaShapes(t, metal, at.x, at.y, a["cols"].toInt(1), a["rows"].toInt(1))) ids.append((double)l.addLayoutShape(cell, view, s));
               });
               return QJsonValue(ids);
           }});
    r.add({"layout_pin_add", "Adds a pin: a metal rectangle with its pin shape and a label naming the net. Returns their ids.",
           object({{"library", kLib}, {"cell", kCell}, {"name", param("string", "the net")}, {"metal", param("string", "the metal, as Metal1")}, {"box", param("array", "x0, y0, x1, y1, database units")}},
                  {{"view", kView}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const std::string cell = str(a, "cell"), view = viewOf(a);
               needLayoutView(l, cell, view);
               const layout::Tech& t = needTech(l);
               const int m = layout::metalIndex(t, str(a, "metal"));
               if (m < 0) throw std::invalid_argument(str(a, "metal") + " is no metal of " + t.name);
               if (str(a, "name").empty()) throw std::invalid_argument("a pin needs its net's name");
               const QJsonArray b = a["box"].toArray();
               if (b.size() != 4) throw std::invalid_argument("the box is x0, y0, x1, y1");
               const layout::Rect r{whole(b[0], "x0"), whole(b[1], "y0"), whole(b[2], "x1"), whole(b[3], "y1")};
               if (r.x0 >= r.x1 || r.y0 >= r.y1) throw std::invalid_argument("the box is empty");
               const int layer = t.metals[m].layer;
               QJsonArray ids;
               l.change("pin " + str(a, "name") + " in " + cell, [&] {
                   ids.append((double)l.addLayoutShape(cell, view, layout::rectShape({layer, 0}, r)));
                   ids.append((double)l.addLayoutShape(cell, view, layout::rectShape({layer, t.pinDatatype}, r)));
                   oadb::LayoutShape text;
                   text.layer = layer;
                   text.datatype = t.textDatatype;
                   text.kind = "text";
                   text.points = {{(r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2}};
                   text.text = str(a, "name");
                   ids.append((double)l.addLayoutShape(cell, view, text));
               });
               return QJsonValue(ids);
           }});

    r.add({"layout_pcell_mos", "Generates a transistor master from the process's rules (once per size) and returns its cell's name.",
           object({{"library", kLib}, {"type", param("string", "nmos or pmos")}, {"w", param("number", "total width, database units")}, {"l", param("number", "gate length, database units")}},
                  {{"ng", param("integer", "fingers")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const layout::Tech& t = needTech(l);
               const std::string type = str(a, "type");
               if (type != "nmos" && type != "pmos") throw std::invalid_argument("a transistor is nmos or pmos");
               const int64_t w = whole(a["w"], "w"), len = whole(a["l"], "l");
               const int ng = a["ng"].toInt(1);
               const layout::Generated g = layout::mosCell(t, type == "pmos", w, len, ng);
               std::string name;
               l.change("generate " + type, [&] { name = ensureMaster(l, g, mosName(type == "pmos", w, len, ng), t); });
               return QJsonValue(q(name));
           }});
    r.add({"layout_pcell_tap", "Generates a body tie master (ptap in pSD, ntap in NWell) and returns its cell's name.",
           object({{"library", kLib}, {"type", param("string", "ptap or ntap")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const layout::Tech& t = needTech(l);
               const std::string type = str(a, "type");
               if (type != "ptap" && type != "ntap") throw std::invalid_argument("a tie is ptap or ntap");
               std::string name;
               l.change("generate " + type, [&] { name = ensureMaster(l, layout::tapCell(t, type == "ntap"), type, t); });
               return QJsonValue(q(name));
           }});

    r.add({"layout_generate", "Generates a layout view from a schematic: a master per transistor size, n-devices in one row and p-devices above, a tie per body net, a labelled pin per schematic pin, and each terminal's net recorded. The wiring is left to route; layout_connectivity tells what remains.",
           object({{"library", kLib}, {"cell", kCell}}, {{"view", kView}, {"schematic", param("string", "the schematic view (default \"schematic\")")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const layout::Tech& t = needTech(l);
               const std::string libName = str(a, "library"), cell = str(a, "cell"), view = viewOf(a);
               const std::string schematic = a.contains("schematic") ? str(a, "schematic") : "schematic";
               for (const auto& v : l.views(cell))
                   if (v.name == view) throw std::invalid_argument(cell + " already has a view " + view);
               const sch::Connectivity con = sch::extract(c.workspace, libName, cell, schematic);
               if (!con.ok()) throw std::invalid_argument("the schematic has errors: " + con.errors.front());

               struct Device { std::string name; bool pmos; int64_t w, len; int ng; std::map<std::string, std::string> nets; layout::Generated g; };
               std::vector<Device> devices;
               for (const auto& i : l.instances(cell, schematic)) {
                   const bool nmos = i.cell == "sg13_lv_nmos", pmos = i.cell == "sg13_lv_pmos";
                   if (!nmos && !pmos) {
                       if (i.cell == "vdc" || i.cell == "vsin" || i.cell == "vpulse" || i.cell == "gnd" || i.cell == "idc") continue;  // the testbench's: no mask
                       throw std::invalid_argument("no generator for instance " + i.name + " of " + i.cell + " (sg13_lv_nmos and sg13_lv_pmos, in a flat schematic)");
                   }
                   oadb::Library& master = c.workspace.library(i.lib);
                   const QJsonObject defaults = QJsonDocument::fromJson(QByteArray::fromStdString(master.cellProp(i.cell, "params").value_or("{}"))).object();
                   const auto value = [&](const char* p) {
                       const auto it = i.params.find(p);
                       return it != i.params.end() ? it->second : defaults[p].toVariant().toString().toStdString();
                   };
                   if (value("m") != "1") throw std::invalid_argument(i.name + " has m=" + value("m") + ": draw it as fingers (ng), or several instances");
                   Device d{i.name, pmos, nanometres(value("w"), i.name + "'s w", t.grid), nanometres(value("l"), i.name + "'s l", t.grid), std::atoi(value("ng").c_str()), {}, {}};
                   const auto nets = con.instanceNets.find(i.name);
                   if (nets == con.instanceNets.end()) throw std::invalid_argument(i.name + " has no connections");
                   d.nets = nets->second;
                   d.g = layout::mosCell(t, pmos, d.w, d.len, d.ng);
                   devices.push_back(std::move(d));
               }
               if (devices.empty()) throw std::invalid_argument(cell + "/" + schematic + " has no transistors to draw");

               QJsonObject placedNets;
               int pinCount = 0;
               const int64_t kGap = 1000;  // between devices: past NWell's spacing, pSD's and Activ's
               l.change("generate " + cell + "/" + view, [&] {
                   l.createView(cell, view, "layout");
                   l.setProp(cell, view, "metersPerDbu", QString::number(t.metersPerDbu, 'g', 17).toStdString());
                   // Two rows: n-devices from y = 0, p-devices above the tallest n-device
                   // Both rows share their columns: every origin is past the widest left extent, so a p and an n device line up
                   constexpr int64_t kMargin = 310;  // NWell's enclosure, the largest reach left of a cell's origin
                   int64_t rowTop = 0, x[2] = {0, 0}, nwellX0 = INT64_MAX, nwellX1 = INT64_MIN, nwellY0 = INT64_MAX, nwellY1 = INT64_MIN;
                   for (const auto& d : devices)
                       if (!d.pmos) rowTop = std::max(rowTop, layout::extentOf(d.g).y1);
                   const int64_t pY = rowTop + 1500;
                   std::map<std::pair<bool, std::string>, bool> tied;
                   const auto place = [&](const std::string& master, const layout::Generated& g, bool upper, const QJsonObject& nets) {
                       const layout::CellExtent e = layout::extentOf(g);
                       oadb::LayoutInstance i;
                       i.cell = master;
                       i.at = {x[upper] + kMargin, upper ? pY + kMargin : 0};
                       x[upper] += kMargin + e.x1 + kGap;
                       if (upper) {
                           nwellX0 = std::min(nwellX0, i.at.x + e.x0);
                           nwellX1 = std::max(nwellX1, i.at.x + e.x1);
                           nwellY0 = std::min(nwellY0, i.at.y + e.y0);
                           nwellY1 = std::max(nwellY1, i.at.y + e.y1);
                       }
                       const int64_t id = l.addLayoutInstance(cell, view, i);
                       placedNets[QString::number(id)] = nets;
                   };
                   for (const auto& d : devices) {
                       const std::string master = ensureMaster(l, d.g, mosName(d.pmos, d.w, d.len, d.ng), t);
                       QJsonObject nets;
                       for (const char* term : {"D", "G", "S"}) {
                           const auto n = d.nets.find(term);
                           if (n == d.nets.end()) throw std::invalid_argument(d.name + " has no net on " + term);
                           nets[term] = q(n->second);
                       }
                       place(master, d.g, d.pmos, QJsonObject{{"instance", q(d.name)}, {"nets", nets}});
                       const auto body = d.nets.find("B");
                       if (body == d.nets.end()) throw std::invalid_argument(d.name + " has no net on B");
                       tied[{d.pmos, body->second}] = true;
                   }
                   for (const auto& [key, unused] : tied) {
                       (void)unused;
                       const layout::Generated g = layout::tapCell(t, key.first);
                       const std::string master = ensureMaster(l, g, key.first ? "ntap" : "ptap", t);
                       place(master, g, key.first, QJsonObject{{"instance", q("tie " + key.second)}, {"nets", QJsonObject{{"B", q(key.second)}}}});
                   }
                   // One well across the p row: the devices share their tie (LU.a)
                   if (nwellX0 < nwellX1) l.addLayoutShape(cell, view, layout::rectShape({31, 0}, {nwellX0, nwellY0, nwellX1, nwellY1}));
                   // A pin for each schematic pin, in a column to the left of the rows
                   int64_t pinY = 0;
                   const int metal = 0;
                   for (const auto& p : l.pins(cell, schematic)) {
                       const layout::Rect box{-1200, pinY, -940, pinY + 350};
                       const int layer = t.metals[metal].layer;
                       l.addLayoutShape(cell, view, layout::rectShape({layer, 0}, box));
                       l.addLayoutShape(cell, view, layout::rectShape({layer, t.pinDatatype}, box));
                       oadb::LayoutShape text;
                       text.layer = layer;
                       text.datatype = t.textDatatype;
                       text.kind = "text";
                       text.points = {{(box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2}};
                       text.text = p.name;
                       l.addLayoutShape(cell, view, text);
                       ++pinCount;
                       pinY += 700;
                   }
                   l.setProp(cell, view, "connectivity", QJsonDocument(QJsonObject{{"schematic", q(schematic)}, {"instances", placedNets}}).toJson(QJsonDocument::Compact).toStdString());
               });
               return QJsonValue(QJsonObject{{"devices", (int)devices.size()}, {"pins", pinCount}, {"instances", placedNets.size()}});
           }});
    r.add({"layout_connectivity", "How much of a generated layout is wired: per net its terminals, separately wired groups and the flight lines still to route; and any nets the wiring shorts. Bounding boxes only: sign-off is LVS.",
           object({{"library", kLib}, {"cell", kCell}}, {{"view", kView}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = libOf(c, a);
               const layout::ConnectivityReport rep = layout::checkConnectivity(l, needTech(l), str(a, "cell"), viewOf(a));
               QJsonArray nets, shorts;
               for (const auto& n : rep.nets) {
                   QJsonArray lines;
                   for (const auto& f : n.flightLines) lines.append(QJsonArray{(double)f.x0, (double)f.y0, (double)f.x1, (double)f.y1});
                   nets.append(QJsonObject{{"net", q(n.name)}, {"terminals", n.terminals}, {"groups", n.groups}, {"flightLines", lines}});
               }
               for (const auto& [x, y] : rep.shorts) shorts.append(QJsonArray{q(x), q(y)});
               return QJsonValue(QJsonObject{{"nets", nets}, {"shorts", shorts}, {"clean", rep.clean()}});
           },
           true});
}

}  // namespace cmd
