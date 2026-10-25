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
#include <map>
#include <tuple>
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
        {
            std::vector<std::string> steps;
            sim::evaluate("bandwidth(VF(h))", lookup, &steps);
            std::string all;
            for (const auto& t : steps) all += t + "\n";
            const bool order = steps.size() >= 4 && steps[0].rfind("VF(h) = a waveform of 4001 points", 0) == 0 && steps[1].rfind("bandwidth(", 0) == 0;
            check(order && all.find("at the first point (1) is 40 dB; 3 dB below is 37 dB") != std::string::npos &&
                      all.find("the gain in dB: crosses 37 between (") != std::string::npos && all.find("linearly at 997.6") != std::string::npos,
                  "a derivation: the vector read, the function's result, the reference, the bracketing points, the interpolation\n" + all);
            steps.clear();
            sim::evaluate("riseTime(VT(a)) * 2", lookup, &steps);
            all.clear();
            for (const auto& t : steps) all += t + "\n";
            check(all.find("initial 0 (the first point), final 10 (the last): it rises by 10") != std::string::npos &&
                      all.find("10% of the transition: crosses 1 between") != std::string::npos && all.find("8 * 2 = 16") != std::string::npos,
                  "a rise time's derivation names its levels and crossings; arithmetic shows its operands\n" + all);
        }
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
    {
        const QJsonObject p = pts[0].toObject(), bw = p["outputs"].toObject()["bw"].toObject(), dc = p["outputs"].toObject()["dc"].toObject();
        const QJsonArray models = p["models"].toArray();
        check(p["design"] == "sims/rc/schematic" && models.size() == 1 && models[0].toString().endsWith("models.lib (tt)") &&
                  p["analyses"].toString().contains(".ac dec 200 1k 100meg") && bw["equation"].toString().startsWith("bandwidth(VF(out)) = ") &&
                  bw["steps"].toArray().size() >= 3 && dc["spec"].toString().contains("<=") && dc["spec"].toString().endsWith(": pass"),
              "a point records its design, models and analyses; an output its equation, derivation and spec verdict (" +
                  QJsonDocument(bw).toJson(QJsonDocument::Compact).left(400).toStdString() + ")");
    }
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
    // ── Monte Carlo: the RC's resistor drawn per sample (agauss, as a PDK's statistics would), seeded ──
    {
        QJsonObject mcSetup = setup;
        mcSetup["montecarlo"] = QJsonObject{{"samples", 60}, {"seed", 5}, {"variables", QJsonObject{{"rval", "agauss(1k,100,1)"}}}};
        must("ade_setup_set", with(view, {{"setup", mcSetup}}));
        const QJsonObject mc = must("ade_run", with(view, {{"mode", "montecarlo"}})).toObject();
        const QJsonArray mp = mc["points"].toArray();
        const QJsonObject bw = mc["statistics"].toObject()["bw"].toObject(), tr = mc["statistics"].toObject()["tr"].toObject();
        bool seeds = mp.size() == 120;
        for (const auto& pv : mp) {
            const QJsonObject p = pv.toObject();
            std::ifstream f(fs::path(p["dir"].toString().toStdString()) / "input.cir");
            const std::string deck((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            seeds = seeds && p["corner"] == "MC" && p["seed"].toInt() == 4 + p["sample"].toInt() && has(deck, "seed=" + std::to_string(p["seed"].toInt())) &&
                    has(deck, ".param rval=agauss(1k,100,1)");
        }
        check(mc["mode"] == "montecarlo" && seeds, "a Monte Carlo run: 60 samples per test, each its own seed (5 .. 64) in its deck");
        // bw = sqrt(10^0.3 - 1) / (2 pi R C) with R ~ N(1k, 100): mean near 158.8k, spread about 10%
        const double bw0 = std::sqrt(std::pow(10, 0.3) - 1) / (2 * M_PI * 1e3 * 1e-9);
        check(bw["n"].toInt() == 60 && near(bw["mean"].toDouble(), bw0, 0.05) && bw["sd"].toDouble() > 0.05 * bw0 && bw["sd"].toDouble() < 0.2 * bw0 &&
                  bw["min"].toDouble() < bw["mean"].toDouble() && bw["max"].toDouble() > bw["mean"].toDouble(),
              "statistics over the samples: the bandwidth's mean " + std::to_string(bw["mean"].toDouble()) + ", sd " + std::to_string(bw["sd"].toDouble()));
        check(tr["n"].toInt() == 60 && tr["pass"].toInt() + tr["fail"].toInt() == 60 && tr["yield"].toDouble() == tr["pass"].toDouble() / 60,
              "the rise time's yield against its spec (" + std::to_string(tr["pass"].toInt()) + " of 60 pass)");
        const QJsonObject again = must("ade_run", with(view, {{"mode", "montecarlo"}})).toObject();
        check(again["statistics"].toObject()["bw"].toObject()["mean"] == bw["mean"], "the same seed repeats the run exactly");
        mcSetup["montecarlo"] = QJsonObject{{"samples", 0}};
        check(has(error("ade_setup_set", with(view, {{"setup", mcSetup}})), "montecarlo.samples"), "zero samples are refused");
        mcSetup["montecarlo"] = QJsonObject{{"samples", 3}};
        must("ade_setup_set", with(view, {{"setup", mcSetup}}));
        check(has(error("ade_run", with(view, {{"mode", "montecarlo"}})), "no sweeps"), "Monte Carlo over a swept variable (rval 1k 2k) is refused");
        check(has(error("ade_run", with(view, {{"mode", "sideways"}})), "not one of"), "an unknown run mode is refused");
    }
    must("ade_setup_set", with(view, {{"setup", setup}}));
    must("edit_undo", {{"library", "sims"}});  // the Monte Carlo setups, undone again
    must("edit_undo", {{"library", "sims"}});
    must("edit_undo", {{"library", "sims"}});
    check(call("edit_undo", {{"library", "sims"}}).ok && must("ade_setup_get", view).toObject() == setup, "undo restores the previous setup");

    // ── Assembler: the summary and its worst points, comparing histories, test variables, run plans ──
    {
        const QJsonObject sum = must("ade_summary", with(view, {{"history", "Interactive.1"}})).toObject();
        auto entry = [&](const char* name) {
            for (const auto& e : sum["outputs"].toArray()) if (e.toObject()["output"] == name) return e.toObject();
            return QJsonObject();
        };
        const QJsonObject tr = entry("tr"), dc = entry("dc"), bw = entry("bw"), worst = tr["worst"].toObject();
        check(tr["n"] == 4 && tr["pass"] == 3 && tr["fail"] == 1 && worst["margin"].toDouble() < 0 && worst["corner"] == "bigc" &&
                  worst["variables"].toObject()["rval"] == "2k" && worst["derivation"].toString().startsWith("margin = 5u - 8.78"),
              "the summary names the worst point and how far past its spec it is (" + worst["derivation"].toString().toStdString() + ")");
        check(dc["pass"] == 4 && dc["worst"].toObject()["margin"].toDouble() > 0 && !bw.contains("worst") &&
                  bw["min_at"].toObject()["corner"] == "bigc" && bw["min_at"].toObject()["variables"].toObject()["rval"] == "2k" &&
                  bw["max_at"].toObject()["corner"] == "Nominal" && bw["max_at"].toObject()["variables"].toObject()["rval"] == "1k",
              "a spec met everywhere has a positive worst margin; an output with no spec has its min and max points");
        check(sum["tests"].toObject()["ac"] == QJsonObject{{"pass", 4}, {"points", 4}} && sum["tests"].toObject()["tran"] == QJsonObject{{"pass", 3}, {"points", 4}},
              "per test, the points that pass every spec");

        const QJsonObject same = must("ade_compare", with(view, {{"history", "Interactive.1"}, {"against", "Interactive.2"}})).toObject();
        bool zero = !same["rows"].toArray().isEmpty();
        for (const auto& r : same["rows"].toArray()) zero = zero && (!r.toObject().contains("change") || r.toObject()["change"].toDouble() == 0) && !r.toObject().contains("spec");
        check(zero && same["only_a"].toArray().isEmpty() && same["only_b"].toArray().isEmpty(), "two runs of one setup compare equal, point by point");

        must("instance_set_params", {{"library", "sims"}, {"cell", "step"}, {"view", "schematic"}, {"instance", "R0"}, {"params", QJsonObject{{"r", "{rval*1.5}"}}}});
        const QString changed = must("ade_run", view).toObject()["name"].toString();
        const QJsonObject diff = must("ade_compare", with(view, {{"history", "Interactive.1"}, {"against", changed}})).toObject();
        int flips = 0;
        bool scaled = true;
        for (const auto& r : diff["rows"].toArray()) {
            const QJsonObject row = r.toObject();
            if (row["output"] == "tr") scaled = scaled && near(row["relative"].toDouble(), 0.5, 1e-2);
            flips += row["spec"] == "now fails";
        }
        check(scaled && flips == 2, "after a 1.5x resistor the comparison shows each rise time 50% up and two specs now failing");
        must("edit_undo", {{"library", "sims"}});

        QJsonObject tv = setup;
        QJsonArray ts = tv["tests"].toArray();
        QJsonObject tran = ts[1].toObject();
        tran["variables"] = QJsonObject{{"cval", "3n"}};
        ts[1] = tran;
        tv["tests"] = ts;
        must("ade_setup_set", with(view, {{"setup", tv}}));
        bool own = true;
        for (const auto& pv : must("ade_run", view).toObject()["points"].toArray()) {
            const QJsonObject p = pv.toObject();
            const QString c = p["variables"].toObject()["cval"].toString();
            own = own && (p["test"] == "tran" ? c == (p["corner"] == "bigc" ? "2n" : "3n") : c == (p["corner"] == "bigc" ? "2n" : "1n"));
        }
        check(own, "a test's own variables win over the setup's (and a corner's over both)");

        check(has(error("ade_run_plan", with(view, {{"modes", QJsonArray{"corners", "montecarlo"}}})), "montecarlo section"), "a run plan with a run it cannot do is refused");
        const QJsonArray names = must("ade_history_list", view).toArray();
        const QJsonObject left = must("ade_history_get", with(view, {{"history", names.last()}})).toObject();
        check(left["status"] == "stopped" && left["error"].toString().contains("run plan stopped"), "and the run it had prepared says it was stopped, and why");
        tv["montecarlo"] = QJsonObject{{"samples", 4}, {"variables", QJsonObject{{"rval", "agauss(1k,100,1)"}}}};
        must("ade_setup_set", with(view, {{"setup", tv}}));
        const QJsonArray plan = must("ade_run_plan", with(view, {{"modes", QJsonArray{"corners", "montecarlo"}}})).toArray();
        check(plan.size() == 2 && plan[0].toObject()["mode"] == "corners" && plan[1].toObject()["mode"] == "montecarlo" &&
                  plan[0].toObject()["status"] == "done" && plan[1].toObject()["status"] == "done" &&
                  must("ade_summary", with(view, {{"history", plan[1].toObject()["name"]}})).toObject().contains("yield"),
              "a run plan runs corners, then Monte Carlo, each its own history; the Monte Carlo summary has its yield");
        must("ade_setup_set", with(view, {{"setup", setup}}));
    }

    // ── stb: a two-pole loop (an inverting gain of 1000, poles at 1 kHz and 100 kHz), its margins against the analytic ──
    {
        must("cell_create", {{"library", "sims"}, {"cell", "vcvs"}});
        must("view_create", {{"library", "sims"}, {"cell", "vcvs"}, {"view", "symbol"}, {"type", "symbol"}});
        for (auto [n, x, y] : {std::tuple{"PLUS", 0, 100}, std::tuple{"MINUS", 0, -100}, std::tuple{"NCP", -100, 50}, std::tuple{"NCN", -100, -50}})
            must("pin_add", {{"library", "sims"}, {"cell", "vcvs"}, {"view", "symbol"}, {"name", n}, {"direction", "inputOutput"}, {"x", x}, {"y", y}});
        must("cell_prop_set", {{"library", "sims"}, {"cell", "vcvs"}, {"key", "spice"}, {"value", QJsonObject{{"prefix", "E"}, {"template", "[|PLUS] [|MINUS] [|NCP] [|NCN] [@gain]"}}}});
        must("cell_create", {{"library", "sims"}, {"cell", "loop2"}});
        must("view_create", {{"library", "sims"}, {"cell", "loop2"}, {"view", "schematic"}, {"type", "schematic"}});
        struct I { const char* name; const char* master; std::map<std::string, const char*> nets; QJsonObject params; };
        const std::vector<I> parts = {
            {"VPRB", "vdc", {{"PLUS", "x"}, {"MINUS", "y"}}, {{"dc", "0"}}},
            {"E1", "vcvs", {{"PLUS", "a"}, {"MINUS", "gnd!"}, {"NCP", "gnd!"}, {"NCN", "x"}}, {{"gain", "1000"}}},
            {"R1", "res", {{"PLUS", "a"}, {"MINUS", "b"}}, {{"r", "1k"}}},
            {"C1", "cap", {{"PLUS", "b"}, {"MINUS", "gnd!"}}, {{"c", "159.155n"}}},
            {"E2", "vcvs", {{"PLUS", "c"}, {"MINUS", "gnd!"}, {"NCP", "b"}, {"NCN", "gnd!"}}, {{"gain", "1"}}},
            {"R2", "res", {{"PLUS", "c"}, {"MINUS", "y"}}, {{"r", "1k"}}},
            {"C2", "cap", {{"PLUS", "y"}, {"MINUS", "gnd!"}}, {{"c", "1.59155n"}}}};
        int x0 = 0;
        for (const I& i : parts) {
            must("instance_add", {{"library", "sims"}, {"cell", "loop2"}, {"view", "schematic"}, {"master_library", "sims"}, {"master_cell", i.master},
                                  {"name", i.name}, {"x", x0}, {"y", 0}, {"params", i.params}});
            for (const auto& p : must("view_get", {{"library", "sims"}, {"cell", i.master}, {"view", "symbol"}}).toObject()["pins"].toArray()) {
                const QJsonArray at = p.toObject()["at"].toArray();
                must("label_add", {{"library", "sims"}, {"cell", "loop2"}, {"view", "schematic"}, {"name", i.nets.at(p.toObject()["name"].toString().toStdString())},
                                   {"x", x0 + at[0].toInt()}, {"y", at[1].toInt()}});
            }
            x0 += 500;
        }
        must("view_create", {{"library", "sims"}, {"cell", "loop2"}, {"view", "ade"}, {"type", "maestro"}});
        const QJsonObject sv{{"library", "sims"}, {"cell", "loop2"}, {"view", "ade"}};
        must("ade_setup_set", with(sv, {{"setup", QJsonObject{
            {"tests", QJsonArray{QJsonObject{{"name", "loop"}, {"design", "sims/loop2/schematic"},
                                             {"analyses", QJsonArray{QJsonObject{{"type", "stb"}, {"sweep", "dec"}, {"points", "200"}, {"start", "1"},
                                                                                 {"stop", "1g"}, {"probe", "VPRB"}}}}}}},
            {"outputs", QJsonArray{QJsonObject{{"name", "pm"}, {"test", "loop"}, {"expression", "phaseMargin(STB(loopgain))"}, {"min", "45"}},
                                   QJsonObject{{"name", "ugf"}, {"test", "loop"}, {"expression", "ugf(STB(loopgain))"}},
                                   QJsonObject{{"name", "dc"}, {"test", "loop"}, {"expression", "dB20(value(STB(loopgain), 1))"}}}}}}}));
        const QJsonObject o = must("ade_run", sv).toObject()["points"].toArray()[0].toObject()["outputs"].toObject();
        const double pm = o["pm"].toObject()["value"].toDouble(), ugf = o["ugf"].toObject()["value"].toDouble(), dc = o["dc"].toObject()["value"].toDouble();
        check(near(dc, 60, 1e-4) && near(ugf, 308422.4, 1e-3) && std::abs(pm - 18.150) < 0.05 && !o["pm"].toObject()["pass"].toBool(),
              "stb in ADE: the loop's 60 dB, its unity-gain 308.4 kHz and 18.15 deg phase margin (failing its 45 deg spec): " +
                  std::to_string(dc) + " dB, " + std::to_string(ugf) + " Hz, " + std::to_string(pm) + " deg");
        check(o["pm"].toObject()["steps"].toArray()[0].toString().startsWith("STB(loopgain) = a waveform of 1801 points over frequency"),
              "and its derivation starts from the stb loop gain");
        check(has(error("ade_setup_set", with(sv, {{"setup", QJsonObject{{"tests", QJsonArray{QJsonObject{{"name", "t"}, {"design", "sims/loop2/schematic"},
                  {"analyses", QJsonArray{QJsonObject{{"type", "stb"}, {"sweep", "dec"}, {"points", "10"}, {"start", "1"}, {"stop", "1g"}}}}}}}}}})), "stb needs probe"),
              "an stb without its probe is refused");
    }

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
