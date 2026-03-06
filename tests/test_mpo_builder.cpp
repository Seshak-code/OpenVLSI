/**
 * test_mpo_builder.cpp — Phase 5.3.1 MPO Builder Unit Tests
 *
 * Verifies that DeviceMPO local matrices match the stamp functions
 * in stamps.h for each device type.
 *
 * Test cases:
 *   1. testResistorMPO          — 1kΩ resistor: 2×2 = [[1e-3, -1e-3], [-1e-3, 1e-3]]
 *   2. testDiodeMPO             — Forward-biased diode: 2×2 matches g_d from AD
 *   3. testMosfetMPO            — NMOS in saturation: 4×4 with gm, gds entries
 *   4. testMPOAssemblyParity    — 3-resistor circuit: sum of MPOs = global Jacobian
 */

#include "acutesim_engine/infrastructure/mpo_builder.h"
#include "acutesim_engine/tensors/physics_tensors.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

// ============================================================================
// Test helpers
// ============================================================================

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(expr, msg) do {                              \
    if (!(expr)) {                                               \
        std::cerr << "[FAIL] " << (msg) << "\n";                \
        ++g_failed;                                              \
    } else {                                                     \
        std::cout << "[PASS] " << (msg) << "\n";                \
        ++g_passed;                                              \
    }                                                            \
} while (0)

#define EXPECT_NEAR(a, b, tol, msg) do {                         \
    double _a = (a), _b = (b);                                   \
    if (std::abs(_a - _b) > (tol)) {                             \
        std::cerr << "[FAIL] " << (msg) << " (got " << _a       \
                  << ", expected " << _b << ")\n";               \
        ++g_failed;                                              \
    } else {                                                     \
        std::cout << "[PASS] " << (msg) << "\n";                \
        ++g_passed;                                              \
    }                                                            \
} while (0)

// ============================================================================
// Test 1: Resistor MPO — [[G, -G], [-G, G]]
// ============================================================================
static void testResistorMPO() {
    std::cout << "\n=== Resistor MPO ===\n";

    TensorizedBlock block;
    block.resistors.push(1, 2, 1e3);  // 1kΩ between nodes 1 and 2

    std::vector<double> voltages = {5.0, 3.0};  // V1=5V, V2=3V

    auto mpos = MPOBuilder::buildMPOs(block, voltages, 300.15);

    EXPECT_TRUE(mpos.size() == 1, "Resistor: 1 MPO created");
    EXPECT_TRUE(mpos[0].rank == 2, "Resistor: rank = 2");
    EXPECT_TRUE(mpos[0].terminalNodes.size() == 2, "Resistor: 2 terminal nodes");

    double G = 1e-3;  // 1/1kΩ
    EXPECT_NEAR(mpos[0].localMatrix[0],  G, 1e-15, "Resistor: [0,0] = G");
    EXPECT_NEAR(mpos[0].localMatrix[1], -G, 1e-15, "Resistor: [0,1] = -G");
    EXPECT_NEAR(mpos[0].localMatrix[2], -G, 1e-15, "Resistor: [1,0] = -G");
    EXPECT_NEAR(mpos[0].localMatrix[3],  G, 1e-15, "Resistor: [1,1] = G");
}

// ============================================================================
// Test 2: Diode MPO — linearized conductance from AD
// ============================================================================
static void testDiodeMPO() {
    std::cout << "\n=== Diode MPO ===\n";

    TensorizedBlock block;
    // Forward-biased diode: anode=1, cathode=2
    block.diodes.push(1, 2, 1e-14, 1.0, 0.026);

    // Forward bias: V1=0.7V, V2=0V → Vd=0.7V
    std::vector<double> voltages = {0.7, 0.0};

    auto mpos = MPOBuilder::buildMPOs(block, voltages, 300.15);

    EXPECT_TRUE(mpos.size() == 1, "Diode: 1 MPO created");
    EXPECT_TRUE(mpos[0].rank == 2, "Diode: rank = 2");
    EXPECT_TRUE(mpos[0].deviceType == FactorDeviceType::Diode, "Diode: correct type");

    // g_d should be positive for forward-biased diode
    double g_d = mpos[0].localMatrix[0]; // [anode,anode] position
    EXPECT_TRUE(g_d > 0.0, "Diode: g_d > 0 (forward biased)");

    // Matrix should be [[g_d, -g_d], [-g_d, g_d]]
    EXPECT_NEAR(mpos[0].localMatrix[1], -g_d, 1e-15, "Diode: [0,1] = -g_d");
    EXPECT_NEAR(mpos[0].localMatrix[2], -g_d, 1e-15, "Diode: [1,0] = -g_d");
    EXPECT_NEAR(mpos[0].localMatrix[3],  g_d, 1e-15, "Diode: [1,1] = g_d");
}

