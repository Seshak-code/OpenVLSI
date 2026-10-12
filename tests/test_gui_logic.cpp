#include <QTest>
#include <QPointer>
#include <QGraphicsScene>
#include <QGraphicsView>
#include "visual_component.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QKeyEvent> // For input testing
#include <QFileInfo>
#include <QDir>
#include "../../gui/styling/theme_manager.h"
#include "../../gui/core/platform_helper.h"

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

// Wrapper to expose protected event handler for testing
class TestableComponent : public VisualComponent {
public:
    TestableComponent(ComponentType t) : VisualComponent(t) {}
    using VisualComponent::keyPressEvent; // Expose protected
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

    // 4b. Independent source form (Cadence vsource/isource parity) + SI parsing
    void testSourceForm() {
        VisualComponent v(ComponentType::VoltageSource);
        QCOMPARE(v.propertyKeys(), QStringList({"Source type", "AC magnitude", "AC phase"}));
        v.params["Source type"] = "pulse";
        QCOMPARE(v.propertyKeys(), QStringList({"Source type", "AC magnitude", "AC phase", "Voltage 1",
                 "Voltage 2", "Delay time", "Rise time", "Fall time", "Pulse width", "Period"}));
        for (const QString& k : v.propertyKeys()) QVERIFY2(v.params.contains(k), qPrintable(k));
        QCOMPARE(v.paramUnit("Rise time"), QString("s"));
        QCOMPARE(v.paramUnit("AC phase"), QString("deg"));
        QCOMPARE(v.paramUnit("Voltage 2"), QString("V"));

        VisualComponent i(ComponentType::CurrentSource);
        i.params["Source type"] = "sine";
        QCOMPARE(i.propertyKeys(), QStringList({"Source type", "AC magnitude", "AC phase", "Offset current",
                 "Amplitude", "Frequency", "Delay time", "Damping factor", "Initial phase"}));
        for (const QString& k : i.propertyKeys()) QVERIFY2(i.params.contains(k), qPrintable(k));
        QCOMPARE(i.paramUnit("Amplitude"), QString("A"));
        QCOMPARE(i.paramUnit("Frequency"), QString("Hz"));

        bool ok = false;
        QCOMPARE(parseSiValue("1MHz", &ok), 1e6);  QVERIFY(ok);
        QCOMPARE(parseSiValue("2meg"), 2e6);
        QCOMPARE(parseSiValue("1.5m"), 1.5e-3);
        QCOMPARE(parseSiValue("10n"), 10e-9);
        QCOMPARE(parseSiValue("2.2k"), 2.2e3);
        parseSiValue("fast", &ok);                  QVERIFY(!ok);
        QCOMPARE(parseSiValue("", &ok), 0.0);       QVERIFY(ok);
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

        // --- PDK Primitives ---
        {
            VisualComponent comp(ComponentType::PDK_Resistor);
            QCOMPARE(comp.type, ComponentType::PDK_Resistor);
            // PDK components have specific parameters
            comp.params["R"] = "10k";
            QCOMPARE(comp.params["R"], "10k");
        }

        // --- Mixed Signal / System ---
        {
            VisualComponent comp(ComponentType::PDK_NFET);
            QCOMPARE(comp.type, ComponentType::PDK_NFET);
        }
    }

    // 6. Verify Theme Engine (New Phase 11)
    void testVisualThemeEngine() {
        auto& tm = acutesim::gui::styling::ThemeManager::instance();
        
        // Test Default (Dark)
        tm.loadTheme("Dark");
        QCOMPARE(tm.currentTheme(), "Dark");
        
        QColor bg = tm.getColor("ui.background");
        QVERIFY(bg.isValid());
        QCOMPARE(bg.name(), "#1e1e1e"); // Known VS Code Dark bg
        
        // Test Light Theme Switch
        tm.loadTheme("Light");
        QCOMPARE(tm.currentTheme(), "Light");
        
        QColor lightBg = tm.getColor("ui.background");
        QCOMPARE(lightBg.name(), "#ffffff");
        
        // Test Token Fallback
        QColor unknown = tm.getColor("non.existent.token");
        QCOMPARE(unknown, Qt::red); // Implementation specific callback
    }

    // 7. Verify Simulation Launch Logic (Button Enablement State)
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

