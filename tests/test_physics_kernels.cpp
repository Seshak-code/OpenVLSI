#include <QtTest/QtTest>
#include <vector>
#include <cmath>
#include <iostream>

#include "acutesim_engine/physics/device_physics.h"
#include "acutesim_engine/math/emulated_f64.h"

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
        Mos1Params<T> p;
        p.beta = T(200e-6 * 10.0);  // KP * W/L
        p.vto = T(0.7);
        p.lambda = T(0.02);
        auto id = [&](double vd, double vg) {  // source and bulk at 0
            return static_cast<double>(mos1_currents(T(vd), T(vg), T(0.0), T(0.0), p).d);
        };

        // Cutoff: only the reverse bulk-drain junction (IS + GMIN*Vd) remains
        QVERIFY(std::abs(id(5.0, 0.5)) < 1e-11);

        // Saturation: I = 0.5 * beta * Vov^2 * (1 + lambda*Vds) = 1.859e-3
        QVERIFY(std::abs(id(5.0, 2.0) - 0.5 * 2e-3 * 1.69 * 1.1) < 1e-9);

        // Linear, and the source/drain exchange: Vds < 0 mirrors the current
        QVERIFY(id(0.1, 2.0) > 0.0);
        QVERIFY(std::abs(id(-0.1, 1.9) + id(0.1, 2.0)) < 1e-9);
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
