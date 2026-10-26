# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**OpenVLSI** (repo Seshak-code/OpenVLSI): a macOS-first chip-design suite whose tools match Cadence's, rebuilt from
2026-10-24 on (`docs/rebuild.md`: read it first). TreeSPICE (`acutesim_engine/`, submodule) is the simulator for
everything; Yosys/OpenROAD/KLayout/Netgen/OpenRCX/OpenSTA are backends used as they are; OpenVLSI is the frontend
that ties them together. Platform order: macOS, other desktops, then web. Engine code keeps its old identifiers
(`acutesim::` namespaces, the `acutesim_engine` target).

## Build and test (macOS)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # Qt 6 from Homebrew (found by its standard prefix), SQLite from the SDK
cmake --build build -j$(( $(sysctl -n hw.ncpu) - 2 ))
ctest --test-dir build                          # oadb, commands, mcp, schematic, simulation, ade, library_manager + view_editor + ade_explorer (offscreen), flow/SoC, isolation
open build/OpenVLSI.app                         # the CIW and Library Manager; `OpenVLSI <workspace.libs>`, default ~/OpenVLSI/openvlsi.libs
```

The flow and SoC gates (`test_rtl_flow`, `test_firmware_on_silicon`, `test_soc_hello`) run the real tools from
`~/eda` (`scripts/setup_eda_tools.sh`) and `datasets/`; without them they skip.

The M2 gate grows with M2's slices: `python3 tools/m2_gate.py --mcp build/openvlsi-mcp --pdk ... --osdi ...`.
The M1 gate (docs/rebuild.md), through openvlsi-mcp alone, against ngspice:

```bash
python3 tools/m1_gate.py --mcp build/openvlsi-mcp --pdk datasets/pdk/ihp/ihp-sg13g2 --osdi <psp103.osdi> [--bundle build/deploy/OpenVLSI.app]
cmake --build build --target bundle   # build/deploy/OpenVLSI.app: Qt inside (tools/deploy_macos.sh)
```

## Architecture (docs/rebuild.md)

```
app/ (Qt 6: CIW, Library Manager, schematic/symbol editors on QRhi, ADE Explorer, waveforms) → cmd/ (command registry) → oadb/ (design database)
                              ├── mcp/ (MCP server)          sim/ (netlister; treespice runs) → acutesim_engine/ (TreeSPICE)
                              └── flow/ (backends), soc/
```

- **Every action is a command** (`cmd/registry.h`): a name (`[a-z][a-z0-9_]*`), a description, a JSON-schema
  parameter object, a handler. Calls are validated fail-closed (an unknown key, a wrong type, a missing parameter is
  an error result), logged, and undoable through the database. The GUI, the scripting console (embedded Python, a
  venv per tool) and `openvlsi-mcp` all call `Registry::call`. A new GUI feature without a command is incomplete.
- **Simulation** (`sim/`): the netlister writes a schematic's hierarchy (a config view's view list and bindings
  switch cells between `spice` primitives and schematics) as SPICE; `simulate` runs the `treespice` beside the
  executable (copied there at build) in a worker process and reads its raw file. Cards are one analysis or control
  line each; anything else is refused. ADE (`sim/ade.h`): a maestro view's setup (tests, variables and sweeps,
  corners, outputs with specs) expands into points run in parallel, each output a calculator expression
  (`sim/calc.h`); runs are histories on disk (`simulation/<lib>/<cell>/<view>/Interactive.N`), in the background
  through `ade_run` with `wait: false`; `mode: montecarlo` runs the setup's montecarlo section once per sample (its
  own `.options seed`) and adds each output's statistics and yield. RF analyses: `stb` (the loop gain at an `iprobe`)
  and `pss` (shooting Newton; `oscnode` "0" driven, else an oscillator's node), `pac` and `pnoise` about it (the
  conversion matrix; `relharm` 1 for phase noise), read by `STB(loopgain)`, `VPSS`/`VH`, `VPAC(net, m)`, `VPN(out)`,
  `harmonic`, `thd` and `fundamental`. Every output records its equation, the parameters
  its point ran with and the calculator's steps (its derivation). ADE Assembler: `ade_summary` (each spec's
  worst point and margin, yields), `ade_compare` (two histories point by point), `ade_run_plan` (several runs in order);
  tests may carry their own variables. `pdk_install` builds analogLib and the SG13G2
  device library (spice templates, defaults, the model files and corner sections they need). The waveform viewer (`app/waveform_viewer`) plots calculator expressions
  over one run; `wave_eval` is the calculator as a command; it cross-probes with the schematic both ways
  (`waveform_probe`, `editor_select_net`).
- **oadb** (`oadb/oadb.h`): one SQLite file per library; cells, views, per-view JSON properties. `Library::change`
  makes one undo group (trigger-logged inverse SQL; session-scoped history). A workspace file lists libraries
  (`DEFINE <library> <path>`).
- **Engine boundary:** the app uses TreeSPICE's public API only (`engine_api/`, `netlist/circuit.h`,
  `netlist/spice_parser.h`); `tools/check_engine_isolation.py` (a ctest) enforces it for the app's directories.

## Critical Rules

### Floating-Point Determinism (mandatory)

`-fno-fast-math -frounding-math -ffp-contract=off` (MSVC `/fp:strict`), set in CMakeLists.txt. Never flush
subnormals to zero.

### Fail closed

An unsupported option, a missing report or a malformed value is an explicit error, never a silent default or a
fake success (the 2026-10-24 review removed many of these).

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
