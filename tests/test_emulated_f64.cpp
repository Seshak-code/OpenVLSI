#include <QtTest/QtTest>
#include <cmath>
#include "compute/math/emulated_f64.h"

class TestEmulatedF64 : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Setup
    }

    void testConstruction() {
        EmulatedF64 a(1.5);
        double val = (double)a;
        QCOMPARE(val, 1.5);
    }

    void testArithmetic() {
        EmulatedF64 a(2.0);
        EmulatedF64 b(3.0);
        
        EmulatedF64 c = a + b;
        QCOMPARE((double)c, 5.0);
        
        c = b - a;
        QCOMPARE((double)c, 1.0);
        
        c = a * b;
        QCOMPARE((double)c, 6.0);
        
        c = b / a;
        QCOMPARE((double)c, 1.5);
    }

    void testMathFunctions() {
        // Test ADL exp
        EmulatedF64 x(1.0);
        using std::exp;
        EmulatedF64 y = exp(x); // Should find global exp(EmulatedF64)
        
        double y_d = (double)y;
        double expected = std::exp(1.0);
        QVERIFY(std::abs(y_d - expected) < 1e-6);
        
        // Test min/max
        EmulatedF64 m1 = min(EmulatedF64(1.0), EmulatedF64(2.0));
        QCOMPARE((double)m1, 1.0);
        
        EmulatedF64 m2 = max(EmulatedF64(1.0), EmulatedF64(2.0));
        QCOMPARE((double)m2, 2.0);
    }
};

QTEST_MAIN(TestEmulatedF64)
#include "test_emulated_f64.moc"
