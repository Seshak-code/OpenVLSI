#pragma once
#include <QString>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <vector>
#include <string>

namespace AIAPI {

/**
 * Step 1.1: Simulation Trace Schema
 * Represents a discrete simulation run as a queryable knowledge object.
 */

struct TraceFailure {
    QString failureType;     // e.g., "Singular Jacobian", "Floating Node"
    QString description;     // Human-readable cause
    double timestamp;        // Simulation time at failure
    QVector<QString> involvedNodes; // Nodes implicated in the divergence
    QJsonObject debugContext; // Additional solver state (residues, pivot indices)
};

struct TraceConfig {
    QString solverType;      // "PCG", "LU", "GMRES"
    double initialTimeStep;
    bool gminSteppingEnabled;
    double reltol;
    double abstol;
    bool predictionEnabled;  // Was Copilot "What-If" prediction active?
};

/**
 * Step 1.2: Embedding Chunk
 * A normalized segment of the trace suitable for Vector Database ingestion.
 */
struct TraceChunk {
    QString chunkId;         // Unique ID (TraceID + SegmentID)
    QString contentType;     // "Summary", "DeviceCharacteristics", "FailureLog", "WaveformSummary"
    QString textContent;     // Textual representation for embedding model (e.g., "NMOS M1 entered saturation...")
    QVector<float> embedding; // Dense vector (e.g., 1536-dim from OpenAI/Bert)
    QJsonObject metadata;    // Filter keys: { "topology": "DiffPair", "result": "Converged" }
};

/**
 * The Root Knowledge Object
 */
struct SimulationTrace {
    QString traceId;
    QString topologyHash;    // Canonical ID of the circuit structure
    QMap<QString, int> deviceMix; // e.g., {"NMOS": 12, "Resistor": 5, "BJT": 2}
    
    QVector<TraceFailure> failureModes; // If empty, simulation was successful
    
    // Raw Data (for regeneration or detailed analysis)
    // Map: NodeName -> TimeSeries [v(t0), v(t1), ...]
    QMap<QString, std::vector<double>> voltageSeries; 
    std::vector<double> timePoints;

    TraceConfig configParams;
    double timestamp;        // Real-world Unix timestamp of execution

    // RAG Data
    QVector<TraceChunk> ragChunks;
    
    // Helpers
    bool isFailure() const { return !failureModes.empty(); }
    QString getSummary() const; // Auto-generate textual summary
};

/**
 * Step 1.3: Vector Store Interface (Stub)
 * Abstract interface for interacting with the RAG database (FAISS/Pinecone/Milvus).
 */
class VectorStoreInterface {
public:
    virtual ~VectorStoreInterface() = default;

    // Ingest a trace, chunk it, embed it, and store it.
    virtual bool upsertTrace(const SimulationTrace& trace) = 0;

    // Retrieve similar traces based on a natural language query or vector.
    virtual QVector<SimulationTrace> querySimilar(const QString& naturalQuery, int topK = 5) = 0;
    virtual QVector<SimulationTrace> querySimilar(const QVector<float>& queryVec, int topK = 5) = 0;
    
    // "What-If" Retrieval: Find traces with same topology but different config
    virtual QVector<SimulationTrace> findCounterfactuals(const QString& topologyHash, const TraceConfig& targetConfig) = 0;
};

} // namespace AIAPI
