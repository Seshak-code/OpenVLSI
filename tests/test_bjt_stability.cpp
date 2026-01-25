#include <QtTest/QtTest>
#include <vector>
#include <cmath>
#include <iostream>

#include "physics/device_physics.h"
#include "physics/circuitsim.h"
#include "components/circuit.h"
#include "compute/math/emulated_f64.h"

class TestBJTStability : public QObject {
    Q_OBJECT

private:
    // Templated Helper - Moves the logic out of a slot (slots cannot be templates)
    template <typename T>
    void runIsClamping() {
        DiodeParams<T> p;
        p.N = T(1.0);
        p.Vt = T(0.02585);
        p.Tnom = T(300.15);
        p.Temp = T(300.15);
        p.Eg = T(1.11);
        p.Xti = T(3.0);

        // Case 1: Extremely small Is 
        p.Is = T(1e-30); 
        T current = diode_current_clamped(T(0.7), p);
        
        // Convert to double for QTest macros which expect comparable types or scalars
        double current_d = (double)current;
        QVERIFY(std::isfinite(current_d));
        QVERIFY(current_d > 0.0);

        // Case 2: Large Is 
        p.Is = T(1e-6);
        current = diode_current_clamped(T(2.0), p); 
        current_d = (double)current;
        QVERIFY(std::isfinite(current_d));
    }
    
    template <typename T>
    void runThermalSweep() {
        BJTParams<T> p;
        p.Is = T(1e-16);
        p.BetaF = T(100.0);
        p.BetaR = T(1.0);
        p.Vt = T(0.02585); 
        p.Tnom = T(300.15);
        p.Eg = T(1.11);
        p.Xti = T(3.0);
        p.Xtb = T(0.0);

        std::vector<double> temps = {218.15, 273.15, 300.15, 398.15};
        
        double previous_ic = 0.0;
        
        for (double temp : temps) {
             p.Temp = T(temp);
             p.Vt = T(temp / 11604.5); 
             
             // Run physics with type T
             BJTCurrents currents = bjt_ebers_moll(T(5.0), T(0.7), T(0.0), true, p);
             
             // Verify
             double ic_d = (double)currents.Ic;
             double ib_d = (double)currents.Ib;
             
             QVERIFY(std::isfinite(ic_d));
             QVERIFY(std::isfinite(ib_d));
             QVERIFY(ic_d > 0.0);
             
             if (previous_ic != 0.0) {
                 // Qualitative check: current typically increases with temp for fixed Vbe
             }
             previous_ic = ic_d;
        }
    }
    
    template <typename T>
    void runPNJLIM() {
        T Vt = T(0.02585);
        T Is = T(1e-16);
        T Vcrit = compute_vcrit(Vt, Is);
        
        // Case 1: Small step
        T vnew = T(0.6);
        T vold = T(0.59);
        T vlim = pnjlim(vnew, vold, Vt, Vcrit);
        
        // Compare with double tolerances
        double difference = std::abs((double)vlim - (double)vnew);
        QVERIFY(difference < 1e-9);
        
        // Case 2: Huge positive step
        vnew = T(10.0);
        vold = T(0.7);
        vlim = pnjlim(vnew, vold, Vt, Vcrit);
        
        double vlim_d = (double)vlim;
        double vnew_d = (double)vnew;
        double vold_d = (double)vold;
        
        QVERIFY(vlim_d < vnew_d);
        QVERIFY(vlim_d > vold_d);
    }

private slots:
    void initTestCase() {
    }

    // Double (Regression)
    void testIsClamping_Double() { runIsClamping<double>(); }
    void testThermalSweep_Double() { runThermalSweep<double>(); }
    void testPNJLIM_Double() { runPNJLIM<double>(); }
    
    // EmulatedF64 (Architecture Validation)
    void testIsClamping_Emulated() { runIsClamping<EmulatedF64>(); }
    void testThermalSweep_Emulated() { runThermalSweep<EmulatedF64>(); }
    void testPNJLIM_Emulated() { runPNJLIM<EmulatedF64>(); }
};

QTEST_MAIN(TestBJTStability)
#include "test_bjt_stability.moc"
