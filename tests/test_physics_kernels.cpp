#include <QtTest/QtTest>
#include <vector>
#include <cmath>
#include <iostream>

#include "physics/device_physics.h"
#include "compute/math/emulated_f64.h"

class TestPhysicsKernels : public QObject {
    Q_OBJECT

private:
    // ------------------------------------------------------------------------
    // Helper: Run Diode Checks
    // ------------------------------------------------------------------------
    template <typename T>
    void runDiodeChecks() {
        DiodeParams<T> p;
        p.Is = T(1e-14);
        p.N = T(1.0);
        p.Vt = T(0.02585);
        p.Rs = T(0.0);
        
        // Test Forward Bias
        T vd = T(0.7);
        T id = diode_current(vd, p);
        T id_clamped = diode_current_clamped(vd, p);
        
        QVERIFY(static_cast<double>(id) > 0.0);
        QVERIFY(std::abs(static_cast<double>(id - id_clamped)) < 1e-9);
        
        // Test Reverse Bias
        vd = T(-1.0);
        id = diode_current(vd, p);
        // Is * (exp(-large) - 1) ~= -Is
        double val = static_cast<double>(id);
        double target = -1.0e-14;
        QVERIFY(std::abs(val - target) < 1e-16);
    }

    // ------------------------------------------------------------------------
    // Helper: Run MOSFET Checks
    // ------------------------------------------------------------------------
    template <typename T>
    void runMosfetChecks() {
        MosfetParams<T> p;
        p.Kp = T(200e-6);
        p.W = T(10e-6);
        p.L = T(1e-6);
        p.Vth = T(0.7);
        p.lambda = T(0.02);
        
        // Cutoff
        T vgs = T(0.5);
        T vds = T(5.0);
        T ids = mosfet_ids(vgs, vds, p);
        QCOMPARE(static_cast<double>(ids), 0.0);
        
        // Saturation
        vgs = T(2.0); // Vov = 1.3
        vds = T(5.0); // Vds > Vov
        ids = mosfet_ids(vgs, vds, p);
        
        // I = 0.5 * Kp * (W/L) * Vov^2 * (1 + lambda*Vds)
        // I = 0.5 * 200e-6 * 10 * 1.69 * (1 + 0.1)
        // I = 1000e-6 * 1.69 * 1.1 = 1.859e-3 roughly
        double val = static_cast<double>(ids);
        QVERIFY(val > 1.8e-3 && val < 1.9e-3);
        
        // Linear
        vds = T(0.1); // Small Vds
        ids = mosfet_ids(vgs, vds, p);
        QVERIFY(static_cast<double>(ids) > 0.0);
    }
    
    // ------------------------------------------------------------------------
    // Helper: Run BJT Checks (Merged from test_bjt_stability)
    // ------------------------------------------------------------------------
    template <typename T>
    void runBJTChecks() {
        BJTParams<T> p;
        p.Is = T(1e-16);
        p.BetaF = T(100.0);
        p.BetaR = T(1.0);
        p.Vt = T(0.02585);
        
        // Active Region
        T vc = T(5.0);
        T vb = T(0.7);
        T ve = T(0.0);
        
        BJTCurrents<T> currents = bjt_ebers_moll(vc, vb, ve, true, p);
        
        double ic = static_cast<double>(currents.Ic);
        double ib = static_cast<double>(currents.Ib);
        double beta = ic / ib;
        
        QVERIFY(ic > 0.0);
        QVERIFY(beta > 90.0 && beta < 110.0); // Should be near BetaF
    }

private slots:
    void initTestCase() {}
    
    // Runner Wrappers
    void testDiode_Double() { runDiodeChecks<double>(); }
    void testDiode_Emulated() { runDiodeChecks<EmulatedF64>(); }
    
    void testMosfet_Double() { runMosfetChecks<double>(); }
    void testMosfet_Emulated() { runMosfetChecks<EmulatedF64>(); }
    
    void testBJT_Double() { runBJTChecks<double>(); }
    void testBJT_Emulated() { runBJTChecks<EmulatedF64>(); }
};

QTEST_MAIN(TestPhysicsKernels)
#include "test_physics_kernels.moc"
