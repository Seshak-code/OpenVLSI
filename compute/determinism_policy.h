#pragma once
#include <type_traits>

/**
 * @file determinism_policy.h
 * @brief Global policy for floating-point and execution determinism.
 */

static_assert(sizeof(double) == 8, "Simulator requires IEEE-754 double precision.");

namespace acutesim {
namespace compute {

enum class PrecisionMode {
    /**
     * @brief Strict IEEE-754 double precision semantics.
     * - Mandatory for all targets (CPU, WASM, GPU).
     * - GPU/WASM targets must use emulated df64 if native f64 is unavailable.
     * - No FMA contraction if it changes reproducibility.
     * - Fixed reduction order (no atomic-interleaved merges).
     */
    StrictF64
};

/**
 * @struct PrecisionConfig
 * @brief Configuration for a specific solver run.
 */
struct PrecisionConfig {
    PrecisionMode mode = PrecisionMode::StrictF64;
    
    // Policy enforcement flags
    bool allowFMA = false;
    bool allowParallelReductions = false; // Always false under StrictF64
    
    static PrecisionConfig hardened() {
        PrecisionConfig c;
        c.mode = PrecisionMode::StrictF64;
        c.allowFMA = false;
        c.allowParallelReductions = false;
        return c;
    }
};

} // namespace compute
} // namespace acutesim
