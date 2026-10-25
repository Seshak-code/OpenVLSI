// The design database's commands: what the Library Manager does, callable from the GUI, scripts and MCP alike
#include "cmd/registry.h"
#include "oadb/oadb.h"

#include <QJsonDocument>

#include <stdexcept>
#include <string>

namespace cmd {

namespace {

QJsonObject str(const QString& description) { return {{"type", "string"}, {"description", description}}; }

// An object schema whose listed properties are all required
QJsonObject params(std::initializer_list<std::pair<const char*, QJsonObject>> props) {
    QJsonObject p;
    QJsonArray required;
    for (const auto& [k, s] : props) {
        p[k] = s;
        required.append(k);
    }
    return {{"type", "object"}, {"properties", p}, {"required", required}};
}

std::string s(const QJsonObject& a, const char* key) { return a[key].toString().toStdString(); }
QString q(const std::string& v) { return QString::fromStdString(v); }

oadb::Library& lib(Context& c, const QJsonObject& a) { return c.workspace.library(s(a, "library")); }

const QJsonObject kLib = str("library name"), kCell = str("cell name"), kView = str("view name");

}  // namespace

void addCoreCommands(Registry& r) {
    r.add({"workspace_libraries", "Lists the workspace's libraries.", params({}), [](Context& c, const QJsonObject&) {
               QJsonArray out;
               for (const auto& n : c.workspace.libraries()) out.append(q(n));
               return QJsonValue(out);
           }});
    r.add({"library_create", "Creates a library file and defines it in the workspace.",
           params({{"library", kLib}, {"path", str("file path, relative to the workspace file")}}),
           [](Context& c, const QJsonObject& a) {
               c.workspace.createLibrary(s(a, "library"), s(a, "path"));
               return QJsonValue(a["library"]);
           }});
    r.add({"library_define", "Adds an existing library file to the workspace.",
           params({{"library", kLib}, {"path", str("file path, relative to the workspace file")}}),
           [](Context& c, const QJsonObject& a) {
               c.workspace.defineLibrary(s(a, "library"), s(a, "path"));
               return QJsonValue(a["library"]);
           }});
    r.add({"library_undefine", "Removes a library from the workspace (its file stays).", params({{"library", kLib}}),
           [](Context& c, const QJsonObject& a) {
               c.workspace.undefineLibrary(s(a, "library"));
               return QJsonValue(a["library"]);
           }});
    r.add({"library_export", "Returns the library as deterministic text (for diffs and review).", params({{"library", kLib}}),
           [](Context& c, const QJsonObject& a) { return QJsonValue(q(lib(c, a).exportText())); }});

    r.add({"cell_list", "Lists a library's cells.", params({{"library", kLib}}), [](Context& c, const QJsonObject& a) {
               QJsonArray out;
               for (const auto& n : lib(c, a).cells()) out.append(q(n));
               return QJsonValue(out);
           }});
    r.add({"cell_create", "Creates an empty cell.", params({{"library", kLib}, {"cell", kCell}}), [](Context& c, const QJsonObject& a) {
               lib(c, a).createCell(s(a, "cell"));
               return QJsonValue(a["cell"]);
           }});
    r.add({"cell_rename", "Renames a cell.", params({{"library", kLib}, {"cell", kCell}, {"to", str("new cell name")}}),
           [](Context& c, const QJsonObject& a) {
               lib(c, a).renameCell(s(a, "cell"), s(a, "to"));
               return QJsonValue(a["to"]);
           }});
    r.add({"cell_delete", "Deletes a cell with all its views.", params({{"library", kLib}, {"cell", kCell}}),
           [](Context& c, const QJsonObject& a) {
               lib(c, a).deleteCell(s(a, "cell"));
               return QJsonValue(a["cell"]);
           }});

    r.add({"view_list", "Lists a cell's views with their types.", params({{"library", kLib}, {"cell", kCell}}),
           [](Context& c, const QJsonObject& a) {
               QJsonArray out;
               for (const auto& v : lib(c, a).views(s(a, "cell"))) out.append(QJsonObject{{"view", q(v.name)}, {"type", q(v.type)}});
               return QJsonValue(out);
           }});
    r.add({"view_create", "Creates an empty view of a cell.",
           params({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"type", str("view type: schematic, symbol, layout, ...")}}),
           [](Context& c, const QJsonObject& a) {
               lib(c, a).createView(s(a, "cell"), s(a, "view"), s(a, "type"));
               return QJsonValue(a["view"]);
           }});
    r.add({"view_delete", "Deletes a view.", params({{"library", kLib}, {"cell", kCell}, {"view", kView}}),
           [](Context& c, const QJsonObject& a) {
               lib(c, a).deleteView(s(a, "cell"), s(a, "view"));
               return QJsonValue(a["view"]);
           }});

    r.add({"prop_set", "Sets a view's property to a JSON value (undoable).",
           params({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"key", str("property name")},
                   {"value", QJsonObject{{"description", "any JSON value"}}}}),
           [](Context& c, const QJsonObject& a) {
               const QJsonDocument d = a["value"].isObject() ? QJsonDocument(a["value"].toObject())
                                       : a["value"].isArray() ? QJsonDocument(a["value"].toArray())
                                                              : QJsonDocument(QJsonArray{a["value"]});  // a scalar: wrapped
               QByteArray text = d.toJson(QJsonDocument::Compact);
               if (!a["value"].isObject() && !a["value"].isArray()) text = text.mid(1, text.size() - 2);  // unwrapped again
               lib(c, a).setProp(s(a, "cell"), s(a, "view"), s(a, "key"), text.toStdString());
               return a["value"];
           }});
    r.add({"prop_get", "Returns a view's property.", params({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"key", str("property name")}}),
           [](Context& c, const QJsonObject& a) {
               const auto v = lib(c, a).prop(s(a, "cell"), s(a, "view"), s(a, "key"));
               if (!v) throw std::runtime_error("no property '" + s(a, "key") + "' on " + s(a, "cell") + "/" + s(a, "view"));
               const QByteArray wrapped = "[" + QByteArray::fromStdString(*v) + "]";
               return QJsonDocument::fromJson(wrapped).array().at(0);
           }});

    r.add({"edit_undo", "Undoes the library's last change; returns what it undid.", params({{"library", kLib}}),
           [](Context& c, const QJsonObject& a) {
               auto& l = lib(c, a);
               const std::string label = l.undoLabel();
               if (!l.undo()) throw std::runtime_error("nothing to undo in library " + l.name());
               return QJsonValue(q(label));
           }});
    r.add({"edit_redo", "Redoes the library's last undone change; returns what it redid.", params({{"library", kLib}}),
           [](Context& c, const QJsonObject& a) {
               auto& l = lib(c, a);
               const std::string label = l.redoLabel();
               if (!l.redo()) throw std::runtime_error("nothing to redo in library " + l.name());
               return QJsonValue(q(label));
           }});
}

}  // namespace cmd
