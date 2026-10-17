// ============================================================================
// test_veriloga_roundtrip.cpp — Verilog-A Generator / Parser / Driver Tests
// ============================================================================
// Tests:
//   1. testGeneratorAllComponents — all 9 types produce expected VA patterns
//   2. testRoundTrip              — generate + parse gives back same component
//                                   counts and parameter values (%.17g precision)
//  2b. testSourceWaveformRoundTrip — V/I PULSE, SINE and AC mag/phase fields
//  2c. testDesignDbSourcePath     — the GUI's DesignDb → DTO path for sources
//   3. testDriverVerilogAPath     — DC simulation via VERILOG_A source matches
//                                   the JSON path for a simple resistive circuit
//   4. testDriverAcNoiseFromDesignDb — GUI-path AC and noise runs give the
//                                   analytic RC answers
// ============================================================================

#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include <atomic>
#include <chrono>
#include <thread>

// Generator (components layer)
#include "components/veriloga_generator.h"
#include "components/design_db.h"

// Parser (orchestration layer)
#include "compute/orchestration/driver/veriloga_parser.h"

// Driver (for integration test)
#include "compute/orchestration/driver/local_worker_driver.h"
#include "engine_api/simulation_dto.h"

// ── Helpers ─────────────────────────────────────────────────────────────────

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [line " << __LINE__ << "]: " #cond "\n"; \
            return false; \
        } \
    } while (0)

#define ASSERT_CONTAINS(haystack, needle) \
    do { \
        if ((haystack).find(needle) == std::string::npos) { \
            std::cerr << "FAIL [line " << __LINE__ << "]: expected to find \"" \
                      << (needle) << "\" in output\n"; \
            return false; \
        } \
    } while (0)

#define ASSERT_NEAR(a, b, eps) \
    do { \
        if (std::fabs((a) - (b)) > (eps)) { \
            std::cerr << "FAIL [line " << __LINE__ << "]: |" \
                      << (a) << " - " << (b) << "| > " << (eps) << "\n"; \
            return false; \
        } \
    } while (0)

// Build a TensorNetlist with all 9 component types for testing
static TensorNetlist makeAllComponentsNetlist() {
    TensorNetlist nl;
    nl.numGlobalNodes = 5;
    auto& b = nl.globalBlock;

    // Resistor R1 (n1→n2, 1000Ω)
    b.addResistor("R1", 1, 2, 1000.0);

    // Capacitor C1 (n2→gnd, 1e-9F)
    b.addCapacitor("C1", 2, 0, 1e-9);

    // Inductor L1 (n3→n4, 1e-6H)
    b.addInductor("L1", 3, 4, 1e-6);

    // VoltageSource V1 (n1→gnd, 5V)
    b.addVoltageSource(1, 0, 5.0);

    // CurrentSource I1 (n2→gnd, 1e-3A)
    b.addCurrentSource(2, 0, 1e-3);

    // Diode D1 (anode=n3, cathode=n4)
    {
        Diode d;
        d.instanceName           = "D1";
        d.anode                  = 3;
        d.cathode                = 4;
        d.saturationCurrent_I_S_A = 1e-14;
        d.emissionCoefficient_N  = 1.0;
        d.thermalVoltage_V_T_V   = 0.02585;
        b.diodes.push_back(d);
    }

    // BJT Q1 NPN (C=n4, B=n3, E=gnd)
    {
        BJT q;
        q.instanceName           = "Q1";
        q.isNPN                  = true;
        q.nodeCollector          = 4;
        q.base                   = 3;
        q.emitter                = 0;
        q.saturationCurrent_I_S_A = 1e-14;
        q.thermalVoltage_V_T_V   = 0.02585;
        q.betaF                  = 100.0;
        q.betaR                  = 1.0;
        b.bjts.push_back(q);
    }

    // MOSFET M1 NMOS (D=n4, G=n3, S=gnd, B=gnd)
    {
        Mosfet m;
        m.instanceName = "M1";
        m.drain        = 4;
        m.gate         = 3;
        m.source       = 0;
        m.body         = 0;
        m.w            = 2e-6;
        m.l            = 0.18e-6;
        m.modelName    = "nfet_01v8";
        b.mosfets.push_back(m);
    }

    // Probe P1
    {
        Probe p;
        p.name         = "Vout";
        p.nodePositive = 2;
        p.nodeNegative = 0;
        b.probes.push_back(p);
    }

    nl.globalState.capacitorState.resize(b.capacitors.size());
    nl.globalState.inductorState.resize(b.inductors.size());
    return nl;
}

