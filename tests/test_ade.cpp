// test_ade: the calculator, and ADE runs on TreeSPICE through the commands. The calculator's measurements on known
// waveforms (a ramp, a single pole, an exponential settle); its errors. A setup of two tests × two corners × a swept
// variable: each point's netlist (.param, .temp, the corner's model section), its outputs against the analytic RC
// (bandwidth, rise time, dc), specs passing and failing, an output that cannot be computed; histories on disk,
// background runs and stopping one; and every malformed setup refused.
#include "cmd/registry.h"
#include "oadb/oadb.h"
#include "sim/calc.h"

#include <QJsonDocument>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}
static bool near(double a, double b, double rel) { return std::abs(a - b) <= rel * std::abs(b); }

int main() {
    namespace fs = std::filesystem;
    using cplx = std::complex<double>;

    // ── the calculator ──
    {
        bool si = sim::siNumber("1.5k") == 1500 && sim::siNumber("10meg") == 1e7 && sim::siNumber("2u") == 2e-6 &&
                  sim::siNumber("1e-9") == 1e-9 && sim::siNumber("1MEG") == 1e6 && sim::siNumber("5V") == 5 && sim::siNumber("3m") == 3e-3;
        for (const char* bad : {"abc", "1 k", "", "1.5k!"}) {
            try { sim::siNumber(bad); si = false; } catch (const std::runtime_error&) {}
        }
        check(si, "SPICE numbers: suffixes (meg before m), units ignored, anything else refused");

        sim::Wave ramp, pole, settle, peak;
        for (int k = 0; k <= 100; ++k) ramp.x.push_back(k * 0.1), ramp.y.push_back(k * 0.1);
        for (int k = 0; k <= 4000; ++k) {  // 100/(1 + jf/1k), 1 Hz .. 10 MHz
            const double f = std::pow(10.0, k / 4000.0 * 7);
            pole.x.push_back(f), pole.y.push_back(100.0 / cplx(1, f / 1e3));
        }
        for (int k = 0; k <= 10000; ++k) {
            const double t = k * 1e-3;
            settle.x.push_back(t), settle.y.push_back(1 - std::exp(-t));
            peak.x.push_back(t), peak.y.push_back(t < 1 ? 1.2 * t : t < 2 ? 1.2 - 0.2 * (t - 1) : 1.0);
        }
        auto lookup = [&](const std::string& kind, const std::string& name) {
            if (kind == "VT" && name == "a") return ramp;
            if (kind == "VF" && name == "h") return pole;
            if (kind == "VT" && name == "s") return settle;
            if (kind == "VT" && name == "p") return peak;
            throw std::runtime_error("no " + kind + "(" + name + ")");
        };
        auto val = [&](const std::string& e) { return sim::evaluate(e, lookup).y.at(0).real(); };
        check(near(val("cross(VT(a), 5)"), 5, 1e-12) && near(val("value(VT(a), 2.55)"), 2.55, 1e-12) && near(val("ymax(VT(a) * 2 + 1)"), 21, 1e-12) &&
                  near(val("2^3 - -1"), 9, 1e-12) &&
                  near(val("average(VT(a))"), 5, 1e-12) && near(val("riseTime(VT(a))"), 8, 1e-12) && near(val("slewRate(VT(a))"), 1, 1e-12) &&
                  near(val("delay(VT(a), 2, VT(a), 7)"), 5, 1e-12),
              "a ramp: cross, value, arithmetic, average, rise time, slew rate, delay");
        // 3 dB down exactly: 1 + (f/fp)² = 10^0.3, a hair under the pole
        check(near(val("bandwidth(VF(h))"), 1e3 * std::sqrt(std::pow(10, 0.3) - 1), 1e-4) && near(val("ugf(VF(h))"), 1e3 * std::sqrt(1e4 - 1), 2e-3) &&
                  near(val("phaseMargin(VF(h))"), 180 - std::atan(std::sqrt(1e4 - 1)) * 180 / M_PI, 2e-3) && near(val("dB20(value(VF(h), 1))"), 40, 1e-6),
              "a single pole: bandwidth (3 dB), ugf, phase margin 90.6°, dc gain 40 dB");
        check(near(val("settlingTime(VT(s), 1)"), std::log(1 / (0.01 * (1 - std::exp(-10.0)) + std::exp(-10.0))), 1e-3) &&
                  near(val("overshoot(VT(p))"), 20, 1e-9),
              "an exponential settles to 1% at ln(100); a 1.2 peak over 1 is 20% overshoot");
        auto refuses = [&](const std::string& e, const std::string& why) {
            try { sim::evaluate(e, lookup); } catch (const std::runtime_error& x) { return std::string(x.what()).find(why) != std::string::npos; }
            return false;
        };
        check(refuses("frob(1)", "no function frob") && refuses("VT(a) + VF(h)", "different scales") && refuses("cross(VT(a), 20)", "no crossing") &&
                  refuses("1 +", "ends early") && refuses("ymax(VF(h))", "complex") && refuses("VT(zz)", "no VT(zz)") &&
                  refuses("value(VT(a), 50)", "outside") && refuses("ymax(1)", "needs a waveform"),
              "the calculator's errors: unknown function, mixed scales, no crossing, syntax, complex, missing vector, range");
    }

    // ── ADE ──
    const fs::path dir = fs::temp_directory_path() / ("ade test " + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    cmd::Registry r;
    cmd::addCoreCommands(r);
    cmd::addSchematicCommands(r);
    cmd::addSimulationCommands(r);
    cmd::addAdeCommands(r);
    auto ws = oadb::Workspace::load((dir / "openvlsi.libs").string());
    cmd::Context ctx{ws, OPENVLSI_TREESPICE};
    auto call = [&](const char* name, QJsonObject a) { return r.call(ctx, name, a); };
    auto must = [&](const char* name, QJsonObject a) {
        auto res = call(name, a);
        if (!res.ok) check(false, std::string(name) + " must succeed: " + res.error.toStdString());
        return res.value;
    };
    auto error = [&](const char* name, QJsonObject a) { auto res = call(name, a); return res.ok ? std::string() : res.error.toStdString(); };
    auto has = [](const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; };

    must("library_create", {{"library", "sims"}, {"path", "sims.oalib"}});
    auto primitive = [&](const char* cell, const char* prefix, const char* tmpl, QJsonObject params) {
        must("cell_create", {{"library", "sims"}, {"cell", cell}});
        must("view_create", {{"library", "sims"}, {"cell", cell}, {"view", "symbol"}, {"type", "symbol"}});
        for (auto [n, y] : {std::pair{"PLUS", 100}, std::pair{"MINUS", -100}})
            must("pin_add", {{"library", "sims"}, {"cell", cell}, {"view", "symbol"}, {"name", n}, {"direction", "inputOutput"}, {"x", 0}, {"y", y}});
        must("cell_prop_set", {{"library", "sims"}, {"cell", cell}, {"key", "spice"}, {"value", QJsonObject{{"prefix", prefix}, {"template", tmpl}}}});
        must("cell_prop_set", {{"library", "sims"}, {"cell", cell}, {"key", "params"}, {"value", params}});
    };
    primitive("res", "R", "[|PLUS] [|MINUS] [@r]", {{"r", "1k"}});
    primitive("cap", "C", "[|PLUS] [|MINUS] [@c]", {{"c", "1p"}});
    primitive("vdc", "V", "[|PLUS] [|MINUS] dc [@dc] [@ac:ac %]", {{"dc", "0"}});
    primitive("vpulse", "V", "[|PLUS] [|MINUS] pulse(0 [@v2] 0 1n 1n 1 2)", {{"v2", "1"}});
    auto rc = [&](const char* cell, const char* source, QJsonObject sourceParams) {  // source - R(rval) - C(cval), nets in, out
        must("cell_create", {{"library", "sims"}, {"cell", cell}});
        must("view_create", {{"library", "sims"}, {"cell", cell}, {"view", "schematic"}, {"type", "schematic"}});
        struct I { const char* name; const char* master; int x; const char* minus; const char* plus; QJsonObject params; };
        for (const I& i : {I{"V0", source, 0, "gnd!", "in", sourceParams}, I{"R0", "res", 500, "out", "in", {{"r", "{rval}"}}},
                           I{"C0", "cap", 1000, "gnd!", "out", {{"c", "{cval}"}}}}) {
            must("instance_add", {{"library", "sims"}, {"cell", cell}, {"view", "schematic"}, {"master_library", "sims"}, {"master_cell", i.master},
                                  {"name", i.name}, {"x", i.x}, {"y", 0}, {"params", i.params}});
            for (auto [net, y] : {std::pair{i.minus, -100}, std::pair{i.plus, 100}})
                must("label_add", {{"library", "sims"}, {"cell", cell}, {"view", "schematic"}, {"name", net}, {"x", i.x}, {"y", y}});
        }
    };
    rc("rc", "vdc", {{"dc", "1"}, {"ac", "1"}});
    rc("step", "vpulse", {});
    std::ofstream(dir / "models.lib") << ".lib tt\n.param skew=1\n.endl tt\n.lib ss\n.param skew=2\n.endl ss\n";
    must("cell_create", {{"library", "sims"}, {"cell", "bench"}});
    must("view_create", {{"library", "sims"}, {"cell", "bench"}, {"view", "ade"}, {"type", "maestro"}});
    const QJsonObject view{{"library", "sims"}, {"cell", "bench"}, {"view", "ade"}};
    auto with = [](QJsonObject a, QJsonObject extra) { for (auto it = extra.begin(); it != extra.end(); ++it) a[it.key()] = it.value(); return a; };

    const QJsonObject setup{
        {"tests", QJsonArray{QJsonObject{{"name", "ac"}, {"design", "sims/rc/schematic"}, {"models", QJsonArray{QJsonObject{{"file", "models.lib"}, {"section", "tt"}}}},
                                         {"analyses", QJsonArray{QJsonObject{{"type", "op"}},
                                                                 QJsonObject{{"type", "ac"}, {"sweep", "dec"}, {"points", "200"}, {"start", "1k"}, {"stop", "100meg"}}}}},
                             QJsonObject{{"name", "tran"}, {"design", "sims/step/schematic"}, {"options", QJsonObject{{"reltol", "1e-4"}}},
                                         {"analyses", QJsonArray{QJsonObject{{"type", "tran"}, {"step", "10n"}, {"stop", "60u"}}}}}}},
        {"variables", QJsonObject{{"rval", "1k 2k"}, {"cval", "1n"}}},
        {"corners", QJsonArray{QJsonObject{{"name", "bigc"}, {"temperature", "85"}, {"variables", QJsonObject{{"cval", "2n"}}},
                                           {"models", QJsonArray{QJsonObject{{"file", "models.lib"}, {"section", "ss"}}}}},
                               QJsonObject{{"name", "off"}, {"enabled", false}}}},
        {"outputs", QJsonArray{QJsonObject{{"name", "bw"}, {"test", "ac"}, {"expression", "bandwidth(VF(out))"}},
                               QJsonObject{{"name", "dc"}, {"test", "ac"}, {"expression", "VOP(out)"}, {"min", "0.99"}, {"max", "1.01"}},
                               QJsonObject{{"name", "tr"}, {"test", "tran"}, {"expression", "riseTime(VT(out))"}, {"max", "5u"}},
                               QJsonObject{{"name", "wave"}, {"test", "tran"}, {"expression", "VT(out)"}},
                               QJsonObject{{"name", "broken"}, {"test", "ac"}, {"expression", "ymax(VT(out))"}}}}};
    must("ade_setup_set", with(view, {{"setup", setup}}));
    check(must("ade_setup_get", view).toObject() == setup, "the setup round-trips through the maestro view");

    const QJsonObject h = must("ade_run", view).toObject();
    const QJsonArray pts = h["points"].toArray();
    check(h["name"] == "Interactive.1" && h["status"] == "done" && pts.size() == 8, "a run: 2 tests × (Nominal, bigc; off disabled) × rval 1k, 2k = 8 points");
    bool allDone = true, specs = true, bws = true, trs = true, decks = true;
    int fails = 0;
    for (const auto& pv : pts) {
        const QJsonObject p = pv.toObject(), o = p["outputs"].toObject();
        allDone = allDone && p["status"] == "done";
        const double R = sim::siNumber(p["variables"].toObject()["rval"].toString().toStdString()),
                     C = sim::siNumber(p["variables"].toObject()["cval"].toString().toStdString());
        std::ifstream f(fs::path(p["dir"].toString().toStdString()) / "input.cir");
        const std::string deck((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const bool big = p["corner"] == "bigc";
        decks = decks && has(deck, ".temp " + std::string(big ? "85" : "27")) && has(deck, ".param cval=" + std::string(big ? "2n" : "1n")) &&
                (p["test"] == "tran" ? has(deck, ".options reltol=1e-4") : has(deck, std::string("models.lib\" ") + (big ? "ss" : "tt")));
        if (p["test"] == "ac") {
            bws = bws && near(o["bw"].toObject()["value"].toDouble(), std::sqrt(std::pow(10, 0.3) - 1) / (2 * M_PI * R * C), 1e-3);
            specs = specs && o["dc"].toObject()["pass"].toBool() && has(o["broken"].toObject()["error"].toString().toStdString(), "no Transient");
        } else {
            const double tr = o["tr"].toObject()["value"].toDouble();
            trs = trs && near(tr, std::log(9.0) * R * C, 1e-2) && o["wave"].toObject()["value"].toObject()["wave"].toInt() > 10;
            fails += !o["tr"].toObject()["pass"].toBool();
            specs = specs && o["tr"].toObject()["pass"].toBool() == (tr <= 5e-6);
        }
    }
    check(allDone && decks, "every point ran; its deck has the corner's .temp, the variables as .params, options and model section");
    check(bws, "the RC's 3 dB bandwidth is √(10^0.3 − 1)/(2πRC) at each point");
    check(trs, "the step's 10-90% rise time is ln 9 · RC, and VT(out) is a waveform");
    check(specs && fails == 1, "specs: dc passes everywhere; rise time < 5u fails only at 2k with 2n; a tran-only output in the ac test is an error");

    const QJsonObject bg = must("ade_run", with(view, {{"wait", false}})).toObject();
    QJsonObject bgh;
    for (int k = 0; k < 600 && (bgh = must("ade_history_get", with(view, {{"history", bg["name"]}})).toObject())["status"] == "running"; ++k)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check(bg["status"] == "running" && bg["name"] == "Interactive.2" && bgh["status"] == "done" && bgh["points"].toArray().size() == 8,
          "a background run comes back running, then its history says done");
    QJsonObject many = setup;
    many["variables"] = QJsonObject{{"rval", "1k 1.1k 1.2k 1.3k 1.4k 1.5k 1.6k 1.7k 1.8k 1.9k 2k 2.1k 2.2k 2.3k 2.4k 2.5k"}, {"cval", "1n"}};
    must("ade_setup_set", with(view, {{"setup", many}}));
    const QJsonObject st = must("ade_run", with(view, {{"wait", false}})).toObject();
    const bool stopped = must("ade_stop", with(view, {{"history", st["name"]}})).toBool();
    QJsonObject sth;
    for (int k = 0; k < 600 && (sth = must("ade_history_get", with(view, {{"history", st["name"]}})).toObject())["status"] == "running"; ++k)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int stoppedPoints = 0;
    for (const auto& p : sth["points"].toArray()) stoppedPoints += p.toObject()["status"] == "stopped";
    check(stopped && sth["status"] == "stopped" && stoppedPoints > 0, "ade_stop stops a background run (" + std::to_string(stoppedPoints) + " points stopped)");
    check(must("ade_history_list", view).toArray() == QJsonArray{"Interactive.1", "Interactive.2", "Interactive.3"}, "the histories, oldest first");
    check(has(error("ade_history_get", with(view, {{"history", "../../x"}})), "Interactive.N"), "a history name cannot leave the view's directory");
    check(call("edit_undo", {{"library", "sims"}}).ok && must("ade_setup_get", view).toObject() == setup, "undo restores the previous setup");

    // ── malformed setups ──
    auto refused = [&](const char* what, std::function<void(QJsonObject&)> spoil, const std::string& why) {
        QJsonObject s = setup;
        spoil(s);
        const std::string e = error("ade_setup_set", with(view, {{"setup", s}}));
        check(has(e, why), std::string(what) + " (" + e + ")");
    };
    auto test0 = [](QJsonObject& s, std::function<void(QJsonObject&)> f) {
        QJsonArray ts = s["tests"].toArray();
        QJsonObject t = ts[0].toObject();
        f(t);
        ts[0] = t;
        s["tests"] = ts;
    };
    refused("an unknown key", [](QJsonObject& s) { s["colour"] = "red"; }, "unknown key 'colour'");
    refused("an unknown analysis", [&](QJsonObject& s) { test0(s, [](QJsonObject& t) { t["analyses"] = QJsonArray{QJsonObject{{"type", "hb"}}}; }); }, "type 'hb'");
    refused("a tran without its stop", [&](QJsonObject& s) { test0(s, [](QJsonObject& t) { t["analyses"] = QJsonArray{QJsonObject{{"type", "tran"}, {"step", "1n"}}}; }); },
            "tran needs stop");
    refused("an ac sweep that is not dec/oct/lin", [&](QJsonObject& s) {
        test0(s, [](QJsonObject& t) { t["analyses"] = QJsonArray{QJsonObject{{"type", "ac"}, {"sweep", "log"}, {"points", "1"}, {"start", "1"}, {"stop", "2"}}}; });
    }, "'log' is not one of its choices");
    refused("a value hiding a second card", [&](QJsonObject& s) {
        test0(s, [](QJsonObject& t) { t["analyses"] = QJsonArray{QJsonObject{{"type", "tran"}, {"step", "1n"}, {"stop", "1u .include x"}}}; });
    }, "without spaces");
    refused("an output of a missing test", [](QJsonObject& s) {
        s["outputs"] = QJsonArray{QJsonObject{{"name", "x"}, {"test", "nope"}, {"expression", "1"}}};
    }, "'nope' is not a test");
    refused("a spec that is not a number", [](QJsonObject& s) {
        s["outputs"] = QJsonArray{QJsonObject{{"name", "x"}, {"test", "ac"}, {"expression", "1"}, {"min", "big"}}};
    }, "not a number");
    refused("a variable that is not a name", [](QJsonObject& s) { s["variables"] = QJsonObject{{"1x", "1"}}; }, "is not a name");
    refused("a corner named Nominal", [](QJsonObject& s) { s["corners"] = QJsonArray{QJsonObject{{"name", "Nominal"}}}; }, "not Nominal");
    refused("two tests of one name", [](QJsonObject& s) {
        QJsonArray ts = s["tests"].toArray();
        ts.append(ts[0]);
        s["tests"] = ts;
    }, "a unique name");
    QJsonObject missing = setup;
    test0(missing, [](QJsonObject& t) { t["models"] = QJsonArray{QJsonObject{{"file", "nowhere.lib"}, {"section", "tt"}}}; });
    must("ade_setup_set", with(view, {{"setup", missing}}));
    check(has(error("ade_run", view), "does not exist"), "a run with a missing model file is refused before it starts");
    check(has(error("ade_run", {{"library", "sims"}, {"cell", "rc"}, {"view", "schematic"}}), "not an ADE"), "ade_run needs a maestro view");

    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
