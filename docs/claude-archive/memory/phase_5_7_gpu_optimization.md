---
name: Phase 5.7 GPU Optimization Pillars
description: Phase 5.7.2 (Apple UMA zero-copy + thermal monitor) and 5.7.4 (Render Graph Scheduler) implementation details
type: project
---

## Phase 5.7.2: Apple Silicon UMA Zero-Copy + Thermal Monitor (2026-03-18) — COMPLETE

### New Files
- `acutesim_engine/solvers/apple_uma.h` — `ACUTESIM_APPLE_UMA` macro, `UMAStagingBuffer`, `AppleUMAManager` (mappedAtCreation staging)
- `acutesim_engine/solvers/thermal_monitor.h` — `ThermalMonitor` polls machdep.xcpm.cpu_thermal_level, scales batch sizes (Nominal/Fair/Serious/Critical)

### Architecture
- `AppleUMAManager::createStaging()` uses `mappedAtCreation=true` with `MapWrite|CopySrc` — avoids Dawn's internal staging on wgpuQueueWriteBuffer
- Single `copyBufferToBuffer` flush (near-free on UMA since CPU and GPU share physical memory)
- `ThermalMonitor::adjustBatch()` scales GPU dispatch sizes: 1.0/0.75/0.50/0.25 based on thermal pressure
- `ACUTESIM_APPLE_UMA` is 1 on AArch64 macOS non-Emscripten, 0 otherwise
- `ACUTESIM_HAS_THERMAL_MONITOR` same guard, uses `<sys/sysctl.h>`

### Integration
- `webgpu_solver.h` includes `apple_uma.h` and `thermal_monitor.h`
- `uploadContractionProgram()` uses UMA staging path with `#if ACUTESIM_APPLE_UMA`
- `executeContractionSweep()` polls thermal at start, adjusts batch sizes

## Phase 5.7.4: Render Graph Scheduler (2026-03-18) — COMPLETE

### New Files
- `acutesim_engine/infrastructure/contraction_render_graph.h` — SlotLifetime, WavefrontGroup, RenderGraphSchedule structs, ContractionRenderGraph analyzer
- `acutesim_engine/infrastructure/contraction_render_graph.cpp` — 4-step analysis: lifetime, greedy first-fit aliasing, wavefront grouping by depth, barrier computation
- `tests/test_render_graph.cpp` — 12 tests, 78 assertions (all passing)

### Architecture
- `nodeIdToPhysical` maps ContractionNode::id → physical slot (greedy first-fit aliasing)
- Leaves get `firstWrite=0` (bulk uploaded), internals get `firstWrite=walkIndex`
- Root gets `lastRead=N` to survive entire walk (CPU readback after)
- Wavefronts group internal nodes by tree depth for parallel GPU dispatch
- Barriers track physical slots consumed (children) and produced (output) per pass
- Expected VRAM reduction: O(treeNodes) → O(treewidth) physical slots

### Integration
- `TNCompiledProgram.renderGraph` field in `tn_compiler.h`
- `TNCompiler::compile()` Step 6 calls `ContractionRenderGraph::analyze()`
- `WebGPUSolver::renderGraph_` member stores schedule
- `uploadContractionProgram()` uses `renderGraph_.numPhysicalSlots` for buffer sizing
- `executeContractionSweep()` uses `physSlot()` lambda for aliased slot resolution:
  ```cpp
  auto physSlot = [&](uint32_t nodeId) -> uint32_t {
      if (renderGraph_.valid && nodeId < renderGraph_.nodeIdToPhysical.size())
          return renderGraph_.nodeIdToPhysical[nodeId];
      return nodeId;  // fallback: identity mapping
  };
  ```

### Key Constants
- `TN_MAX_TILE_RANK = 8`, `TN_MAX_TILE_ELEMS = 64` — slot sizing
- slotBytes = (maxRank^2 + maxRank) * 2 * sizeof(float) for EmulatedF64 hi/lo

**Why:** Render graph aliasing reduces GPU VRAM from O(N) to O(treewidth) for contraction workspace. UMA zero-copy eliminates redundant staging copies on Apple Silicon.
**How to apply:** All slot operations in webgpu_solver.cpp must go through physSlot() for aliased resolution. If renderGraph_.valid is false, identity mapping preserves backward compatibility.