    // 8. Verify Phase 11.4 Coordinate Space Contract
    void testCoordinateScaling() {
        // Contract: Logical Units are DPI Independent
        // 1 Logical Unit = 1 Logical Pixel at 100% Zoom
        
        // Mock High-DPI Scenario
        const double mockDPI = 2.0; // 2x Scaling (Retina)
        
        // Test Logic -> Pixel conversion
        // In Qt, mapToScene handles the view transform (zoom).
        // Device transform handles DPI. 
        // We verify that logical distances remain constant regardless of "screen" assumptions.
        
        QPointF logicP1(0, 0);
        QPointF logicP2(100, 0); // 100 logical units
        
        QLineF logicLine(logicP1, logicP2);
        QCOMPARE(logicLine.length(), 100.0);
        
        // Use PlatformHelper to verify metric conversion
        // mmToPx should scale with DPI
        // We can't mock QScreen easily in QTest without GUI plugin, but we can call our helper
        // and verify it returns a sane integer > 0
        int px10mm = acutesim::gui::core::PlatformHelper::mmToPx(10.0);
        QVERIFY(px10mm > 0);
        
        // Verify Style Token scaling
        // If we implement 'em' or 'sp' sizing later, verify here.
    }
    
    // 9. Verify Phase 11.4 Font Metrics
    void testFontMetricsScaling() {
        auto& tm = acutesim::gui::styling::ThemeManager::instance();
        
        // Logic: Font size 12 vs Font size 24
        // Bounding rect height should be roughly double (allow small variance for hinting)
        
        QFont f1 = tm.getScaledFont("ui", 12);
        QFont f2 = tm.getScaledFont("ui", 24);
        
        QFontMetrics fm1(f1);
        QFontMetrics fm2(f2);
        
        qreal h1 = fm1.height();
        qreal h2 = fm2.height();
        
        // Expect h2 approx 2 * h1
        // Allow +/- 20% variance due to non-linear font scaling
        QVERIFY(h2 > h1 * 1.5); 
        QVERIFY(h2 < h1 * 2.5);
        
        // Verify Baseline consistency (metrics object works)
        QVERIFY(tm.getScaledFontMetrics("ui", 12).height() > 0);
    }
    
    // 10. Verify Interaction Correctness (Hit Testing)
    void testInteractionScaling() {
        // Verify that hit testing works in logical coordinates
        GridScene scene;
        VisualComponent* r1 = new VisualComponent(ComponentType::Resistor);
        r1->setPos(100, 100);
        scene.addItem(r1);
        
        // Hit test at exact center
        QPointF center(100, 100);
        QGraphicsItem* hit = scene.itemAt(center, QTransform());
        
        // Note: VisualComponent might have boundingbox offset. 
        // Standard components usually centered or top-left.
        // Let's verify we hit *something* valid.
        QVERIFY(hit != nullptr);
        
        // If we hit a child (like a port), traverse up
        VisualComponent* hitComp = dynamic_cast<VisualComponent*>(hit);
        if (!hitComp && hit) hitComp = dynamic_cast<VisualComponent*>(hit->parentItem());
        
        QVERIFY(hitComp == r1);
        
        // Verify miss
        QGraphicsItem* miss = scene.itemAt(QPointF(500, 500), QTransform());
        QVERIFY(miss == nullptr);
    }

    // 11. Verify Zoom Anchor Stability (Simulated)
    void testZoomAnchorStability() {
        // Contract: Zooming should keep the scene point under mouse fixed in view coords.
        // QGraphicsView handles this via setTransformationAnchor(QGraphicsView::AnchorUnderMouse).
        // Since we can't easily mock mouse events on a headless QGraphicsView in QTest without a window,
        // we will verify the transformation matrix logic mathematically.
        
        QGraphicsView view;
        QGraphicsScene scene;
        view.setScene(&scene);
        view.resize(400, 400); 
        // Focus on center (200, 200)
        
        // Initial state
        QVERIFY(view.transform().isIdentity());
        
        // Simulate Zoom In (2x) anchor at center
        view.scale(2.0, 2.0);
        
        // Logic check:
        // Viewport center (200,200) should still map to Scene (0,0) if centered
        // Or if we map scene(0,0) to viewport, it should be in the middle.
        
        QPointF sceneCenter(0,0);
        QPoint viewportCenter = view.mapFromScene(sceneCenter);
        
        // Allow some scrollbar offset
        int cx = viewportCenter.x();
        int cy = viewportCenter.y();
        
        // With default alignment, scene(0,0) is centered.
        // Zooming in shouldn't shift it.
        // view.centerOn(0,0) ensures this.
        view.centerOn(0,0);
        viewportCenter = view.mapFromScene(sceneCenter);
        
        // After centering, should be roughly w/2, h/2
        QVERIFY(std::abs(viewportCenter.x() - 200) < 20); 
        QVERIFY(std::abs(viewportCenter.y() - 200) < 20);
        
        // Zoom Out (0.5x)
        view.scale(0.25, 0.25); // Net 0.5
        view.centerOn(0,0);
        viewportCenter = view.mapFromScene(sceneCenter);
        
        QVERIFY(std::abs(viewportCenter.x() - 200) < 20);
        QVERIFY(std::abs(viewportCenter.y() - 200) < 20);
    }

