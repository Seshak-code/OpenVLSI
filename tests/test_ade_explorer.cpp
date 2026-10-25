// test_ade_explorer: ADE Explorer, offscreen. It shows a maestro view's setup in its tables; an edit is the setup in
// the database, a refused one reverts and says why; Run goes to the background and its results fill the outputs
// table, specs coloured; an undo elsewhere redraws it.
#include "app/ade_explorer.h"
#include "app/ciw.h"
#include "sim/calc.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTableWidget>

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
static QString cell(QTableWidget* t, int r, int c) { return t->item(r, c) ? t->item(r, c)->text() : QString(); }

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ade explorer test " + std::to_string(::getpid()));
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
        // a divider: R0 1k from in (1 V) to out, R1 {rval} from out to ground
        must("library_create", {{"library", "l"}, {"path", "l.oalib"}});
        for (auto [c, prefix, tmpl] : {std::tuple{"res", "R", "[|PLUS] [|MINUS] [@r]"}, std::tuple{"vdc", "V", "[|PLUS] [|MINUS] dc 1"}}) {
            must("cell_create", {{"library", "l"}, {"cell", c}});
            must("view_create", {{"library", "l"}, {"cell", c}, {"view", "symbol"}, {"type", "symbol"}});
            for (auto [n, y] : {std::pair{"PLUS", 100}, std::pair{"MINUS", -100}})
                must("pin_add", {{"library", "l"}, {"cell", c}, {"view", "symbol"}, {"name", n}, {"direction", "inputOutput"}, {"x", 0}, {"y", y}});
            must("cell_prop_set", {{"library", "l"}, {"cell", c}, {"key", "spice"}, {"value", QJsonObject{{"prefix", prefix}, {"template", tmpl}}}});
        }
        must("cell_create", {{"library", "l"}, {"cell", "div"}});
        must("view_create", {{"library", "l"}, {"cell", "div"}, {"view", "schematic"}, {"type", "schematic"}});
        struct I { const char* name; const char* master; int x; const char* minus; const char* plus; const char* r; };
        for (const I& i : {I{"V0", "vdc", 0, "gnd!", "in", nullptr}, I{"R0", "res", 500, "out", "in", "1k"}, I{"R1", "res", 1000, "gnd!", "out", "{rval}"}}) {
            QJsonObject a{{"library", "l"}, {"cell", "div"}, {"view", "schematic"}, {"master_library", "l"}, {"master_cell", i.master}, {"name", i.name},
                          {"x", i.x}, {"y", 0}};
            if (i.r) a["params"] = QJsonObject{{"r", i.r}};
            must("instance_add", a);
            for (auto [net, y] : {std::pair{i.minus, -100}, std::pair{i.plus, 100}})
                must("label_add", {{"library", "l"}, {"cell", "div"}, {"view", "schematic"}, {"name", net}, {"x", i.x}, {"y", y}});
        }
        must("view_create", {{"library", "l"}, {"cell", "div"}, {"view", "ade"}, {"type", "maestro"}});
        const QJsonObject view{{"library", "l"}, {"cell", "div"}, {"view", "ade"}};
        QJsonObject set = view;
        set["setup"] = QJsonObject{{"tests", QJsonArray{QJsonObject{{"name", "op"}, {"design", "l/div/schematic"}, {"analyses", QJsonArray{QJsonObject{{"type", "op"}}}}}}},
                                   {"variables", QJsonObject{{"rval", "1k"}}},
                                   {"outputs", QJsonArray{QJsonObject{{"test", "op"}, {"name", "vout"}, {"expression", "VOP(out)"}, {"min", "0.4"}, {"max", "0.6"}}}}};
        must("ade_setup_set", set);
        auto setup = [&] { return must("ade_setup_get", view).toObject(); };

        check(must("editor_open", view).toString() == "maestro", "editor_open opens ADE Explorer on a maestro view");
        app::AdeExplorer* ade = ciw.explorer("l", "div", "ade");
        check(ade && cell(ade->tests(), 0, 1) == "op" && cell(ade->tests(), 0, 3) == "op" && ade->tests()->rowCount() == 2 &&
                  cell(ade->variables(), 0, 0) == "rval" && cell(ade->outputs(), 0, 2) == "VOP(out)",
              "its tables show the setup, with a blank row to add to");

        ade->variables()->item(0, 1)->setText("1k 3k");
        check(setup()["variables"].toObject()["rval"] == "1k 3k" && ade->status() == "Setup saved", "an edit is saved: rval now sweeps 1k, 3k");
        ade->tests()->item(0, 3)->setText("bogus");
        check(ade->status().contains("type 'bogus'") && setup()["tests"].toArray()[0].toObject()["analyses"].toArray()[0].toObject()["type"] == "op" &&
                  cell(ade->tests(), 0, 3) == "op",
              "a refused edit says why and reverts (" + ade->status().toStdString() + ")");

        ade->run();
        const bool started = ade->running();
        QElapsedTimer t;
        t.start();
        while (ade->running() && t.elapsed() < 60000) qapp.processEvents(QEventLoop::AllEvents, 50);
        QTableWidget* o = ade->outputs();
        check(started && !ade->running() && o->columnCount() == 7 && cell(o, 0, 5) == "0.5" && cell(o, 0, 6) == "0.75" &&
                  o->item(0, 6)->foreground().color() == Qt::red && o->item(0, 5)->foreground().color() != Qt::red &&
                  ade->histories() == QStringList{"Interactive.1"},
              "Run in the background: one column per point, 0.5 passes, 0.75 fails its max (" + cell(o, 0, 5).toStdString() + ", " +
                  cell(o, 0, 6).toStdString() + ", " + ade->status().toStdString() + ")");

        o->setCurrentCell(0, 6);
        const QString d = ade->derivation(0, 6);
        check(d.contains("Equation    VOP(out) = 750m") && d.contains("variables    rval = 3k") && d.contains("corner       Nominal, temperature 27") &&
                  d.contains("analyses     .op") && d.contains("input.cir") && d.contains("1. VOP(out) = 750m") && d.contains("Spec        0.4 <= 750m <= 0.6: fail"),
              "a result's derivation: its equation, every parameter of its point, the steps, the spec's verdict\n" + d.toStdString());
        if (const char* to = std::getenv("OPENVLSI_GRAB")) ade->grab().save(to);  // to look at it
        check(session.call("edit_undo", {{"library", "l"}}).ok && cell(ade->variables(), 0, 1) == "1k", "an undo from anywhere redraws the setup");

        // Monte Carlo: its tab, its run, a column per statistic, how each came about
        ade->montecarlo()->item(0, 3)->setText("rval=agauss(1k,100,1)");
        ade->montecarlo()->item(0, 0)->setText("8");
        check(setup()["montecarlo"].toObject()["samples"].toInt() == 8 && setup()["montecarlo"].toObject()["variables"].toObject()["rval"] == "agauss(1k,100,1)",
              "the Monte Carlo tab edits the setup's montecarlo section");
        ade->run("montecarlo");
        t.restart();
        while (ade->running() && t.elapsed() < 60000) qapp.processEvents(QEventLoop::AllEvents, 50);
        const QString meanHeader = o->horizontalHeaderItem(6) ? o->horizontalHeaderItem(6)->text() : QString();
        const double mean = cell(o, 0, 6).isEmpty() ? 0 : sim::siNumber(cell(o, 0, 6).toStdString());
        const QString md = ade->derivation(0, 6);
        check(!ade->running() && o->columnCount() == 11 && meanHeader == "MC mean" && cell(o, 0, 5) == "8" && mean > 0.4 && mean < 0.6 &&
                  cell(o, 0, 10).endsWith("%"),
              "a Monte Carlo history shows n, mean, σ, min, max and yield (mean " + cell(o, 0, 6).toStdString() + ", yield " + cell(o, 0, 10).toStdString() + "; " + ade->status().toStdString() + ")");
        check(md.contains("Monte Carlo over 8 samples (seeds 1 .. 8") && md.contains("σ    = √(Σ(x − mean)² / (n − 1))") &&
                  md.contains("yield = pass / (pass + fail)") && md.contains("variables    rval = agauss(1k,100,1)"),
              "its derivation: the samples and seeds, the parameters, each statistic's formula and value\n" + md.toStdString());
    }
    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
