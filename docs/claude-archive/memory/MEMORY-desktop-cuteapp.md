# CuteApp/AcuteSim Project Memory

## Project Overview
- AI-agent ASIC design IDE ("Cognitive Foundry") with Qt6 frontend + custom MNA tensor backend
- Dual Kernel: AcuteSim (physics body) + CuteApp (AI mind)
- Platforms: macOS/Windows/WASM | C++17 | Google Dawn (WebGPU) | gRPC orchestration

## Key Architecture Files
- `context.md` - Comprehensive interconnection map (updated 2026-02-05)
- `Roadmap.md` - 2026 development plan
- Every directory has a `flow.md` with rules and SME checklists
- `main.cpp` - Entry point (Qt app + CLI: --stress, --monte-carlo, --benchmark)

## Critical Rules
1. Physics in `device_physics.h` must be STATELESS (no static vars)
2. Use unqualified `exp()` not `std::exp` for Dual<T> ADL compatibility
3. TensorBlock changes MUST update: graph_canonical.h, netlist_diff.h, physics_tensors.h
4. gRPC code guarded by `#ifdef ACUTESIM_ENABLE_ORCHESTRATION` for WASM
5. FP determinism mandatory: -fno-fast-math, -ffloat-store, /fp:strict
6. AI actions MUST go through AiActionEnvelope (never direct QGraphicsScene access)
7. Dependencies flow DOWNWARD only (gui->ai_api->compute->physics->autodiff)

## Build
- CMake + Qt6 (homebrew on macOS: /opt/homebrew/opt/qt)
- Tests: `cd build && ctest --verbose`
- WASM: Emscripten build with statx shim for Qt 6.9.3
- Use `-DFETCHCONTENT_UPDATES_DISCONNECTED=ON` to avoid Dawn network fetches

## WASM Exception Handling (CRITICAL)
- Qt 6.9.3 wasm_singlethread has `wasm_exceptions` in QT_DISABLED_PUBLIC_FEATURES
- Do NOT use `-fwasm-exceptions` or `-sSUPPORT_LONGJMP=wasm` — Qt uses old-style invoke_ exceptions
- Using `-fwasm-exceptions` causes: `AssertionError: invoke_ functions exported but exceptions and longjmp are both disabled`
- WASM serve port is 8080 (not 8000 as CLAUDE.md says); emsdk at /Users/seshak/Qt/emsdk/; ninja at /Users/seshak/Qt/Tools/Ninja/ninja
- WASM configure: `source /Users/seshak/Qt/emsdk/emsdk_env.sh && EMSDK_QUIET=1 emcmake cmake -S . -B build-wasm -G Ninja -DQt6_DIR=/Users/seshak/Qt/6.9.3/wasm_singlethread/lib/cmake/Qt6 -DCMAKE_MAKE_PROGRAM=/Users/seshak/Qt/Tools/Ninja/ninja`

## CMakeLists Source Dependency Rules (2026-02-18)
- New source groups travel together in ALL test targets that need them:
  - `simulation_workspace.cpp` requires: `variables_panel.cpp`, `calculator_widget.cpp`, `parametric_sweep_dialog.cpp`, `postproc_engine.cpp`, `simulation_context.cpp`, `batch_runner.cpp`, `netlist_compiler_ctx.cpp`
  - `editor_manager.cpp` requires: `editor_factory.cpp`, `storage_manager.cpp`, viewer .cpp files
  - `project_explorer.cpp` requires: `library_model.cpp`, `library_index.cpp`
- Use `replace_all: true` with 8-space-indented patterns to update all test targets at once (4-space = ACUTESIM_SOURCES)
- Split context-dependent functions to separate .cpp to avoid transitive linker deps (e.g., `netlist_compiler_ctx.cpp` separates from `netlist_compiler.cpp`)
- `QJSEngine` is Qt::Qml — not available; use hand-written recursive descent parser for expression eval
- `gui/editors/schematic_editor.h` compat header needed (real header is in `schematic/` subdirectory)
- `QHash<T,U>` needs `#include <QHash>` — `qcontainerfwd.h` only forward-declares

