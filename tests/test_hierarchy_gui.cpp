#include <QApplication>
#include <QGraphicsScene>
#include <QDebug>
#include <cassert>
#include "gui/editors/schematic/schematic_editor.h"
#include "gui/components/visual_component.h"
#include "acutesim_engine/netlist/circuit.h"

// Helper to check assertion
#define TEST_ASSERT(cond) \
    if (!(cond)) { \
        qCritical() << "Assertion failed at line" << __LINE__ << ": " << #cond; \
        exit(1); \
    }

int main(int argc, char *argv[]) {
    // headless setup
    // qputenv("QT_QPA_PLATFORM", "offscreen"); // Allow external override
    QApplication app(argc, argv);

    // 1. Setup Scene
    // Use Headless mode to avoid GUI overheads if implemented
    GridScene scene(GridScene::Mode::Headless);
    scene.setSceneRect(0, 0, 1000, 1000);

    qDebug() << "Test: Placing components...";

    // 2. Add PowerPort (Subcircuit Interface Pin)
    // Assuming "PowerPort" string maps to ComponentType::PowerPort
    QString portId = scene.placeComponent("PowerPort", QPointF(100, 100), 0, {});
    VisualComponent* powerPort = scene.findComponent(portId);
    
    // Fallback if "PowerPort" string alias not registered, try manual creation if possible
    // But placeComponent is the factory.
    if (!powerPort) {
        qCritical() << "Failed to place PowerPort. Check component registry.";
        return 1;
    }
    
    TEST_ASSERT(powerPort != nullptr);
    TEST_ASSERT(powerPort->type == ComponentType::PowerPort);

    // 3. Add Resistor
    QString resId = scene.placeComponent("Resistor", QPointF(200, 100), 0, {});
    VisualComponent* res = scene.findComponent(resId);
    TEST_ASSERT(res != nullptr);

    // 4. Add Wire connecting them
    // Get terminal positions (mapped to scene)
    auto portTerms = powerPort->getTerminals();
    auto resTerms = res->getTerminals();
    TEST_ASSERT(!portTerms.isEmpty());
    TEST_ASSERT(!resTerms.isEmpty());

    // PowerPort has 1 terminal
    QPointF p1 = powerPort->mapToScene(portTerms[0]);
    
    // Resistor has 2 terminals. Connect to the first one.
    QPointF p2 = res->mapToScene(resTerms[0]);

    qDebug() << "Connecting PowerPort at" << p1 << "to Resistor at" << p2;

    VisualWire* wire = new VisualWire(QLineF(p1, p2));
    scene.addItem(wire);

    // 5. Generate Netlist
    qDebug() << "Test: Generating Netlist...";
    scene.generateNetlist();
    TensorNetlist netlist = scene.toNetlist();

    // 6. Verify Interface Pins
    qDebug() << "Test: Verifying interface pins...";
    bool foundPowerPort = false;
    for (const auto& pin : netlist.globalBlock.interfacePins) {
        if (pin.isPower) {
            foundPowerPort = true;
            qDebug() << "Found PowerPort pin:" << QString::fromStdString(pin.name) 
                     << " NetID:" << QString::fromStdString(pin.netUUIDStr)
                     << " NodeIndex:" << pin.nodeIndex;
        }
    }
    
    if (!foundPowerPort) {
        qCritical() << "FAILED: PowerPort was not identified as an interface pin!";
        // Print all interface pins for debug
        for (const auto& pin : netlist.globalBlock.interfacePins) {
            qDebug() << "Pin:" << QString::fromStdString(pin.name) << " Power:" << pin.isPower;
        }
    }
    TEST_ASSERT(foundPowerPort);

    qDebug() << "SUCCESS: Hierarchy Verification Passed.";
    return 0;
}
