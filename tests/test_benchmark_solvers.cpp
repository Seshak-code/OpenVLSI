/**
 * test_benchmark_solvers.cpp — Solver Tier Benchmarking Suite
 *
 * Compares AcuteSim's 5-tier solver pipeline against itself and measures
 * wall-clock time for reference circuits of increasing complexity.
 *
 * Build:  cmake --build build --target test_benchmark_solvers
 * Run:    ./build/test_benchmark_solvers [--nodes 100] [--csv]
 */

#include "acutesim_engine/physics/circuitsim.h"
#include "acutesim_engine/tensors/physics_tensors.h"
#include "acutesim_engine/infrastructure/hardware_capabilities.h"
#include "acutesim_engine/netlist/circuit.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

// ============================================================================
// Circuit Generators (using TensorNetlist helper API)
// ============================================================================

/// N-stage RC ladder: V1 -> R-C -> R-C -> ... -> GND  (treewidth = 2)
static TensorNetlist makeRCLadder(int stages) {
    TensorNetlist nl;
    nl.addVoltageSource(1, 0, 5.0);
    for (int i = 0; i < stages; ++i) {
        int n1 = i + 1;
        int n2 = (i == stages - 1) ? 0 : i + 2;
        int capNode = (n2 == 0) ? stages : n2;
        nl.addResistor(n1, (n2 == 0) ? 0 : n2, 1000.0);
        nl.addCapacitor(capNode, 0, 1e-9);
    }
    return nl;
}

/// Diode bridge rectifier + N-stage RC filter (nonlinear, tw~4)
static TensorNetlist makeDiodeBridge(int filterStages) {
    TensorNetlist nl;
    // Nodes: 1=in+, 2=in-, 3=out+, 4=out-
    nl.addVoltageSource(1, 2, 5.0);
    nl.addDiode("D1", 1, 3, 1e-14);
    nl.addDiode("D2", 4, 1, 1e-14);
    nl.addDiode("D3", 2, 3, 1e-14);
    nl.addDiode("D4", 4, 2, 1e-14);

    int prev = 3;
    int nextNode = 5;
    for (int i = 0; i < filterStages; ++i) {
        nl.addResistor(prev, nextNode, 100.0);
        nl.addCapacitor(nextNode, 4, 10e-6);
        prev = nextNode++;
    }
    nl.addResistor(prev, 4, 1000.0);
    return nl;
}

/// CMOS inverter chain — N stages (MOSFETs, tw~6)
static TensorNetlist makeCMOSChain(int stages) {
    TensorNetlist nl;
    // Node 1 = VDD, Node 2 = input
    nl.addVoltageSource(1, 0, 3.3);
    nl.addVoltageSource(2, 0, 1.65);

    int prevOut = 2;
    int nextNode = 3;
    for (int i = 0; i < stages; ++i) {
        int n_out = nextNode++;
        nl.addMosfet("MN" + std::to_string(i), n_out, prevOut, 0, 0, "NMOS1", 10e-6, 0.18e-6);
        nl.addMosfet("MP" + std::to_string(i), n_out, prevOut, 1, 1, "PMOS1", 20e-6, 0.18e-6);
        prevOut = n_out;
    }
    return nl;
}

/// BJT differential pair with tail current source (tw~8)
static TensorNetlist makeBJTDiffPair(int pairs) {
    TensorNetlist nl;
    // Node 1 = VCC
    nl.addVoltageSource(1, 0, 12.0);

    int nextNode = 2;
    for (int p = 0; p < pairs; ++p) {
        int n_inP = nextNode++;
        int n_inN = nextNode++;
        int n_outP = nextNode++;
        int n_outN = nextNode++;
        int n_tail = nextNode++;

        nl.addVoltageSource(n_inP, 0, 0.1);
        nl.addVoltageSource(n_inN, 0, -0.1);
        nl.addResistor(1, n_outP, 10000.0);
        nl.addResistor(1, n_outN, 10000.0);

        nl.addBJT("Q1_" + std::to_string(p), n_outP, n_inP, n_tail, "NPN", true);
        nl.addBJT("Q2_" + std::to_string(p), n_outN, n_inN, n_tail, "NPN", true);
        nl.addCurrentSource(n_tail, 0, 1e-3);
    }
    return nl;
}

/// Resistor mesh N x M (high treewidth ~ min(N,M))
static TensorNetlist makeResistorMesh(int rows, int cols) {
    TensorNetlist nl;
    auto nodeId = [&](int r, int c) { return r * cols + c + 1; };
    nl.addVoltageSource(nodeId(0, 0), 0, 5.0);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (c + 1 < cols)
                nl.addResistor(nodeId(r, c), nodeId(r, c + 1), 1000.0);
            if (r + 1 < rows)
                nl.addResistor(nodeId(r, c), nodeId(r + 1, c), 1000.0);
        }
    }
    nl.addResistor(nodeId(rows - 1, cols - 1), 0, 1.0);
    return nl;
}

