// test_verifier: ADE Verifier. A requirement passes only on what a history measured: a real resistive divider through
// ade_run, then a spec it fails, a setup changed after the run, a stopped run and a missing mapping each withhold it.
#include "app/shell.h"
#include "app/verifier.h"
#include "sim/verifier.h"

#include <QApplication>
#include <QJsonArray>
#include <QSettings>
#include <QTreeWidget>

#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    QApplication::setOrganizationName("OpenVLSI-test");
    QSettings().clear();
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("verifier " + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::current_path(dir);

    // Refusals of the requirements themselves
    const auto refused = [](QJsonObject setup) {
        try {
            sim::validateRequirements(setup);
            return false;
        } catch (const std::exception&) {
            return true;
        }
    };
    check(refused({{"requirements", QJsonArray{QJsonObject{{"id", "a"}}, QJsonObject{{"id", "a"}}}}}), "a duplicate id is refused");
    check(refused({{"requirements", QJsonArray{QJsonObject{{"id", "a"}, {"parent", "b"}}}}}), "an unknown parent is refused");
    check(refused({{"requirements", QJsonArray{QJsonObject{{"id", "a"}, {"parent", "b"}}, QJsonObject{{"id", "b"}, {"parent", "a"}}}}}), "a cycle is refused");
    check(refused({{"requirements", QJsonArray{QJsonObject{{"id", "a"}, {"parent", "a"}}}}}), "its own parent is refused");
    check(refused({{"requirements", QJsonArray{QJsonObject{{"id", "a"}, {"min", 3}, {"max", 1}}}}}), "min above max is refused");
    check(refused({{"bogus", 1}}), "an unknown field is refused");

    {
        app::Theme::apply(qapp, app::Theme::Mode::Dark);
        app::Session session(QString::fromStdString((dir / "openvlsi.libs").string()));
        app::Shell shell(session);
        const auto must = [&](const QString& c, const QJsonObject& a) {
            const cmd::Result r = session.call(c, a);
            if (!r.ok) std::printf("  (%s: %s)\n", c.toUtf8().constData(), r.error.toUtf8().constData());
            return r;
        };
        must("library_create", {{"library", "l"}, {"path", "l.oalib"}});
        for (const auto& [c, prefix, tmpl] : {std::tuple{"res", "R", "[|p] [|n] [@r]"}, {"vdc", "V", "[|p] [|n] DC [@v]"}}) {
            must("cell_create", {{"library", "l"}, {"cell", c}});
            must("view_create", {{"library", "l"}, {"cell", c}, {"view", "symbol"}, {"type", "symbol"}});
            for (const auto& [n, y] : {std::pair{"p", 100}, {"n", -100}}) must("pin_add", {{"library", "l"}, {"cell", c}, {"view", "symbol"}, {"name", n}, {"direction", "inputOutput"}, {"x", 0}, {"y", y}});
            must("cell_prop_set", {{"library", "l"}, {"cell", c}, {"key", "spice"}, {"value", QJsonObject{{"prefix", prefix}, {"template", tmpl}}}});
        }
        must("cell_create", {{"library", "l"}, {"cell", "div"}});
        must("view_create", {{"library", "l"}, {"cell", "div"}, {"view", "schematic"}, {"type", "schematic"}});
        struct Inst { const char* name; const char* master; int x; const char* key; const char* value; const char* up; const char* down; };
        for (const Inst& i : {Inst{"V1", "vdc", 0, "v", "2", "in", "gnd!"}, Inst{"R1", "res", 400, "r", "1k", "in", "out"}, Inst{"R2", "res", 800, "r", "1k", "out", "gnd!"}}) {
            must("instance_add", {{"library", "l"}, {"cell", "div"}, {"view", "schematic"}, {"master_library", "l"}, {"master_cell", i.master}, {"name", i.name}, {"x", i.x}, {"y", 0},
                                  {"params", QJsonObject{{i.key, i.value}}}});
            must("label_add", {{"library", "l"}, {"cell", "div"}, {"view", "schematic"}, {"name", i.up}, {"x", i.x}, {"y", 100}});
            must("label_add", {{"library", "l"}, {"cell", "div"}, {"view", "schematic"}, {"name", i.down}, {"x", i.x}, {"y", -100}});
        }
        must("view_create", {{"library", "l"}, {"cell", "div"}, {"view", "ade"}, {"type", "maestro"}});
        const QJsonObject ade{{"library", "l"}, {"cell", "div"}, {"view", "ade"}};
        QJsonObject set = ade;
        const QJsonObject setup{{"tests", QJsonArray{QJsonObject{{"name", "op"}, {"design", "l/div/schematic"}, {"analyses", QJsonArray{QJsonObject{{"type", "op"}}}}}}},
                                {"outputs", QJsonArray{QJsonObject{{"test", "op"}, {"name", "half"}, {"expression", "VOP(out)"}, {"min", "0.9"}, {"max", "1.1"}},
                                                       QJsonObject{{"test", "op"}, {"name", "free"}, {"expression", "VOP(out)"}}}}};
        set["setup"] = setup;
        check(must("ade_setup_set", set).ok, "an ADE view of a real divider");

        must("view_create", {{"library", "l"}, {"cell", "div"}, {"view", "verify"}, {"type", "verifier"}});
        const QJsonObject where{{"library", "l"}, {"cell", "div"}, {"view", "verify"}};
        const auto with = [&](QJsonObject a) {
            for (auto it = where.begin(); it != where.end(); ++it) a[it.key()] = it.value();
            return a;
        };
        const auto checked = [&] { return session.call("verifier_check", where).value.toObject(); };
        const auto statusOf = [&](const QString& id) {
            for (const auto& q : checked()["requirements"].toArray())
                if (q.toObject()["id"] == id) return q.toObject()["status"].toString();
            return QString("(absent)");
        };
        check(must("verifier_requirement_add", with({{"id", "top"}, {"title", "The divider"}})).ok, "a top requirement");
        check(must("verifier_requirement_add", with({{"id", "gain"}, {"title", "Halves its input"}, {"parent", "top"}})).ok, "one under it, on the output's spec");
        check(must("verifier_requirement_add", with({{"id", "tight"}, {"title", "Above 1.5 V"}, {"parent", "top"}, {"min", 1.5}})).ok, "one with its own limit");
        check(must("verifier_requirement_add", with({{"id", "nospec"}, {"title", "Is measured"}, {"parent", "top"}})).ok, "one with no spec anywhere");
        check(!session.call("verifier_requirement_add", with({{"id", "gain"}, {"title", "again"}})).ok, "a repeated id is refused");
        check(statusOf("gain") == "unmapped" && statusOf("top") == "unmapped", "nothing is verified before it is mapped");
        const auto map = [&](const QString& id, const QString& output) {
            return session.call("verifier_map", with({{"id", id}, {"mapLibrary", "l"}, {"mapCell", "div"}, {"mapView", "ade"}, {"test", "op"}, {"output", output}}));
        };
        check(!map("gain", "absent").ok, "mapping to an output that does not exist is refused");
        check(map("gain", "half").ok && map("tight", "half").ok && map("nospec", "free").ok, "mapped");
        check(statusOf("gain") == "notRun" && statusOf("top") == "notRun", "mapped but never run is not verified");
        check(!checked()["complete"].toBool(), "and the whole is not complete");

        QJsonObject run = ade;
        run["wait"] = true;
        check(must("ade_run", run).ok, "ade_run, really");
        check(statusOf("gain") == "pass", "a measured 1 V inside its 0.9 to 1.1 spec proves it");
        check(statusOf("tight") == "fail", "a measured 1 V below its 1.5 V limit fails it");
        check(statusOf("nospec") == "noSpec", "a value with no spec proves nothing");
        check(statusOf("top") == "fail", "the parent takes its worst child");
        QJsonObject tightMap;
        for (const auto& q : checked()["requirements"].toArray())
            if (q.toObject()["id"] == "tight") tightMap = q.toObject()["mappings"].toArray()[0].toObject();
        check(std::abs(tightMap["margin"].toDouble() + 0.5) < 1e-6 && tightMap["derivation"].toString().contains("margin"), "with its margin, -0.5, and its derivation");
        const QJsonObject counts = checked()["counts"].toObject();
        check(checked()["leaves"].toInt() == 3 && checked()["verified"].toInt() == 1 && counts["fail"].toInt() == 1 && counts["noSpec"].toInt() == 1, "the counts are of the leaves");

        // The verifier's window, in a tab
        check(must("editor_open", where).ok, "editor_open on a verifier view");
        check(shell.tabTitles().contains("div verify"), "it opens as a tab");
        auto* v = shell.ciw()->verifier("l", "div", "verify");
        check(v && v->tree()->topLevelItemCount() == 1 && v->tree()->topLevelItem(0)->childCount() == 3, "the tree holds the hierarchy");
        check(v->status() == "1 of 3 verified: 1 fails, 1 no spec", "the status counts it: " + v->status().toStdString());
        v->tree()->setCurrentItem(v->tree()->topLevelItem(0)->child(1));
        check(v->derivation().contains("margin"), "selecting one shows what it was checked against");

        // A setup changed after the run: nothing it measured counts any more
        QJsonObject changed = setup;
        QJsonArray tests = changed["outputs"].toArray();
        QJsonObject out0 = tests[0].toObject();
        out0["max"] = "1.2";
        tests[0] = out0;
        changed["outputs"] = tests;
        set["setup"] = changed;
        check(must("ade_setup_set", set).ok, "the setup edited");
        check(statusOf("gain") == "stale", "the old history is out of date");
        check(v->status().contains("out of date"), "and the window follows");
        check(v->runUnverified() == 1, "Run What Is Unverified starts the one view");
        for (int k = 0; k < 600 && statusOf("gain") != "pass"; ++k) {
            qapp.processEvents();
            ::usleep(50000);
        }
        check(statusOf("gain") == "pass", "and its new run proves it again");
        session.call("edit_undo", {{"library", "l"}});
        check(statusOf("gain") == "stale", "undoing the edit leaves the setup differing from the newest run");

        check(session.call("verifier_requirement_remove", with({{"id", "top"}})).ok == false, "a requirement with children is not removed");
        check(session.call("verifier_requirement_remove", with({{"id", "tight"}})).ok, "a leaf is");
        check(!session.call("verifier_check", {{"library", "l"}, {"cell", "div"}, {"view", "ade"}}).ok, "verifier_check needs a verifier view");
    }
    QSettings().clear();
    fs::current_path(fs::temp_directory_path());
    fs::remove_all(dir);
    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
