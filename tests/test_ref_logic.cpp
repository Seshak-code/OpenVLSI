#include <iostream>
#include <cassert>
#include <vector>
#include <string>

// Include the reference library
// Note: We include circuit.h via reference_designs.h. 
// Assuming files are in components/
#include "../components/reference_designs.h"

int main() {
    std::cout << "[TEST] Verifying Analog Designer MVP Reference Generators..." << std::endl;
    
    // 1. LNA Cascode
    {
        std::cout << "  -> Testing LNA Cascode... ";
        auto lna = ReferenceLibrary::createLNA_Cascode();
        
        // Checks
        assert(lna.numInternalNodes == 8);
        assert(!lna.mosfets.empty());
        assert(lna.mosfets.size() == 2); // M_CS, M_CG
        assert(!lna.inductors.empty());
        assert(lna.inductors.size() == 3); // Gate, Source, Drain
        
        std::cout << "PASS (8 nodes, 2 FETs, 3 Inductors)" << std::endl;
    }
    
    // 2. OpAmp Two-Stage
    {
        std::cout << "  -> Testing OpAmp Two-Stage... ";
        auto opamp = ReferenceLibrary::createOpAmp_TwoStage();
        
        // Checks
        assert(opamp.numInternalNodes == 9);
        // M1, M2 (diff), M3, M4 (load), M6 (CS), M7 (Load). Total 6.
        assert(opamp.mosfets.size() == 6); 
        assert(!opamp.capacitors.empty()); // Cc
        
        std::cout << "PASS (9 nodes, 6 FETs)" << std::endl;
    }
    
    // 3. Bandgap Brokaw
    {
        std::cout << "  -> Testing Bandgap Brokaw... ";
        auto bg = ReferenceLibrary::createBandgap_Brokaw();
        
        // Checks
        assert(bg.numInternalNodes == 8);
        assert(bg.bjts.size() == 2); // Q1, Q2
        assert(bg.resistors.size() >= 5); // R1, R2, R3, Rc1, Rc2...
        
        std::cout << "PASS (8 nodes, 2 BJTs)" << std::endl;
    }
    
    std::cout << "[TEST] All Reference Designs Verified Successfully!" << std::endl;
    return 0;
}
