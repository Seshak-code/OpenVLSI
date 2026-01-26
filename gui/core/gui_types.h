#pragma once

// ============================================================================
// Application Mode (DEPRECATED)
// ============================================================================
// The three-mode architecture has been replaced with a unified component library.
// All components are now available at all times.
//
// This enum is kept for backwards compatibility but mode filtering is no longer
// enforced. All code paths now treat modes equivalently.
//
// DEPRECATED: Use of AppMode for filtering is no longer recommended.

enum class AppMode {
    BENCH_TEST,         // (Deprecated) Was: Ideal sources, implicit ground
    BLOCK_SUBSYSTEM,    // (Deprecated) Was: PowerPort interfaces
    SYSTEM_INTEGRATION  // (Deprecated) Was: RailDrivers only
};

// Power Domain Metadata (for multi-rail ERC)
struct PowerDomain {
    const char* name;           // "CORE", "IO", "ANALOG"
    double nominalVoltage;      // 1.8V, 3.3V, 5V
    double minVoltage;          // Lower bound
    double maxVoltage;          // Upper bound
};
