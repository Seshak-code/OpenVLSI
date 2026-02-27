#include <QTest>
#include <QTemporaryFile>
#include "../gui/analysis/pdk_manager.h"
#include "../gui/analysis/pdk_compliance.h"
#include "../gui/components/visual_component.h"
#include "../gui/editors/schematic_editor.h" // For GridScene if needed, or mock it

// Mock Scene for testing
class MockScene : public QGraphicsScene {
public:
    MockScene() {}
};

class TestPDKSwap : public QObject {
    Q_OBJECT

private slots:
    void testSwap() {
        // 1. Create a Component that is valid in PDK A but invalid in PDK B
        // Component: NFET W=0.2u
        
        MockScene scene;
        VisualComponent* comp = new VisualComponent(ComponentType::PDK_NFET);
        comp->name = "M1";
        comp->params["Model"] = "nfet_01v8";
        comp->params["W"] = "0.2"; // 0.2um
        scene.addItem(comp);
        
        // 2. Load Permissive PDK A (Min W = 0.1)
        QString jsonA = R"({
            "name": "PermissivePDK",
            "device_classes": { "nfet_01v8": "MOSFET" },
            "param_rules": {
                "nfet_01v8": { "W": { "min": 0.1, "units": "um" } }
            }
        })";
        QTemporaryFile fileA;
        QVERIFY(fileA.open());
        fileA.write(jsonA.toUtf8());
        fileA.flush();
        fileA.close();
        
        PDKManager::instance().loadPDK(fileA.fileName());
        ComplianceReport reportA = PDKComplianceChecker::checkCompliance((GridScene*)&scene); // Cast hack or use real GridScene
        
        // SHOULD PASS
        if (!reportA.passed) {
             for(auto v : reportA.violations) qDebug() << "A Fail:" << v.message;
        }
        QVERIFY(reportA.passed);
        
        // 3. Load Strict PDK B (Min W = 0.5)
        QString jsonB = R"({
            "name": "StrictPDK",
            "device_classes": { "nfet_01v8": "MOSFET" },
            "param_rules": {
                "nfet_01v8": { "W": { "min": 0.5, "units": "um" } }
            }
        })";
        QTemporaryFile fileB;
        QVERIFY(fileB.open());
        fileB.write(jsonB.toUtf8());
        fileB.flush();
        fileB.close();
        
        PDKManager::instance().loadPDK(fileB.fileName());
        ComplianceReport reportB = PDKComplianceChecker::checkCompliance((GridScene*)&scene);
        
        // SHOULD FAIL
        QVERIFY(!reportB.passed);
        QCOMPARE(reportB.errorCount, 1);
        QVERIFY(reportB.violations[0].message.contains("< min"));
        
        delete comp;
    }
};

QTEST_MAIN(TestPDKSwap)
#include "test_pdk_swap.moc"
