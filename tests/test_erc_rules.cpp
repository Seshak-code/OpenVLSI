#include "../components/circuit.h"
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
    
    // Validate in BLOCK mode (should error)
    ViolationReport report = validate<DesignMode::BLOCK>(block);
    
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

// 2. Test System Mode Forbids Generic Sources
bool testSystemModeGenericSource() {
    printHeader("Test 2: System Mode Rejects Generic VSource");
    
    TensorBlock block;
    block.name = "SystemWithGeneric";
    
    // Generic Source
    block.addVoltageSource(1, 0, 1.8, "Generic");
    
    // Validate in SYSTEM mode
    ViolationReport report = validate<DesignMode::SYSTEM>(block);
    
    bool hasSourceError = false;
    for (const auto& e : report.entries) {
        if (e.code & Violation::INDEPENDENT_SOURCE) {
            std::cout << "  Captured Error: " << e.message << "\n";
            hasSourceError = true;
        }
    }
    
    printResult("Rejects Generic Source in System Mode", hasSourceError);
    return hasSourceError;
}

// 3. Test System Mode Allows RailDriver
bool testSystemModeRailDriver() {
    printHeader("Test 3: System Mode Accepts RailDriver");
    
    TensorBlock block;
    block.name = "SystemWithRailDriver";
    
    // RailDriver Source
    block.addVoltageSource(1, 0, 3.3, "RailDriver");
    
    // Validate in SYSTEM mode
    ViolationReport report = validate<DesignMode::SYSTEM>(block);
    
    bool hasSourceError = false;
    bool hasErrors = report.hasErrors;
    
    for (const auto& e : report.entries) {
         if (e.code & Violation::INDEPENDENT_SOURCE) hasSourceError = true;
         if (e.getSeverity(DesignMode::SYSTEM) == ViolationEntry::Severity::ERR) hasErrors = true;
         // Print any errors found
         std::cout << "  Entry: " << e.message << "\n";
    }
    
    // Should be valid (no source error)
    bool passed = !hasSourceError;
    printResult("Accepts RailDriver in System Mode", passed);
    return passed;
}

// 4. Test Valid Block (Single Driver)
bool testValidBlock() {
    printHeader("Test 4: Valid Block (Single Driver)");
    
    TensorBlock block;
    block.name = "ValidBlock";
    
    // V1: Drives Node 1
    // Wait, Block mode usually forbids ANY source.
    // If we use "Generic" it fails INDEPENDENT_SOURCE.
    // If we want to test MULTIPLE_DRIVERS validity separately, we might trigger INDEPENDENT_SOURCE too.
    // So let's test in BENCH mode where sources are allowed.
    
    block.addVoltageSource(1, 0, 5.0, "Generic");
    
    ViolationReport report = validate<DesignMode::BENCH>(block);
    
    bool hasMultipleError = false;
    for(const auto& e : report.entries) {
        if (e.code & Violation::MULTIPLE_DRIVERS) hasMultipleError = true;
    }
    
    printResult("Single driver in Bench mode is valid", !hasMultipleError);
    return !hasMultipleError;
}

int main() {
    bool allPassed = true;
    
    if (!testMultipleDrivers()) allPassed = false;
    if (!testSystemModeGenericSource()) allPassed = false;
    if (!testSystemModeRailDriver()) allPassed = false;
    if (!testValidBlock()) allPassed = false;
    
    std::cout << "\nOverall Result: " << (allPassed ? "PASS" : "FAIL") << "\n";
    return allPassed ? 0 : 1;
}
