#pragma once

// ============================================================================
// Three-Mode Simulation Architecture
// ============================================================================
// A symbol is a VIEW of a device role, not the device itself.
// Same electrical meaning, different semantic roles.

enum class AppMode { 
    BENCH_TEST,         // Ideal sources, implicit ground, device testing
    BLOCK_SUBSYSTEM,    // PowerPort interfaces, internal dependent sources
    SYSTEM_INTEGRATION  // RailDrivers only, explicit VSS/VDD symbols
};

// Power Domain Metadata (for multi-rail ERC)
struct PowerDomain {
    const char* name;           // "CORE", "IO", "ANALOG"
    double nominalVoltage;      // 1.8V, 3.3V, 5V
    double minVoltage;          // Lower bound
    double maxVoltage;          // Upper bound
};
