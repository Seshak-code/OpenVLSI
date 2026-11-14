// The Test stage: the designed chip under test. A testbench on the RTL or on the routed netlist with its layout's delays, and
// firmware on the routed core. Each run is a directory (testing/<name>/run.N: run.json, sim.log, dump.vcd), so it survives the app.
#include "cmd/registry.h"
#include "flow/rtl_flow.h"
#include "flow/vcd.h"
#include "oadb/oadb.h"
#include "sim/ade.h"
#include "sim/chip_deck.h"
#include "sim/netlist.h"
#include "sim/run.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#ifndef _WIN32
#include <csignal>
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

namespace cmd {

namespace {

namespace fs = std::filesystem;
using namespace acutesim::compute::orchestration;

const QJsonObject kRun = param("string", "a test's run: <name>/run.N");
const QJsonObject kFlowRun = param("string", "an implementation run: <design>/run.N");

std::string s(const QJsonObject& a, const char* key) { return a[key].toString().toStdString(); }
QString q(const std::string& text) { return QString::fromStdString(text); }

fs::path base(Context& c) { return fs::absolute(fs::path(c.workspace.file()).parent_path()); }

sim::Jobs& jobs(Context& c) {
    if (!c.jobs) c.jobs = std::make_shared<sim::Jobs>();
    return *c.jobs;
}

fs::path runPath(Context& c, const QJsonObject& a) {
    static const QRegularExpression name("^[A-Za-z0-9_]+/run\\.[0-9]+$");
    if (!name.match(a["run"].toString()).hasMatch()) throw std::invalid_argument("a test's run is named <name>/run.N");
    const fs::path d = base(c) / "testing" / s(a, "run");
    if (!fs::is_directory(d)) throw std::invalid_argument("no test run " + s(a, "run"));
    return d;
}

QJsonObject readJson(const fs::path& file) {
    QFile f(QString::fromStdString(file.string()));
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read " + file.string());
    QJsonParseError e;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError) throw std::runtime_error(file.string() + ": " + e.errorString().toStdString());
    return d.object();
}

void writeRecord(const fs::path& dir, const QJsonObject& rec) {
    const fs::path tmp = dir / "run.json.tmp";
    std::ofstream(tmp) << QJsonDocument(rec).toJson().toStdString();
    fs::rename(tmp, dir / "run.json");
}

double now() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

// A new run's directory: the number after the highest there
fs::path newRun(Context& c, const std::string& name, std::string* runName) {
    static const QRegularExpression good("^[A-Za-z0-9_]+$");
    if (!good.match(q(name)).hasMatch()) throw std::invalid_argument("a test's name is letters, digits and _");
    const fs::path dir = base(c) / "testing" / name;
    fs::create_directories(dir);
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string f = e.path().filename().string();
        if (f.rfind("run.", 0) == 0) n = std::max(n, std::atoi(f.c_str() + 4));
    }
    while (!fs::create_directory(dir / ("run." + std::to_string(++n)))) {}
    *runName = name + "/run." + std::to_string(n);
    return dir / ("run." + std::to_string(n));
}

// What an implementation run offers a test: its record and results directory
struct Implemented { QJsonObject record; fs::path results; };
Implemented implemented(Context& c, const std::string& run) {
    static const QRegularExpression name("^[A-Za-z0-9_]+/run\\.[0-9]+$");
    if (!name.match(q(run)).hasMatch()) throw std::invalid_argument("an implementation run is named <design>/run.N");
    const fs::path dir = base(c) / "implementation" / run;
    if (!fs::is_directory(dir)) throw std::invalid_argument("no implementation run " + run);
    const QJsonObject rec = readJson(dir / "run.json");
    return {rec, dir / "results" / rec["platform"].toString().toStdString() / rec["design"].toString().toStdString() / "base"};
}

