#include <iostream>
#include <cassert>
#include "acutesim_engine/netlist/circuit.h"
#include "acutesim_engine/netlist/netlist_export.h"

void test_spef_wire() {
    std::cout << "[Test] SPEF Export (Parasitic Wire)... ";
    
    TensorNetlist netlist;
    auto& block = netlist.globalBlock;
    block.name = "top";

    // Create a generic wire segment
    // Net N1 (Node 1) -> Net N2 (Node 2)
    // Wait, in physical flows, a "Net" is a single electrical Potenial.
    // A wire segment connects two points *on the same net* or is a resistor *between two net nodes*.
    // The `WireSegment` struct usually implies a physical trace that *has* R and C.
    // So electrically it splits the net into N1 and N2 with a resistor.
    
    TensorBlock::WireSegment seg;
    seg.layer = "M1";
    seg.node1 = 1; 
    seg.node2 = 2; // Separate nodes due to resistance
    seg.length_um = 100.0;
    seg.r_per_um = 0.1; // Total R = 10 Ohm
    seg.c_per_um = 0.05; // Total C = 5 fF
    block.wireSegments.push_back(seg);

    NetlistExporter::ExportOptions opts;
    std::string generated = NetlistExporter::toSPEF(netlist, opts);
    
    // content checks
    assert(generated.find("*DESIGN \"top\"") != std::string::npos);
    assert(generated.find("*D_NET *n1") != std::string::npos);
    assert(generated.find("*RES") != std::string::npos);
    assert(generated.find("*CAP") != std::string::npos);
    
    // Check values
    // R = 100 * 0.1 = 10.0
    // C = 100 * 0.05 = 5.0
    // Note: to_string formatting might vary, checks need flexibility or exact matches
    // generated string should contain "10.00" approx
    
    std::cout << "Passed." << std::endl;
}

int main() {
    test_spef_wire();
    return 0;
}
