# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**AcuteSim / CuteApp** — "Cognitive Foundry": an AI-agent ASIC design IDE.
- **AcuteSim**: physics simulation kernel (MNA, NR solver, SIMD tensors, GPU via Dawn/WebGPU)
- **CuteApp**: AI cognitive stack (6-layer architecture) + Qt6 GUI IDE

## Build Commands

### macOS (primary)

```bash
# One-time setup
./scripts/setup_dependencies.sh   # installs cmake, ninja, vcpkg (protobuf + gRPC)

# Configure
cmake -B build -S . -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
# With gRPC/vcpkg:
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DFETCHCONTENT_UPDATES_DISCONNECTED=ON

# Build
./scripts/build_safe.sh            # recommended (leaves 2 cores free)
cmake --build build -j$(( $(sysctl -n hw.ncpu) - 2 ))
```

### Windows

```bash
cmake -S . -B out/build/windows-msvc -G "Visual Studio 17 2022" -A x64 \
  -DQt6_DIR="C:/Qt/6.x/msvc2022_64/lib/cmake/Qt6Core"
cmake --build out/build/windows-msvc --config Release --target AcuteSim
```

### WebAssembly

```bash
emcmake cmake -S . -B out/build/wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/wasm --target AcuteSim
python3 scripts/serve.py   # serves at http://localhost:8080
```

## Testing

```bash
# Run all CTest-registered tests
cd build && ctest --verbose

# Run by speed label
ctest -L quick    # fast unit tests (<5s)
ctest -L heavy    # GUI/integration tests

# Run a specific test binary directly
./build/test_physics_kernels
./build/test_hierarchy_headless
./build/test_regression

# CLI modes (no GUI)
./build/AcuteSim --stress 100
./build/AcuteSim --monte-carlo
./build/AcuteSim --benchmark

# Python gRPC parity validator
python3 tests/parity_harness.py
```

Only `test_regression` runs in CI. All other tests are developer-run locally.

Tests NOT registered with CTest (run binary directly): `test_regression`, `test_simulation`, `test_sensitivity`, `test_superposition`, `test_mode_ladder`, `test_dependent_simulation`, `test_soa_bridge`, `test_noise_analysis`, `test_convergence_engine`, `test_ad_integration`, `test_erc_rules`, `test_std_cells`, and many others — see CMakeLists.txt for the full list.

## Architecture

### Dependency Order (strict — never violate)

```
gui/ → ai_api/ → compute/ → physics/ → autodiff/
                     ↓            ↓
               components/ ←────┘
```

Dependencies flow **downward only**. Lower layers must never import from higher layers.

### autodiff/ — Dual Number Forward-Mode AD

- `autodiff/dual.h` (header-only): `Dual<T>` carries `(val, grad)` through math ops.
- Use **unqualified** `exp()`, `sin()`, etc. — NOT `std::exp()`. ADL must find the `Dual<T>` overloads.
- Control flow may branch on `.val` only, never on `.grad`.

### physics/ — Stateless MNA Device Models

- `device_physics.h`: constitutive equations (BJT Ebers-Moll, MOSFET BSIM, diode IV). **Must remain completely stateless** — no static vars, no instance state.
- `circuitsim.h`: MNA stampers + solver orchestrator (`solveDC()`, `stepTransient()`).
- `pnjlim()` is the SPICE voltage limiter that prevents Newton-Raphson oscillations.
- `calculatePhysicalResiduals` verifies KCL conservation (target: < 1 pA).

### components/ — Circuit Data Model

- `circuit.h`: `TensorBlock` (immutable definition: R/L/C/topology), `BlockState` (mutable sim state), `TensorNetlist` (root container).
- `graph_canonical.h`: BFS canonical topology hash (FNV-1a from Ground). Invariant to node numbering order.
- `netlist_diff.h`: Semantic diffing — ADDED/REMOVED → `SimImpact::HIGH`, MODIFIED_PARAMETER → `MEDIUM`.
- **Any `TensorBlock` change must update all three**: `graph_canonical.h`, `netlist_diff.h`, `physics_tensors.h`.

### compute/ — 5-Layer Execution Pipeline

| Layer | Path | Role |
|---|---|---|
| Data | `tensors/` | SoA layout, SIMD kernels (AVX-512/AVX2/Neon/generic) |
| Operations | `math/` | CSR, tensor-contraction direct solver, `EmulatedF64` GPU precision, FPU control |
| Reliability | `infrastructure/` | Postproc engine, hardware capabilities, subprocess sandbox |
| Strategy | `solvers/` | NR loop, DC/transient/AC/noise, Dawn GPU backend |
| Control | `orchestration/` | gRPC to Yosys/OpenROAD/KLayout, batch runner, WASM stubs |

Simulation flow: AoS→SoA (`tensorizeBlock`) → NR loop with the tensor-contraction solver → SIMD kernel dispatch → KCL physical verification. GPU mode currently solves on the CPU path (Phase 4: GPU device evaluation + batched fronts).

SIMD compile flags are set **per-file**, not globally:
```cmake
set_source_files_properties(compute/tensors/physics_kernels_avx2.cpp
    PROPERTIES COMPILE_FLAGS "-mavx2 -mfma")
```

### ai_api/ — 6-Layer Cognitive Stack