// ── Test 1: Generator — all 9 component types ────────────────────────────────

static bool testGeneratorAllComponents() {
    std::cout << "[Test 1] testGeneratorAllComponents ... ";

    auto nl  = makeAllComponentsNetlist();
    auto src = VerilogAGenerator::generate(nl);

    // File header
    ASSERT_CONTAINS(src, "`include \"disciplines.vams\"");
    ASSERT_CONTAINS(src, "module top(");
    ASSERT_CONTAINS(src, "ground gnd;");
    ASSERT_CONTAINS(src, "analog begin");
    ASSERT_CONTAINS(src, "endmodule");

    // Resistor contribution
    ASSERT_CONTAINS(src, "I(n1,n2) <+ V(n1,n2) / R1_val;");
    ASSERT_CONTAINS(src, "parameter real R1_val =");

    // Capacitor contribution (ddt)
    ASSERT_CONTAINS(src, "C1_val * ddt(V(n2,gnd))");
    ASSERT_CONTAINS(src, "parameter real C1_val =");

    // Inductor contribution (ddt of I)
    ASSERT_CONTAINS(src, "L1_val * ddt(I(n3,n4))");
    ASSERT_CONTAINS(src, "parameter real L1_val =");

    // Voltage source
    ASSERT_CONTAINS(src, "V(n1,gnd) <+ V1_val;");

    // Current source
    ASSERT_CONTAINS(src, "I(n2,gnd) <+ I1_val;");

    // Diode (Shockley)
    ASSERT_CONTAINS(src, "D1_Is * (exp(V(n3,n4) / (D1_N * D1_Vt)) - 1.0)");

    // BJT (Ebers-Moll)
    ASSERT_CONTAINS(src, "Q1_em");
    ASSERT_CONTAINS(src, "Q1_alphaF");
    ASSERT_CONTAINS(src, "Q1_IF");
    ASSERT_CONTAINS(src, "Q1_IR");

    // MOSFET (Shichman-Hodges)
    ASSERT_CONTAINS(src, "M1_sh");
    ASSERT_CONTAINS(src, "M1_Vgst");
    ASSERT_CONTAINS(src, "I(n4,gnd) <+ M1_Id");

    // Probe annotation
    ASSERT_CONTAINS(src, "// .probe V(n2,gnd) \"Vout\"");

    std::cout << "PASS\n";
    return true;
}

// ── Test 2: Round-trip — generate → parse → verify ──────────────────────────

static bool testRoundTrip() {
    std::cout << "[Test 2] testRoundTrip ... ";

    auto nl  = makeAllComponentsNetlist();
    auto src = VerilogAGenerator::generate(nl);

    auto res = acutesim::compute::orchestration::parseVerilogA(src);
    ASSERT_TRUE(res.success);

    const auto& b = res.netlist.globalBlock;

    // Component counts
    ASSERT_TRUE(b.resistors.size()     == 1);
    ASSERT_TRUE(b.capacitors.size()    == 1);
    ASSERT_TRUE(b.inductors.size()     == 1);
    ASSERT_TRUE(b.voltageSources.size()== 1);
    ASSERT_TRUE(b.currentSources.size()== 1);
    ASSERT_TRUE(b.diodes.size()        == 1);
    ASSERT_TRUE(b.bjts.size()          == 1);
    ASSERT_TRUE(b.mosfets.size()       == 1);
    ASSERT_TRUE(b.probes.size()        == 1);

    // Resistor value preserved (%.17g round-trip)
    ASSERT_NEAR(b.resistors[0].resistance_ohms, 1000.0, 1e-10);
    ASSERT_TRUE(b.resistors[0].nodeTerminal1 == 1);
    ASSERT_TRUE(b.resistors[0].nodeTerminal2 == 2);

    // Capacitor
    ASSERT_NEAR(b.capacitors[0].capacitance_farads, 1e-9, 1e-20);

    // Inductor
    ASSERT_NEAR(b.inductors[0].inductance_henries, 1e-6, 1e-18);

    // Voltage source
    ASSERT_NEAR(b.voltageSources[0].voltage_V, 5.0, 1e-10);
    ASSERT_TRUE(b.voltageSources[0].nodePositive == 1);
    ASSERT_TRUE(b.voltageSources[0].nodeNegative == 0);

    // Current source
    ASSERT_NEAR(b.currentSources[0].current_A, 1e-3, 1e-15);

    // Diode parameters
    ASSERT_NEAR(b.diodes[0].saturationCurrent_I_S_A, 1e-14, 1e-25);
    ASSERT_NEAR(b.diodes[0].emissionCoefficient_N,   1.0,   1e-10);
    ASSERT_NEAR(b.diodes[0].thermalVoltage_V_T_V,    0.02585, 1e-10);

    // BJT
    ASSERT_TRUE(b.bjts[0].isNPN);
    ASSERT_NEAR(b.bjts[0].betaF, 100.0, 1e-10);
    ASSERT_TRUE(b.bjts[0].nodeCollector == 4);
    ASSERT_TRUE(b.bjts[0].base         == 3);

    // MOSFET
    ASSERT_NEAR(b.mosfets[0].w, 2e-6,    1e-18);
    ASSERT_NEAR(b.mosfets[0].l, 0.18e-6, 1e-19);
    ASSERT_TRUE(b.mosfets[0].modelName == "nfet_01v8");

    // Probe
    ASSERT_TRUE(b.probes[0].name        == "Vout");
    ASSERT_TRUE(b.probes[0].nodePositive == 2);
    ASSERT_TRUE(b.probes[0].nodeNegative == 0);

    std::cout << "PASS\n";
    return true;
}

