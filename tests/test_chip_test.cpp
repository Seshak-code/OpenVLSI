// test_chip_test: the Test stage. A small real design and testbench under Icarus when it is here: a good one passes, a wrong
// answer fails, a silent one fails, a hung one is stopped at its limit. The VCD reader and the waveform widget by hand.
#include "app/shell.h"
#include "app/test_stage.h"
#include "flow/vcd.h"

#include <QApplication>
#include <QJsonArray>
#include <QSettings>
#include <QTreeWidget>

#include <cstdio>
#include <cstdlib>
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
    const fs::path dir = fs::temp_directory_path() / ("chip test " + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    {   // The VCD reader, on a dump written here
        const fs::path f = dir / "hand.vcd";
        std::ofstream(f) << "$date today $end\n$version x $end\n$timescale 1ps $end\n$scope module tb $end\n$var wire 1 ! clk $end\n$var reg 4 \" count [3:0] $end\n"
                            "$scope module dut $end\n$var wire 1 ! clk $end\n$upscope $end\n$upscope $end\n$enddefinitions $end\n#0\n$dumpvars\n0!\nb0000 \"\n$end\n#5\n1!\nb1010 \"\n#10\n0!\nbx \"\n#15\n1!\nb1zz1 \"\n";
        const flow::Vcd header = flow::readVcd(f.string(), true);
        check(header.vars.size() == 3 && header.vars[0].name == "tb.clk" && header.vars[2].name == "tb.dut.clk" && header.secondsPerUnit == 1e-12, "the header: names, widths, timescale");
        check(header.changes.empty(), "a header read keeps no changes");
        const flow::Vcd v = flow::readVcd(f.string());
        check(v.endTime == 15 && v.changes.at("!").size() == 4 && v.changes.at("\"").size() == 4, "changes by identifier, shared between signals");
        check(flow::toHex(flow::valueAt(v.changes.at("\""), 7)) == "a" && flow::valueAt(v.changes.at("!"), 7) == "1", "a value at a time");
        check(flow::toHex(flow::valueAt(v.changes.at("\""), 12)) == "x" && flow::toHex(flow::valueAt(v.changes.at("\""), 15)) == "z", "x and z show in hex");
        check(flow::toHex("111100001") == "1e1", "hex takes the odd bits at the top");
        const flow::Vcd some = flow::readVcd(f.string(), false, {"tb.count"});
        check(some.changes.size() == 1 && some.changes.count("\""), "only what was asked for is kept");
        std::ofstream(dir / "bad.vcd") << "this is not a dump\n";
        bool threw = false;
        try { flow::readVcd((dir / "bad.vcd").string()); } catch (const std::exception&) { threw = true; }
        check(threw, "a file that is not a dump is refused");
        app::DigitalWaves w;
        w.resize(800, 300);
        w.load(v);
        check(w.show("tb.count") && !w.show("tb.absent"), "the widget shows a signal, and refuses one not recorded");
        w.setCursorTime(7);
        check(w.valueAtCursor("tb.count") == "a", "its cursor reads the value");
        w.grab();
    }

    const bool icarus = std::system("command -v iverilog >/dev/null && command -v vvp >/dev/null") == 0;
    if (!icarus) {
        std::printf("  SKIP: no Icarus Verilog\n");
    } else {
        const fs::path design = dir / "counter.v", bench = dir / "tb.v";
        std::ofstream(design) << "module counter(input clk, input reset, output reg [3:0] count);\n  always @(posedge clk) if (reset) count <= 0; else count <= count + 1;\nendmodule\n";
        std::ofstream(bench) << "`timescale 1ns/1ps\nmodule tb;\n  reg clk = 0, reset = 1;\n  wire [3:0] count;\n  counter dut(.clk(clk), .reset(reset), .count(count));\n"
                                "  always #5 clk = ~clk;\n  initial begin\n    #12 reset = 0;\n    #100;\n"
                                "`ifdef HANG\n    forever #10;\n`endif\n`ifdef SILENT\n    $finish;\n`endif\n"
                                "    if (count === `EXPECT) $display(\"PASS\"); else $display(\"FAIL count=%d\", count);\n    $finish;\n  end\nendmodule\n";
        app::Theme::apply(qapp, app::Theme::Mode::Dark);
        app::Session session(QString::fromStdString((dir / "openvlsi.libs").string()));
        app::Shell shell(session);
        const auto test = [&](const QString& name, QJsonArray defines, double limit = 1e6) {
            return session.call("test_run", {{"name", name}, {"testbench", QJsonArray{QString::fromStdString(bench.string())}}, {"top", "tb"}, {"level", "rtl"},
                                             {"verilog", QJsonArray{QString::fromStdString(design.string())}}, {"defines", defines}, {"time_limit_ns", limit}});
        };
        // After reset falls at 12 and 100 ns more, ten rising edges have counted
        cmd::Result r = test("good", {"EXPECT=10"});
        check(r.ok && r.value.toObject()["passed"].toBool(), "a testbench that gets the right answer passes: " + r.error.toStdString() + r.value.toObject()["error"].toString().toStdString());
        r = test("wrong", {"EXPECT=3"});
        check(r.ok && !r.value.toObject()["passed"].toBool() && r.value.toObject()["error"].toString().contains("failure"), "one that gets a wrong answer fails");
        r = test("silent", {"SILENT", "EXPECT=10"});
        check(r.ok && !r.value.toObject()["passed"].toBool() && r.value.toObject()["error"].toString().contains("without printing"), "one that says nothing does not pass");
        r = test("hung", {"HANG", "EXPECT=10"}, 1000);
        check(r.ok && !r.value.toObject()["passed"].toBool() && r.value.toObject()["error"].toString().contains("time limit"), "one that never finishes is stopped at its limit");
        std::ofstream(dir / "broken.v") << "module tb; this is not verilog\nendmodule\n";
        r = session.call("test_run", {{"name", "broken"}, {"testbench", QJsonArray{QString::fromStdString((dir / "broken.v").string())}}, {"top", "tb"}, {"level", "rtl"},
                                      {"verilog", QJsonArray{QString::fromStdString(design.string())}}});
        check(r.ok && !r.value.toObject()["passed"].toBool() && r.value.toObject()["error"].toString().contains("compile"), "one that does not compile says so");
        check(!session.call("test_run", {{"name", "x"}, {"testbench", QJsonArray{"/nonexistent/tb.v"}}, {"top", "tb"}, {"level", "rtl"}, {"verilog", QJsonArray{QString::fromStdString(design.string())}}}).ok, "a missing file is refused");
        check(!session.call("test_run", {{"name", "x"}, {"testbench", QJsonArray{QString::fromStdString(bench.string())}}, {"top", "tb"}, {"level", "gate"}}).ok, "gate level without a flow run is refused");
        check(!session.call("test_run", {{"name", "x y"}, {"testbench", QJsonArray{QString::fromStdString(bench.string())}}, {"top", "tb"}, {"level", "rtl"}, {"verilog", QJsonArray{QString::fromStdString(design.string())}}}).ok, "a bad name is refused");
        check(!session.call("test_status", {{"run", "../../etc"}}).ok, "a path is not a run");

        check(session.call("test_list").value.toArray().size() == 5, "test_list holds them all");
        const cmd::Result value = session.call("test_value", {{"run", "good/run.1"}, {"signal", "tb.count"}});
        check(value.ok && value.value.toObject()["value"].toString() == "a", "test_value reads the recorded result: " + value.value.toObject()["value"].toString().toStdString());
        check(session.call("test_signals", {{"run", "good/run.1"}}).value.toObject()["signals"].toArray().size() >= 4, "test_signals lists what was recorded");

        check(session.call("test_view", {{"run", "good/run.1"}}).ok, "test_view opens a run");
        check(shell.tabTitles().contains("good run.1"), "as a tab");
        auto* view = shell.ciw()->testView("good/run.1");
        check(view && view->verdict() == "Passed", "the verdict: " + (view ? view->verdict().toStdString() : ""));
        check(view->waves()->shown().contains("tb.count"), "the testbench's own signals are shown");
        check(view->signalTree()->topLevelItemCount() >= 4, "every signal is listed");
        check(session.call("test_view", {{"run", "wrong/run.1"}}).ok && shell.ciw()->testView("wrong/run.1")->verdict().startsWith("Failed"), "a failure is called one");
        check(session.call("ui_theme", {{"mode", "light"}}).ok, "the stage follows the theme");
        shell.showStage(4);
        check(shell.testPanel()->runs()->topLevelItemCount() == 5, "the rail's Test panel lists them");
    }
    QSettings().clear();
    fs::current_path(fs::temp_directory_path());
    fs::remove_all(dir);
    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
