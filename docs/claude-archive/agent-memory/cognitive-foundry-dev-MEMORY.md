# CuteApp/AcuteSim Cognitive Foundry — Agent Memory

## Phase 4: Tensor MNA Engine — API-First Expansion (2026-02-25)

### Static Registration Pattern (CRITICAL)
C++ static-init self-registration objects in static libraries are DEAD-STRIPPED
by the linker when no symbol from the TU is referenced. Fix:
1. Add `registerAllAnalyses()` function in `compute/solvers/register_analyses.cpp`
2. Call it explicitly from main() or test setUp
3. Do NOT rely on `static RegisterAnalysis<T,Fn>` objects alone in static libs

### Phase 4 Key Files
- `compute/analysis_registry.h/.cpp` — Singleton map: AnalysisTypeDTO → AnalysisHandler
- `compute/staged_dispatcher.h/.cpp` — Circuit metrics → ExecutionTier selector
- `compute/orchestration/driver/job_scheduler.h/.cpp` — Thread pool (WASM: inline sync)
- `compute/solvers/tf_analysis.h/.cpp` — Transfer function (TF) solver
- `compute/solvers/pz_analysis.h/.cpp` — Pole-zero (PZ) solver
- `compute/solvers/fourier_analysis.h/.cpp` — Fourier/THD solver
- `compute/solvers/register_analyses.h/.cpp` — Explicit registration entrypoint
- `compute/orchestration/dto/simulation_dto.h` — Extended with TF/PZ/FOURIER types

### DTO Namespace
simulation_dto.h uses NESTED namespace: `acutesim::compute::orchestration`
NOT `acutesim::compute`. All DTOs live in the innermost namespace.

### HardwareCapabilities Field Names
- `hw_.simdTier` is of type `HardwareCapabilities::SIMDTier` (NOT `SimdTier`)
- Values: `SIMDTier::SCALAR`, `SIMDTier::AVX2`, `SIMDTier::AVX512`, `SIMDTier::NEON`
- `hw_.hasGPU` (bool), `hw_.physicalCpuCores` (int)

### AnalysisTypeDTO extended values (Phase 4)
- `TRANSFER_FUNCTION = 6`
- `POLE_ZERO = 7`
- `FOURIER = 8`

### New Solver Architecture Pattern
New solvers (TF/PZ/Fourier) use a two-layer design:
1. `compute/solvers/xxx_analysis.h` — pure function API: `(TensorNetlist, dcSolution, req) → ResponseDTO`
2. `local_worker_driver.cpp` Impl — calls DC OP first, then delegates to solver fn
3. Registry stubs only for `supports()` / `registeredTypes()` queries

### ExecutionTier Selection Rules (StagedDispatcher)
- nodeCount < 30 AND isLinear → SCALAR
- nodeCount > 500 AND isLinear AND hasGPU → GPU
- activeDeviceCount > 50 → MULTICORE_CPU
- nodeCount < 200 AND simdTier >= AVX2 → SIMD_CPU
- nodeCount >= 200 AND !hasGPU → MULTICORE_CPU
- nodeCount >= 200 AND hasGPU → GPU

### Parallel Batch Pattern (Phase 4.8)
- `executeBatchParallel(req, solver, progress, cancelled, scheduler)`
- scheduler==nullptr falls back to sequential executeBatch
- Pre-allocates result slots by index to avoid mutex on result vector
- Uses barrier (condition_variable + atomic doneCount) for completion

### Test Count After Phase 4
29 CTest tests (27 original + test_analysis_registry + test_parallel_batch)
All 29 pass on macOS as of 2026-02-25.
