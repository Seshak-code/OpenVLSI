#pragma once

#include "simulation_trace.h"
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>

namespace AIAPI {

/**
 * LocalVectorStore - In-memory implementation of VectorStoreInterface.
 * 
 * This is a placeholder/development implementation that:
 * - Stores traces in memory (no disk persistence)
 * - Uses simple topology hash matching instead of actual vector similarity
 * - Provides the same API as production Pinecone/FAISS implementations
 * 
 * For production, swap with PineconeVectorStore or FAISSVectorStore.
 */
class LocalVectorStore : public VectorStoreInterface {
public:
    LocalVectorStore() = default;
    ~LocalVectorStore() override = default;
    
    /**
     * Upsert (insert or update) a simulation trace.
     * Traces are keyed by traceId; duplicate IDs overwrite.
     */
    bool upsertTrace(const SimulationTrace& trace) override {
        QMutexLocker locker(&mutex_);
        traces_[trace.traceId] = trace;
        
        // Index by topology hash for fast counterfactual lookup
        topologyIndex_[trace.topologyHash].append(trace.traceId);
        
        return true;
    }
    
    /**
     * Query similar traces by natural language query.
     * 
     * NOTE: This local implementation does NOT perform actual embedding.
     * It returns traces that match keywords in the query string.
     * Replace with real embedding + cosine similarity for production.
     */
    QVector<SimulationTrace> querySimilar(const QString& naturalQuery, int topK = 5) override {
        QMutexLocker locker(&mutex_);
        QVector<SimulationTrace> results;
        
        // Simple keyword matching (placeholder for embedding similarity)
        QString queryLower = naturalQuery.toLower();
        
        for (auto it = traces_.begin(); it != traces_.end(); ++it) {
            const SimulationTrace& trace = it.value();
            
            // Match on failure type or topology
            bool match = false;
            if (queryLower.contains("fail") && trace.isFailure()) match = true;
            if (queryLower.contains("converge") && !trace.isFailure()) match = true;
            if (queryLower.contains(trace.topologyHash.toLower())) match = true;
            
            // Match on device types
            for (auto dit = trace.deviceMix.begin(); dit != trace.deviceMix.end(); ++dit) {
                if (queryLower.contains(dit.key().toLower())) {
                    match = true;
                    break;
                }
            }
            
            if (match) {
                results.append(trace);
                if (results.size() >= topK) break;
            }
        }
        
        return results;
    }
    
    /**
     * Query similar traces by embedding vector.
     * 
     * NOTE: Local implementation ignores the vector and returns recent traces.
     * Replace with actual cosine similarity for production.
     */
    QVector<SimulationTrace> querySimilar(const QVector<float>& /*queryVec*/, int topK = 5) override {
        QMutexLocker locker(&mutex_);
        QVector<SimulationTrace> results;
        
        // Just return most recent traces (placeholder)
        int count = 0;
        for (auto it = traces_.begin(); it != traces_.end() && count < topK; ++it, ++count) {
            results.append(it.value());
        }
        
        return results;
    }
    
    /**
     * Find counterfactual traces: same topology, different configuration.
     * 
     * Useful for "What-If" analysis: "What happened when GMIN stepping was off?"
     */
    QVector<SimulationTrace> findCounterfactuals(const QString& topologyHash, 
                                                  const TraceConfig& targetConfig) override {
        QMutexLocker locker(&mutex_);
        QVector<SimulationTrace> results;
        
        // Get all traces with the same topology
        if (!topologyIndex_.contains(topologyHash)) {
            return results;
        }
        
        for (const QString& traceId : topologyIndex_[topologyHash]) {
            if (!traces_.contains(traceId)) continue;
            const SimulationTrace& trace = traces_[traceId];
            
            // Check if config differs in meaningful ways
            bool configDiffers = false;
            if (trace.configParams.gminSteppingEnabled != targetConfig.gminSteppingEnabled) configDiffers = true;
            if (trace.configParams.solverType != targetConfig.solverType) configDiffers = true;
            if (std::abs(trace.configParams.reltol - targetConfig.reltol) > 1e-12) configDiffers = true;
            
            if (configDiffers) {
                results.append(trace);
            }
        }
        
        return results;
    }
    
    // ==========================================================================
    // Additional Local-Only Methods (not in interface)
    // ==========================================================================
    
    /**
     * Get a specific trace by ID.
     */
    SimulationTrace getTrace(const QString& traceId) const {
        QMutexLocker locker(&mutex_);
        return traces_.value(traceId);
    }
    
    /**
     * Check if a trace exists.
     */
    bool hasTrace(const QString& traceId) const {
        QMutexLocker locker(&mutex_);
        return traces_.contains(traceId);
    }
    
    /**
     * Get total number of stored traces.
     */
    int count() const {
        QMutexLocker locker(&mutex_);
        return traces_.size();
    }
    
    /**
     * Clear all stored traces.
     */
    void clear() {
        QMutexLocker locker(&mutex_);
        traces_.clear();
        topologyIndex_.clear();
    }
    
    /**
     * Get all traces for a given topology.
     */
    QVector<SimulationTrace> getTracesByTopology(const QString& topologyHash) const {
        QMutexLocker locker(&mutex_);
        QVector<SimulationTrace> results;
        
        if (topologyIndex_.contains(topologyHash)) {
            for (const QString& traceId : topologyIndex_[topologyHash]) {
                if (traces_.contains(traceId)) {
                    results.append(traces_[traceId]);
                }
            }
        }
        
        return results;
    }

private:
    mutable QMutex mutex_;
    
    // Primary storage: traceId -> SimulationTrace
    QMap<QString, SimulationTrace> traces_;
    
    // Secondary index: topologyHash -> [traceIds]
    QMap<QString, QVector<QString>> topologyIndex_;
};

} // namespace AIAPI
