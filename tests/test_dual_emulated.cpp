#include <QtTest/QtTest>
#include <cmath>
#include "autodiff/dual.h"
#include "compute/math/emulated_f64.h"

class TestDualEmulated : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {}

    void testDualDouble() {
        // Baseline check
        Dual<double> x(2.0, 1.0); // x = 2, x' = 1
        // f(x) = x^2 + 1
        Dual<double> y = x * x + 1.0;
        
        QCOMPARE(y.val, 5.0);
        QCOMPARE(y.grad, 4.0);
    }

    void testDualEmulated() {
        // Verify Dual works with complex type T
        Dual<EmulatedF64> x(EmulatedF64(2.0), EmulatedF64(1.0));
        
        // f(x) = x * x + 1.0
        // We rely on implicit conversion of 1.0 -> EmulatedF64 -> Dual<EmulatedF64>?
        // Or explicit construction.
        // Dual operators support (Dual op T) via constructor promotion if defined.
        // Let's be explicit to avoid ambiguity.
        
        Dual<EmulatedF64> one(EmulatedF64(1.0));
        Dual<EmulatedF64> y = x * x + one;
        
        QCOMPARE((double)y.val, 5.0);
        QCOMPARE((double)y.grad, 4.0);
        
        // Test Division: f(x) = 1/x -> f'(x) = -1/x^2
        Dual<EmulatedF64> z = one / x;
        QCOMPARE((double)z.val, 0.5);
        QCOMPARE((double)z.grad, -0.25);
    }
    
    void testDualTranscendental() {
        // f(x) = exp(x)
        Dual<EmulatedF64> x(EmulatedF64(1.0), EmulatedF64(1.0));
        
        using std::exp;
        // This requires exp(Dual<T>) to find exp(T) which finds exp(EmulatedF64) via ADL
        Dual<EmulatedF64> y = exp(x);
        
        double e = std::exp(1.0);
        QVERIFY(std::abs((double)y.val - e) < 1e-6);
        QVERIFY(std::abs((double)y.grad - e) < 1e-6); // d/dx exp(x) = exp(x)
    }
};

QTEST_MAIN(TestDualEmulated)
#include "test_dual_emulated.moc"