## Qt6 MOC Gotchas (Qt 6.9)
- MOC follows `#include` and tracks `namespace` from headers
- If headers open `namespace AIAPI {}`, MOC thinks Q_OBJECT class is inside it
- Fix: Wrap AIAPI includes in `#ifndef Q_MOC_RUN ... #endif` in test .cpp files
- AI-layer tests use `tests/stubs/ai_api_test_stubs.cpp` to avoid GUI deps
- For Qt::Gui headers without linking: `$<TARGET_PROPERTY:Qt6::Gui,INTERFACE_INCLUDE_DIRECTORIES>`
- Dual Qt homebrew (qt 6.9.0 + qtbase 6.9.3) causes runtime crash for GUI-linked tests

## Phases 5.7.2 & 5.7.4: GPU Optimization Pillars (2026-03-18) — COMPLETE
See `phase_5_7_gpu_optimization.md` for implementation details.
- 5.7.2: Apple UMA zero-copy (apple_uma.h) + Thermal monitor (thermal_monitor.h)
- 5.7.4: Render Graph Scheduler (contraction_render_graph.h/.cpp) — slot aliasing O(N)→O(tw)
- test_render_graph: 12 tests, 78 assertions passing

## Phases 5.3.0–5.3.3 + Gaps 1–4: TN Solver Pipeline — ALL COMPLETE
See `tn_solver_phases.md` for detailed file lists and bug notes.
- Gap 1: GPU Contraction Dispatch (executeContractionSweep, hybrid GPU-CPU tree walk)
- Gap 2: SM Budget Enforcement (already implemented in codebase)
- Gap 3: Mixed-Precision IR Wiring (AdaptivePrecisionRouter into Tier-3, enableFP32 default false)
- Gap 4: Neural Convergence Seeder (dV/dt extrapolation in solveDC + stepTransient)

### Summary
- 5.3.0: Bond dimension profiler + viability gate (χ ≤ sqrt(N)*2+5)
- 5.3.1: MPO builder + contraction tree builder
- 5.3.2: Schur complement solver (SCHUR_TW_LIMIT=100)
- 5.3.3: Contraction execution engine (CONTRACTION_TW_LIMIT=80)

### Solver Tier Routing (circuitsim_solver.cpp)
- Tier 1: N<20 or no tw → Dense LU
- Tier 2: tw≤50 + symbolic → Sparse LU
- Tier 3: tw≤80 + viable TN → Contraction Executor (NEW in 5.3.3)
- Tier 4: tw≤100 + partitions>1 → Schur
- Tier 5: else → PCG
- All tiers fall back to dense LU on failure

### Test Coverage (9 suites, 170+ assertions all passing)
- test_factor_graph (33), test_sparse_lu (25), test_graph_partitioner (14)
- test_bond_dimension (11), test_mpo_builder (26), test_contraction_tree (13)
- test_schur_solver (23), test_contraction_executor (26), test_contraction_parity (9)

## Phase B: Treewidth-Guided Sparse LU Solver (2026-03-02) — COMPLETE

### New Files
- `acutesim_engine/math/sparse_lu.h` — SymbolicFactorization struct, symbolicFactorize(), solveSparse_LU()
- `acutesim_engine/math/sparse_lu.cpp` — Two-phase sparse solver: symbolic (pattern) + numeric (values)
- `tests/test_sparse_lu.cpp` — 6 test cases, 25 assertions (run directly, not ctest-registered)

### Architecture
- Symbolic phase: builds L+U CSR pattern from Min-Fill elimination ordering (symmetric fill tracking)
- Numeric phase: permutes matrix via perm/iperm, dense LU on permuted system, unpermutes solution
- Key fix: L+U pattern must be symmetrized — recording only remaining neighbors gives only U; must also insert L entries
- `CircuitSim::cachedSymbolic_` — cached alongside `cachedPattern_`, invalidated on topology hash change
- `CircuitSim::compiledBlock_` — shared_ptr set via `setCompiledBlock()` or aliasing constructor in `solveDC(CompiledTensorBlock&)`
- Treewidth-based solver routing in `circuitsim_solver.cpp` lines 260-298:
  - `!tw.isAnalyzed || N < 20` → dense LU (legacy path)
  - `tw.estimatedTreewidth <= 50 && cachedSymbolic_` → sparse LU with Min-Fill ordering
  - else → PCG with dense LU fallback
