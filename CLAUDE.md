# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**OpenVLSI** (formerly CuteApp / AcuteSim; repo Seshak-code/OpenVLSI) — "Cognitive Foundry": an
AI-agent ASIC design IDE covering schematic/RTL → synthesis → P&R → simulation → sign-off.
- **TreeSPICE** (`acutesim_engine/`, git submodule, formerly Karam): the SPICE-equivalent simulation
  engine (MNA, Newton, contraction-tree solver; TREE = Tensor Recursive Elimination Engine)
- **App**: AI cognitive stack (6-layer architecture) + Qt6 GUI IDE
- Code identifiers keep the old names for now (`acutesim::` namespaces, `ACUTESIM_` macros,
  `acutesim_engine` target, `AcuteSim` binary/CMake project, Qt settings names)

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

# Python gRPC parity validator
python3 compute/orchestration/parity_harness.py
```

CI (`.github/workflows/ci.yml`) runs `ctest -LE gpu` on macOS, Linux and Windows. Some test binaries are built but
not registered with CTest (`test_regression` among them): run those directly; CMakeLists.txt has the list.

## Architecture

### Dependency Order (strict — never violate)

```
gui/ → ai_api/ → compute/ → components/ → acutesim_engine/ (TreeSPICE: netlist, physics, solvers, math, autodiff)
```

Dependencies flow **downward only**. Lower layers must never import from higher layers. The engine layers below
(autodiff, physics, the data model, tensors, math, solvers, infrastructure) live in the `acutesim_engine/` submodule;
their rules still hold there.

### autodiff/ — Dual Number Forward-Mode AD (engine)

- `autodiff/dual.h` (header-only): `Dual<T>` carries `(val, grad)` through math ops.
- Use **unqualified** `exp()`, `sin()`, etc. — NOT `std::exp()`. ADL must find the `Dual<T>` overloads.
- Control flow may branch on `.val` only, never on `.grad`.

### physics/ — Stateless MNA Device Models (engine)

- `device_physics.h`: constitutive equations (BJT Ebers-Moll, MOSFET BSIM, diode IV). **Must remain completely stateless** — no static vars, no instance state.
- `circuitsim.h`: MNA stampers + solver orchestrator (`solveDC()`, `stepTransient()`).
- `pnjlim()` is the SPICE voltage limiter that prevents Newton-Raphson oscillations.
- `calculatePhysicalResiduals` verifies KCL conservation (target: < 1 pA).

### Circuit data model (engine: `acutesim_engine/netlist/`)

- `circuit.h`: `TensorBlock` (immutable definition: R/L/C/topology), `BlockState` (mutable sim state), `TensorNetlist` (root container).
- `graph_canonical.h`: BFS canonical topology hash (FNV-1a from Ground). Invariant to node numbering order.
- **A `TensorBlock` change** is an engine change: update `graph_canonical.h` and `tensors/physics_tensors.h` there,
  then bump the submodule here. `components/` holds the app's component library on top of it.

### Engine layers (`acutesim_engine/`) and the app's `compute/`

| Layer | Path | Role |
|---|---|---|
| Data | `acutesim_engine/tensors/` | SoA layout of the linear elements; device-model resolvers (`mos1Params`, `diodeModel`, `bjtModel`) |
| Operations | `acutesim_engine/math/` | CSR, tensor-contraction direct solver |
| Reliability | `acutesim_engine/infrastructure/` | Postproc engine, hardware capabilities |
| Strategy | `acutesim_engine/solvers/` | NR loop, DC/transient/AC/noise, analysis registry |
| Control | `compute/orchestration/` | gRPC to Yosys/OpenROAD/KLayout, batch runner, worker driver, WASM stubs |

Simulation flow: AoS→SoA (`tensorizeBlock`, linear elements) → NR loop: devices (MOSFET, diode, BJT) evaluated per instance and stamped by `stampByDual` (exact Jacobian from Dual), tensor-contraction solver → KCL physical verification. GPU mode currently solves on the CPU path (Phase 4: GPU device evaluation + batched fronts).

### ai_api/ — 6-Layer Cognitive Stack

| # | Layer | Path | Role |
|---|---|---|---|
| 1 | Senses | `system/` | Circuit state for the AI context (`schema_api`), feedback loop |
| 2 | Perception | `analysis/` | Topology analysis, failure classification |
| 3 | Memory | `context/` | RAG vector store, context engine, context budget |
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

The schematic editor's header is `gui/editors/schematic/schematic_editor.h` (no compat header at `gui/editors/`).

### AI Layer Tests

Use `tests/stubs/ai_api_test_stubs.cpp` to avoid pulling in GUI dependencies for AI-layer tests.

## Engine = TreeSPICE submodule

`acutesim_engine/` is the git submodule **Seshak-code/TreeSPICE** (the standalone engine repo since 2026-10-13; formerly Karam).
- Fresh clone or new worktree: `git submodule update --init` (worktrees do not share submodule checkouts).
- Engine change: commit + push **inside `acutesim_engine/`** (TreeSPICE branch → PR on TreeSPICE), then commit the
  new submodule pointer here. Engine-only checks live in TreeSPICE (`acutesim_engine/tests/`: C++ suites +
  ngspice/Xyce/IBM harnesses, `acutesim_engine/ENGINE_HANDOFF.md` §3); integration suites stay here.
- Public engine API: `acutesim_engine/engine_api/` + the netlist data model (`netlist/circuit.h`,
  `netlist/spice_parser.h`). App-side orchestration headers stay in this repo's `engine_api/`
  (drivers). Do not grow new app dependencies on other engine internals.

## Cross-Agent Collaboration (Claude Code ⇄ Cline)

This repo is worked by **both** Claude Code and Cline. They hand off to each
other through git, the handoff docs, and the memory bank:

- **Session start:** read `acutesim_engine/ENGINE_HANDOFF.md` §0, then run the sweep:
  `git status --short` (if dirty and you didn't make it dirty, another agent may
  be mid-flight — stop and check), `git worktree list --porcelain`, and
  `git log --no-merges origin/main..HEAD`. Background tasks
  are **not** reliably worktree-isolated (a 2026-10-12 background task edited this
  very checkout).
- **Session end:** update `acutesim_engine/ENGINE_HANDOFF.md` §0 and
  `memory-bank/activeContext.md` (what was done, commits, what's next) with an
  agent stamp — e.g. "Last session: Claude Code, 2026-10-12" — then commit
  (conventional prefix, explain why) and push. Uncommitted work is invisible to
  the other agent.
- **Merges:** record every branch/worktree merge in `acutesim_engine/ENGINE_HANDOFF.md` §0 with
  the commit hash and which agent did it.
- **Delegation:** QA goes to Cline/Gemini through `memory-bank/qa-queue.md`.
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
