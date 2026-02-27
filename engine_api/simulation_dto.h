#pragma once
// ============================================================================
// simulation_dto.h — Pure Data Transfer Objects for the Orchestration Layer
// ============================================================================
// ARCHITECTURAL RULE: This header must NEVER include Qt, Eigen, Physics, or
// any Solver types. Only standard library types are allowed.
// If this header compiles without QApplication, you are compliant.
// ============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace acutesim {
namespace compute {
namespace orchestration {

// ── Analysis Types (mirrored as strings for serialization safety) ────────────
enum class AnalysisTypeDTO : int {
    DC        = 0,
    TRANSIENT = 1,
    AC        = 2,
    NOISE     = 3,
    MONTE_CARLO = 4,
    SENSITIVITY = 5,
    // Phase 4.4: Extended analysis types
    TRANSFER_FUNCTION = 6,  // H(jω) = V_out/V_in with gain/phase margins
    POLE_ZERO         = 7,  // Poles & zeros via linearised Jacobian eigenvalues
    FOURIER           = 8   // DFT of transient waveform → THD%
};

inline std::string analysisTypeToString(AnalysisTypeDTO t) {
    switch (t) {
        case AnalysisTypeDTO::DC:                return "DC";
        case AnalysisTypeDTO::TRANSIENT:         return "TRANSIENT";
        case AnalysisTypeDTO::AC:                return "AC";
        case AnalysisTypeDTO::NOISE:             return "NOISE";
        case AnalysisTypeDTO::MONTE_CARLO:       return "MONTE_CARLO";
        case AnalysisTypeDTO::SENSITIVITY:       return "SENSITIVITY";
        case AnalysisTypeDTO::TRANSFER_FUNCTION: return "TRANSFER_FUNCTION";
        case AnalysisTypeDTO::POLE_ZERO:         return "POLE_ZERO";
        case AnalysisTypeDTO::FOURIER:           return "FOURIER";
    }
    return "UNKNOWN";
}

// ── Run Modes ──────────────────────────────────────────────────────────────
enum class RunModeDTO : int {
    FULL = 0,
    QUICK = 1,
    RESUME = 2,
    INCREMENTAL = 3
};

// ── Corner Parameters (POD) ─────────────────────────────────────────────────
struct CornerParametersDTO {
    std::string name        = "TT";
    double temperature      = 27.0;    // °C
    double supplyVoltage    = 1.8;     // V (nominal)
    double voltageScale     = 1.0;
    std::string processCorner = "TT";  // TT, FF, SS, SF, FS

    static std::vector<CornerParametersDTO> standard5() {
        return {
            {"TT", 27.0, 1.8, 1.0, "TT"},
            {"FF", -40.0, 1.8, 1.1, "FF"},
            {"SS", 125.0, 1.8, 0.9, "SS"},
            {"SF", 27.0, 1.8, 1.0, "SF"},
            {"FS", 27.0, 1.8, 1.0, "FS"}
        };
    }
};

// ── Netlist Transport Format ────────────────────────────────────────────────
enum class NetlistFormatDTO : int {
    JSON     = 0,  // Legacy JSON (only R/C/V/Probe deserialized)
    VERILOG_A = 1  // Verilog-A contribution statements (all 9 component types)
};

// Carries the canonical netlist payload across the GUI→driver boundary.
// When format == VERILOG_A, payload is the complete generated source text.
// When format == JSON, payload mirrors netlistJson for backward compat.
struct SimulationSourceDTO {
    NetlistFormatDTO format  = NetlistFormatDTO::JSON;
    std::string      payload;  // JSON string or Verilog-A source text
};

// ── Simulation Request ──────────────────────────────────────────────────────
// This is the RPC contract. The GUI constructs this and passes it across the
// driver boundary. The GUI must NOT know what happens inside.
struct SimulationRequestDTO {
    // [DEPRECATED] Legacy JSON netlist transport. Use 'source' instead.
    [[deprecated("Use source instead")]] 
    std::string netlistJson;

    // [NEW] Canonical netlist transport — preferred over netlistJson when set.
    // Populated alongside netlistJson during transition period for compatibility.
    SimulationSourceDTO source;

    // Which analysis to run
    AnalysisTypeDTO analysis = AnalysisTypeDTO::DC;

    // Simulation parameters (analysis-specific, key-value)
    std::unordered_map<std::string, std::string> parameters;

    // Corner configuration
    CornerParametersDTO corner;

    // Topology hash for idempotency / caching
    uint64_t topologyHash = 0;

    // Stateless session token (cloud-readiness)
    std::string sessionToken;