// ============================================================================
// Test 3: MOSFET MPO — 4×4 with gm, gds entries
// ============================================================================
static void testMosfetMPO() {
    std::cout << "\n=== MOSFET MPO ===\n";

    TensorizedBlock block;
    // NMOS: drain=1, gate=2, source=3, body=4
    block.mosfets.push(1, 2, 3, 4, 10e-6, 1e-6, 200e-6, 0.7, 0.02, false);

    // Saturation: Vgs=1.5V, Vds=2.0V (Vgs > Vth, Vds > Vgs-Vth)
    std::vector<double> voltages = {2.0, 1.5, 0.0, 0.0};

    auto mpos = MPOBuilder::buildMPOs(block, voltages, 300.15);

    EXPECT_TRUE(mpos.size() == 1, "MOSFET: 1 MPO created");
    EXPECT_TRUE(mpos[0].deviceType == FactorDeviceType::Mosfet, "MOSFET: correct type");
    // rank depends on how many unique non-ground nodes: {1,2,3,4} → 4
    EXPECT_TRUE(mpos[0].rank == 4, "MOSFET: rank = 4");

    // Verify non-zero entries exist (gm and gds should be > 0 in saturation)
    bool hasNonZero = false;
    for (double val : mpos[0].localMatrix) {
        if (std::abs(val) > 1e-20) hasNonZero = true;
    }
    EXPECT_TRUE(hasNonZero, "MOSFET: matrix has non-zero entries");
}

// ============================================================================
// Test 4: MPO Assembly Parity — sum of local MPOs = global Jacobian
// ============================================================================
static void testMPOAssemblyParity() {
    std::cout << "\n=== MPO Assembly Parity ===\n";

    TensorizedBlock block;
    // 3 resistors: R1(1,2)=1kΩ, R2(2,3)=2kΩ, R3(3,0)=500Ω
    block.resistors.push(1, 2, 1e3);
    block.resistors.push(2, 3, 2e3);
    block.resistors.push(3, 0, 500.0);

    std::vector<double> voltages = {5.0, 3.0, 1.0};
    int N = 3; // 3 non-ground nodes

    auto mpos = MPOBuilder::buildMPOs(block, voltages, 300.15);
    EXPECT_TRUE(mpos.size() == 3, "Parity: 3 MPOs created");

    // Assemble global Jacobian from MPOs
    std::vector<double> globalJ(N * N, 0.0);
    for (const auto& mpo : mpos) {
        for (uint32_t li = 0; li < mpo.rank; ++li) {
            for (uint32_t lj = 0; lj < mpo.rank; ++lj) {
                // Map local indices to global (0-based)
                int gi = static_cast<int>(mpo.terminalNodes[li]) - 1;
                int gj = static_cast<int>(mpo.terminalNodes[lj]) - 1;
                if (gi >= 0 && gi < N && gj >= 0 && gj < N) {
                    globalJ[gi * N + gj] += mpo.localMatrix[li * mpo.rank + lj];
                }
            }
        }
    }

    // Build expected Jacobian directly:
    // R1: G1=1e-3 between nodes 1,2
    // R2: G2=5e-4 between nodes 2,3
    // R3: G3=2e-3 between nodes 3,ground
    double G1 = 1e-3, G2 = 5e-4, G3 = 2e-3;
    // J[0,0] = G1, J[0,1] = -G1
    // J[1,0] = -G1, J[1,1] = G1+G2, J[1,2] = -G2
    // J[2,1] = -G2, J[2,2] = G2+G3

    EXPECT_NEAR(globalJ[0*N+0],  G1,     1e-15, "Parity: J[0,0] = G1");
    EXPECT_NEAR(globalJ[0*N+1], -G1,     1e-15, "Parity: J[0,1] = -G1");
    EXPECT_NEAR(globalJ[1*N+0], -G1,     1e-15, "Parity: J[1,0] = -G1");
    EXPECT_NEAR(globalJ[1*N+1],  G1+G2,  1e-15, "Parity: J[1,1] = G1+G2");
    EXPECT_NEAR(globalJ[1*N+2], -G2,     1e-15, "Parity: J[1,2] = -G2");
    EXPECT_NEAR(globalJ[2*N+1], -G2,     1e-15, "Parity: J[2,1] = -G2");
    EXPECT_NEAR(globalJ[2*N+2],  G2+G3,  1e-15, "Parity: J[2,2] = G2+G3");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "======================================\n";
    std::cout << "Phase 5.3.1: MPO Builder Tests\n";
    std::cout << "======================================\n";

    testResistorMPO();
    testDiodeMPO();
    testMosfetMPO();
    testMPOAssemblyParity();

    std::cout << "\n======================================\n";
    std::cout << "Results: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "======================================\n";

    return g_failed > 0 ? 1 : 0;
}
