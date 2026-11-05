// test_implement: the Implement stage, offscreen, on run records written here: the pipeline follows a run's stages, a report's
// failures are shown with their reasons, a finished flow is not called clean. A real run is tools/m5_gate.py's.
#include "app/implement.h"
#include "app/shell.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTreeWidget>

#include <cstdio>
#include <filesystem>
#include <fstream>
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
    const fs::path dir = fs::temp_directory_path() / ("implement" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    const auto writeRun = [&](const std::string& name, const QString& state, const QStringList& states) {
        const fs::path run = dir / "implementation" / "gcd" / name;
        fs::create_directories(run);
        static const char* names[] = {"synth", "floorplan", "place", "cts", "route", "finish", "drc", "lvs"};
        QJsonArray stages;
        for (int k = 0; k < 8; ++k) stages.append(QJsonObject{{"name", names[k]}, {"state", states[k]}});
        const QJsonObject rec{{"run", QString::fromStdString("gcd/" + name)}, {"design", "gcd"}, {"top", "gcd"}, {"platform", "ihp-sg13g2"}, {"verilog", QJsonArray{}},
                              {"clock_period_ns", 10.0}, {"clock_port", "clk"}, {"core_utilization", 40.0}, {"dir", QString::fromStdString(run.string())},
                              {"state", state}, {"stage", QJsonValue(QJsonValue::Null)}, {"stages", stages}, {"pid", (qint64)::getpid()}, {"started", 1.0}};
        std::ofstream(run / "run.json") << QJsonDocument(rec).toJson().toStdString();
        std::ofstream(run / "flow.log") << "line one\nthe end of the log\n";
        return run;
    };
    writeRun("run.1", "running", {"done", "done", "running", "pending", "pending", "pending", "pending", "pending"});
    const fs::path finished = writeRun("run.2", "done", {"done", "done", "done", "done", "done", "done", "done", "done"});

    // One stage with real metrics, in ORFS's own file: what a finished run leaves
    const fs::path logs = finished / "logs" / "ihp-sg13g2" / "gcd" / "base";
    fs::create_directories(logs);
    std::ofstream(logs / "1_synth.json") << R"({"synth__design__instance__count": 421, "synth__design__instance__area": 5947.6})";
    std::ofstream(logs / "6_report.json") << R"({"finish__design__instance__count": 500, "finish__design__instance__area": 6330.4,
        "finish__design__die__area": 24630, "finish__design__core__area": 14000, "finish__design__instance__utilization": 0.445, "finish__timing__setup__ws": -0.25,
        "finish__timing__setup__tns": -3.5, "finish__timing__hold__ws": 0.298, "finish__power__total": 0.000481, "finish__power__internal__total": 0.0002,
        "finish__power__switching__total": 0.0002, "finish__power__leakage__total": 0.00001})";

    {
        app::Theme::apply(qapp, app::Theme::Mode::Dark);
        app::Session session(QString::fromStdString((dir / "openvlsi.libs").string()));
        app::Shell shell(session);

        shell.showStage(2);
        auto* panel = shell.implementPanel();
        check(panel != nullptr, "the rail has an Implement stage");
        panel->refresh();
        check(panel->runs()->topLevelItemCount() == 1 && panel->runs()->topLevelItem(0)->childCount() == 2, "the panel lists the design's runs");

        const cmd::Result opened = session.call("flow_view", {{"run", "gcd/run.2"}});
        check(opened.ok, "flow_view opens a run: " + opened.error.toStdString());
        check(shell.tabTitles().contains("gcd run.2"), "as a tab");
        auto* view = shell.ciw()->flowView("gcd/run.2");
        check(view && view->pipeline()->stateOf("route") == "done", "the pipeline shows its stages");
        check(view->logText().contains("the end of the log"), "the log tails");
        check(view->verdict() == "Finished, not clean", "a flow that finished with missing reports and a negative slack is not called clean: " + view->verdict().toStdString());
        check(view->reasons().contains("setup timing not met"), "and it says why");
        check(view->reasons().contains("no report") || view->reasons().contains("no GDS"), "a missing report is a reason, not a blank");

        int slackRow = -1, finishColumn = -1;
        for (int k = 0; k < view->metrics()->rowCount(); ++k)
            if (view->metrics()->verticalHeaderItem(k)->text().startsWith("Setup slack")) slackRow = k;
        for (int k = 0; k < view->metrics()->columnCount(); ++k)
            if (view->metrics()->horizontalHeaderItem(k)->text() == "Finish") finishColumn = k;
        check(slackRow >= 0 && finishColumn >= 0 && view->metrics()->item(slackRow, finishColumn) && view->metrics()->item(slackRow, finishColumn)->text() == "-0.250", "metrics read from ORFS's reports, by stage");
        check(view->metrics()->item(slackRow, finishColumn)->foreground().color() == app::Theme::current().fail, "and a negative slack is marked");

        check(session.call("flow_view", {{"run", "gcd/run.1"}}).ok, "a running one too");
        auto* live = shell.ciw()->flowView("gcd/run.1");
        check(live->pipeline()->stateOf("place") == "running" && live->verdict().startsWith("Running"), "it says it is running");
        check(live->findChild<QPushButton*>() != nullptr, "with its buttons");

        check(session.call("flow_view", {{"run", "gcd/run.2"}}).ok && shell.tabTitles().count("gcd run.2") == 1, "opening one again shows the same tab");
        check(!session.call("flow_view", {{"run", "gcd/run.9"}}).ok, "a missing run is refused");
        check(!session.call("flow_view", {{"run", "../../etc"}}).ok, "a path is not a run");
        const cmd::Result noGds = session.call("flow_import_layout", {{"run", "gcd/run.2"}});
        check(!noGds.ok && noGds.error.contains("GDS"), "importing a run with no GDS says so");
        check(session.call("ui_theme", {{"mode", "light"}}).ok, "the stage follows the theme");
    }
    QSettings().clear();
    fs::remove_all(dir);
    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
