// ADE Explorer's commands: a maestro view's setup, its runs (waited for, or in the background) and their histories
#include "cmd/registry.h"
#include "oadb/oadb.h"
#include "sim/ade.h"

#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace cmd {

namespace {

namespace fs = std::filesystem;

std::string s(const QJsonObject& a, const char* key) { return a[key].toString().toStdString(); }

const QJsonObject kLib = param("string", "library name"), kCell = param("string", "cell name"),
                  kView = param("string", "the ADE (maestro) view");
const QJsonObject kHistory = param("string", "a history: Interactive.N");

oadb::Library& maestro(Context& c, const QJsonObject& a) {
    oadb::Library& l = c.workspace.library(s(a, "library"));
    if (l.viewType(s(a, "cell"), s(a, "view")) != "maestro")
        throw std::invalid_argument(s(a, "library") + "/" + s(a, "cell") + "/" + s(a, "view") + " is not an ADE (maestro) view");
    return l;
}
std::string root(Context& c) { return fs::absolute(fs::path(c.workspace.file()).parent_path() / "simulation").string(); }
fs::path base(Context& c, const QJsonObject& a) { return fs::path(root(c)) / s(a, "library") / s(a, "cell") / s(a, "view"); }
std::string historyDir(Context& c, const QJsonObject& a) {
    static const QRegularExpression name("^Interactive\\.[0-9]+$");
    if (!name.match(a["history"].toString()).hasMatch()) throw std::invalid_argument("a history is named Interactive.N");
    const fs::path d = base(c, a) / s(a, "history");
    if (!fs::is_directory(d)) throw std::invalid_argument("no history " + s(a, "history"));
    return d.string();
}
sim::Jobs& jobs(Context& c) {
    if (!c.jobs) c.jobs = std::make_shared<sim::Jobs>();
    return *c.jobs;
}

}  // namespace

