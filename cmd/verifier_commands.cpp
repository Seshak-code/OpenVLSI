// ADE Verifier: requirements in a verifier view, mapped to ADE outputs, checked against their histories (sim/verifier.h).
#include "cmd/registry.h"
#include "oadb/oadb.h"
#include "sim/ade.h"
#include "sim/verifier.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <filesystem>
#include <stdexcept>

namespace cmd {

namespace {

namespace fs = std::filesystem;

const QJsonObject kLib = param("string", "library name"), kCell = param("string", "cell name"), kView = param("string", "the verifier view");

std::string s(const QJsonObject& a, const char* k) { return a[k].toString().toStdString(); }

oadb::Library& verifier(Context& c, const QJsonObject& a) {
    oadb::Library& l = c.workspace.library(s(a, "library"));
    if (l.viewType(s(a, "cell"), s(a, "view")) != "verifier")
        throw std::invalid_argument(s(a, "library") + "/" + s(a, "cell") + "/" + s(a, "view") + " is not a verifier view");
    return l;
}

QJsonObject setupOf(oadb::Library& l, const QJsonObject& a) {
    const auto t = l.prop(s(a, "cell"), s(a, "view"), "requirements");
    return t ? QJsonDocument::fromJson(QByteArray::fromStdString(*t)).object() : QJsonObject{{"requirements", QJsonArray{}}};
}

void save(oadb::Library& l, const QJsonObject& a, const QJsonObject& setup) {
    sim::validateRequirements(setup);
    l.setProp(s(a, "cell"), s(a, "view"), "requirements", QJsonDocument(setup).toJson(QJsonDocument::Compact).toStdString());
}

}  // namespace

void addVerifierCommands(Registry& r) {
    r.add({"verifier_setup_get", "A verifier view's requirements, as they are stored.", object({{"library", kLib}, {"cell", kCell}, {"view", kView}}),
           [](Context& c, const QJsonObject& a) { return QJsonValue(setupOf(verifier(c, a), a)); }, true});
    r.add({"verifier_setup_set", "Replaces a verifier view's requirements (validated first; one undo step). See sim/verifier.h.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"setup", param("object", "the whole setup")}}),
           [](Context& c, const QJsonObject& a) {
               save(verifier(c, a), a, a["setup"].toObject());
               return a["setup"];
           }});
    r.add({"verifier_requirement_add", "Adds a requirement. min and max replace the mapped output's own spec; minYield asks a Monte Carlo history's share of samples to meet that spec.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"id", param("string", "its id, unique in the view")}, {"title", param("string", "what it asks")}},
                  {{"parent", param("string", "the id it belongs under")}, {"min", param("number", "required minimum")}, {"max", param("number", "required maximum")},
                   {"minYield", param("number", "required yield, 0 to 1")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = verifier(c, a);
               QJsonObject setup = setupOf(l, a);
               QJsonObject req{{"id", a["id"]}, {"title", a["title"]}, {"mappings", QJsonArray{}}};
               for (const char* k : {"parent", "min", "max", "minYield"})
                   if (a.contains(k)) req[k] = a[k];
               QJsonArray reqs = setup["requirements"].toArray();
               reqs.append(req);
               setup["requirements"] = reqs;
               save(l, a, setup);
               return a["id"].toVariant().toJsonValue();
           }});
    r.add({"verifier_requirement_remove", "Removes a requirement; one with children is refused.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"id", param("string", "its id")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = verifier(c, a);
               QJsonObject setup = setupOf(l, a);
               QJsonArray kept;
               bool found = false;
               for (const auto& v : setup["requirements"].toArray()) {
                   if (v.toObject()["parent"] == a["id"]) throw std::invalid_argument("requirement " + s(a, "id") + " has children");
                   if (v.toObject()["id"] == a["id"]) found = true;
                   else kept.append(v);
               }
               if (!found) throw std::invalid_argument("no requirement " + s(a, "id"));
               setup["requirements"] = kept;
               save(l, a, setup);
               return QJsonValue(kept.size());
           }});
    r.add({"verifier_map", "Maps a requirement to an output of an ADE view; history names one run, without it the latest counts.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"id", param("string", "the requirement")},
                   {"mapLibrary", param("string", "the ADE view's library")}, {"mapCell", param("string", "its cell")}, {"mapView", param("string", "its view")},
                   {"test", param("string", "the test")}, {"output", param("string", "the output")}},
                  {{"history", param("string", "a history, as Interactive.3")}}),
           [](Context& c, const QJsonObject& a) {
               oadb::Library& l = verifier(c, a);
               const std::string lib = s(a, "mapLibrary"), cell = s(a, "mapCell"), view = s(a, "mapView");
               if (c.workspace.library(lib).viewType(cell, view) != "maestro") throw std::invalid_argument(lib + "/" + cell + "/" + view + " is not an ADE (maestro) view");
               const auto st = c.workspace.library(lib).prop(cell, view, "setup");
               const QJsonObject ade = st ? QJsonDocument::fromJson(QByteArray::fromStdString(*st)).object() : QJsonObject{};
               bool has = false;
               for (const auto& o : ade["outputs"].toArray()) has = has || (o.toObject()["test"] == a["test"] && o.toObject()["name"] == a["output"]);
               if (!has) throw std::invalid_argument(lib + "/" + cell + "/" + view + " has no output " + s(a, "output") + " in test " + s(a, "test"));
               QJsonObject setup = setupOf(l, a);
               QJsonArray reqs = setup["requirements"].toArray();
               bool found = false;
               for (auto it = reqs.begin(); it != reqs.end(); ++it) {
                   QJsonObject q = it->toObject();
                   if (q["id"] != a["id"]) continue;
                   QJsonArray maps = q["mappings"].toArray();
                   QJsonObject m{{"library", a["mapLibrary"]}, {"cell", a["mapCell"]}, {"view", a["mapView"]}, {"test", a["test"]}, {"output", a["output"]}};
                   if (a.contains("history")) m["history"] = a["history"];
                   maps.append(m);
                   q["mappings"] = maps;
                   *it = q;
                   found = true;
               }
               if (!found) throw std::invalid_argument("no requirement " + s(a, "id"));
               setup["requirements"] = reqs;
               save(l, a, setup);
               return QJsonValue(true);
           }});
    r.add({"verifier_check", "Checks every requirement against its mapped histories: per requirement its status (pass, fail, error, stale, noSpec, notRun, unmapped) with each mapping's measured range, margin and derivation; overall how many are mapped and verified.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}}),
           [](Context& c, const QJsonObject& a) {
               const QJsonObject setup = setupOf(verifier(c, a), a);
               const fs::path sims = fs::absolute(fs::path(c.workspace.file()).parent_path() / "simulation");
               const auto historyOf = [&](const QJsonObject& m) {
                   const fs::path base = sims / s(m, "library") / s(m, "cell") / s(m, "view");
                   std::string name = m.contains("history") ? s(m, "history") : "";
                   if (name.empty() && fs::is_directory(base)) {
                       int latest = -1;
                       for (const auto& e : fs::directory_iterator(base)) {
                           const std::string n = e.path().filename().string();
                           if (e.is_directory() && n.rfind("Interactive.", 0) == 0) latest = std::max(latest, std::atoi(n.c_str() + 12));
                       }
                       if (latest >= 0) name = "Interactive." + std::to_string(latest);
                   }
                   if (name.empty() || !fs::is_directory(base / name)) return QJsonObject{};
                   return sim::readHistory((base / name).string());
               };
               const auto currentOf = [&](const QJsonObject& m) {
                   oadb::Library& l = c.workspace.library(s(m, "library"));
                   const auto st = l.viewType(s(m, "cell"), s(m, "view")) == "maestro" ? l.prop(s(m, "cell"), s(m, "view"), "setup") : std::nullopt;
                   return st ? QJsonDocument::fromJson(QByteArray::fromStdString(*st)).object() : QJsonObject{};
               };
               return QJsonValue(sim::evaluateRequirements(setup, historyOf, currentOf));
           },
           true});
}

}  // namespace cmd
