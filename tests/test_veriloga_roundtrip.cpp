// ============================================================================
// test_veriloga_roundtrip.cpp — Verilog-A Generator / Parser / Driver Tests
// ============================================================================
// Tests:
//   1. testGeneratorAllComponents — all 9 types produce expected VA patterns
//   2. testRoundTrip              — generate + parse gives back same component
//                                   counts and parameter values (%.17g precision)
//   3. testDriverVerilogAPath     — DC simulation via VERILOG_A source matches
//                                   the JSON path for a simple resistive circuit
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
    nl.globalState.powerRailState.resize(0);

    // Generate Verilog-A source
    std::string vaSrc = VerilogAGenerator::generate(nl);

    // Build the DTO with VERILOG_A format
    using namespace acutesim::compute::orchestration;
    SimulationRequestDTO req;
    req.analysis               = AnalysisTypeDTO::DC;
    req.source.format          = NetlistFormatDTO::VERILOG_A;
    req.source.payload         = vaSrc;
    req.netlistJson            = "{}";  // empty — driver must use VA path
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
        completed = true;  // set last so success is visible before completed
        auto it = resp.probeValues.find("Vmid");
        if (it != resp.probeValues.end() && !it->second.empty()) {
            vmidValue = it->second[0];
        }
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

// ── Entry point ──────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== test_veriloga_roundtrip ===\n";

    int failed = 0;
    if (!testGeneratorAllComponents()) ++failed;
    if (!testRoundTrip())              ++failed;
    if (!testDriverVerilogAPath())     ++failed;

    if (failed == 0) {
        std::cout << "All tests PASSED.\n";
        return 0;
    } else {
        std::cout << failed << " test(s) FAILED.\n";
        return 1;
    }
}