void addAdeCommands(Registry& r) {
    r.add({"ade_setup_get", "Returns an ADE (maestro) view's setup: tests, variables, corners, outputs.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}}),
           [](Context& c, const QJsonObject& a) {
               const auto t = maestro(c, a).prop(s(a, "cell"), s(a, "view"), "setup");
               return QJsonValue(t ? QJsonDocument::fromJson(QByteArray::fromStdString(*t)).object() : QJsonObject());
           },
           true});
    r.add({"ade_setup_set", "Replaces an ADE view's setup (validated first; one undo step). See sim/ade.h for its fields.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"setup", param("object", "the whole setup")}}),
           [](Context& c, const QJsonObject& a) {
               auto& l = maestro(c, a);
               const QJsonObject setup = a["setup"].toObject();
               sim::validateSetup(setup);
               l.setProp(s(a, "cell"), s(a, "view"), "setup", QJsonDocument(setup).toJson(QJsonDocument::Compact).toStdString());
               return QJsonValue(setup);
           }});
    r.add({"ade_run", "Runs an ADE view's setup on TreeSPICE: every enabled test, corner and sweep point. Waits and returns the "
                      "history, or with wait false starts it in the background and returns its name (poll ade_history_get).",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}},
                  {{"wait", param("boolean", "wait for the results (default true)")}, {"timeout", param("integer", "seconds per point (default 600)")},
                   {"mode", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"corners", "montecarlo"}},
                                        {"description", "corners (the corners and sweeps; the default) or montecarlo (the setup's montecarlo section)"}}}}),
           [](Context& c, const QJsonObject& a) {
               maestro(c, a);
               const int timeout = a.contains("timeout") ? a["timeout"].toInt() : 600;
               if (timeout <= 0) throw std::invalid_argument("timeout: a positive number of seconds");
               auto job = std::make_shared<sim::Job>(sim::prepare(c.workspace, s(a, "library"), s(a, "cell"), s(a, "view"), root(c),
                                                                  a.contains("mode") ? s(a, "mode") : "corners"));
               if (!a.contains("wait") || a["wait"].toBool()) return QJsonValue(sim::execute(*job, c.simulator, std::chrono::seconds(timeout), nullptr));
               jobs(c).start(job->dir, [job, sim = c.simulator, timeout](const std::atomic<bool>& cancel) {
                   try {
                       sim::execute(*job, sim, std::chrono::seconds(timeout), &cancel);
                   } catch (const std::exception&) {  // a history it could not write: what is on disk stays the record
                   }
               });
               return QJsonValue(QJsonObject{{"name", job->name}, {"status", "running"}, {"points", (int)job->points.size()}});
           },
           true});
    r.add({"ade_stop", "Stops a background ADE run; its unfinished points are marked stopped.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"history", kHistory}}),
           [](Context& c, const QJsonObject& a) { return QJsonValue(jobs(c).cancel(historyDir(c, a))); }, true});
    r.add({"ade_history_list", "Lists an ADE view's histories, oldest first.", object({{"library", kLib}, {"cell", kCell}, {"view", kView}}),
           [](Context& c, const QJsonObject& a) {
               maestro(c, a);
               std::vector<int> ns;
               if (fs::is_directory(base(c, a)))
                   for (const auto& e : fs::directory_iterator(base(c, a))) {
                       const std::string n = e.path().filename().string();
                       if (e.is_directory() && n.rfind("Interactive.", 0) == 0) ns.push_back(std::atoi(n.c_str() + 12));
                   }
               std::sort(ns.begin(), ns.end());
               QJsonArray out;
               for (int n : ns) out.append("Interactive." + QString::number(n));
               return QJsonValue(out);
           },
           true});
    r.add({"ade_run_plan", "Runs several runs in order (each its own history: corners, then montecarlo, say); waits and returns the "
                           "histories, or with wait false runs them in the background and returns their names.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView},
                   {"modes", QJsonObject{{"type", "array"}, {"description", "the runs, in order"},
                                         {"items", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"corners", "montecarlo"}}}}}}},
                  {{"wait", param("boolean", "wait for the results (default true)")}, {"timeout", param("integer", "seconds per point (default 600)")}}),
           [](Context& c, const QJsonObject& a) {
               maestro(c, a);
               const int timeout = a.contains("timeout") ? a["timeout"].toInt() : 600;
               if (timeout <= 0) throw std::invalid_argument("timeout: a positive number of seconds");
               if (a["modes"].toArray().isEmpty()) throw std::invalid_argument("modes: at least one run");
               // every run prepared first (the database's thread), so a bad setup fails before anything runs
               auto runs = std::make_shared<std::vector<sim::Job>>();
               try {
                   for (const auto& m : a["modes"].toArray())
                       runs->push_back(sim::prepare(c.workspace, s(a, "library"), s(a, "cell"), s(a, "view"), root(c), m.toString().toStdString()));
               } catch (const std::exception& e) {  // the runs already prepared will not run: their histories say so
                   for (const auto& j : *runs) sim::abandon(j, std::string("the run plan stopped before it started: ") + e.what());
                   throw;
               }
               QJsonArray out;
               if (!a.contains("wait") || a["wait"].toBool()) {
                   for (const auto& j : *runs) out.append(sim::execute(j, c.simulator, std::chrono::seconds(timeout), nullptr));
                   return QJsonValue(out);
               }
               for (const auto& j : *runs) out.append(QJsonObject{{"name", j.name}, {"status", "running"}, {"points", (int)j.points.size()}});
               jobs(c).start(runs->front().dir, [runs, sim = c.simulator, timeout](const std::atomic<bool>& cancel) {
                   for (const auto& j : *runs) {
                       try {
                           sim::execute(j, sim, std::chrono::seconds(timeout), &cancel);
                       } catch (const std::exception&) {  // a history it could not write: what is on disk stays the record
                       }
                   }
               });
               return QJsonValue(out);
           },
           true});
    r.add({"ade_summary", "Assembler's summary of a history: per output its range, its spec's pass count and the worst point (with "
                          "its parameters and margin); per test the points passing every spec; a Monte Carlo history's yields.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"history", kHistory}}),
           [](Context& c, const QJsonObject& a) { return QJsonValue(sim::summary(sim::readHistory(historyDir(c, a)))); }, true});
    r.add({"ade_compare", "Compares two histories point by point: each output's change, specs that flipped, points only one has.",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"history", kHistory}, {"against", kHistory}}),
           [](Context& c, const QJsonObject& a) {
               QJsonObject other = a;
               other["history"] = a["against"];
               return QJsonValue(sim::compare(sim::readHistory(historyDir(c, a)), sim::readHistory(historyDir(c, other))));
           },
           true});
    r.add({"ade_history_get", "Returns a history: its status, and each point's corner, variables, status, run directory and outputs "
                              "(value, pass).",
           object({{"library", kLib}, {"cell", kCell}, {"view", kView}, {"history", kHistory}}),
           [](Context& c, const QJsonObject& a) { return QJsonValue(sim::readHistory(historyDir(c, a))); }, true});
}

}  // namespace cmd