| # | Layer | Path | Role |
|---|---|---|---|
| 1 | Senses | `system/` | Geometric manipulation, crash feedback |
| 2 | Perception | `analysis/` | Topology analysis, failure classification |
| 3 | Memory | `context/` | RAG vector store, prompt builder, context budget |
| 4 | Imagination | `simulation/` | Speculative "what-if" simulation forks |
| 5 | Action | `core/` | Dispatches JSON commands to workbench |
| 6 | Conscience | `policy/` | Evidence-first protocols, citation validation |

- Central truth: `CircuitState` in `introspection_api.h` (topology hash, node voltages, residual, convergence).
- `adk/`: Google Agent Development Kit — tool registry, session manager, tool handlers.
- **AI actions MUST go through `AiActionEnvelope`** — never touch `QGraphicsScene` directly.

### gui/ — Qt6 IDE

AI action flow: JSON from worker → `AiActionEnvelope` → Ghost preview → user approval → `AiActionHistory` push → `QGraphicsScene` commit.

## Critical Rules

### Floating-Point Determinism (mandatory)

```cmake
# GCC/Clang: -fno-fast-math -frounding-math -ffloat-store
# MSVC: /fp:strict
```
`fpu_control.h` enforces `FE_TONEAREST` at runtime. Never flush subnormals to zero (no DAZ/FTZ). Target: 0-bit deviation across x86, ARM, WASM.

### WASM / gRPC Guard Pattern

All gRPC/orchestration code must be wrapped:
```cpp
#ifdef ACUTESIM_ENABLE_ORCHESTRATION
    // gRPC code
#endif
```
WASM builds use `wasm_worker_client.cpp` and `wasm_stubs.cpp`. Many test targets are guarded with `if(NOT EMSCRIPTEN)` in CMakeLists.

WASM requires a `statx` shim in `main.cpp` (Qt 6.9.3 calls `statx` but Emscripten doesn't provide it):
```cpp
#ifdef __EMSCRIPTEN__
extern "C" int statx(int, const char*, int, unsigned int, struct statx*) { return -1; }
#endif
```

### Qt MOC Gotchas (Qt 6.9)

- MOC tracks `namespace` from `#include`d headers. If a header opens `namespace AIAPI {}`, MOC thinks any `Q_OBJECT` in the including `.cpp` is inside that namespace.
- Fix: Wrap AIAPI includes with `#ifndef Q_MOC_RUN ... #endif` in test `.cpp` files.
- `QHash<T,U>` requires `#include <QHash>` — `qcontainerfwd.h` only forward-declares it.
- No `QJSEngine` (Qt::Qml unavailable) — use the hand-written recursive descent parser for expression evaluation.
- Dual Homebrew Qt installs (qt 6.9.0 + qtbase 6.9.3) cause runtime crashes for GUI-linked tests. Guard with:
  ```cpp
  qputenv("QT_PLUGIN_PATH", "/opt/homebrew/opt/qt/share/qt/plugins");
  ```
- For Qt::Gui headers in non-GUI-linked targets: `$<TARGET_PROPERTY:Qt6::Gui,INTERFACE_INCLUDE_DIRECTORIES>`

### CMakeLists Source Group Dependencies

These `.cpp` files have transitive link dependencies and **must travel together** in any test target that includes them:

- `simulation_workspace.cpp` requires: `variables_panel.cpp`, `calculator_widget.cpp`, `parametric_sweep_dialog.cpp`, `postproc_engine.cpp`, `simulation_context.cpp`
- `editor_manager.cpp` requires: `editor_factory.cpp`, `storage_manager.cpp`, plus viewer `.cpp` files
- `project_explorer.cpp` requires: `library_model.cpp`, `library_index.cpp`

When updating test targets in CMakeLists.txt: use `replace_all: true` with 8-space-indented patterns (4-space = `ACUTESIM_SOURCES` block).

The compat header `gui/editors/schematic_editor.h` exists because the real header is in `gui/editors/schematic/`.

### AI Layer Tests

Use `tests/stubs/ai_api_test_stubs.cpp` to avoid pulling in GUI dependencies for AI-layer tests.

## Cross-Agent Collaboration (Claude Code ⇄ Cline)

This repo is worked by **both** Claude Code and Cline. They hand off to each
other through git, the handoff docs, and the memory bank:

- **Session start:** read `ENGINE_HANDOFF.md` §0, then run the sweep:
  `git status --short` (if dirty and you didn't make it dirty, another agent may
  be mid-flight — stop and check), `git worktree list --porcelain`, and
  `git log --no-merges origin/engine/spice-correctness..HEAD`. Background tasks
  are **not** reliably worktree-isolated (a 2026-10-12 background task edited this
  very checkout).
- **Session end:** update `ENGINE_HANDOFF.md` §0 and
  `memory-bank/activeContext.md` (what was done, commits, what's next) with an
  agent stamp — e.g. "Last session: Claude Code, 2026-10-12" — then commit
  (conventional prefix, explain why) and push. Uncommitted work is invisible to
  the other agent.
- **Merges:** record every branch/worktree merge in `ENGINE_HANDOFF.md` §0 with
  the commit hash and which agent did it.
- **Delegation:** focused tasks can be delegated to Cline headlessly via the
  `/cline` command (`.claude/commands/cline.md`).
- `.clinerules/` (00–05) carries the collaboration protocol, gotchas, workflow
  and memory-bank instructions for Cline; the same rules apply to Claude Code by
  symmetry.

@memory-bank/activeContext.md

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
