#pragma once
// ============================================================================
// ihdl_bridge.h — Pluggable HDL → TensorNetlist Compilation Interface
// ============================================================================
// Supports Verilog-A and Yosys JSON → TensorNetlist compilation.
// Compilation is expensive; all bridges must check the cache key first.
// ABI hash prevents stale cache entries after engine updates.
// ============================================================================

#include <string>
#include <unordered_map>
#include <cstdint>

namespace acutesim::engine_api {

// ── Compilation cache key ─────────────────────────────────────────────────────
struct CompilationCacheKey {
    uint64_t    sourceHash;            // FNV-1a of HDL source text
    uint64_t    paramHash;             // FNV-1a of parameter overrides
    std::string toolchainVersion;      // e.g. "veriloga-parser-1.2"
    uint64_t    engineAbiHash;         // Invalidates on engine ABI changes
                                       // Prevents silently stale cached netlists
};

// ── HDL compilation result ────────────────────────────────────────────────────
struct HdlCompilationResult {
    bool        success = false;
    std::string netlistJson;    // Serialised TensorNetlist for downstream DTOs
    std::string errorMessage;
    bool        fromCache = false;      // true if served from cache hit
    CompilationCacheKey cacheKey;
};

// ── Abstract HDL bridge ───────────────────────────────────────────────────────
class IHdlBridge {
public:
    virtual ~IHdlBridge() = default;

    /// Compile HDL source to TensorNetlist JSON.
    /// Implementations must check cache via cacheKey before recompiling.
    virtual HdlCompilationResult compile(
        const std::string&                              hdlSource,
        const std::unordered_map<std::string,
                                 std::string>&          parameters = {},
        CompilationCacheKey*                            outKey     = nullptr) = 0;

    /// True if this bridge can handle files with the given extension.
    virtual bool supportsFormat(const std::string& extension) const = 0;

    /// Human-readable bridge name for logging ("Verilog-A Bridge 1.2").
    virtual std::string bridgeName() const = 0;
};

// ── Registry ──────────────────────────────────────────────────────────────────
class HdlRegistry {
public:
    static HdlRegistry& instance();

    void registerBridge(IHdlBridge* bridge);

    /// Find a bridge for the given file extension (.va, .json, etc.)
    IHdlBridge* findFor(const std::string& extension) const;

    HdlCompilationResult compile(
        const std::string& extension,
        const std::string& hdlSource,
        const std::unordered_map<std::string, std::string>& parameters = {});

private:
    HdlRegistry() = default;
    std::vector<IHdlBridge*> bridges_;
};

} // namespace acutesim::engine_api