- Fallback safety: if sparse solver residual > 1e-6, retry dense LU
- `SPARSE_LU_TW_LIMIT = 50` constant in sparse_lu.h

### CMake
- `sparse_lu.cpp` added to `acutesim_engine_objects` (in `# === Math ===` section)
- `test_sparse_lu` target in root CMakeLists.txt (not ctest-registered)

## Phase A: Analytical Topology Engine (2026-03-01) — COMPLETE

### New Files
- `acutesim_engine/infrastructure/factor_graph.h/.cpp` — FactorGraph IR, VariableType (NodeVoltage/BranchCurrent), variableToFactors lazy adjacency
- `acutesim_engine/infrastructure/treewidth_analyzer.h/.cpp` — Min-Fill heuristic, TreewidthAnalysis (no estimatedBondDimension)
- `tests/test_factor_graph.cpp` — 7 tests (run directly, not ctest-registered)

### Architecture
- `CompiledTensorBlock.tnAnalysis` (TreewidthAnalysis) — new field after Phase C partitions block
- `NetlistCompiler::compile()` — step 6 is FactorGraph+TreewidthAnalyzer; old step 6 → step 7
- MNA branch-current variables for vsources+inductors are first-class MNA unknowns (IDs start at numCircuitNodes)
- Acyclicity via |E| ≤ |V|-1 edge count; isAcyclic is informational only — always runs Min-Fill
- Sort candidates before each Min-Fill step for determinism (never iterate unordered containers)
- `variableToFactors` provides O(k) lazy neighbor lookup; `buildPrimalGraph()` diagnostic only

## Mixed-Signal Acceleration Pillars (2026-02-28) — ALL 3 PHASES COMPLETE

### Phase A: Virtual VLIW (gpu_nr_loop.wgsl)
- `f64_select(false_val, true_val, cond)` — component-wise select for EmulatedF64 structs
- `f64_exp` fully predicated (no early returns): underflow/linear/Horner all computed, `f64_select()` picks
- Voltage gathers: `select(0u, route, route!=0xFFFF)` safe clamp, then `select(0.0, voltages_hi[safe], route!=0xFFFF)`
- MOSFET: two-stage `f64_select(f64_select(sat, lin, in_linear), cut, in_cutoff)`; sign via `select()`

### Phase B: Static Routing (webgpu_solver.h/cpp + gpu_nr_loop.wgsl)
- Route buffers: `diodeVoltageRouteBuffer` (2×u32/dev), `mosfetVoltageRouteBuffer` (4×), `bjtVoltageRouteBuffer` (3×)
- Allocated in `setupResources()`, populated in `buildStampMaps()`, sentinel 0xFFFFFFFF = ground
- WGSL: `@group(1) @binding(7/8/9)`; `var<workgroup>` caches (128/256/192 f32); `workgroupBarrier()` after preload

### Phase C: Graph Partitioning (new files)
- `acutesim_engine/infrastructure/graph_partitioner.h/cpp` — BFS from highest-degree device, flat encoding
- `compiled_block.h` — `std::vector<TensorPartition> partitions` field added
- `netlist_compiler.cpp` — calls `GraphPartitioner::partition()` after `tensorizeNetlist()`
- `acutesim_engine/CMakeLists.txt` — `graph_partitioner.cpp` added to `acutesim_engine_objects`
- `tests/test_graph_partitioner.cpp` — 6 tests (run directly, not ctest-registered)

## Phase 5: GUI Testing Framework (2026-02-26) — COMPLETE (31/31 tests)

### Key Files
- `tests/gui_driver/iguidriver.h` — pure C++ interface (no Qt)
- `tests/gui_driver/headless_driver.h/.cpp` — TensorNetlist+VerilogAGenerator+LocalWorkerDriver (no GridScene)
- `tests/gui_driver/scenario_runner.h/.cpp` — JSON DSL dispatcher (Qt::Core JSON only)
- `tests/scenarios/rc_lowpass_dc.json`, `voltage_divider_dc.json` — two verified scenarios
- CMake: `test_gui_headless_scenarios` links `acutesim_engine Qt6::Core Qt6::Test` only

