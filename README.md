# OpenVLSI

**An open, AI-assisted chip-design environment covering schematic and RTL entry, synthesis,
place-and-route, simulation and sign-off in one application.** (Formerly CuteApp / AcuteSim.)

> **Status: in development.** The SPICE engine is verified against ngspice and Xyce. The full
> flow (Yosys synthesis, OpenROAD P&R, KLayout sign-off) is wired through orchestration drivers,
> but it is not yet a production tool.

The goal is a single app in which you can design an SoC, attach RAM, run firmware on it and see
the program's output. Transistor-level simulation is used where it matters, and RTL/gate-level
models handle the rest.

## Parts

| Part | Where | What it does |
|---|---|---|
| **TreeSPICE** engine | `acutesim_engine/` (git submodule, [Seshak-code/TreeSPICE](https://github.com/Seshak-code/TreeSPICE)) | SPICE-equivalent simulator (op/dc/tran/ac/noise) on a contraction-tree direct solver. TREE = Tensor Recursive Elimination Engine |
| GUI IDE | `gui/` | Qt6 schematic editor, simulation workspace, waveform and analysis views |
| AI copilot | `ai_api/` | Topology analysis, speculative "what-if" simulation, evidence-checked actions |
| Flow orchestration | `compute/orchestration/` | Yosys, OpenROAD and KLayout drivers, batch and Monte Carlo runs |
| Design model | `components/` | Circuit data model, Verilog/SPEF export, design-rule checks |

## Getting the code

The engine is a submodule, so clone recursively:

```bash
git clone --recurse-submodules https://github.com/Seshak-code/OpenVLSI.git
```

In an existing clone or a new git worktree:

```bash
git submodule update --init
```

## Building (macOS)

```bash
./scripts/setup_dependencies.sh                        # one time: cmake, ninja, vcpkg
cmake -B build -S . -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
cmake --build build -j$(( $(sysctl -n hw.ncpu) - 2 ))
./build/AcuteSim                                       # the app binary keeps its old name for now
```

Windows (MSVC) and WebAssembly builds are described in `CLAUDE.md`.

## Testing

```bash
cd build && ctest                                      # registered suites
./build/test_regression                                # CI gate; run it from the repo root
```

Engine-only checks (C++ suites and the ngspice/Xyce/IBM differential harnesses) live in the
TreeSPICE repo. See `acutesim_engine/README.md` and `acutesim_engine/ENGINE_HANDOFF.md`.

## Contributing

Make engine changes in TreeSPICE first, then bump the `acutesim_engine` submodule pointer
here. Development and QA hand-offs follow `CLAUDE.md`, `.clinerules/` and `memory-bank/`.