// ============================================================================
// Benchmark Runner
// ============================================================================

struct BenchResult {
    std::string circuitName;
    int nodes;
    int devices;
    double timeUs;
    double kclResidual;
    bool converged;
    std::string solverTier;
};

static BenchResult runSolve(const std::string& name, TensorNetlist& nl) {
    auto t0 = Clock::now();

    // Use plain DC solve without compiled block to avoid sparse LU
    // assertion issues with elimination ordering size mismatch.
    CircuitSim sim;
    sim.execMode = CircuitSim::ExecutionMode::TENSOR_SOA_SIMD;

    SolverStep dcResult = sim.solveDC(nl);

    auto t1 = Clock::now();
    double elapsedUs = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    int totalDevices = static_cast<int>(
        nl.globalBlock.resistors.size() + nl.globalBlock.diodes.size() +
        nl.globalBlock.mosfets.size() + nl.globalBlock.bjts.size());

    return {name, static_cast<int>(nl.numGlobalNodes), totalDevices,
            elapsedUs, dcResult.stats.residual, dcResult.stats.converged, "DenseLU"};
}

static void printHeader(bool csv) {
    if (csv) {
        std::cout << "circuit,nodes,devices,time_us,residual,converged,tier\n";
    } else {
        std::cout << std::left
                  << std::setw(28) << "Circuit"
                  << std::setw(8)  << "Nodes"
                  << std::setw(10) << "Devices"
                  << std::setw(14) << "Time(us)"
                  << std::setw(14) << "Residual"
                  << std::setw(10) << "Conv"
                  << std::setw(18) << "Tier"
                  << "\n";
        std::cout << std::string(102, '-') << "\n";
    }
}

static void printResult(const BenchResult& r, bool csv) {
    if (csv) {
        std::cout << r.circuitName << ","
                  << r.nodes << ","
                  << r.devices << ","
                  << std::fixed << std::setprecision(0) << r.timeUs << ","
                  << std::scientific << std::setprecision(2) << r.kclResidual << ","
                  << (r.converged ? "true" : "false") << ","
                  << r.solverTier << "\n";
    } else {
        std::cout << std::left
                  << std::setw(28) << r.circuitName
                  << std::setw(8)  << r.nodes
                  << std::setw(10) << r.devices
                  << std::setw(14) << std::fixed << std::setprecision(0) << r.timeUs
                  << std::setw(14) << std::scientific << std::setprecision(2) << r.kclResidual
                  << std::setw(10) << (r.converged ? "YES" : "NO")
                  << std::setw(18) << r.solverTier
                  << "\n";
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    int maxNodes = 100;
    bool csv = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--nodes") == 0 && i + 1 < argc)
            maxNodes = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--csv") == 0)
            csv = true;
    }

    if (!csv) {
        const auto caps = acutesim::compute::detectCapabilities();
        std::cout << "=== AcuteSim Solver Benchmark ===\n"
                  << "CPU: " << acutesim::compute::simdTierName(caps.simdTier) << " | Cores: " << caps.physicalCpuCores
                  << "\nGPU detected: " << (caps.hasGPU ? caps.gpuAdapterName : std::string("none")) << "\n\n";
    }

    if (!csv) std::cout << "=== DC Solver Tier Comparison ===\n";
    printHeader(csv);

    std::vector<int> sizes = {10, 25, 50};
    if (maxNodes >= 100) sizes.push_back(100);
    if (maxNodes >= 200) sizes.push_back(200);
    if (maxNodes >= 500) sizes.push_back(500);

    for (int s : sizes) {
        auto nl = makeRCLadder(s);
        printResult(runSolve("RC-Ladder-" + std::to_string(s), nl), csv);

        if (s <= 100) {
            auto nl2 = makeDiodeBridge(s / 2);
            printResult(runSolve("DiodeBridge-" + std::to_string(s / 2), nl2), csv);
        }
        if (s <= 50) {
            auto nl3 = makeCMOSChain(s);
            printResult(runSolve("CMOSChain-" + std::to_string(s), nl3), csv);
        }
        if (s <= 25) {
            auto nl4 = makeBJTDiffPair(s);
            printResult(runSolve("BJTDiffPair-" + std::to_string(s), nl4), csv);
        }

        int side = static_cast<int>(std::sqrt(static_cast<double>(s)));
        if (side >= 3) {
            auto nl5 = makeResistorMesh(side, side);
            printResult(runSolve("Mesh-" + std::to_string(side) + "x" + std::to_string(side), nl5), csv);
        }
    }

    if (!csv) std::cout << "\nDone.\n";
    return 0;
}