### Design Decisions
- Connectivity via **explicit n+/n- net-name params** (not position/wire union-find)
- Net "gnd"/"GND"/"0" → node 0; Ground component "net" param → node 0
- `_scenarioId` injected by ScenarioRunner so probe IDs match simulation result keys
- Forwarder headers: `compute/orchestration/dto/simulation_dto.h` → `engine_api/simulation_dto.h`

## Phase 3: Engine Decoupling (2026-02-24) — ALL 5 PHASES COMPLETE (27/27 tests)

### Library Dependency Graph (updated)
```
acutesim_compute_core (Qt-free: physics + compute kernels)
  ↓ PUBLIC
acutesim_engine (Qt-free: compute_core + LocalWorkerDriver + BatchExecutionEngine + subprocess + HardwareCapabilities)
  ↓ PRIVATE
acutesim_orchestration (Qt6::Core PUBLIC: BatchRunner(QObject) + EDA tool drivers + gRPC)
  ↓
AcuteSim Qt GUI
```

### Key Phase 3 Files
- `acutesim_engine/engine.h` — aggregated Qt-free public API header
- `compute/hardware_capabilities.h/.cpp` — CPU SIMD tier + GPU detection (`detectCapabilities()`)
- `compute/orchestration/driver/local_worker_driver.h` — Qt-free simulation driver
- `compute/orchestration/driver/batch_execution_engine.h` — Qt-free batch runner
- `compute/orchestration/server/subprocess.h/.cpp` — POSIX subprocess (no QProcess)
- `compute/orchestration/drivers/openroad_task.h` — has `RCXConfig` + `generateRCXTcl()`
- `compute/orchestration/drivers/klayout_task.h` — has `GDSExportConfig` + `generateGDSExportScript()`
- `compute/orchestration/drivers/tool_driver.h` — has `runOpenROADRCX()`, `runKLayoutPEX()`, `runKLayoutGDSExport()`
- `compute/orchestration/proto/worker_service.proto` — has `RunLVS`, `RunPEX`, `RunGDSExport` RPCs
- `compute/generated/worker_service.grpc.pb.h` — pre-generated gRPC stubs (regenerate with protoc when proto changes)

### Qt→std:: API Migration Pattern (Phase 3.1)
- `QMap<K,V>` → `std::unordered_map<K,V>`, `.isEmpty()` → `.empty()`, `.contains(k)` → `.count(k)>0`
- `QVector<T>` → `std::vector<T>`, `.append()` → `.push_back()`
- `QString` → `std::string`, `QElapsedTimer` → `std::chrono::steady_clock`
- GUI boundary: convert at gui/ layer using `.toStdString()` / `QString::fromStdString()`
- `DesignDb::snapshotWithNodeMap()` returns `GraphSnapshotResult{graph, netToNode}` (not `buildGraphWithNodeMap`)
- Regenerate gRPC stubs: `protoc --proto_path=compute/orchestration/proto --cpp_out=compute/generated --grpc_out=compute/generated --plugin=protoc-gen-grpc=/opt/homebrew/bin/grpc_cpp_plugin`

### RTL-to-GDS Stage DAG
```
Yosys → OpenROAD (P&R+CTS+STA) → KLayout DRC → KLayout+netgen LVS → OpenROAD RCX (PEX) → KLayout GDSII Export
```

## Subsystems (8 major)
- `physics/` - Stateless device models, stamps, solver orchestration
- `components/` - Circuit definitions, topology hashing, diffing, export
- `compute/` - Tensors (SoA), solvers (NR/PCG/GPU), math (CSR/LU), orchestration (gRPC)
- `autodiff/` - Dual<T> forward-mode AD
- `ai_api/` - 6-layer cognitive stack (Senses/Perception/Memory/Imagination/Action/Conscience)
- `gui/` - Qt6 IDE (editors, analysis, panels, widgets, orchestration bridge)
- `tests/` - 60+ test files, regression golden DB, stress/benchmark harnesses
- `resources/` - Schematic cells, PDK (sky130), themes, icons