// ── Test 2b: Source waveform + AC fields survive generate → parse ───────────
// Every field gets a distinct non-zero value, so a dropped field parses back
// as 0 and fails its own check.

template <class Src>
static bool sameSourceFields(const Src& got, const Src& want) {
#define CHECK_FIELD(f) \
    if (got.f != want.f) { \
        std::cerr << "FAIL: " #f " = " << got.f << ", expected " << want.f << "\n"; \
        return false; \
    }
    CHECK_FIELD(name) CHECK_FIELD(nodePositive) CHECK_FIELD(nodeNegative) CHECK_FIELD(type)
    CHECK_FIELD(ac_mag) CHECK_FIELD(ac_phase)
    CHECK_FIELD(pulse_v1) CHECK_FIELD(pulse_v2) CHECK_FIELD(pulse_td) CHECK_FIELD(pulse_tr)
    CHECK_FIELD(pulse_tf) CHECK_FIELD(pulse_pw) CHECK_FIELD(pulse_per)
    CHECK_FIELD(sine_vo) CHECK_FIELD(sine_va) CHECK_FIELD(sine_freq) CHECK_FIELD(sine_td)
    CHECK_FIELD(sine_theta) CHECK_FIELD(sine_phase)
#undef CHECK_FIELD
    return true;
}

template <class Src>
static Src makePulse(int np, double ac) {
    Src s{};
    s.nodePositive = np; s.nodeNegative = 0; s.type = "PULSE";
    s.pulse_v1 = 0.1; s.pulse_v2 = 1.7; s.pulse_td = 1e-9; s.pulse_tr = 2e-10;
    s.pulse_tf = 3e-10; s.pulse_pw = 5e-9; s.pulse_per = 1.1e-8;
    s.ac_mag = ac; s.ac_phase = 45.0;
    return s;
}

template <class Src>
static Src makeSine(int np, double ac) {
    Src s{};
    s.nodePositive = np; s.nodeNegative = 0; s.type = "SINE";
    s.sine_vo = 0.9; s.sine_va = 0.3; s.sine_freq = 1e6; s.sine_td = 2e-6;
    s.sine_theta = 1e3; s.sine_phase = 30.0;
    s.ac_mag = ac; s.ac_phase = -90.0;
    return s;
}

