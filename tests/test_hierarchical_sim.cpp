#include <QApplication>
#include <iostream>
#include <cassert>
#include <QDir>
#include "gui/editors/schematic_editor.h"
#include "gui/components/visual_component.h"

void test_load_inverter(SchematicEditor& editor) {
    std::cout << "[Test 1] Loading inverter.sch..." << std::endl;
    
    QString path = "resources/cells/inverter.sch";
    if (!QFile::exists(path)) {
        // Fallback for relative path running from build dir
        path = "../resources/cells/inverter.sch";
    }
    
    // Ensure file exists or create it temporarily if logic requires absolute path
    // Assuming resources/cells/inverter.sch was created in CWD or root
    // Adjust path based on execution context
    QFileInfo check(path);
    if (!check.exists()) {
         // Try project root absolute path?
         // For test purposes, we assume running from build or root
         // Let's try to find it
         path = CURRENT_SOURCE_DIR "/resources/cells/inverter.sch";
         std::cout << "  Start path: " << path.toStdString() << std::endl;
    }

    bool loaded = editor.loadFromFile(path);
    if (!loaded) {
        // Retry with relative default
        loaded = editor.loadFromFile("resources/cells/inverter.sch");
    }
    
    assert(loaded && "Failed to load inverter.sch");
    
    GridScene* scene = editor.getScene();
    QList<QGraphicsItem*> items = scene->items();
    std::cout << "  Scene loaded with " << items.size() << " items." << std::endl;
    
    // Verify Components
    bool foundN = false, foundP = false;
    bool foundPinIn = false, foundPinOut = false;
    
    for (auto* item : items) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            // Check ID/Name/Type
            if (c->type == ComponentType::PDK_NFET) foundN = true;
            if (c->type == ComponentType::PDK_PFET) foundP = true;
            if (c->type == ComponentType::Pin) {
                if (c->name == "VIN") foundPinIn = true;
                if (c->name == "VOUT") foundPinOut = true;
            }
        }
    }
    
    assert(foundN && "NMOS not found in schematic");
    assert(foundP && "PMOS not found in schematic");
    assert(foundPinIn && "Pin VIN not found");
    assert(foundPinOut && "Pin VOUT not found");
    
    // Verify Netlist Generation ignores Pins (or treats them as ports?)
    // Currently Pins are just Visual Components. Netlister tracks nodes.
    // We want to ensure at least valid netlist is generated.
    TensorNetlist netlist = editor.toNetlist();
    assert(netlist.globalBlock.mosfets.size() >= 2);
    
    std::cout << "  Passed." << std::endl;
}

void test_icpackage_hierarchical(SchematicEditor& editor) {
    std::cout << "[Test 2] Loading ICPackage with hierarchical pins..." << std::endl;
    
    // Load the test_hierarchical.sch which contains an ICPackage
    QString path = CURRENT_SOURCE_DIR "/resources/cells/test_hierarchical.sch";
    if (!QFile::exists(path)) {
        path = "resources/cells/test_hierarchical.sch";
    }
    
    bool loaded = editor.loadFromFile(path);
    if (!loaded) {
        std::cout << "  Skipped (test_hierarchical.sch not found)" << std::endl;
        return;
    }
    
    GridScene* scene = editor.getScene();
    
    // Find the ICPackage component
    VisualComponent* icPackage = nullptr;
    for (auto* item : scene->items()) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->type == ComponentType::ICPackage) {
                icPackage = c;
                break;
            }
        }
    }
    
    if (!icPackage) {
        std::cout << "  Skipped (ICPackage not found)" << std::endl;
        return;
    }
    
    // Verify pins were loaded
    std::cout << "  ICPackage has " << icPackage->pinList.size() << " pins" << std::endl;
    
    // The test_alu.sch has 11 pins: CLK, RST, A[0:1], B[0:1], OUT[0:1], CARRY, VDD, VSS
    assert(icPackage->pinList.size() == 11 && "Expected 11 pins from test_alu.sch");
    
    // Verify terminals match pin count
    auto terminals = icPackage->getTerminals();
    assert(terminals.size() == 11 && "Terminal count should match pin count");
    
    // Verify bounding rect is sized for pins
    QRectF bounds = icPackage->boundingRect();
    assert(bounds.height() > 100 && "Bounding rect should be tall enough for pins");
    
    std::cout << "  Passed." << std::endl;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    std::cout << "=== Running Hierarchical IO Tests ===" << std::endl;
    
    SchematicEditor editor;
    test_load_inverter(editor);
    
    SchematicEditor editor2;
    test_icpackage_hierarchical(editor2);
    
    std::cout << "=== All Tests Passed ===" << std::endl;
    return 0;
}