QJsonObject shownSignals(const flow::Vcd& v) {
    QJsonArray list;
    for (const auto& sg : v.vars) list.append(QJsonObject{{"name", q(sg.name)}, {"width", sg.width}});
    return {{"signals", list}, {"seconds_per_unit", v.secondsPerUnit}};
}

std::string tailOf(const fs::path& file, size_t bytes = 64 * 1024) {
    std::ifstream f(file, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamoff size = f.tellg();
    const std::streamoff from = size > (std::streamoff)bytes ? size - (std::streamoff)bytes : 0;
    f.seekg(from);
    std::string out((size_t)(size - from), 0);
    f.read(out.data(), (std::streamsize)out.size());
    return out;
}

}  // namespace

void addTestCommands(Registry& r) {
    r.add({"test_run", "Runs a testbench of yours on the design: level rtl on its Verilog (given, or a flow run's), level gate on a flow run's routed netlist with the delays "
                       "of its layout. It passes only if it finishes by itself, prints pass_pattern (default PASS) and reports no line starting FAIL, ERROR or FATAL. Every signal is "
                       "recorded. Waits and returns the result, or with wait false starts it in the background (poll test_status).",
           object({{"name", param("string", "the test's name: letters, digits, _")},
                   {"testbench", QJsonObject{{"type", "array"}, {"description", "the testbench's Verilog files: absolute paths"}, {"items", QJsonObject{{"type", "string"}}}}},
                   {"top", param("string", "the testbench's module")},
                   {"level", param("string", "rtl or gate")}},
                  {{"flow_run", kFlowRun},
                   {"verilog", QJsonObject{{"type", "array"}, {"description", "level rtl without a flow run: the design's Verilog"}, {"items", QJsonObject{{"type", "string"}}}}},
                   {"dut", param("string", "level gate: the design's instance, as tb.dut")},
                   {"corner", param("string", "level gate: the timing corner whose delays it runs with, as slow (default the flow's own)")},
                   {"defines", QJsonObject{{"type", "array"}, {"description", "Verilog defines: NAME or NAME=VALUE"}, {"items", QJsonObject{{"type", "string"}}}}},
                   {"pass_pattern", param("string", "the text that means it passed (default PASS)")},
                   {"time_limit_ns", param("number", "simulated time before it counts as hung (default 1e6)")},
                   {"wait", param("boolean", "wait for it (default true)")}}),
           [](Context& c, const QJsonObject& a) {
               TestbenchConfig cfg;
               cfg.top = s(a, "top");
               for (const auto& v : a["testbench"].toArray()) cfg.testbench.push_back(v.toString().toStdString());
               for (const auto& v : a["defines"].toArray()) cfg.defines.push_back(v.toString().toStdString());
               if (a.contains("pass_pattern")) cfg.passPattern = s(a, "pass_pattern");
               if (a.contains("time_limit_ns")) cfg.timeLimitNs = a["time_limit_ns"].toDouble();
               const std::string level = s(a, "level");
               if (level != "rtl" && level != "gate") throw std::invalid_argument("level is rtl or gate");
               for (const auto& f : cfg.testbench)
                   if (!fs::exists(f)) throw std::invalid_argument("no testbench file " + f);
               QString flowRun;
               if (a.contains("flow_run")) {
                   const Implemented impl = implemented(c, s(a, "flow_run"));
                   flowRun = a["flow_run"].toString();
                   cfg.platform = s(impl.record, "platform");
                   cfg.results = impl.results.string();
                   if (level == "rtl")
                       for (const auto& v : impl.record["verilog"].toArray()) cfg.rtl.push_back(v.toString().toStdString());
               }
               if (level == "rtl") {
                   for (const auto& v : a["verilog"].toArray()) cfg.rtl.push_back(v.toString().toStdString());
                   if (cfg.rtl.empty()) throw std::invalid_argument("level rtl needs verilog, or a flow_run to take it from");
                   for (const auto& f : cfg.rtl)
                       if (!fs::exists(f)) throw std::invalid_argument("no source file " + f);
               } else {
                   if (flowRun.isEmpty()) throw std::invalid_argument("level gate needs a flow_run: its routed netlist is what runs");
                   if (!a.contains("dut")) throw std::invalid_argument("level gate needs dut, the design's instance as tb.dut, for the delays");
                   cfg.dut = s(a, "dut");
                   if (a.contains("corner")) {
                       const flow::Platform* pdk = flow::findPlatform(cfg.platform);
                       if (!pdk) throw std::runtime_error("the run's platform is not known");
                       flow::libertyFor(*pdk, s(a, "corner"));  // refuses one that does not exist, before a run is made
                       cfg.corner = s(a, "corner");
                   }
               }
               std::string run;
               const fs::path dir = newRun(c, s(a, "name"), &run);
               cfg.workDir = dir.string();
               QJsonObject rec{{"run", q(run)}, {"name", a["name"]}, {"kind", "testbench"}, {"level", a["level"]}, {"top", a["top"]}, {"state", "running"}, {"started", now()},
                               {"pid", QCoreApplication::applicationPid()}};
               if (!flowRun.isEmpty()) rec["flow_run"] = flowRun;
               if (!cfg.corner.empty()) rec["corner"] = q(cfg.corner);
               writeRecord(dir, rec);
               const auto execute = [dir, rec, cfg](const std::atomic<bool>* cancel) {
                   auto logFile = std::make_shared<std::ofstream>(dir / "sim.log");
                   const TestbenchResult res = runTestbench(cfg, RtlFlowTools::detect(), [logFile](const std::string& line, bool) { *logFile << line << "\n" << std::flush; }, cancel);
                   QJsonObject done = rec;
                   done["finished"] = now();
                   done["seconds"] = res.seconds;
                   done["state"] = cancel && *cancel ? "stopped" : "done";
                   done["passed"] = res.passed;
                   if (!res.error.empty()) done["error"] = q(res.error);
                   if (fs::exists(dir / "dump.vcd")) done["vcd"] = q((dir / "dump.vcd").string());
                   writeRecord(dir, done);
               };
               if (!a.contains("wait") || a["wait"].toBool()) {
                   execute(nullptr);
                   return QJsonValue(readJson(dir / "run.json"));
               }
               jobs(c).start(dir.string(), [execute](const std::atomic<bool>& cancel) { execute(&cancel); });
               return QJsonValue(rec);
           },
           true});

    r.add({"test_firmware", "Runs firmware on an implemented RISC-V core: level rtl, or level gate on its routed netlist with its layout's delays, in a testbench of the reference SoC's "
                            "memory map. Returns its UART output and cycles; expect_uart makes wrong output a failure. Words come as a hex file, one 32-bit word per line.",
           object({{"name", param("string", "the test's name")}, {"flow_run", kFlowRun}, {"firmware", param("string", "hex file: one word a line, word 0 at address 0")},
                   {"level", param("string", "rtl or gate")}},
                  {{"clock_period_ns", param("number", "the clock (default the run's)")}, {"max_cycles", param("integer", "cycles before it counts as hung (default 100000)")},
                   {"expect_uart", param("string", "the UART output it must give")}, {"wait", param("boolean", "wait for it (default true)")}}),
           [](Context& c, const QJsonObject& a) {
               const Implemented impl = implemented(c, s(a, "flow_run"));
               const std::string level = s(a, "level");
               if (level != "rtl" && level != "gate") throw std::invalid_argument("level is rtl or gate");
               FirmwareSimConfig cfg;
               cfg.top = s(impl.record, "top");
               cfg.results = impl.results.string();
               cfg.platform = s(impl.record, "platform");
               cfg.clockPeriodNs = a.contains("clock_period_ns") ? a["clock_period_ns"].toDouble() : impl.record["clock_period_ns"].toDouble();
               if (a.contains("max_cycles")) cfg.maxCycles = a["max_cycles"].toInt();
               if (level == "rtl")
                   for (const auto& v : impl.record["verilog"].toArray()) cfg.rtl.push_back(v.toString().toStdString());
               std::ifstream hex(s(a, "firmware"));
               if (!hex) throw std::invalid_argument("cannot read firmware " + s(a, "firmware"));
               for (std::string word; hex >> word;) {
                   char* end = nullptr;
                   const unsigned long w = std::strtoul(word.c_str(), &end, 16);
                   if (*end || word.size() > 8) throw std::invalid_argument("firmware word '" + word + "' is not 32-bit hex");
                   cfg.rom.push_back((uint32_t)w);
               }
               if (cfg.rom.empty()) throw std::invalid_argument("the firmware file holds no words");
               std::string run;
               const fs::path dir = newRun(c, s(a, "name"), &run);
               cfg.workDir = dir.string();
               QJsonObject rec{{"run", q(run)}, {"name", a["name"]}, {"kind", "firmware"}, {"level", a["level"]}, {"flow_run", a["flow_run"]}, {"clock_period_ns", cfg.clockPeriodNs},
                               {"state", "running"}, {"started", now()}, {"pid", QCoreApplication::applicationPid()}};
               writeRecord(dir, rec);
               const bool expecting = a.contains("expect_uart");
               const std::string expected = s(a, "expect_uart");
               const auto execute = [dir, rec, cfg, expecting, expected](const std::atomic<bool>* cancel) {
                   auto logFile = std::make_shared<std::ofstream>(dir / "sim.log");
                   const FirmwareSimResult res = runFirmware(cfg, RtlFlowTools::detect(), [logFile](const std::string& line, bool) { *logFile << line << "\n" << std::flush; }, cancel);
                   QJsonObject done = rec;
                   done["finished"] = now();
                   done["seconds"] = res.seconds;
                   done["state"] = cancel && *cancel ? "stopped" : "done";
                   done["uart"] = q(res.uart);
                   done["cycles"] = (double)res.cycles;
                   bool passed = res.ok;
                   QString why = q(res.error);
                   if (passed && expecting && res.uart != expected) {
                       passed = false;
                       why = "its UART output differs from what was expected";
                   }
                   done["passed"] = passed;
                   if (!why.isEmpty()) done["error"] = why;
                   writeRecord(dir, done);
               };
               if (!a.contains("wait") || a["wait"].toBool()) {
                   execute(nullptr);
                   return QJsonValue(readJson(dir / "run.json"));
               }
               jobs(c).start(dir.string(), [execute](const std::atomic<bool>& cancel) { execute(&cancel); });
               return QJsonValue(rec);
           },
           true});

    r.add({"test_spice", "Runs the routed chip at transistor level on TreeSPICE: its layout's netlist with each net's capacitance from the SPEF, driven at its pins by what a test run recorded, every output compared with that "
                         "recording at the end of each clock cycle. It needs pdk_install's device library, and passes only if every sample agrees. Waits and returns, or with wait false starts in the background.",
           object({{"name", param("string", "the test's name")}, {"flow_run", kFlowRun}, {"recording", kRun}, {"dut", param("string", "the design's instance in the recording, as tb.dut")}},
                  {{"stop_ns", param("number", "how much of the recording to replay (default all of it)")}, {"clock_port", param("string", "the clock's port (default the run's)")},
                   {"parasitics", param("boolean", "add the SPEF's capacitance (default true)")}, {"threads", param("integer", "TreeSPICE's threads (default all)")},
                   {"device_library", param("string", "the library holding the transistors (default sg13g2)")}, {"timeout_s", param("integer", "how long it may run (default 3600)")},
                   {"wait", param("boolean", "wait for it (default true)")}}),
           [](Context& c, const QJsonObject& a) {
               const Implemented impl = implemented(c, s(a, "flow_run"));
               const flow::Platform* pdk = flow::findPlatform(s(impl.record, "platform"));
               if (!pdk) throw std::runtime_error("the run's platform is not known");
               const fs::path cdlFile = impl.results / "6_lvs.cdl", verilog = impl.results / "6_final.v", spefFile = impl.results / "6_final.spef";
               if (!fs::exists(cdlFile)) throw std::invalid_argument("this run has no transistor netlist (" + cdlFile.string() + "): its platform's LVS step writes it");
               QJsonObject which = a;
               which["run"] = a["recording"];
               const fs::path recording = runPath(c, which) / "dump.vcd";
               if (!fs::exists(recording)) throw std::invalid_argument("that test recorded nothing");
               const auto slurp = [](const fs::path& f) {
                   std::ifstream in(f);
                   return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
               };
               // The device library's model cards, as a simulation of its transistor would have them
               const std::string library = a.contains("device_library") ? s(a, "device_library") : "sg13g2";
               const auto known = c.workspace.libraries();
               if (std::find(known.begin(), known.end(), library) == known.end()) throw std::invalid_argument("no device library " + library + " in this workspace (pdk_install)");
               oadb::Library& devices = c.workspace.library(library);
               const auto spice = devices.cellProp("sg13_lv_nmos", "spice");
               if (!spice) throw std::invalid_argument(library + " has no sg13_lv_nmos with its models");
               const QJsonObject spiceJson = QJsonDocument::fromJson(QByteArray::fromStdString(*spice)).object();
               std::string cards;
               const QJsonObject globals = spiceJson["globals"].toObject();
               for (auto it = globals.begin(); it != globals.end(); ++it) cards += ".param " + it.key().toStdString() + "=" + it.value().toVariant().toString().toStdString() + "\n";
               for (const auto& m : spiceJson["models"].toArray()) cards += sim::modelCard({m.toObject()["file"].toString().toStdString(), m.toObject()["section"].toString().toStdString()}) + "\n";

               sim::ChipDeckConfig cfg;
               cfg.cdl = slurp(cdlFile);
               cfg.verilog = slurp(verilog);
               if (a["parasitics"].toBool(true) && fs::exists(spefFile)) cfg.spef = slurp(spefFile);
               cfg.top = s(impl.record, "top");
               cfg.dut = s(a, "dut");
               cfg.modelCards = cards;
               cfg.supply = pdk->supplyVolts;
               if (a.contains("stop_ns")) cfg.stopSeconds = a["stop_ns"].toDouble() * 1e-9;
               const std::string clock = a.contains("clock_port") ? s(a, "clock_port") : s(impl.record, "clock_port");
               const flow::Vcd vcd = flow::readVcd(recording.string());
               const sim::ChipDeck deck = sim::buildChipDeck(cfg, vcd, clock);

               std::string run;
               const fs::path dir = newRun(c, s(a, "name"), &run);
               const double period = impl.record["clock_period_ns"].toDouble();
               QJsonObject rec{{"run", q(run)}, {"name", a["name"]}, {"kind", "spice"}, {"level", "transistor"}, {"flow_run", a["flow_run"]}, {"recording", a["recording"]}, {"state", "running"},
                               {"started", now()}, {"pid", QCoreApplication::applicationPid()}, {"transistors", (double)deck.transistors}, {"parasitic_nets", (double)deck.nets},
                               {"parasitic_pf", deck.capacitanceFarads * 1e12}, {"stop_ns", deck.stopSeconds * 1e9}};
               writeRecord(dir, rec);
               // At most a tenth of the fastest edge between samples: the step is an upper bound, and the engine's own error control does the rest
               const double step = std::max(1e-12, std::min(50e-12, period * 1e-9 / 100));
               std::string text = deck.text;
               text += ".save";
               for (const auto& p : deck.ports) text += " v(" + p.node + ")";
               text += "\n.tran " + QString::number(step, 'g', 12).toStdString() + " " + QString::number(deck.stopSeconds, 'g', 12).toStdString() + "\n.end\n";
               const std::string simulator = c.simulator;
               const int threads = a["threads"].toInt(0);
               const auto timeout = std::chrono::seconds(a["timeout_s"].toInt(3600));
               const std::string dut = cfg.dut;
               const double supply = cfg.supply;
               const auto execute = [dir, rec, deck, text, simulator, threads, timeout, vcd, dut, supply](const std::atomic<bool>* cancel) {
                   QJsonObject done = rec;
                   const auto started = std::chrono::steady_clock::now();
                   try {
                       const sim::Run result = sim::simulate(simulator, dir.string(), text, timeout, cancel, threads);
                       std::map<std::string, std::vector<std::pair<double, double>>> voltages;
                       const sim::Plot* plot = nullptr;
                       for (const auto& pl : result.plots)
                           if (pl.name.find("ransient") != std::string::npos) plot = &pl;
                       if (!plot) throw std::runtime_error("the simulation wrote no transient");
                       const auto column = [&](const std::string& name) -> int {
                           for (size_t k = 0; k < plot->vectors.size(); ++k) {
                               std::string n = plot->vectors[k];
                               for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
                               if (n == name || n == "v(" + name + ")") return (int)k;
                           }
                           return -1;
                       };
                       const int timeAt = column("time");
                       if (timeAt < 0) throw std::runtime_error("the transient has no time");
                       const auto& time = plot->data[(size_t)timeAt];
                       for (const auto& p : deck.ports) {
                           if (!p.output) continue;
                           std::string node = p.node;
                           for (auto& ch : node) ch = (char)std::tolower((unsigned char)ch);
                           const int at = column(node);
                           if (at < 0) throw std::runtime_error("the transient has no " + p.node);
                           auto& wave = voltages[p.node];
                           wave.reserve(time.size());
                           for (size_t k = 0; k < time.size(); ++k) wave.push_back({time[k].real(), plot->data[(size_t)at][k].real()});
                       }
                       const sim::ChipCompare cmp = sim::compareOutputs(deck, vcd, dut, voltages, supply);
                       done["samples"] = (double)cmp.samples;
                       done["mismatches"] = (double)cmp.mismatches;
                       done["undecided"] = (double)cmp.undecided;
                       done["steps"] = (double)time.size();
                       done["passed"] = cmp.samples > 0 && cmp.mismatches == 0;
                       if (!cmp.samples) done["error"] = "the recording held no value to compare in that time";
                       else if (cmp.mismatches) done["error"] = QString("%1 of %2 samples differ; first ").arg((double)cmp.mismatches).arg((double)cmp.samples) + q(cmp.first);
                       done["raw"] = q((dir / "out.raw").string());
                   } catch (const std::exception& e) {
                       done["passed"] = false;
                       done["error"] = q(e.what());
                   }
                   done["finished"] = now();
                   done["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                   done["state"] = cancel && *cancel ? "stopped" : "done";
                   std::ifstream log(dir / "log.txt");
                   std::ofstream(dir / "sim.log") << log.rdbuf();
                   writeRecord(dir, done);
               };
               if (!a.contains("wait") || a["wait"].toBool()) {
                   execute(nullptr);
                   return QJsonValue(readJson(dir / "run.json"));
               }
               jobs(c).start(dir.string(), [execute](const std::atomic<bool>& cancel) { execute(&cancel); });
               return QJsonValue(rec);
           },
           true});

    r.add({"test_shmoo", "Runs a testbench on the routed netlist across clock periods: its PERIOD define takes each value (ns). Returns each one's verdict, the fastest that passed and the slowest that failed: where the chip really stops working in simulation.",
           object({{"name", param("string", "the shmoo's name")}, {"flow_run", kFlowRun}, {"testbench", QJsonObject{{"type", "array"}, {"description", "the testbench's Verilog files"}, {"items", QJsonObject{{"type", "string"}}}}},
                   {"top", param("string", "the testbench's module")}, {"dut", param("string", "the design's instance, as tb.dut")},
                   {"periods_ns", QJsonObject{{"type", "array"}, {"description", "the clock periods to try, in ns"}, {"items", QJsonObject{{"type", "number"}}}}}},
                  {{"defines", QJsonObject{{"type", "array"}, {"description", "more Verilog defines"}, {"items", QJsonObject{{"type", "string"}}}}}, {"pass_pattern", param("string", "the text that means it passed (default PASS)")},
                   {"corner", param("string", "the timing corner whose delays it runs with, as slow (default the flow's own)")}}),
           [](Context& c, const QJsonObject& a) {
               const Implemented impl = implemented(c, s(a, "flow_run"));
               const QJsonArray periods = a["periods_ns"].toArray();
               if (periods.isEmpty()) throw std::invalid_argument("no periods to try");
               const RtlFlowTools tools = RtlFlowTools::detect();
               const std::string corner = a.contains("corner") ? s(a, "corner") : std::string();
               if (const flow::Platform* pdk = flow::findPlatform(s(impl.record, "platform"))) flow::libertyFor(*pdk, corner);
               QJsonArray points;
               double fastestPass = 0, slowestFail = 0;
               for (const auto& p : periods) {
                   const double period = p.toDouble();
                   if (!(period > 0)) throw std::invalid_argument("a period must be above 0");
                   TestbenchConfig cfg;
                   cfg.top = s(a, "top");
                   for (const auto& v : a["testbench"].toArray()) cfg.testbench.push_back(v.toString().toStdString());
                   for (const auto& v : a["defines"].toArray()) cfg.defines.push_back(v.toString().toStdString());
                   cfg.defines.push_back("PERIOD=" + q(std::to_string(period)).toStdString());
                   if (a.contains("pass_pattern")) cfg.passPattern = s(a, "pass_pattern");
                   cfg.results = impl.results.string();
                   cfg.platform = s(impl.record, "platform");
                   cfg.dut = s(a, "dut");
                   cfg.timeLimitNs = period * 1e6;
                   cfg.corner = corner;
                   std::string run;
                   const fs::path dir = newRun(c, s(a, "name"), &run);
                   cfg.workDir = dir.string();
                   const TestbenchResult res = runTestbench(cfg, tools);
                   writeRecord(dir, {{"run", q(run)}, {"name", a["name"]}, {"kind", "testbench"}, {"level", "gate"}, {"flow_run", a["flow_run"]}, {"state", "done"}, {"passed", res.passed}, {"period_ns", period},
                                     {"error", q(res.error)}, {"seconds", res.seconds}, {"vcd", q(res.vcd)}});
                   points.append(QJsonObject{{"period_ns", period}, {"passed", res.passed}, {"run", q(run)}, {"error", q(res.error)}});
                   if (res.passed && (fastestPass == 0 || period < fastestPass)) fastestPass = period;
                   if (!res.passed && period > slowestFail) slowestFail = period;
               }
               QJsonObject out{{"points", points}, {"consistent", fastestPass == 0 || slowestFail < fastestPass}};
               if (!corner.empty()) out["corner"] = q(corner);
               if (fastestPass > 0) out["fastest_pass_ns"] = fastestPass;
               if (slowestFail > 0) out["slowest_fail_ns"] = slowestFail;
               return QJsonValue(out);
           },
           true});

    r.add({"test_status", "A test run's state (running, done, stopped, interrupted), whether it passed and why not, and its log's tail.",
           object({{"run", kRun}}),
           [](Context& c, const QJsonObject& a) {
               const fs::path dir = runPath(c, a);
               QJsonObject rec = readJson(dir / "run.json");
#ifndef _WIN32
               if (rec["state"] == "running" && kill((pid_t)rec["pid"].toInteger(), 0) != 0) rec["state"] = "interrupted";  // its process is gone
#endif
               rec.remove("pid");
               rec["log_tail"] = q(tailOf(dir / "sim.log"));
               return QJsonValue(rec);
           },
           true});
    r.add({"test_stop", "Stops a test running in this session.", object({{"run", kRun}}),
           [](Context& c, const QJsonObject& a) { return QJsonValue(jobs(c).cancel(runPath(c, a).string())); }, true});
    r.add({"test_list", "The test runs on disk, per test oldest first: each one's run, level, state and whether it passed.", object({}),
           [](Context& c, const QJsonObject&) {
               QJsonArray out;
               const fs::path root = base(c) / "testing";
               if (!fs::is_directory(root)) return QJsonValue(out);
               std::vector<std::pair<std::string, int>> runs;
               for (const auto& t : fs::directory_iterator(root)) {
                   if (!t.is_directory()) continue;
                   for (const auto& e : fs::directory_iterator(t.path())) {
                       const std::string f = e.path().filename().string();
                       if (e.is_directory() && f.rfind("run.", 0) == 0) runs.push_back({t.path().filename().string(), std::atoi(f.c_str() + 4)});
                   }
               }
               std::sort(runs.begin(), runs.end());
               for (const auto& [name, n] : runs) {
                   const std::string run = name + "/run." + std::to_string(n);
                   try {
                       QJsonObject rec = readJson(root / run / "run.json");
                       rec.remove("pid");
                       out.append(rec);
                   } catch (const std::exception& e) {
                       out.append(QJsonObject{{"run", q(run)}, {"error", q(e.what())}});
                   }
               }
               return QJsonValue(out);
           },
           true});
    r.add({"test_signals", "The signals a test run recorded: name and width.", object({{"run", kRun}}),
           [](Context& c, const QJsonObject& a) {
               const fs::path vcd = runPath(c, a) / "dump.vcd";
               if (!fs::exists(vcd)) throw std::runtime_error("this run recorded no signals");
               return QJsonValue(shownSignals(flow::readVcd(vcd.string(), true)));
           },
           true});
    r.add({"test_value", "A recorded signal's value at a time, as hex, and its changes up to a limit.",
           object({{"run", kRun}, {"signal", param("string", "its full name, as tb.dut.count")}}, {{"time", param("number", "in the dump's units (default its end)")}, {"changes", param("integer", "how many changes to return (default 100)")}}),
           [](Context& c, const QJsonObject& a) {
               const fs::path vcd = runPath(c, a) / "dump.vcd";
               if (!fs::exists(vcd)) throw std::runtime_error("this run recorded no signals");
               const flow::Vcd v = flow::readVcd(vcd.string(), false, {s(a, "signal")});
               for (const auto& sg : v.vars) {
                   if (sg.name != s(a, "signal")) continue;
                   const auto found = v.changes.find(sg.code);
                   static const std::vector<flow::VcdChange> none;
                   const auto& changes = found == v.changes.end() ? none : found->second;
                   const int64_t t = a.contains("time") ? (int64_t)a["time"].toDouble() : v.endTime;
                   const int limit = a["changes"].toInt(100);
                   QJsonArray list;
                   for (size_t k = 0; k < changes.size() && (int)k < limit; ++k) list.append(QJsonArray{(double)changes[k].time, q(flow::toHex(changes[k].value))});
                   return QJsonValue(QJsonObject{{"value", q(flow::toHex(flow::valueAt(changes, t)))}, {"bits", q(flow::valueAt(changes, t))}, {"time", (double)t}, {"end", (double)v.endTime}, {"count", (double)changes.size()}, {"changes", list}});
               }
               throw std::invalid_argument("no signal " + s(a, "signal"));
           },
           true});
}

}  // namespace cmd
