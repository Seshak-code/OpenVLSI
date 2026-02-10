# RAG Pipeline - flow.md

## Purpose
End-to-end retrieval-augmented generation pipeline for simulation intelligence.
Connects ingestion, chunking, storage, retrieval, reranking, budget allocation,
and prompt assembly into a single orchestrated flow.

## Data Flow
```
Solver Output (SimulationTrace)
  -> RAGPipelineOrchestrator::ingestTrace()
    -> TraceChunker::chunk()          [ChunkingStrategy: BALANCED/FAILURE_FOCUSED/...]
    -> LocalVectorStore::upsertTrace() [In-memory, topology-indexed]
    -> ContextEngine::setLastTrace()   [Live context cache]

User Query
  -> RAGPipelineOrchestrator::buildPromptContext(query, maxTokens)
    -> LocalVectorStore::querySimilar(query, 2x topK)
    -> FeedbackLoop::rerankContext()   [Composite: base + keyword + EWMA + failure boost]
    -> ContextEngine::serialize*()     [Live topology + probes + solver]
    -> TopologyAnalyzer::toJson()      [Compressed topology prompt]
    -> ContextBudgetManager::allocate() [Greedy knapsack by priority]
    <- Token-budgeted prompt string

Feedback
  -> FeedbackLoop::submitFeedback(queryId, rating)
    -> ChunkStats EWMA update (alpha=0.1)
    -> Context weight promotion (+0.05, capped 1.0)
    -> Slow decay for unused chunks (0.99x)
    -> Atomic persist via SafeIo
```

## Rules
1. All ingestion goes through RAGPipelineOrchestrator (never direct vector store access)
2. Retrieval always fetches 2x candidates then reranks to topK
3. Budget allocation MUST NOT exceed maxTokens
4. FeedbackLoop::registerQueryContext() MUST be called before submitFeedback()
5. Token estimation uses conservative heuristic (words * 1.3 * 1.05)
6. Legacy feedback format (bare JSON array) supported for backward compatibility

## Priority Levels
| Priority | Content Types | Use Case |
|----------|--------------|----------|
| CRITICAL | FailureLog, DebugContext | Active failures, convergence errors |
| HIGH | SignalPath, ProbeData | Signal analysis, active measurements |
| MEDIUM | Summary, DeviceMix, SolverDecisions | General circuit context |
| LOW | Waveform, WaveformSummary | Detailed time-series data |
| OPTIONAL | ReferenceDesign, SimilarTrace | Historical comparisons |

## SME Checklist
- [ ] Token budget never exceeded in allocation output
- [ ] EWMA scores converge toward 1.0 for consistently helpful chunks
- [ ] Failure chunks always included when circuit has active failures
- [ ] Counterfactual queries return different configs for same topology
- [ ] Session turns bounded by FIFO eviction at 200 turns
