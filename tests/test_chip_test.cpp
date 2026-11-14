// test_chip_test: the Test stage. A small real design and testbench under Icarus when it is here: a good one passes, a wrong
// answer fails, a silent one fails, a hung one is stopped at its limit. The VCD reader and the waveform widget by hand.
#include "app/shell.h"
#include "app/test_stage.h"
#include "flow/vcd.h"
#include "sim/chip_deck.h"

#include <QApplication>
#include <QJsonArray>
#include <QSettings>
#include <QTreeWidget>

#include <cmath>
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

    {   // The transistor-level deck and its comparison, by hand: it must be able to fail
        std::ofstream(dir / "c.vcd") << "$timescale 1ns $end\n$scope module tb $end\n$scope module dut $end\n$var wire 1 ! clk $end\n$var wire 1 # d $end\n$var wire 2 $ q [1:0] $end\n$upscope $end\n$upscope $end\n$enddefinitions $end\n"
                                         "#0\n0!\n0#\nbxx $\n#10\n1!\n#20\n0!\n1#\nb10 $\n#30\n1!\n#40\n0!\nb01 $\n#50\n1!\n#60\n0!\n";
        const flow::Vcd v = flow::readVcd((dir / "c.vcd").string());
        sim::ChipDeckConfig cfg;
        cfg.top = "t";
        cfg.dut = "tb.dut";
        cfg.supply = 1.2;
        cfg.modelCards = ".param mc_mismatch=0\n";
        cfg.verilog = "module t(clk, d, q);\n input clk;\n input d;\n output [1:0] q;\nendmodule\n";
        cfg.cdl = ".SUBCKT inv A Y VDD VSS\nMN0 Y A VSS VSS sg13_lv_nmos m=1 w=640.00n l=130.00n ng=1\nMP0 Y A VDD VDD sg13_lv_pmos m=1 w=1.00u l=130.00n ng=1\n.ENDS\n"
                  ".SUBCKT t clk d q[0] q[1]\n+ VDD VSS\nX_1_ d$x\\[3\\] q[0] VDD VSS inv\nX_2_ clk q[1] VDD VSS inv\n.ENDS t\n";
        cfg.spef = "*C_UNIT 1 PF\n*NAME_MAP\n*1 d$x\\[3\\]\n*2 VDD\n*D_NET *1 0.5\n*D_NET *2 9.0\n";
        const sim::ChipDeck deck = sim::buildChipDeck(cfg, v, "clk");
        check(deck.transistors == 4, "transistors are counted through placements: " + std::to_string(deck.transistors));
        check(deck.text.find("XMN0 Y A VSS VSS sg13_lv_nmos m=1 w=640.00n l=130.00n ng=1 mm_ok={mc_mismatch}") != std::string::npos, "a transistor becomes the device library's subcircuit");
        check(deck.text.find("d_x_3_") != std::string::npos && deck.text.find('$') == std::string::npos && deck.text.find('[') == std::string::npos, "names SPICE would misread are rewritten");
        check(deck.nets == 1 && std::abs(deck.capacitanceFarads - 0.5e-12) < 1e-18, "a net takes its SPEF capacitance, the supply does not");
        check(deck.text.find("Cnet1 d_x_3_ 0 5e-13") < deck.text.find(".ends\nXchip"), "inside the top subcircuit, where its names are nets");
        check(deck.resistors == 0 && deck.lumpedNets == 1, "a net whose SPEF has no resistors keeps one capacitor");
        check(deck.text.find("Vsupply VDD 0 dc 1.2") != std::string::npos, "the supply is driven");
        {   // A net's RC network: each pin a node of its own, joined through the wire
            sim::ChipDeckConfig rc = cfg;
            rc.cdl = ".SUBCKT t clk d q[0] q[1]\n+ VDD VSS\nX_1_ d n\[1\] VDD VSS inv\nX_2_ n\[1\] q[0] VDD VSS inv\nX_3_ clk q[1] VDD VSS inv\n.ENDS t\n"
                     ".SUBCKT inv A Y VDD VSS\nMN0 Y A VSS VSS sg13_lv_nmos m=1 w=640.00n l=130.00n ng=1\nMP0 Y A VDD VDD sg13_lv_pmos m=1 w=1.00u l=130.00n ng=1\n.ENDS\n";
            const std::string head = "*C_UNIT 1 FF\n*R_UNIT 1 KOHM\n*NAME_MAP\n*1 n\\[1\\]\n*2 _1_\n*3 _2_\n*4 d\n*5 q\\[0\\]\n";
            const std::string mid = "*D_NET *1 3.0\n*CONN\n*I *2:Y O *D inv\n*I *3:A I *D inv\n*CAP\n1 *2:Y 1.0\n2 *1:7 1.5\n3 *4:2 *3:A 0.5\n*RES\n1 *2:Y *1:7 0.2\n2 *1:7 *3:A 0\n*END\n";
            const std::string port = "*D_NET *4 2.0\n*CONN\n*P d I\n*I *2:A I *D inv\n*CAP\n1 d 1.0\n2 *2:A 1.0\n*RES\n1 d *2:A 0.05\n*END\n";
            const std::string open = "*D_NET *5 4.0\n*CONN\n*P q[0] O\n*I *3:Y O *D inv\n*CAP\n1 q[0] 2.0\n2 *3:Y 2.0\n*RES\n1 *5:1 *3:Y 0.05\n*END\n";
            rc.spef = head + mid + port + open;
            const sim::ChipDeck d = sim::buildChipDeck(rc, v, "clk");
            check(d.text.find("X_1_ _1_:A _1_:Y VDD VSS inv") != std::string::npos && d.text.find("X_2_ _2_:A q_0_ VDD VSS inv") != std::string::npos,
                  "pins on a net with its network are nodes of their own; a net whose network leaves its port apart keeps the net's node");
            check(d.text.find("Rw1 _1_:Y n_1_:7 200\n") != std::string::npos && d.text.find(" n_1_:7 _2_:A 0.001\n") != std::string::npos, "its resistors join them, in ohms, and none is zero");
            check(d.text.find(" _2_:A 0 5e-16") != std::string::npos, "coupling capacitance is grounded at its own net's node, whichever side the SPEF names first");
            check(d.text.find(" d _1_:A 50\n") != std::string::npos, "a port is the net's own node");
            check(d.resistors == 3 && d.nets == 3 && d.lumpedNets == 1 && d.text.find(" q_0_ 0 4e-15") != std::string::npos && std::abs(d.capacitanceFarads - 9e-15) < 1e-21,
                  "counted: " + std::to_string(d.resistors) + " resistors, " + std::to_string(d.lumpedNets) + " net lumped, " + std::to_string(d.capacitanceFarads * 1e15) + " fF");
            rc.wireResistance = false;
            const sim::ChipDeck lumped = sim::buildChipDeck(rc, v, "clk");
            check(lumped.resistors == 0 && lumped.lumpedNets == 3 && lumped.text.find("X_1_ d n_1_ VDD VSS inv") != std::string::npos && std::abs(lumped.capacitanceFarads - 9e-15) < 1e-21,
                  "without wire resistance every net is one capacitor of the same total");
            rc.wireResistance = true;
            rc.spef = head + "*D_NET *1 3.0\n*CONN\n*I *2:Y O *D inv\n*CAP\n1 *2:Y 3.0\n*RES\n1 *2:Y *1:7 0.2\n*END\n";
            check(sim::buildChipDeck(rc, v, "clk").resistors == 0, "a network that misses one of the netlist's pins is not used: the net would come apart");
        }
        check(deck.text.find("V_d d 0 pwl(0 0 2e-08 0 2.005e-08 1.2)") != std::string::npos, "an input replays its recording, with an edge: " + deck.text.substr(deck.text.find("V_d"), 60));
        check(deck.text.find("Cload_q_0_ q_0_ 0 5e-15") != std::string::npos, "an output is loaded");

        // q is x until 20 ns, 10 until 40, then 01. Samples just before each rising edge after the first: 30 ns and 50 ns.
        const auto wave = [](double at30, double at50) { return std::vector<std::pair<double, double>>{{0, at30}, {35e-9, at30}, {36e-9, at50}, {60e-9, at50}}; };
        auto cmp = sim::compareOutputs(deck, v, "tb.dut", {{"q_0_", wave(0.0, 1.2)}, {"q_1_", wave(1.2, 0.0)}}, 1.2);
        check(cmp.samples == 4 && cmp.mismatches == 0, "right voltages agree: " + std::to_string(cmp.samples) + " samples, " + std::to_string(cmp.mismatches) + " differ");
        cmp = sim::compareOutputs(deck, v, "tb.dut", {{"q_0_", wave(1.2, 1.2)}, {"q_1_", wave(1.2, 0.0)}}, 1.2);
        check(cmp.mismatches == 1 && cmp.first.find("q_0_") != std::string::npos, "one wrong bit is found: " + cmp.first);
        cmp = sim::compareOutputs(deck, v, "tb.dut", {{"q_0_", wave(0.6, 1.2)}, {"q_1_", wave(1.2, 0.0)}}, 1.2);
        check(cmp.mismatches == 1 && cmp.undecided == 1, "a value stuck mid-rail is no pass");
        bool threw = false;
        try { sim::compareOutputs(deck, v, "tb.dut", {{"q_0_", wave(0.0, 1.2)}}, 1.2); } catch (const std::exception&) { threw = true; }
        check(threw, "an output the simulation did not write is an error, not a pass");
        threw = false;
        cfg.cdl = ".SUBCKT t clk d q[0] q[1] VDD VSS\nR1 clk d 1k\n.ENDS\n";
        try { sim::buildChipDeck(cfg, v, "clk"); } catch (const std::exception&) { threw = true; }
        check(threw, "a device it cannot simulate is refused");
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
        shell.showStage(shell.stageIndex("test"));
        check(shell.testPanel()->runs()->topLevelItemCount() == 5, "the rail's Test panel lists them");
    }
    QSettings().clear();
    fs::current_path(fs::temp_directory_path());
    fs::remove_all(dir);
    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