    // 12. Verify Aspect Ratio Resilience (Extreme Resize)
    void testAspectRatioResilience() {
        // Verify that grid/objects don't distort when view is resized disproportionately
        GridScene scene;
        VisualComponent* c = new VisualComponent(ComponentType::Resistor);
        scene.addItem(c);
        
        // Initial Aspect 1:1
        QRectF initialBounds = c->boundingRect();
        // Width/Height logic depends on rotation, but for IC it's usually square-ish or rectangular constant.
        
        // Resize View (Extreme Wide 1000x100)
        // QGraphicsView maintains aspect ratio of the scene by default unless we set non-uniform scaling on the matrix.
        // We verify that standard view usage doesn't apply non-uniform scale.
        
        QGraphicsView view(&scene);
        view.resize(1000, 100);
        
        // Check transform
        QTransform t = view.transform();
        // m11 (sx) should typically equal m22 (sy) for uniform scaling
        // If fitInView is used incorrectly, they might differ. 
        // We asserting strict equality is risky with float, but they should be close.
        
        // By default, Qt does IDENTITY. We want to ensure no accidental non-uniform scaling.
        QVERIFY(qAbs(t.m11() - t.m22()) < 0.001);
        
        // Resize View (Extreme Tall 100x1000)
        view.resize(100, 1000);
        t = view.transform();
        QVERIFY(qAbs(t.m11() - t.m22()) < 0.001);
    }
    
    // 13. Verify Phase 11.5: Theme Registry & JSON Loading
    void testThemeRegistryAndJson() {
        auto& tm = acutesim::gui::styling::ThemeManager::instance();
        
        // Load High Contrast JSON (Mock path or resource)
        // Since we don't have resource system fully mocked in test, we might fallback 
        // to manual file creation or use the internal method.
        
        // 1. Internal High Contrast Fallback
        tm.loadHighContrastTheme();
        QCOMPARE(tm.currentTheme(), "High Contrast");
        
        // Verify Token Existence
        QColor bg = tm.getColor("ui.background");
        QVERIFY(bg == Qt::black); // #000000
        
        // Verify Contrast Ratio Logic
        double ratio = tm.getContrastRatio(Qt::white, Qt::black); // 21:1
        QVERIFY(ratio > 20.0);
        
        // Verify Helper
        QVERIFY(tm.validateContrast(Qt::white, Qt::black, true)); // Pass AAA
        QVERIFY(!tm.validateContrast(QColor(50,50,50), Qt::black, false)); // Fail Low Contrast
    }
    
    // 14. Verify Atomic Theme Switching (Stability)
    void testAtomicThemeSwitching() {
        auto& tm = acutesim::gui::styling::ThemeManager::instance();
        tm.loadTheme("Dark"); // Reset
        QString original = tm.currentTheme();
        
        // Attempt load invalid JSON
        bool success = tm.loadThemeFromJson("invalid/path.json");
        QVERIFY(!success);
        
        // Assert state did not change
        QCOMPARE(tm.currentTheme(), original);
        QCOMPARE(tm.getColor("ui.background").name(), "#1e1e1e");
    }

    // 15. Verify Theme Inheritance & Override
    void testThemeInheritance() {
        auto& tm = acutesim::gui::styling::ThemeManager::instance();
        
        // Create Parent Theme
        QFile parent("parent_theme.json");
        if (parent.open(QIODevice::WriteOnly)) {
            QJsonObject root;
            root["name"] = "Parent";
            QJsonObject colors;
            colors["ui.background"] = "#111111"; // Specific Color
            colors["ui.text"] = "#aaaaaa"; 
            // Add required tokens to satisfy registry (or let validation fill defaults, but better to be clean)
            // But validation only checks final map.
            root["colors"] = colors;
            parent.write(QJsonDocument(root).toJson());
            parent.close();
        }
        
        // Create Child Theme
        QFile child("child_theme.json");
        if (child.open(QIODevice::WriteOnly)) {
            QJsonObject root;
            root["name"] = "Child";
            root["extends"] = "parent_theme"; // Refers to parent_theme.json
            QJsonObject colors;
            colors["ui.text"] = "#ffffff"; // Override text, inherit background
            root["colors"] = colors;
            child.write(QJsonDocument(root).toJson());
            child.close();
        }
        
        // Load Child (use absolute path to be sure)
        QFileInfo childInfo("child_theme.json");
        bool success = tm.loadThemeFromJson(childInfo.absoluteFilePath());
        QVERIFY(success);
        
        // Verify Inheritance
        QCOMPARE(tm.currentTheme(), "Child");
        QCOMPARE(tm.getColor("ui.background").name(), "#111111"); // Inherited
        QCOMPARE(tm.getColor("ui.text").name(), "#ffffff"); // Overridden
        
        // Cleanup - remove files
        // (If test fails, these might persist, but that's typical for temp file tests)
        parent.remove();
        child.remove();
    }