    // Compute engine selection (maps to CircuitSim::ExecutionMode)
    // 0=LEGACY_AOS, 1=TENSOR_SOA_SCALAR, 2=TENSOR_SOA_SIMD, 3=GPU, 4=GPU_DEBUG
    int compute_mode = 0;
};

// ── Phase 4.4: Extended Result DTOs ─────────────────────────────────────────
// Defined before SimulationResponseDTO so they can be embedded by value.

// Transfer-function result: gain/phase at each frequency + stability margins.
struct TFResultDTO {
    std::vector<double> frequencies;      // Hz
    std::vector<double> gainDb;           // 20*log10(|H(jω)|)  [dB]
    std::vector<double> phaseDegrees;     // arg(H(jω))         [degrees]
    double gainMarginDb      = 0.0;       // margin at 180° phase crossing
    double phaseMarginDeg    = 0.0;       // margin at 0 dB gain crossing
    double gainCrossoverHz   = 0.0;       // frequency of 0 dB crossing
    double phaseCrossoverHz  = 0.0;       // frequency of 180° crossing
};

// Pole-zero result: roots of numerator/denominator of small-signal transfer fn.
struct PZResultDTO {
    std::vector<double> poleReal;
    std::vector<double> poleImag;
    std::vector<double> zeroReal;
    std::vector<double> zeroImag;
    double dcGain = 1.0;
};

// Fourier/THD result: spectral content extracted from a transient waveform.
struct FourierResultDTO {
    double fundamentalHz   = 0.0;
    std::vector<double> harmonicFreqHz;
    std::vector<double> harmonicMagDb;
    std::vector<double> harmonicPhaseDeg;
    double thdPercent      = 0.0;
};

// Optional extended results embedded in SimulationResponseDTO.
struct ExtendedResultsDTO {
    bool             hasTF      = false;
    bool             hasPZ      = false;
    bool             hasFourier = false;
    TFResultDTO      tf;
    PZResultDTO      pz;
    FourierResultDTO fourier;
};

// ── Convergence Telemetry ───────────────────────────────────────────────────
struct ConvergenceTelemetryDTO {
    int    iterations         = 0;
    double maxResidual        = 0.0;
    double rmsResidual        = 0.0;   // Phase 2.9: L2 norm of per-node KCL residual
    std::string method        = "Newton-Raphson";
    bool   gminSteppingUsed   = false;
    bool   sourceSteppingUsed = false;
    double solveTimeMs        = 0.0;

    // Phase 2.9: Numerical Intelligence
    bool     rankDeficient      = false;
    int      rankDeficientNode  = -1;   // 0-based row of smallest LU pivot
    double   smallestPivot      = 0.0;
    bool     stagnationDetected = false;
    bool     divergenceDetected = false;
    uint32_t worstNodeIndex     = 0;    // 1-based node with max |KCL residual|
    double   worstNodeVoltage   = 0.0;
    double   gminApplied        = 0.0;  // 0 = nominal (no significant regularization)
    uint32_t gminInjections     = 0;
};

// ── Simulation Response ─────────────────────────────────────────────────────
// This is what the driver returns. The GUI receives raw arrays and renders them.
// No solver internals leak through this boundary.
struct SimulationResponseDTO {
    uint64_t topologyHash = 0;
    bool     success      = false;
    std::string errorMessage;
    int32_t  runIndex     = -1;  // -1 for single runs, 0..N for batch/MC

    // Raw coordinate arrays for direct plotting
    std::vector<double> timeVector;

    // Node voltages: nodeIndex -> voltage array
    std::unordered_map<int, std::vector<double>> nodeVoltages;

    // Branch currents: branchIndex -> current array
    std::unordered_map<int, std::vector<double>> branchCurrents;

    // Named probe results: probeName -> voltage array
    std::unordered_map<std::string, std::vector<double>> probeValues;

    // DC operating point (single-value per node, for DC analysis)
    std::vector<double> dcOperatingPoint;

    // Convergence telemetry
    ConvergenceTelemetryDTO convergence;

    // Phase 4.4: Optional TF / PZ / Fourier results.
    ExtendedResultsDTO extended;
};

// ── Progress Update ─────────────────────────────────────────────────────────
struct JobProgressDTO {
    uint64_t jobId     = 0;
    float    percent   = 0.0f;
    std::string message;
};

// ── Batch & Statistical Execution ───────────────────────────────────────────

// Represents a single parameter change in a sweep or monte carlo run
struct ParameterVariationDTO {
    std::string componentName; // transitional (long term: replace with UUID)
    std::string propertyName;
    double overrideValue;
};

// Adds stable run identity essential for parallel async execution
struct BatchRunDTO {
    uint32_t runIndex;
    std::vector<ParameterVariationDTO> overrides;
};

// Represents a full batch campaign (Sweep or generic batch)
struct SimulationBatchRequestDTO {
    // [DEPRECATED] The nominal circuit (legacy JSON). Use 'source' instead.
    [[deprecated("Use source instead")]]
    std::string baseNetlistJson; 

    // Canonical netlist transport — REQUIRED for all new calls.
    SimulationSourceDTO source;
    AnalysisTypeDTO analysis;
    std::unordered_map<std::string, std::string> parameters; // analysis params (stopTime, fStart, etc.)
    CornerParametersDTO corner;                               // corner config for all runs
    std::vector<BatchRunDTO> runs;
    std::string sessionToken;
};

// Statistical summary computed from Monte Carlo run results
struct StatisticalSummaryDTO {
    std::unordered_map<std::string, double> mean;    // probe -> mean
    std::unordered_map<std::string, double> stddev;  // probe -> stddev
    std::unordered_map<std::string, double> minVal;  // probe -> min
    std::unordered_map<std::string, double> maxVal;  // probe -> max
};

// Separate response DTO for batch to keep single-runs clean
struct SimulationBatchResponseDTO {
    std::string sessionToken;
    std::vector<SimulationResponseDTO> runResults;
    bool hadFailures = false;
    StatisticalSummaryDTO statistics;  // Populated only for Monte Carlo runs
};

// Defines a statistical variation to be generated by the driver
struct MonteCarloVariationDTO {
    std::string componentName;
    std::string propertyName;
    std::string distributionType; // "Gaussian", "Uniform"
    double nominalValue;
    double tolerance;             // 0.05 for 5%
};

// Specific configuration for driver-side statistical generation
struct MonteCarloConfigDTO {
    uint32_t sampleCount = 10;
    uint64_t seed = 42;
    std::vector<MonteCarloVariationDTO> variations;
};

} // namespace orchestration
} // namespace compute
} // namespace acutesim