static bool testSourceWaveformRoundTrip() {
    std::cout << "[Test 2b] testSourceWaveformRoundTrip ... ";

    TensorNetlist nl;
    auto& b = nl.globalBlock;
    auto vp = makePulse<VoltageSource>(1, 1.0);
    vp.name = "Vclk";  // named source: params and contribution must agree
    vp.voltage_V  = 0.1;
    auto vs = makeSine<VoltageSource>(2, 0.5);
    vs.name = "Vsin";
    vs.voltage_V  = 0.9;
    auto ip = makePulse<CurrentSource>(3, 1e-3);
    ip.name       = "Ia";
    ip.current_A  = 0.1;
    auto is = makeSine<CurrentSource>(4, 2e-3);
    is.name       = "Ib";
    is.current_A  = 0.9;
    b.voltageSources = {vp, vs};
    b.currentSources = {ip, is};
    nl.numGlobalNodes = 4;

    auto res = acutesim::compute::orchestration::parseVerilogA(VerilogAGenerator::generate(nl));
    ASSERT_TRUE(res.success);
    const auto& p = res.netlist.globalBlock;
    ASSERT_TRUE(p.voltageSources.size() == 2);
    ASSERT_TRUE(p.currentSources.size() == 2);

    ASSERT_TRUE(p.voltageSources[0].voltage_V == vp.voltage_V);
    ASSERT_TRUE(p.voltageSources[1].voltage_V == vs.voltage_V);
    ASSERT_TRUE(p.currentSources[0].current_A == ip.current_A);
    ASSERT_TRUE(p.currentSources[1].current_A == is.current_A);
    ASSERT_TRUE(sameSourceFields(p.voltageSources[0], vp));
    ASSERT_TRUE(sameSourceFields(p.voltageSources[1], vs));
    ASSERT_TRUE(sameSourceFields(p.currentSources[0], ip));
    ASSERT_TRUE(sameSourceFields(p.currentSources[1], is));

    std::cout << "PASS\n";
    return true;
}

// ── Test 2c: GUI path — DesignDb → DTO → generate → parse ───────────────────
// The schematic fills a DesignDb exactly like this. Catches: sub-1e-6 values
// lost to 6-decimal formatting, dropped current sources, source names (noise
// finds its input source by name), VSS not grounded, netIndex disagreeing
// with the nodes the netlist actually uses.

static bool testDesignDbSourcePath() {
    std::cout << "[Test 2c] testDesignDbSourcePath ... ";
    using acutesim::components::DesignDb;

    DesignDb db;
    auto in = db.createNet("in"), out = db.createNet("out"), vss = db.createNet("VSS");
    auto two = [&](const char* master, const char* name, decltype(in) a, decltype(in) b) {
        auto id = db.createInstance(master, name);
        db.connectPin(id, 0, a);
        db.connectPin(id, 1, b);
        return id;
    };
    auto v = two("VoltageSource", "Vin", in, vss);
    db.setParam(v, "V", 0.25);
    db.setStringParam(v, "type", "PULSE");
    const double pv[] = {0.1, 1.7, 1e-9, 2e-11, 3e-11, 5e-9, 1.1e-8};
    const char* pk[]  = {"v1", "v2", "td", "tr", "tf", "pw", "per"};
    for (int k = 0; k < 7; ++k) db.setParam(v, pk[k], pv[k]);
    db.setParam(v, "acmag", 1.0);
    db.setParam(v, "acphase", 45.0);

    auto i = two("CurrentSource", "Ibias", out, vss);
    db.setParam(i, "I", 1e-7);
    db.setStringParam(i, "type", "SINE");
    const double sv[] = {1e-7, 2e-8, 1e6, 2e-6, 1e3, 30.0};
    const char* sk[]  = {"vo", "va", "freq", "td", "theta", "phase"};
    for (int k = 0; k < 6; ++k) db.setParam(i, sk[k], sv[k]);
    db.setParam(i, "acmag", 3e-9);
    db.setParam(i, "acphase", -90.0);

    auto c = two("Capacitor", "C1", out, vss);
    db.setParam(c, "C", 1e-12);

    auto hier = db.recursiveBuild([](const std::string&, const std::map<std::string, double>&) {
        return std::optional<acutesim::compute::orchestration::HierarchicalSchematicDTO>{};
    });
    auto res = acutesim::compute::orchestration::parseVerilogA(VerilogAGenerator::generate(hier));
    ASSERT_TRUE(res.success);
    const auto& p = res.netlist.globalBlock;

    const int nIn = VerilogAGenerator::netIndex(hier, "in"), nOut = VerilogAGenerator::netIndex(hier, "out");
    ASSERT_TRUE(nIn > 0 && nOut > 0 && nIn != nOut);
    ASSERT_TRUE(VerilogAGenerator::netIndex(hier, "VSS") == 0);
    ASSERT_TRUE(VerilogAGenerator::netIndex(hier, "nope") == -1);

    VoltageSource ev{};
    ev.name = "Vin"; ev.nodePositive = nIn; ev.nodeNegative = 0; ev.type = "PULSE";
    ev.pulse_v1 = pv[0]; ev.pulse_v2 = pv[1]; ev.pulse_td = pv[2]; ev.pulse_tr = pv[3];
    ev.pulse_tf = pv[4]; ev.pulse_pw = pv[5]; ev.pulse_per = pv[6];
    ev.ac_mag = 1.0; ev.ac_phase = 45.0;
    CurrentSource ei{};
    ei.name = "Ibias"; ei.nodePositive = nOut; ei.nodeNegative = 0; ei.type = "SINE";
    ei.sine_vo = sv[0]; ei.sine_va = sv[1]; ei.sine_freq = sv[2]; ei.sine_td = sv[3];
    ei.sine_theta = sv[4]; ei.sine_phase = sv[5];
    ei.ac_mag = 3e-9; ei.ac_phase = -90.0;

    ASSERT_TRUE(p.voltageSources.size() == 1);
    ASSERT_TRUE(p.currentSources.size() == 1);
    ASSERT_TRUE(p.capacitors.size() == 1);
    ASSERT_TRUE(p.voltageSources[0].voltage_V == 0.25);
    ASSERT_TRUE(p.currentSources[0].current_A == 1e-7);
    ASSERT_TRUE(p.capacitors[0].capacitance_farads == 1e-12);
    ASSERT_TRUE(p.capacitors[0].nodePlate1 == nOut);
    ASSERT_TRUE(sameSourceFields(p.voltageSources[0], ev));
    ASSERT_TRUE(sameSourceFields(p.currentSources[0], ei));

    std::cout << "PASS\n";
    return true;
}