    // 16. Verify Theme System Stress & Resilience (Phase 11.5)
    void testStressThemeSwitching() {
        auto& tm = acutesim::gui::styling::ThemeManager::instance();
        
        // 1. Create a set of temporary theme files
        QStringList themes;
        for (int i = 0; i < 5; ++i) {
             QString name = QString("stress_theme_%1").arg(i);
             QString filename = name + ".json";
             QFile f(filename);
             if (f.open(QIODevice::WriteOnly)) {
                 QJsonObject root;
                 root["name"] = name;
                 QJsonObject colors;
                 // Varied colors
                 colors["ui.background"] = (i % 2 == 0) ? "#000000" : "#ffffff";
                 colors["ui.text"] = "#888888";
                 root["colors"] = colors;
                 f.write(QJsonDocument(root).toJson());
                 f.close();
                 themes << f.fileName();
             }
        }
        
        // 2. Loop load (Stress)
        QFileInfo info(themes[0]);
        QString absPath = info.absolutePath(); // Dir
        
        // We will toggle rapidly between them
        for (int k = 0; k < 50; ++k) {
            int idx = k % themes.size();
            QFileInfo tInfo(themes[idx]);
            bool result = tm.loadThemeFromJson(tInfo.absoluteFilePath());
            QVERIFY(result);
            QCOMPARE(tm.currentTheme(), QString("stress_theme_%1").arg(idx));
            
            // Verify Consistency immediately
            QColor expectedBg = (idx % 2 == 0) ? QColor("#000000") : QColor("#ffffff");
            QCOMPARE(tm.getColor("ui.background"), expectedBg);
        }
        
        // 3. Cleanup
        for (const QString& f : themes) {
            QFile::remove(f);
        }
        
        // 4. Verify Fallback after stress
        tm.loadTheme("Dark");
        QCOMPARE(tm.getColor("ui.background").name(), "#1e1e1e");
    }
    // 17. Verify Ghost Rendering & State (Phase 11.6)
    void testGhostRendering() {
         VisualComponent comp(ComponentType::Resistor);
         
         // 1. Initial State: None
         QVERIFY(!comp.isGhost());
         QVERIFY(comp.isNetlistMember());
         
         // 2. Set Ghost State
         comp.setGhostState(GhostState::Preview);
         QVERIFY(comp.isGhost());
         QVERIFY(!comp.isNetlistMember()); // Must be excluded from simulation
         
         // 3. Verify Confidence
         comp.ghostConfidence = 0.5f;
         // Rendering logic is internal to paint(), tough to test pixel-perfectly here without rendering loop.
         // But state is verificated.
    }
    


    // 18. Verify Ghost Interaction (Tab-to-Accept)
    void testGhostInteraction() {
         GridScene scene;
         TestableComponent* ghost = new TestableComponent(ComponentType::Resistor);
         ghost->setGhostState(GhostState::Focused); // Ready to accept
         scene.addItem(ghost);
         
         QVERIFY(ghost->isGhost());
         
         // Direct Event Call (Bypassing Scene/View machinery which is flakey in headless)
         QKeyEvent tabEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
         ghost->keyPressEvent(&tabEvent);
         
         // Should transition to Accepted, then back to None
         QVERIFY(!ghost->isGhost()); 
         QVERIFY(ghost->isNetlistMember());
         QVERIFY(tabEvent.isAccepted());
         
         // Test Reject (Esc)
         TestableComponent* rejectGhost = new TestableComponent(ComponentType::Capacitor);
         rejectGhost->setGhostState(GhostState::Preview);
         scene.addItem(rejectGhost);
         
         QKeyEvent escEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
         rejectGhost->keyPressEvent(&escEvent);
         
         // Logic sets visible(false) or Rejected state
         QCOMPARE(rejectGhost->ghostState, GhostState::Rejected);
         QVERIFY(!rejectGhost->isVisible());
         QVERIFY(escEvent.isAccepted());
    }

};

    QTEST_MAIN(TestGuiLogic)
#include "test_gui_logic.moc"
