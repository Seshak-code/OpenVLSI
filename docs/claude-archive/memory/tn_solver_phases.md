# Tensor Network Solver Phases (5.3.x)

## Phase 5.3.3: Contraction Execution Engine (2026-03-04) — COMPLETE

### New Files (4)
- `acutesim_engine/solvers/tensor_workspace.h` — EliminationRecord, IntermediateTensor, TensorWorkspace (pre-allocated, reusable via reset())
- `acutesim_engine/solvers/contraction_executor.h/cpp` — ContractionExecutor::solve(); seedLeaves→forwardElimination→backSubstitute; CONTRACTION_TW_LIMIT=80
- `tests/test_contraction_executor.cpp` — 8 tests, 26 assertions (TwoNodeResistor, VoltageDivider, RCLadder5, DiodeCircuit, StarTopology, BackSubRecovery, GMINConditioning, WorkspaceReuse)
- `tests/test_contraction_parity.cpp` — 4 tests, 9 assertions (ParityVsDenseLU 20-node, ParityVsSparseLU 30-node, ParityVsSchur 40-node, NonlinearMPO 3-BJT)

### Modified Files
- `acutesim_engine/physics/circuitsim.h` — Added `#include "tensor_workspace.h"` + `std::unique_ptr<TensorWorkspace> cachedWorkspace_` member
- `acutesim_engine/solvers/circuitsim_solver.cpp` — Added Tier 3 contraction executor routing + MPO rebuild per NR iter + cachedWorkspace_ invalidation on topology change
- `acutesim_engine/infrastructure/tn_compiler.cpp` — Step 5: workspace sizing (estimatedWorkspaceBytes, maxIntermediateDim) computed from tree walk
- `acutesim_engine/infrastructure/mpo_builder.h` — Added `localRHS` field to DeviceMPO (done in 5.3.2 prep)
- `acutesim_engine/CMakeLists.txt` — Added `contraction_executor.cpp` to `# === Solvers ===`
- Root `CMakeLists.txt` — Added test_contraction_executor + test_contraction_parity targets

### Solver Tier Routing (circuitsim_solver.cpp)
```
Tier 1: N < 20 or no treewidth data         → Dense LU
Tier 2: tw ≤ 50 + cached symbolic           → Sparse LU (fallback: dense)
Tier 3: tw ≤ 80 + viable TN program  [NEW]  → Contraction Executor (fallback: sparse → dense)
Tier 4: tw ≤ 100 + partitions > 1           → Schur (fallback: sparse → dense)
Tier 5: else                                 → PCG (fallback: dense)
```

### Key Design Decisions
- MPOs rebuilt EACH NR iteration via MPOBuilder::buildMPOs() at current voltage estimate (needed for nonlinear device RHS)
- TensorWorkspace allocated once per topology, reset() per NR iteration (allocation reuse)
- cachedWorkspace_ invalidated alongside cachedPattern_ on topology hash change
- ContractionExecutor modifies TNCompiledProgram.mpos (non-const through shared_ptr — const propagation stops at shared_ptr boundary)

### Key Bug Fixed
- **Left-skewed tree + global contractedVars**: When building contraction trees, contracted variables must only be specified at the merge node where ALL devices touching those variables have been accumulated. Applying the same contractedVars at every merge node causes double-elimination — a variable gets eliminated at one node, then reappears from a later right child and gets eliminated again with only partial information, corrupting back-substitution.

## Gap 1: GPU Contraction Dispatch (2026-03-11) — COMPLETE

### Modified Files
- `acutesim_engine/solvers/webgpu_solver.h` — Added tree walk state (lastTree_, leafTerminalNodes_, staging buffers), createTNBindGroups()
- `acutesim_engine/solvers/webgpu_solver.cpp` — Full executeContractionSweep() (~250 lines): bottom-up tree walk, GPU merge dispatch, CPU Schur+compact, root solve, back-substitution. Auto-layout pipelines, createTNBindGroups() from auto-derived layouts.
- `acutesim_engine/shaders/tn_contraction.wgsl` — 3 WGSL kernels (tn_seed_leaf, tn_merge_accum, tn_schur_elim), EmulatedF64. Not modified but newly tracked.

### Design Decisions
- **Hybrid GPU-CPU**: GPU does merge/accumulate via tn_merge_accum, CPU downloads merged result, does Schur elimination + compaction, uploads back. Per-node data is ~1KB (MAX_TILE_RANK=8).
- **Auto-layout pipelines**: `d.layout = nullptr` lets Dawn reflect WGSL bindings. Bind groups derived from tn_merge_accum (uses both group 0 and group 1).
- **Only tn_merge_accum dispatched on GPU** in tree walk. Schur done on CPU (avoids shader compaction issue).

## Gap 2: SM Budget Enforcement — Already Implemented (found in codebase)
- hasSchurBoundary field, tileMemoryBudget param, enforcement logic all present in contraction_tree.h/cpp and tn_compiler.cpp

## Gap 3: Mixed-Precision IR Wiring (2026-03-11) — COMPLETE

### Modified Files
- `acutesim_engine/physics/circuitsim.h` — Added prevVoltages_, cachedMpos_, neuralSeeder_ members
- `acutesim_engine/solvers/circuitsim_solver.cpp` — Added adaptive_precision.h include; wired AdaptivePrecisionRouter::assignTiers() into Tier-3 block; save prevVoltages_ on convergence; invalidate on topology change
- `acutesim_engine/infrastructure/adaptive_precision.h` — Already existed (header-only, not modified)

### Architecture
- AdaptivePrecisionRouter called with partitions + current/prev voltages after entering Tier-3 block
- enableFP32 defaults to false (activated after Gap 1 GPU path is verified)
- cachedMpos_ saved per iteration for CACHED_REUSE lookup; cleared on topology change
- Per-partition selective MPO rebuild is a future optimization (metadata available now)

## Gap 4: Neural Convergence Seeder (2026-03-11) — COMPLETE

### Modified Files
- `acutesim_engine/solvers/neural_seeder.h` — Added predict() method: physics-informed dV/dt linear extrapolation with ±1kV/s clamping
- `acutesim_engine/physics/circuitsim.h` — Added neural_seeder.h include + NeuralSeeder neuralSeeder_ member
- `acutesim_engine/solvers/circuitsim_solver.cpp` — Wired seeder into solveDC() (predict → predictSeed → seedInitialGuess layering) and stepTransient() (predict before history rejection); recordSolution() after convergence; reset() on topology change

### Architecture
- NeuralSeeder::predict() uses linear dV/dt extrapolation from two most recent solutions
- NeuralSeeder::predictSeed() uses EMA-weighted history (existing, not modified)
- Seeder runs BEFORE seedInitialGuess() which always applies on top (supply rails, PN junctions)
- History cleared on topology change alongside cachedWorkspace_, cachedMpos_, prevVoltages_
