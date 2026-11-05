// Layout data: stream import and export, the layer table, and what a layout holds (layout_info).
#include "cmd/registry.h"
#include "layout/flatten.h"
#include "layout/gds.h"
#include "layout/tech.h"
#include "oadb/oadb.h"

#include <QFile>
#include <QJsonDocument>
#include <QXmlStreamReader>

#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace cmd {

namespace {

// KLayout's layer properties: colour, name and visibility per layer/datatype, in the file's order
QJsonArray readLyp(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read " + path.toStdString());
    QXmlStreamReader x(&f);
    QJsonArray layers;
    QJsonObject current;
    bool in = false;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            const QString tag = x.name().toString();
            if (tag == "properties") { in = true; current = {}; }
            else if (in && (tag == "frame-color" || tag == "name" || tag == "source" || tag == "visible")) current[tag] = x.readElementText();
        } else if (x.isEndElement() && x.name() == QLatin1String("properties") && in) {
            in = false;
            // a source reads "40/0", or "name 40/0@1"
            QString source = current["source"].toString().section('@', 0, 0).section(' ', -1);
            const int layer = source.section('/', 0, 0).toInt(), datatype = source.section('/', 1, 1).toInt();
            if (!source.contains('/')) continue;  // a group or a wildcard: nothing a shape names
            layers.append(QJsonObject{{"layer", layer}, {"datatype", datatype}, {"name", current["name"].toString()},
                                      {"color", current["frame-color"].toString()}, {"visible", current["visible"].toString() != "false"}});
        }
    }
    if (x.hasError()) throw std::runtime_error(path.toStdString() + ": " + x.errorString().toStdString());
    if (layers.isEmpty()) throw std::runtime_error(path.toStdString() + " names no layers");
    return layers;
}

QJsonObject boxJson(const layout::Box& b, double metersPerDbu) {
    const double um = metersPerDbu * 1e6;
    if (b.empty()) return {};
    return {{"x0", b.x0 * um}, {"y0", b.y0 * um}, {"x1", b.x1 * um}, {"y1", b.y1 * um}, {"unit", "um"}};
}

const char* kView = "layout";

}  // namespace

QJsonObject importGds(oadb::Library& l, const std::string& file, const std::string& lyp) {
    const layout::GdsLibrary gds = layout::readGds(file);
    // Everything checked before anything is written: a refused import leaves the library as it was
    const QJsonArray layers = lyp.empty() ? QJsonArray{} : readLyp(QString::fromStdString(lyp));
    std::set<std::string> have, placed;
    for (const auto& cl : l.cells()) have.insert(cl);
    std::set<std::string> names;
    for (const auto& g : gds.cells) {
        if (!oadb::validName(g.name)) throw std::runtime_error("structure name '" + g.name + "' is not a valid cell name");
        if (!names.insert(g.name).second) throw std::runtime_error("structure " + g.name + " appears twice in the file");
        if (have.count(g.name))
            for (const auto& v : l.views(g.name))
                if (v.name == kView) throw std::runtime_error("cell " + g.name + " already has a layout view in " + l.name());
        for (const auto& i : g.instances) placed.insert(i.cell);
    }
    for (const auto& name : placed)
        if (!names.count(name)) throw std::runtime_error("the file places structure " + name + " but does not define it");
    qint64 shapes = 0, instances = 0;
    QJsonArray tops, cells;
    l.change("import " + file, [&] {
        for (const auto& g : gds.cells) {
            if (!have.count(g.name)) l.createCell(g.name);
            l.createView(g.name, kView, "layout");
            l.setProp(g.name, kView, "metersPerDbu", QString::number(gds.metersPerDbu, 'g', 17).toStdString());
            l.addLayoutObjects(g.name, kView, g.shapes, g.instances);
            shapes += (qint64)g.shapes.size();
            instances += (qint64)g.instances.size();
            cells.append(QString::fromStdString(g.name));
            if (!placed.count(g.name)) tops.append(QString::fromStdString(g.name));
        }
    });
    if (!layers.isEmpty()) l.setLibraryProp("layers", QJsonDocument(layers).toJson(QJsonDocument::Compact).toStdString());
    return {{"cells", cells.size()}, {"tops", tops}, {"shapes", (double)shapes}, {"instances", (double)instances},
            {"metersPerDbu", gds.metersPerDbu}, {"nodesDropped", gds.nodesDropped}};
}