// ── Test 3: Driver integration — VA source vs JSON source for DC ─────────────

static bool testDriverVerilogAPath() {
    std::cout << "[Test 3] testDriverVerilogAPath ... ";

    // Build a simple resistor divider: V1(5V) → R1(1kΩ) → node1 → R2(1kΩ) → GND
    TensorNetlist nl;
    nl.numGlobalNodes = 2;
    auto& b = nl.globalBlock;
    b.addVoltageSource(1, 0, 5.0);
    b.addResistor("R1", 1, 2, 1000.0);
    b.addResistor("R2", 2, 0, 1000.0);
    {
        Probe p;
        p.name = "Vmid"; p.nodePositive = 2; p.nodeNegative = 0;
        b.probes.push_back(p);
    }
    nl.globalState.capacitorState.resize(0);
    nl.globalState.inductorState.resize(0);

    // Generate Verilog-A source
    std::string vaSrc = VerilogAGenerator::generate(nl);

    // Build the DTO with VERILOG_A format
    using namespace acutesim::compute::orchestration;
    SimulationRequestDTO req;
    req.analysis               = AnalysisTypeDTO::DC;
    req.source.format          = NetlistFormatDTO::VERILOG_A;
    req.source.payload         = vaSrc;
    req.topologyHash           = 0;
    req.sessionToken           = "test_va_path";

    // Submit to driver synchronously (EMSCRIPTEN mode executes inline)
    LocalWorkerDriver driver;

    std::atomic<bool> completed{false};
    std::atomic<bool> success{false};
    double vmidValue = -1.0;

    driver.onJobCompleted = [&](uint64_t /*id*/,
                                const SimulationResponseDTO& resp) {
        success   = resp.success;
        auto it = resp.probeValues.find("Vmid");
        if (it != resp.probeValues.end() && !it->second.empty()) {
            vmidValue = it->second[0];
        }
        completed = true;  // set last so every result is visible before completed
    };

#ifdef __EMSCRIPTEN__
    driver.submitJob(req);
#else
    // On desktop the worker runs in a background thread — use a brief spin
    driver.submitJob(req);

    // Wait up to 5 seconds for completion
    auto start = std::chrono::steady_clock::now();
    while (!completed) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() > 5) {
            std::cerr << "FAIL: timeout waiting for DC result\n";
            return false;
        }
    }
#endif

    ASSERT_TRUE(completed);
    ASSERT_TRUE(success);
    // V_mid = V1 * R2/(R1+R2) = 5 * 0.5 = 2.5V ± 1mV
    ASSERT_NEAR(vmidValue, 2.5, 1e-3);

    std::cout << "PASS (Vmid = " << vmidValue << " V)\n";
    return true;
}

// ── Test 4: GUI AC + noise through the driver ──────────────────────────────
// RC low-pass built the way the schematic fills DesignDb (Vin AC 1 → R1 1k →
// out, C1 1µF → VSS). AC at fc must give |V(out)| = 1/√2 (zero if the AC
// magnitude is dropped); noise must find "Vin" by name and give the resistor's
// thermal floor √(4kTR) at low frequency.

