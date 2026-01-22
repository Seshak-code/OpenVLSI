#pragma once

#include "simulation_trace.h"
#include <QString>
#include <QVector>
#include <QCryptographicHash>

namespace AIAPI {

/**
 * ChunkingStrategy - Controls how simulation traces are segmented for RAG.
 */
enum class ChunkingStrategy {
    SUMMARY_ONLY,      // Single chunk with high-level summary
    FULL_WAVEFORMS,    // Include time-series data (larger, more detailed)
    FAILURE_FOCUSED,   // Emphasize failure context (ideal for debugging RAG)
    BALANCED           // Default: Summary + DeviceMix + CondensedWaveforms
};

/**
 * TraceChunker - Converts SimulationTrace into embeddable RAG chunks.
 * 
 * Each chunk is a self-contained semantic unit suitable for:
 * - Vector database ingestion (FAISS/Pinecone/Milvus)
 * - LLM prompt injection
 * - Similarity search
 */
class TraceChunker {
public:
    /**
     * Chunk a simulation trace according to the specified strategy.
     * Returns a vector of TraceChunk objects ready for embedding.
     */
    static QVector<TraceChunk> chunk(const SimulationTrace& trace, 
                                      ChunkingStrategy strategy = ChunkingStrategy::BALANCED) {
        QVector<TraceChunk> chunks;
        
        switch (strategy) {
            case ChunkingStrategy::SUMMARY_ONLY:
                chunks.append(createSummaryChunk(trace));
                break;
                
            case ChunkingStrategy::FULL_WAVEFORMS:
                chunks.append(createSummaryChunk(trace));
                chunks.append(createDeviceMixChunk(trace));
                chunks.append(createWaveformChunks(trace));
                if (trace.isFailure()) {
                    chunks.append(createFailureChunks(trace));
                }
                break;
                
            case ChunkingStrategy::FAILURE_FOCUSED:
                chunks.append(createSummaryChunk(trace));
                if (trace.isFailure()) {
                    chunks.append(createFailureChunks(trace));
                    chunks.append(createDebugContextChunk(trace));
                }
                break;
                
            case ChunkingStrategy::BALANCED:
            default:
                chunks.append(createSummaryChunk(trace));
                chunks.append(createDeviceMixChunk(trace));
                if (trace.isFailure()) {
                    chunks.append(createFailureChunks(trace));
                }
                // Condensed waveform summary (not full time-series)
                chunks.append(createWaveformSummaryChunk(trace));
                break;
        }
        
        return chunks;
    }

private:
    /**
     * Generate a unique chunk ID from trace ID and content hash.
     */
    static QString generateChunkId(const QString& traceId, const QString& contentType) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData((traceId + contentType).toUtf8());
        return traceId + "_" + QString::fromLatin1(hash.result().toHex().left(8));
    }
    
    /**
     * Create high-level summary chunk.
     */
    static TraceChunk createSummaryChunk(const SimulationTrace& trace) {
        TraceChunk chunk;
        chunk.chunkId = generateChunkId(trace.traceId, "Summary");
        chunk.contentType = "Summary";
        
        // Generate natural language summary
        QString text = QString("Simulation %1 on topology %2. ")
                           .arg(trace.isFailure() ? "FAILED" : "SUCCEEDED")
                           .arg(trace.topologyHash);
        
        // Add device count
        int totalDevices = 0;
        for (auto it = trace.deviceMix.begin(); it != trace.deviceMix.end(); ++it) {
            totalDevices += it.value();
        }
        text += QString("Circuit contains %1 devices. ").arg(totalDevices);
        
        // Add solver config
        text += QString("Solver: %1, RELTOL=%2, ABSTOL=%3. ")
                    .arg(trace.configParams.solverType)
                    .arg(trace.configParams.reltol, 0, 'e', 2)
                    .arg(trace.configParams.abstol, 0, 'e', 2);
        
        if (trace.configParams.gminSteppingEnabled) {
            text += "GMIN stepping enabled. ";
        }
        
        chunk.textContent = text;
        chunk.metadata["topology"] = trace.topologyHash;
        chunk.metadata["result"] = trace.isFailure() ? "Failed" : "Converged";
        chunk.metadata["deviceCount"] = totalDevices;
        
        return chunk;
    }
    
