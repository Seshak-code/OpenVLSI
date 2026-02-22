# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**AcuteSim / CuteApp** — "Cognitive Foundry": an AI-agent ASIC design IDE.
- **AcuteSim**: physics simulation kernel (MNA, NR solver, SIMD tensors, GPU via Dawn/WebGPU)
- **CuteApp**: AI cognitive stack (6-layer architecture) + Qt6 GUI IDE
- C++17, CMake ≥ 3.16, Qt 6.9.0, Google Dawn (WebGPU), gRPC orchestration
- Platforms: macOS, Windows (MSVC), Linux, WebAssembly (Emscripten)

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
python3 scripts/serve.py   # serves at http://localhost:8000
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
| Operations | `math/` | PCG, LU, CSR, `EmulatedF64` GPU precision, FPU control |
| Reliability | `infrastructure/` | Netlist compiler, postproc engine, subprocess sandbox |
| Strategy | `solvers/` | NR loop, DC/transient/AC/noise, Dawn GPU backend |
| Control | `orchestration/` | gRPC to Yosys/OpenROAD/KLayout, batch runner, WASM stubs |

Simulation flow: AoS→SoA (`tensorizeBlock`) → NR loop with LU/PCG → SIMD kernel dispatch → KCL physical verification. GPU path (Dawn) uses EmulatedF64 hi-lo pairs for 15–17 digit precision on WGSL.

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

- `core/`: `MainWindow`, `DockingManager`, `EditorManager`, `EditorFactory`, `StorageManager`
- `editors/`: `SchematicEditor` (canvas), `CodeEditor`, `JsonViewer`, `ImageViewer`, `HexViewer`
- `analysis/`: `SimulationWorkspace`, `SimulationExplorer`, `VariablesPanel`, `CalculatorWidget`, `ERCEngine`, waveform widgets
- `orchestration/`: `WorkerManager` (gRPC bridge), `PdkManager`, `RTLPipelinePanel`
- `widgets/`: `CommandPalette`, `ProjectExplorer`, `WaveformWidget`, `ToastManager`, `GhostDiffViewer`
- `styling/`: `ThemeManager` (JSON themes: `dark_modern.json`, `light_modern.json`, `high_contrast.json`)
- `scripting/`: `TclBridge` for automation hooks

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

- `simulation_workspace.cpp` requires: `variables_panel.cpp`, `calculator_widget.cpp`, `parametric_sweep_dialog.cpp`, `postproc_engine.cpp`, `simulation_context.cpp`, `batch_runner.cpp`, `netlist_compiler_ctx.cpp`
- `editor_manager.cpp` requires: `editor_factory.cpp`, `storage_manager.cpp`, plus viewer `.cpp` files
- `project_explorer.cpp` requires: `library_model.cpp`, `library_index.cpp`

When updating test targets in CMakeLists.txt: use `replace_all: true` with 8-space-indented patterns (4-space = `ACUTESIM_SOURCES` block).

The compat header `gui/editors/schematic_editor.h` exists because the real header is in `gui/editors/schematic/`.

### AI Layer Tests

Use `tests/stubs/ai_api_test_stubs.cpp` to avoid pulling in GUI dependencies for AI-layer tests.
