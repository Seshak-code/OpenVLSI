#include <QApplication>
#include <iostream>
#include <cassert>
#include <cmath>

#include "components/circuit.h"
#include "components/standard_cells.h"
#include "gui/editors/schematic_editor.h"
#include "gui/components/visual_component.h"

// --- Part 1: Structural Tests (Standard Library) ---
void test_inverter_pdk_mapping() {
    std::cout << "[Test 1] CMOS Inverter PDK Mapping (Standard Cells)..." << std::endl;
    auto* invDef = StandardCells::StandardCellLibrary::instance().get("CMOS Inverter");
    assert(invDef != nullptr);
    const TensorBlock& block = invDef->internalBlock;
    
    bool foundN = false;
    bool foundP = false;
    for (const auto& m : block.mosfets) {
        if (m.modelName == "nfet_01v8") foundN = true;
        if (m.modelName == "pfet_01v8") foundP = true;
         assert(m.w > 0.0);
         assert(m.l > 0.0);
    }
    assert(foundN && "NMOS must use 'nfet_01v8'");
    assert(foundP && "PMOS must use 'pfet_01v8'");
    std::cout << "  Passed." << std::endl;
}

// --- Part 2: GUI Integration Tests ---
void test_gui_pdk_extraction(SchematicEditor& editor) {
    std::cout << "[Test 2] GUI Component PDK Parameter Extraction..." << std::endl;
    GridScene* scene = editor.getScene();
    scene->clear(); 

    // 1. Inductor
    auto* ind = new VisualComponent(ComponentType::Inductor);
    ind->name = "L1";
    // Set custom params
    ind->params["Model"] = "spiral_ind_hq";
    ind->params["W"] = "5e-6"; 
    ind->params["Area"] = "200p";
    ind->setPos(-100, -100); 
    scene->addItem(ind);

    // 2. PDK NFET
    auto* nfet = new VisualComponent(ComponentType::PDK_NFET);
    nfet->name = "M1";
    nfet->params["W"] = "10e-6";
    nfet->params["L"] = "0.18e-6";
    nfet->setPos(100, 100);
    scene->addItem(nfet);

    // Run Netlister
    TensorNetlist netlist = editor.toNetlist();

    // Verify Inductor
    bool foundInd = false;
    for (const auto& i : netlist.globalBlock.inductors) {
        if (i.name == "L1") {
            foundInd = true;
            assert(i.model_name == "spiral_ind_hq");
            // Check approx float equality
            assert(std::abs(i.width_m - 5e-6) < 1e-9);
            assert(std::abs(i.area_m2 - 200e-12) < 1e-18);
        }
    }
    assert(foundInd && "Inductor L1 not extracted or params mismatch");

    // Verify NFET
    bool foundMos = false;
    for (const auto& m : netlist.globalBlock.mosfets) {
        if (m.instanceName == "M1") {
            foundMos = true;
            assert(m.modelName == "nfet"); // Default for PDK_NFET unless overridden
            assert(std::abs(m.w - 10e-6) < 1e-9);
            assert(std::abs(m.l - 0.18e-6) < 1e-9);
        }
    }
    assert(foundMos && "MOSFET M1 not extracted or params mismatch");

    std::cout << "  Passed." << std::endl;
}

void test_gui_wire_parasitics(SchematicEditor& editor) {
    std::cout << "[Test 3] Parasitic-Aware Wire Extraction (Phase 1.5)..." << std::endl;
    GridScene* scene = editor.getScene();
    scene->clear();

    // Create a wire
    QLineF line(0, 0, 100, 0); // 100 pixels long
    auto* wire = new VisualWire(line);
    wire->layer = "M3";
    wire->width_um = 2.0;
    scene->addItem(wire);

    // Run Netlister
    TensorNetlist netlist = editor.toNetlist();

    bool foundSeg = false;
    for (const auto& seg : netlist.globalBlock.wireSegments) {
        if (seg.layer == "M3") {
            foundSeg = true;
            assert(std::abs(seg.width_um - 2.0) < 1e-6);
            // 1 pixel = 0.1um in logic
            // 100 pixels * 0.1 = 10.0 um
            assert(std::abs(seg.length_um - 10.0) < 1e-6);
        }
    }
    assert(foundSeg && "WireSegment not extracted or properties mismatch");
    
    // Check if parasitic resistor was injected (Length 10um > 1um threshold? Yes implied by logic)
    // Actually logic was > 10.0. 10.0 is not > 10.0. Let's make it longer.
    
    // Let's modify wire length to be sure
    wire->setLine(QLineF(0, 0, 200, 0)); // 200 px = 20 um
    netlist = editor.toNetlist();
    
    bool foundRes = false;
    for (const auto& r : netlist.globalBlock.parasiticResistors) {
        if (r.name.find("R_WIRE") != std::string::npos) {
            foundRes = true;
             // R = L * r_per_um. 
             // L = 20.0. r_per_um = 0.1 (default) -> R=2.0
             assert(std::abs(r.R - 2.0) < 1e-6);
        }
    }
    assert(foundRes && "Parasitic Resistor not injected for long wire");

    std::cout << "  Passed." << std::endl;
}

int main(int argc, char *argv[]) {
    // headless platform interaction
    QApplication app(argc, argv);
    
    std::cout << "=== Running GUI-integrated PDK Compliance Tests ===" << std::endl;

    test_inverter_pdk_mapping();
    
    SchematicEditor editor;
    test_gui_pdk_extraction(editor);
    test_gui_wire_parasitics(editor);

    std::cout << "=== All Tests Passed ===" << std::endl;
    return 0;
}
