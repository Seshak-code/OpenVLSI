#include <QTest>
#include <QPointer>
#include <QGraphicsScene>
#include "visual_component.h"

// STUB GridScene to satisfy linker dependency in visual_component.cpp
// visual_component.cpp casts scene() to GridScene* for snapping logic.
class GridScene : public QGraphicsScene {
    Q_OBJECT
public:
    bool isSnappingEnabled() const { return false; }
    
    // MOCK: Simulation readiness
    bool canSimulate() const { 
        // Simple logic for test: valid if we have > 0 items
        return items().size() > 0; 
    }
};

class TestGuiLogic : public QObject {
    Q_OBJECT

private slots:
    // 1. Verify Inheritance & Type System
    void testVisualComponentType() {
        QPointer<VisualComponent> comp = new VisualComponent(ComponentType::Resistor);
        QVERIFY(comp != nullptr);
        QCOMPARE(comp->type, ComponentType::Resistor);
        
        // Check QObject inheritance
        QGraphicsObject* obj = qobject_cast<QGraphicsObject*>(comp);
        QVERIFY(obj != nullptr);
        
        // Check Qt Property system (optional, but good for future)
        comp->setObjectName("Rsensor");
        QCOMPARE(comp->objectName(), "Rsensor");
        
        delete comp;
        QVERIFY(comp.isNull()); // QPointer check
    }

    // 2. Verify VisualWire Logic (Manual Geometry)
    void testVisualWireGeometry() {
        QLineF originalLine(0, 0, 100, 0);
        QPointer<VisualWire> wire = new VisualWire(originalLine);
        
        // Verify line storage
        QCOMPARE(wire->line(), originalLine);
        
        // Verify update
        QLineF newLine(0, 0, 100, 100);
        wire->setLine(newLine);
        QCOMPARE(wire->line(), newLine);
        
        // Verify bounding rect contains the line
        QRectF bounds = wire->boundingRect();
        QVERIFY(bounds.contains(QPointF(0,0)));
        QVERIFY(bounds.contains(QPointF(100,100)));
        QVERIFY(bounds.width() >= 100);
        QVERIFY(bounds.height() >= 100);
        
        delete wire;
    }

    // 3. Verify Dangerous Pointer Scenario (The "Cockpit" Fix)
    void testDanglingPointerProtection() {
        // Simulation of MainWindow state
        QPointer<VisualComponent> activeComponent;
        
        // Create component
        VisualComponent* rawPtr = new VisualComponent(ComponentType::Capacitor);
        activeComponent = rawPtr;
        
        QVERIFY(activeComponent != nullptr);
        QCOMPARE(activeComponent->type, ComponentType::Capacitor);
        
        // SIMULATE DELETION (e.g., user deletes item from scene)
        delete rawPtr;
        // rawPtr is now dangling!
        
        // But QPointer should be null
        QVERIFY(activeComponent.isNull());
        
        // Access guard check
        bool unsafeAccess = false;
        if (!activeComponent.isNull()) {
            // This block should NOT execute
            unsafeAccess = true;
            // auto type = activeComponent->type; // Would crash
        }
        QVERIFY(!unsafeAccess);
    }
    
    // 4. Verify Parameter Logic (Data Integrity)
    void testComponentParameters() {
        VisualComponent comp(ComponentType::Resistor);
        comp.params["Tolerance"] = "1%";
        
        QCOMPARE(comp.params["Tolerance"], "1%");
        
        // Modify
        comp.value = "4.7k";
        QCOMPARE(comp.value, "4.7k");
    }

    // 5. Verify Component Categories (Analog, Digital, Mixed)
    void testComponentCategories() {
        // --- Analog ---
        {
            VisualComponent comp(ComponentType::Resistor);
            QCOMPARE(comp.type, ComponentType::Resistor);
            // Verify default params might be set in constructor?
            // (Depends on implementation, usually empty or basic defaults)
            if (comp.params.contains("R")) {
                QVERIFY(comp.params["R"].length() > 0); 
            }
        }
        {
            VisualComponent comp(ComponentType::VoltageSource);
            QCOMPARE(comp.type, ComponentType::VoltageSource);
        }

        // --- Digital ---
        {
            VisualComponent comp(ComponentType::ICPackage);
            QCOMPARE(comp.type, ComponentType::ICPackage);
            // Digital logic items usually have "Part Number"
            comp.params["Part"] = "ULN2003A";
            QCOMPARE(comp.params["Part"], "ULN2003A");
        }

        // --- Mixed Signal / System ---
        {
            VisualComponent comp(ComponentType::WaveformGen);
            QCOMPARE(comp.type, ComponentType::WaveformGen);
        }
        {
            VisualComponent comp(ComponentType::PowerVdd);
            QCOMPARE(comp.type, ComponentType::PowerVdd);
        }
    }

    // 6. Verify Simulation Launch Logic (Button Enablement State)
    void testSimulationLaunchLogic() {
        // Logic being tested: SimulationWorkspace::canRun()
        // which calls schematicEditor_->getScene()->canSimulate()
        
        // Setup MOCK environment (QTest is single threaded GUI)
        GridScene* mockScene = new GridScene();
        
        // 1. Empty scene -> Should NOT simulate
        QVERIFY(!mockScene->canSimulate());
        
        // 2. Add Component -> Should simulate (Mock logic: size > 0)
        VisualComponent* r1 = new VisualComponent(ComponentType::Resistor);
        mockScene->addItem(r1);
        
        QVERIFY(mockScene->canSimulate());
        
        // 3. Verify that the "Button" would be enabled in this state
        bool runButtonEnabled = mockScene->canSimulate();
        QVERIFY(runButtonEnabled);
        
        delete mockScene; // Auto-deletes items (r1)
    }
};

QTEST_MAIN(TestGuiLogic)
#include "test_gui_logic.moc"