void addLayoutCommands(Registry& r) {
    const QJsonObject lib = param("string", "library name"), cell = param("string", "cell name"), view = param("string", "the layout view");

    r.add({"layout_import_gds", "Reads a GDSII file into a library: a cell with a layout view per structure. Layers (a KLayout .lyp) name and colour its layers. Returns its cells, top cells and counts.",
           object({{"library", lib}, {"file", param("string", "the GDS file")}}, {{"layers", param("string", "a KLayout layer properties file (.lyp)")}}),
           [](Context& c, const QJsonObject& a) {
               return QJsonValue(importGds(c.workspace.library(a["library"].toString().toStdString()), a["file"].toString().toStdString(),
                                           a.contains("layers") ? a["layers"].toString().toStdString() : std::string()));
           }});

    r.add({"layout_export_gds", "Writes a layout view and everything it places to a GDSII file; returns the structures written.",
           object({{"library", lib}, {"cell", cell}, {"file", param("string", "the GDS file to write")}}, {{"view", view}}),
           [](Context& c, const QJsonObject& a) {
               const std::string libName = a["library"].toString().toStdString();
               layout::GdsLibrary gds;
               gds.name = libName;
               std::set<std::string> seen;
               std::function<void(const std::string&, const std::string&)> add = [&](const std::string& name, const std::string& v) {
                   if (!seen.insert(name).second) return;
                   oadb::Library& l = c.workspace.library(libName);
                   if (l.viewType(name, v) != "layout") throw std::runtime_error(name + "/" + v + " is not a layout view");
                   layout::GdsCell g;
                   g.name = name;
                   g.shapes = l.layoutShapes(name, v);
                   g.instances = l.layoutInstances(name, v);
                   if (const auto u = l.prop(name, v, "metersPerDbu")) gds.metersPerDbu = std::stod(*u);
                   for (const auto& i : g.instances) {
                       if (!i.lib.empty() && i.lib != libName) throw std::runtime_error(name + " places a master of library " + i.lib + ": GDS export follows one library");
                       add(i.cell, i.view.empty() ? kView : i.view);
                   }
                   gds.cells.push_back(std::move(g));
               };
               add(a["cell"].toString().toStdString(), a.contains("view") ? a["view"].toString().toStdString() : kView);
               gds.userPerDbu = gds.metersPerDbu / 1e-6;
               layout::writeGds(a["file"].toString().toStdString(), gds);
               return QJsonValue((int)gds.cells.size());
           },
           true});

    r.add({"layout_layers", "A library's layer table: layer, datatype, name, colour and whether it starts visible, from its import's .lyp or its process; empty when it has neither.",
           object({{"library", lib}}),
           [](Context& c, const QJsonObject& a) {
               const oadb::Library& l = c.workspace.library(a["library"].toString().toStdString());
               if (const auto t = l.libraryProp("layers")) return QJsonValue(QJsonDocument::fromJson(QByteArray::fromStdString(*t)).array());
               QJsonArray layers;
               if (const auto name = l.libraryProp("tech"))
                   if (const layout::Tech* tech = layout::findTech(QJsonDocument::fromJson(QByteArray::fromStdString("[" + *name + "]")).array()[0].toString().toStdString()))
                       for (const auto& k : tech->layers)
                           layers.append(QJsonObject{{"layer", k.layer}, {"datatype", k.datatype}, {"name", QString::fromStdString(k.name)}, {"color", QString::fromStdString(k.color)}, {"visible", true}});
               return QJsonValue(layers);
           },
           true});

    r.add({"layout_info", "What a layout holds through its hierarchy: bounding box in microns, shapes per layer, instances placed, the masters it places directly.",
           object({{"library", lib}, {"cell", cell}}, {{"view", view}}),
           [](Context& c, const QJsonObject& a) {
               const layout::Hierarchy h(c.workspace, a["library"].toString().toStdString(), a["cell"].toString().toStdString(),
                                         a.contains("view") ? a["view"].toString().toStdString() : kView);
               const layout::Flat flat = h.flatten();
               QJsonArray layers, children;
               qint64 total = 0;
               for (const auto& l : flat.layers) {
                   if (!l.shapes) continue;
                   layers.append(QJsonObject{{"layer", l.key.first}, {"datatype", l.key.second}, {"shapes", (double)l.shapes}});
                   total += (qint64)l.shapes;
               }
               for (const auto& m : h.children()) children.append(QString::fromStdString(m));
               return QJsonValue(QJsonObject{{"box", boxJson(flat.box, h.metersPerDbu())}, {"shapes", (double)total}, {"instances", (double)flat.instances},
                                             {"labels", (double)flat.labels.size()}, {"layers", layers}, {"children", children},
                                             {"metersPerDbu", h.metersPerDbu()}, {"unfilled", (double)flat.unfilled}});
           },
           true});
}

}  // namespace cmd
