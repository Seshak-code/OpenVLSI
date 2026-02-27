#pragma once
// ============================================================================
// netlist_contract.h — Standardised Netlist Export API
// ============================================================================
// No Qt. No filesystem assumptions. Paths provided via request DTO.
// Exporters are STATELESS — safe for parallel export pipelines.
// ============================================================================

#include <string>
#include <vector>

// Forward declaration to avoid engine internals leaking
namespace acutesim { struct TensorNetlist; }

namespace acutesim::engine_api {

enum class NetlistFormat : uint8_t {
    SPICE,
    VERILOG_A,
    SPECTRE
};

struct NetlistExportRequest {
    uint32_t       schemaVersion     = 1;      // Future-proofs saved projects
    std::string    netlistJson;                // Transitional source
    NetlistFormat  format            = NetlistFormat::SPICE;
    std::string    topCellName;
    bool           includeParasitics = false;
    std::string    outputPath;                 // DTO-provided — no hardcoded paths
};

struct NetlistExportResult {
    bool        success = false;
    std::string output;          // Serialized netlist text
    std::string errorMessage;
};

// ── Stateless exporter interface ──────────────────────────────────────────────
class INetlistExporter {
public:
    virtual NetlistExportResult exportNetlist(
        const NetlistExportRequest&) const = 0;
    virtual NetlistFormat       supportedFormat() const = 0;
    virtual ~INetlistExporter() = default;
};

// ── Registry — thin, no dynamic loading ───────────────────────────────────────
class NetlistExporterRegistry {
public:
    static NetlistExporterRegistry& instance();

    void registerExporter(INetlistExporter* exporter);

    /// Returns nullptr if no exporter registered for this format.
    const INetlistExporter* find(NetlistFormat format) const;

    NetlistExportResult exportWith(NetlistFormat format,
                                   const NetlistExportRequest& req) const;

private:
    NetlistExporterRegistry() = default;
    std::vector<INetlistExporter*> exporters_;
};

} // namespace acutesim::engine_api
