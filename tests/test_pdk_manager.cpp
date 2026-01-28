#include <QTest>
#include <QTemporaryFile>
#include "../components/pdk_manager.h"

class TestPDKManager : public QObject {
    Q_OBJECT

private slots:
    void testLoadJson() {
        // defined inline for test
        QString json = R"({
            "name": "TestPDK",
            "device_classes": {
                "nfet": "MOSFET"
            },
            "param_rules": {
                "nfet": {
                    "W": { "min": 0.5, "units": "um" }
                }
            }
        })";
        
        QTemporaryFile tempFile;
        if (tempFile.open()) {
            tempFile.write(json.toUtf8());
            tempFile.flush();
            tempFile.close();
            
            bool ok = PDKManager::instance().loadPDK(tempFile.fileName());
            QVERIFY(ok);
            
            const PDKRules& rules = PDKManager::instance().getPDK();
            QCOMPARE(rules.name, QString("TestPDK"));
            QCOMPARE(rules.getDeviceClass("nfet"), QString("MOSFET"));
            
            QVERIFY(rules.paramRules.contains("nfet"));
            QVERIFY(rules.paramRules["nfet"].contains("W"));
            QCOMPARE(rules.paramRules["nfet"]["W"].min, 0.5);
            QCOMPARE(rules.paramRules["nfet"]["W"].units, QString("um"));
        }
    }
    
    void testValidationLogic() {
        ParamRule rule;
        rule.min = 0.5;
        rule.max = 10.0;
        
        QVERIFY(rule.isValid(1.0));
        QVERIFY(rule.isValid(0.5)); // inclusive? logic says < min return false, so >= is valid
        QVERIFY(!rule.isValid(0.4));
        QVERIFY(!rule.isValid(10.1));
    }
};

QTEST_MAIN(TestPDKManager)
#include "test_pdk_manager.moc"