static acutesim::compute::orchestration::SimulationResponseDTO
runJob(const acutesim::compute::orchestration::SimulationRequestDTO& req) {
    acutesim::compute::orchestration::LocalWorkerDriver driver;
    acutesim::compute::orchestration::SimulationResponseDTO out;
    std::atomic<bool> done{false};
    driver.onJobCompleted = [&](uint64_t, const acutesim::compute::orchestration::SimulationResponseDTO& r) {
        out = r;
        done = true;
    };
    driver.submitJob(req);
    for (int i = 0; i < 500 && !done; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return out;
}

static bool testDriverAcNoiseFromDesignDb() {
    std::cout << "[Test 4] testDriverAcNoiseFromDesignDb ... ";
    using namespace acutesim::compute::orchestration;
    acutesim::components::DesignDb db;
    auto in = db.createNet("in"), out = db.createNet("out"), vss = db.createNet("VSS");
    auto two = [&](const char* master, const char* name, decltype(in) a, decltype(in) b) {
        auto id = db.createInstance(master, name);
        db.connectPin(id, 0, a);
        db.connectPin(id, 1, b);
        return id;
    };
    auto v = two("VoltageSource", "Vin", in, vss);
    db.setParam(v, "V", 0.0);
    db.setStringParam(v, "type", "DC");
    db.setParam(v, "acmag", 1.0);
    db.setParam(two("Resistor", "R1", in, out), "R", 1e3);
    db.setParam(two("Capacitor", "C1", out, vss), "C", 1e-6);
    auto hier = db.recursiveBuild([](const std::string&, const std::map<std::string, double>&) {
        return std::optional<HierarchicalSchematicDTO>{};
    });
    const int nOut = VerilogAGenerator::netIndex(hier, "out");
    ASSERT_TRUE(nOut > 0);

    SimulationRequestDTO req;
    req.source.format  = NetlistFormatDTO::VERILOG_A;
    req.source.payload = VerilogAGenerator::generate(hier);
    req.analysis = AnalysisTypeDTO::AC;
    const double fc = 1.0 / (2.0 * M_PI * 1e3 * 1e-6);
    req.parameters = {{"fStart", std::to_string(fc)}, {"fStop", std::to_string(fc)},
                      {"points", "1"}, {"sweepType", "lin"}};
    auto ac = runJob(req);
    ASSERT_TRUE(ac.success);
    ASSERT_TRUE(ac.nodeVoltages.count(nOut - 1) && ac.nodeVoltages.at(nOut - 1).size() == 1);
    ASSERT_NEAR(ac.nodeVoltages.at(nOut - 1)[0], 1.0 / std::sqrt(2.0), 1e-4);

    req.analysis = AnalysisTypeDTO::NOISE;
    req.parameters = {{"fStart", "1"}, {"fStop", "10"}, {"points", "2"}, {"sweepType", "log"},
                      {"inputSource", "Vin"}, {"outputNode", std::to_string(nOut)}, {"refNode", "0"}};
    auto nz = runJob(req);
    if (!nz.success) std::cerr << "noise error: " << nz.errorDetail << "\n";
    ASSERT_TRUE(nz.success && nz.extended.hasNoise);
    ASSERT_TRUE(nz.extended.noise.onoise.size() == 2);
    const double floor = std::sqrt(4.0 * 1.380649e-23 * 300.0 * 1e3);  // ≈ 4.07 nV/√Hz
    ASSERT_NEAR(nz.extended.noise.onoise[0], floor, 0.05 * floor);

    std::cout << "PASS (|H(fc)| = " << ac.nodeVoltages.at(nOut - 1)[0]
              << ", onoise(1 Hz) = " << nz.extended.noise.onoise[0] << " V/rtHz)\n";
    return true;
}

// ── Entry point ──────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== test_veriloga_roundtrip ===\n";

    int failed = 0;
    if (!testGeneratorAllComponents()) ++failed;
    if (!testRoundTrip())              ++failed;
    if (!testSourceWaveformRoundTrip()) ++failed;
    if (!testDesignDbSourcePath())     ++failed;
    if (!testDriverVerilogAPath())     ++failed;
    if (!testDriverAcNoiseFromDesignDb()) ++failed;

    if (failed == 0) {
        std::cout << "All tests PASSED.\n";
        return 0;
    } else {
        std::cout << failed << " test(s) FAILED.\n";
        return 1;
    }
}