    /**
     * Create device mix chunk (what components are in the circuit).
     */
    static TraceChunk createDeviceMixChunk(const SimulationTrace& trace) {
        TraceChunk chunk;
        chunk.chunkId = generateChunkId(trace.traceId, "DeviceMix");
        chunk.contentType = "DeviceMix";
        
        QStringList deviceList;
        for (auto it = trace.deviceMix.begin(); it != trace.deviceMix.end(); ++it) {
            deviceList << QString("%1 x%2").arg(it.key()).arg(it.value());
        }
        
        chunk.textContent = "Device composition: " + deviceList.join(", ") + ".";
        chunk.metadata["topology"] = trace.topologyHash;
        
        return chunk;
    }
    
    /**
     * Create failure-specific chunks for each failure mode.
     */
    static QVector<TraceChunk> createFailureChunks(const SimulationTrace& trace) {
        QVector<TraceChunk> chunks;
        
        for (int i = 0; i < trace.failureModes.size(); ++i) {
            const auto& failure = trace.failureModes[i];
            
            TraceChunk chunk;
            chunk.chunkId = generateChunkId(trace.traceId, QString("Failure_%1").arg(i));
            chunk.contentType = "FailureLog";
            
            chunk.textContent = QString("Failure: %1. %2 Involved nodes: %3. Time: %4s.")
                                    .arg(failure.failureType)
                                    .arg(failure.description)
                                    .arg(failure.involvedNodes.join(", "))
                                    .arg(failure.timestamp, 0, 'e', 3);
            
            chunk.metadata["failureType"] = failure.failureType;
            chunk.metadata["topology"] = trace.topologyHash;
            
            chunks.append(chunk);
        }
        
        return chunks;
    }
    
    /**
     * Create debug context chunk (solver state at failure).
     */
    static TraceChunk createDebugContextChunk(const SimulationTrace& trace) {
        TraceChunk chunk;
        chunk.chunkId = generateChunkId(trace.traceId, "DebugContext");
        chunk.contentType = "DebugContext";
        
        // Aggregate debug info from all failures
        QStringList debugLines;
        for (const auto& failure : trace.failureModes) {
            if (!failure.debugContext.isEmpty()) {
                for (auto it = failure.debugContext.begin(); it != failure.debugContext.end(); ++it) {
                    debugLines << QString("%1=%2").arg(it.key()).arg(it.value().toString());
                }
            }
        }
        
        chunk.textContent = "Debug context: " + debugLines.join("; ");
        chunk.metadata["topology"] = trace.topologyHash;
        
        return chunk;
    }
    
    /**
     * Create full waveform chunks (for detailed analysis).
     */
    static QVector<TraceChunk> createWaveformChunks(const SimulationTrace& trace) {
        QVector<TraceChunk> chunks;
        
        for (auto it = trace.voltageSeries.begin(); it != trace.voltageSeries.end(); ++it) {
            TraceChunk chunk;
            chunk.chunkId = generateChunkId(trace.traceId, "Waveform_" + it.key());
            chunk.contentType = "Waveform";
            
            const auto& series = it.value();
            if (series.empty()) continue;
            
            // Summary statistics
            double minV = *std::min_element(series.begin(), series.end());
            double maxV = *std::max_element(series.begin(), series.end());
            double sum = 0.0;
            for (double v : series) sum += v;
            double mean = sum / series.size();
            
            chunk.textContent = QString("Node %1: Min=%2V, Max=%3V, Mean=%4V (%5 points)")
                                    .arg(it.key())
                                    .arg(minV, 0, 'g', 4)
                                    .arg(maxV, 0, 'g', 4)
                                    .arg(mean, 0, 'g', 4)
                                    .arg(series.size());
            
            chunk.metadata["node"] = it.key();
            chunk.metadata["min"] = minV;
            chunk.metadata["max"] = maxV;
            
            chunks.append(chunk);
        }
        
        return chunks;
    }
    
    /**
     * Create condensed waveform summary (single chunk).
     */
    static TraceChunk createWaveformSummaryChunk(const SimulationTrace& trace) {
        TraceChunk chunk;
        chunk.chunkId = generateChunkId(trace.traceId, "WaveformSummary");
        chunk.contentType = "WaveformSummary";
        
        QStringList summaries;
        for (auto it = trace.voltageSeries.begin(); it != trace.voltageSeries.end(); ++it) {
            const auto& series = it.value();
            if (series.empty()) continue;
            
            double lastV = series.back();
            summaries << QString("%1=%2V").arg(it.key()).arg(lastV, 0, 'g', 4);
        }
        
        chunk.textContent = "Final node voltages: " + summaries.join(", ");
        chunk.metadata["topology"] = trace.topologyHash;
        
        return chunk;
    }
};

} // namespace AIAPI
