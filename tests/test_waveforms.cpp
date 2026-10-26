// test_waveforms: the calculator as a command and the waveform viewer, offscreen, on a real TreeSPICE run of an RC.
// wave_eval returns numbers, waveforms and complex values; the viewer plots traces over one scale (log for frequency),
// reads them at its cursors, calculates; it draws; and it cross-probes both ways with the schematic. An ADE result
// opens there too.
#include "app/ade_explorer.h"
#include "app/ciw.h"
#include "app/view_editor.h"
#include "app/waveform_viewer.h"

#include <QApplication>
#include <QListWidget>
#include <QTableWidget>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}
static bool near(double a, double b, double rel) { return std::abs(a - b) <= rel * std::abs(b); }

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("waveform test " + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        app::Session session(QString::fromStdString((dir / "openvlsi.libs").string()));
        app::Ciw ciw(session);
        auto must = [&](const char* name, const QJsonObject& a) {
            const cmd::Result r = session.call(name, a);
            if (!r.ok) check(false, std::string(name) + " must succeed: " + r.error.toStdString());
            return r.value;
        };
        auto error = [&](const char* name, const QJsonObject& a) { const cmd::Result r = session.call(name, a); return r.ok ? QString() : r.error; };
        // an RC: a pulse (and ac 1) source into 1k, 1n; nets in, out
        must("library_create", {{"library", "l"}, {"path", "l.oalib"}});
        for (auto [c, prefix, tmpl] : {std::tuple{"res", "R", "[|PLUS] [|MINUS] 1k"}, std::tuple{"cap", "C", "[|PLUS] [|MINUS] 1n"},
                                       std::tuple{"src", "V", "[|PLUS] [|MINUS] dc 0 ac 1 pulse(0 1 0 1n 1n 1 2)"}}) {
            must("cell_create", {{"library", "l"}, {"cell", c}});
            must("view_create", {{"library", "l"}, {"cell", c}, {"view", "symbol"}, {"type", "symbol"}});
            for (auto [n, y] : {std::pair{"PLUS", 100}, std::pair{"MINUS", -100}})
                must("pin_add", {{"library", "l"}, {"cell", c}, {"view", "symbol"}, {"name", n}, {"direction", "inputOutput"}, {"x", 0}, {"y", y}});
            must("cell_prop_set", {{"library", "l"}, {"cell", c}, {"key", "spice"}, {"value", QJsonObject{{"prefix", prefix}, {"template", tmpl}}}});
        }
        must("cell_create", {{"library", "l"}, {"cell", "rc"}});
        must("view_create", {{"library", "l"}, {"cell", "rc"}, {"view", "schematic"}, {"type", "schematic"}});
        struct I { const char* name; const char* master; int x; const char* minus; const char* plus; };
        for (const I& i : {I{"V0", "src", 0, "gnd!", "in"}, I{"R0", "res", 500, "out", "in"}, I{"C0", "cap", 1000, "gnd!", "out"}}) {
            must("instance_add", {{"library", "l"}, {"cell", "rc"}, {"view", "schematic"}, {"master_library", "l"}, {"master_cell", i.master},
                                  {"name", i.name}, {"x", i.x}, {"y", 0}});
            for (auto [net, y] : {std::pair{i.minus, -100}, std::pair{i.plus, 100}})
                must("label_add", {{"library", "l"}, {"cell", "rc"}, {"view", "schematic"}, {"name", net}, {"x", i.x}, {"y", y}});
        }
        const QString run = must("simulate", {{"library", "l"}, {"cell", "rc"}, {"view", "schematic"},
                                              {"cards", QJsonArray{".tran 10n 20u", ".ac dec 50 1k 100meg"}}})
                                .toObject()["run"].toString();
        const double tau = 1e-6;

        // ── the calculator as a command ──
        auto eval = [&](const char* e) { return must("wave_eval", {{"run", run}, {"expression", e}}); };
        const QJsonObject vt = eval("VT(out)").toObject(), vf = eval("VF(out)").toObject(), v1k = eval("value(VF(out), 1k)").toObject();
        check(near(eval("riseTime(VT(out))").toDouble(), std::log(9.0) * tau, 1e-2) && vt["x"].toArray().size() == vt["y"].toArray().size() &&
                  vt["x"].toArray().size() > 100 && vf.contains("re") && vf.contains("im") && v1k.contains("re") && !v1k.contains("x"),
              "wave_eval: a number (rise time ln 9 · RC), a waveform {x, y}, a complex waveform and value");
        check(error("wave_eval", {{"run", run}, {"expression", "riseTime("}}).contains("calculator") &&
                  error("wave_eval", {{"run", QString::fromStdString(dir.string())}, {"expression", "1"}}).contains("no run"),
              "wave_eval refuses a malformed expression and a directory that is not a run");

        // noise: the RC's output density at low frequency is the resistor's sqrt(4kTR); its total, sqrt(kT/C)
        {
            const QString nrun = must("simulate", {{"library", "l"}, {"cell", "rc"}, {"view", "schematic"},
                                                   {"cards", QJsonArray{".noise v(out) V0 dec 20 10 100meg"}}})
                                     .toObject()["run"].toString();
            const double kT = 1.380649e-23 * 300.15, density = must("wave_eval", {{"run", nrun}, {"expression", "value(VN(out), 10)"}}).toDouble();
            const double total = must("wave_eval", {{"run", nrun}, {"expression", "VNTOT(out)"}}).toDouble();
            check(near(density, std::sqrt(4 * kT * 1e3), 1e-3) && near(total, std::sqrt(kT / 1e-9), 2e-2),
                  "noise: VN(out) at 10 Hz = sqrt(4kTR) (" + QString::number(density, 'g', 6).toStdString() + "), VNTOT(out) ≈ sqrt(kT/C) (" + QString::number(total, 'g', 6).toStdString() + ")");
        }

        // ── the viewer ──
        check(must("waveform_open", {{"run", run}}).toString() == "l/rc/schematic", "waveform_open knows the run's schematic");
        app::WaveformViewer* v = ciw.viewer(QString::fromStdString(fs::weakly_canonical(run.toStdString()).string()));
        must("waveform_add", {{"run", run}, {"expression", "VT(out)"}});
        app::Plot* p = v ? v->plot() : nullptr;
        check(v && p && v->expressions() == QStringList{"VT(out)"} && !p->logX() && p->xRange().first == 0 && near(p->xRange().second, 20e-6, 1e-9),
              "a transient trace: linear time axis, fitted to 0 .. 20u");
        check(error("waveform_add", {{"run", run}, {"expression", "VF(out)"}}).contains("this plot shows time") &&
                  error("waveform_add", {{"run", run}, {"expression", "riseTime(VT(out))"}}).contains("is a number"),
              "one scale per plot; a number is not a trace");
        p->setCursor(0, 1e-6);
        p->setCursor(1, 3e-6);
        const auto at1 = p->valueAt(p->traces()[0], 1e-6);
        check(at1 && near(*at1, eval("value(VT(out), 1u)").toDouble(), 1e-9) && near(*at1, 1 - std::exp(-1.0), 1e-2) &&
                  v->readout()->item(0, 1)->text() == "1u" && v->readout()->item(0, 3)->text() == "2u" && v->readout()->item(1, 1)->text().toStdString() != "—",
              "cursors A and B: the readout shows x, each trace there and the difference (v(out) at 1u = 1 − e⁻¹)");
        const QString rise = v->calculate("riseTime(VT(out))");
        check(rise.endsWith("u") && near(rise.chopped(1).toDouble(), std::log(9.0) * tau * 1e6, 1e-2), "the calculator line evaluates a number (" + rise.toStdString() + ")");
        check(v->derivation().contains("riseTime(VT(out)) = 2.197") && v->derivation().contains("10% of the transition: crosses 100m between"),
              "and shows its derivation\n" + v->derivation().toStdString());
        const QJsonObject explained = must("wave_eval", {{"run", run}, {"expression", "bandwidth(VF(out))"}, {"explain", true}}).toObject();
        check(explained["value"].isDouble() && explained["steps"].toArray().size() >= 3 &&
                  explained["steps"].toArray()[1].toString().startsWith("bandwidth(VF(out)) = "),
              "wave_eval explains when asked: {value, steps}");
        if (const char* to = std::getenv("OPENVLSI_GRAB")) v->grab().save(to);  // to look at it
        check(v->calculate("VT(in)") == "plotted" && v->expressions().size() == 2, "the calculator line plots a waveform");
        p->resize(800, 400);
        const QImage img = p->grab().toImage();
        int red = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                red += c.red() > 180 && c.green() < 120 && c.blue() < 120;
            }
        check(red > 100, "the plot draws its traces (" + std::to_string(red) + " pixels of the first trace's colour)");
        must("waveform_clear", {{"run", run}});
        must("waveform_add", {{"run", run}, {"expression", "dB20(VF(out))"}});
        check(v->expressions() == QStringList{"dB20(VF(out))"} && p->logX() && near(p->xRange().first, 1e3, 1e-9), "an AC trace: a log frequency axis");

        // ── cross-probing ──
        must("editor_open", {{"library", "l"}, {"cell", "rc"}, {"view", "schematic"}});
        app::ViewEditor* e = ciw.editor("l", "rc", "schematic");
        check(e && e->selectNet("out") == 2 && e->plotSelectedNets() == QStringList{"out"} && v->expressions().contains("VF(out)"),
              "Plot Net in the schematic adds the net to the viewer, as the plot's analysis (VF(out))");
        e->selectNet("in");
        v->selectTrace((int)v->expressions().indexOf("VF(out)"));
        bool labels = !e->selection().empty();
        for (const auto& [kind, key] : e->selection()) labels = labels && kind == "label";
        check(labels && e->selection().size() == 2 && v->status().contains("Selected out"), "selecting a trace selects its net in the schematic");
        check(error("waveform_probe", {{"library", "l"}, {"cell", "res"}, {"view", "symbol"}, {"net", "x"}}).contains("no waveform viewer") &&
                  error("editor_select_net", {{"library", "l"}, {"cell", "rc"}, {"view", "schematic"}, {"net", "nope"}}).contains("no wire or label"),
              "probing a design no viewer shows, or a net the schematic lacks, is an error");
        // a PSS run: Plot Net reads its period (VPSS), or its harmonics once the plot shows frequency (VH)
        const QString prun = must("simulate", {{"library", "l"}, {"cell", "rc"}, {"view", "schematic"}, {"cards", QJsonArray{".pss 1meg 30u 0 100 5"}}})
                                 .toObject()["run"].toString();
        must("waveform_open", {{"run", prun}});
        app::WaveformViewer* sv = ciw.viewer(QString::fromStdString(fs::weakly_canonical(prun.toStdString()).string()));
        const QString empty = sv ? sv->defaultAccess() : "";
        must("waveform_add", {{"run", prun}, {"expression", "mag(VH(out))"}});
        check(sv && empty == "VPSS" && sv->defaultAccess() == "VH", "a PSS run's nets plot as VPSS, or VH on a frequency plot (" + empty.toStdString() + ")");

        // ── an ADE result opens in the viewer ──
        must("view_create", {{"library", "l"}, {"cell", "rc"}, {"view", "ade"}, {"type", "maestro"}});
        must("ade_setup_set", {{"library", "l"}, {"cell", "rc"}, {"view", "ade"},
                               {"setup", QJsonObject{{"tests", QJsonArray{QJsonObject{{"name", "tr"}, {"design", "l/rc/schematic"},
                                                                                      {"analyses", QJsonArray{QJsonObject{{"type", "tran"}, {"step", "10n"}, {"stop", "5u"}}}}}}},
                                                     {"outputs", QJsonArray{QJsonObject{{"test", "tr"}, {"name", "out"}, {"expression", "VT(out)"}}}}}}});
        const QJsonObject h = must("ade_run", {{"library", "l"}, {"cell", "rc"}, {"view", "ade"}}).toObject();
        must("editor_open", {{"library", "l"}, {"cell", "rc"}, {"view", "ade"}});
        app::AdeExplorer* ade = ciw.explorer("l", "rc", "ade");
        const QString pointDir = h["points"].toArray()[0].toObject()["dir"].toString();
        app::WaveformViewer* pv = ade && ade->openResult(0, 5) ? ciw.viewer(QString::fromStdString(fs::weakly_canonical(pointDir.toStdString()).string())) : nullptr;
        check(pv && pv->expressions() == QStringList{"VT(out)"}, "double-clicking an ADE result opens its point with the waveform plotted");
    }
    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
