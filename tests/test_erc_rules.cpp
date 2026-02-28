#include "acutesim_engine/netlist/circuit.h"
#include "../physics/design_rules.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

/**
 * test_erc_rules.cpp
 * 
 * Verifies core physics-backed ERC rules, specifically:
 * 1. Multiple Drivers (Short Circuit Detection)
 * 2. System Mode Constraints (RailDriver vs Generic Source)
 */

using namespace DesignRules;

// Print helper
void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

void printResult(const std::string& test, bool passed) {
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << test << "\n";
}

// 1. Test Multiple Drivers
bool testMultipleDrivers() {
    printHeader("Test 1: Multiple Drivers Detection");
    
    TensorBlock block;
    block.name = "ShortCircuit";
    
    // V1: Drives Node 1
    block.addVoltageSource(1, 0, 5.0, "Generic");
    
    // V2: Also Drives Node 1 (Collision!)
    block.addVoltageSource(1, 0, 3.3, "Generic");
    
    // Validate in raw state
    ViolationReport report = validate(block);
    
    bool hasMultipleDriverError = false;
    for (const auto& e : report.entries) {
        if (e.code & Violation::MULTIPLE_DRIVERS) {
            std::cout << "  Captured Error: " << e.message << "\n";
            hasMultipleDriverError = true;
        }
    }
    
    printResult("Detects multiple drivers on Node 1", hasMultipleDriverError);
    return hasMultipleDriverError;
}

// 2. Test Raw State Rejects Generic Sources
bool testRawStateGenericSource() {
    printHeader("Test 2: Raw State Rejects Generic VSource");
    
    TensorBlock block;
    block.name = "RawWithGeneric";
    
    // Generic Source
    block.addVoltageSource(1, 0, 1.8, "Generic");
    
    // Validate
    ViolationReport report = validate(block);
    
    bool hasSourceError = false;
    for (const auto& e : report.entries) {
        if (e.code & Violation::INDEPENDENT_SOURCE) {
            std::cout << "  Captured Error: " << e.message << "\n";
            hasSourceError = true;
        }
    }
    
    printResult("Rejects Generic Source (Must use RailDriver)", hasSourceError);
    return hasSourceError;
}

// 3. Test Raw State Allows RailDriver
bool testRawStateRailDriver() {
    printHeader("Test 3: Raw State Accepts RailDriver");
    
    TensorBlock block;
    block.name = "RawWithRailDriver";
    
    // RailDriver Source
    block.addVoltageSource(1, 0, 3.3, "RailDriver");
    
    // Validate
    ViolationReport report = validate(block);
    
    bool hasSourceError = false;
    bool hasErrors = report.hasErrors;
    
    for (const auto& e : report.entries) {
         if (e.code & Violation::INDEPENDENT_SOURCE) hasSourceError = true;
         if (e.getSeverity() == ViolationEntry::Severity::ERR) hasErrors = true;
         // Print any errors found
         std::cout << "  Entry: " << e.message << "\n";
    }
    
    // Should be valid (no source error)
    bool passed = !hasSourceError;
    printResult("Accepts RailDriver for power delivery", passed);
    return passed;
}

// 4. Test Valid Block (Single Driver)
bool testValidBlock() {
    printHeader("Test 4: Valid Block (Single Driver)");
    
    TensorBlock block;
    block.name = "ValidBlock";
    
    block.addVoltageSource(1, 0, 5.0, "RailDriver");
    
    ViolationReport report = validate(block);
    
    bool hasMultipleError = false;
    for(const auto& e : report.entries) {
        if (e.code & Violation::MULTIPLE_DRIVERS) hasMultipleError = true;
    }
    
    printResult("Single RailDriver is valid", !hasMultipleError);
    return !hasMultipleError;
}

int main() {
    bool allPassed = true;
    
    if (!testMultipleDrivers()) allPassed = false;
    if (!testRawStateGenericSource()) allPassed = false;
    if (!testRawStateRailDriver()) allPassed = false;
    if (!testValidBlock()) allPassed = false;
    
    std::cout << "\nOverall Result: " << (allPassed ? "PASS" : "FAIL") << "\n";
    return allPassed ? 0 : 1;
}
