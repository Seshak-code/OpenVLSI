# OpenVLSI rebuild: a macOS-first design suite (design, for review)

Status: proposal, 2026-10-24. No code yet. The owner's decisions so far are recorded at the end.

## Goal

Rebuild OpenVLSI as a desktop chip-design suite whose tools match Cadence's, block for block. Build order:

1. macOS desktop.
2. Other desktops.
3. Web.

Only the core application and the engine are priorities now. Technologies are chosen so they carry to the web later
(Qt for WebAssembly, gRPC, and Dawn/WebGPU for the engine's GPU compute), but nothing is built for the web yet.

The current app tried to be cross-platform, web and AI-driven at once. This rebuild drops that breadth. Its first
milestone is one complete analog loop: library, then schematic, then simulation setup, then results. That loop
should be as usable as Virtuoso + ADE Explorer + Spectre for the same circuit.

## Principles

- **Every action is a command.** Each menu item, tool and dialog calls a typed command: a name, JSON-schema
  parameters and a result.
  - Commands are undoable, logged and replayable.
  - The same registry is served to scripts and to agents through an MCP server. This replaces `ai_api/`.
  - Extensibility means "every GUI part is reachable through the API"; it is not a built-in AI layer. A RAG over a
    circuit dataset (Masala-CHAI or similar) can sit on top of the API later.
- **One design database.** Every tool reads and writes the same library/cell/view data (the OpenAccess role).
  Exchange formats are imports and exports, never a tool's private state.
- **TreeSPICE is the simulator for everything; the open tools stay.** Every simulation (analog, mixed-signal,
  post-layout, arrays) runs on TreeSPICE.
  - Yosys, OpenROAD, KLayout, Netgen, OpenRCX and OpenSTA are the implementation and sign-off backends, used as
    they are, not reworked.
  - OpenVLSI is the frontend that ties them together: one database, one command layer, Cadence-style tools over
    them.
- **Fail closed.** An unsupported option, a missing report or a parse problem is an explicit error, never a silent
  default (the review of 2026-10-24 found many of these in the current app).
- **Measured, not assumed.** Each milestone has a gate that is run, and each Cadence-parity claim names the test that
  shows it.

## Architecture

```
┌──────────────────────────── OpenVLSI.app (Qt 6, C++) ────────────────────────────┐
│  Tools: Library Manager · Schematic · Symbol · ADE Explorer · Waveforms · ...    │
│  (Qt Widgets chrome; canvases on QRhi → Metal)                                   │
├──────────────────────────────────────────────────────────────────────────────────┤
│  Command layer: registry · undo/redo · command log · scripting console           │
│        └── MCP server (stdio / local socket): the registry, for agents and scripts│
├──────────────────────────────────────────────────────────────────────────────────┤
│  Design database (oadb): libraries · cells · views · config · versions · PDKs    │
│        └── import/export: SPICE/CDL, Verilog, GDS/OASIS, LEF/DEF, Liberty        │
├───────────────────────────────┬──────────────────────────────────────────────────┤
│  Simulation: TreeSPICE        │  Flow tools (worker processes): Yosys, OpenROAD, │
│  (in a worker process; gRPC)  │  KLayout, Netgen, OpenRCX, OpenSTA               │
└───────────────────────────────┴──────────────────────────────────────────────────┘
```

- **Process model:**
  - Simulations and flow tools run in worker processes. A crash or a hung run never takes the GUI down.
  - Workers talk gRPC. The local worker server exists today, and a remote worker later is a different address,
    not a different design.
- **Rendering:**
  - Qt Widgets for docks, forms and tables.
  - Schematic and layout canvases on Qt's RHI, which is Metal on macOS. In Qt for WebAssembly, RHI runs on
    WebGL, which is the web phase's path.
  - Dawn stays where it is today: the engine's GPU compute. One GPU stack per job, no second one for drawing.
- **Engine boundary:** the app uses TreeSPICE's public API only (`engine_api/`, `netlist/circuit.h`,
  `netlist/spice_parser.h`). New needs grow that API inside TreeSPICE, never an include of its internals.

## Cadence block → OpenVLSI tool

| Cadence | OpenVLSI tool | Underneath | Milestone |
|---|---|---|---|
| OpenAccess | `oadb` design database | ours | M1 |
| Library Manager | Library Manager | oadb | M1 |
| Virtuoso Schematic Editor | Schematic editor: symbols, hierarchy, inherited nets, CDF-style parameters | oadb | M1 |
| Virtuoso Symbol editor | Symbol editor, symbol generation from a schematic | oadb | M1 |
| Hierarchy Editor (config view) | Config editor: view binding per cell | oadb | M1 |
| ADE Explorer | Test setup: analyses, outputs/expressions, sweeps, corners | TreeSPICE | M1 |
| ViVA | Waveform viewer and calculator | TreeSPICE results | M1 |
| Spectre core | TreeSPICE: DC/AC/tran/noise/sens/tf/pz, MC, `.options hier` arrays | TreeSPICE | M1 (exists) |
| ADE Assembler | Multi-testbench, Monte Carlo/yield, specs and pass/fail | TreeSPICE | M2 |
| Spectre RF | PSS, PAC, PNoise, HB, in TreeSPICE | ours | M2 |
| Spectre X / FX | Multithreaded solve; FastSPICE memory (S3 quotient arrays) | ours | M2 |
| Virtuoso Layout L/XL/GXL | Layout editor: connectivity-driven, Pcells, pins, guard rings; MPT later | oadb, KLayout | M3 |
| Pegasus DRC/LVS/ERC | Sign-off views | KLayout, Netgen | M3 |
| Quantus | Extraction to a post-layout netlist / SPEF | OpenRCX, KLayout | M3 |
| ADE Verifier | Requirements traced to Assembler results | ours | M4 |
| Genus | Synthesis views (physical-aware later, MCMM) | Yosys, OpenROAD | M5 |
| Innovus | Floorplan, power grid, place, CTS, route views | OpenROAD (ORFS) | M5 |
| Tempus | STA views | OpenSTA | M5 |
| Voltus | IR drop / EM views | OpenROAD PDN/PSM | M6 |

## Milestone 1: the analog loop

**Scope:**
- **Library Manager:** libraries (user and PDK), cells, views; create, copy, rename, delete; locks.
- **oadb views:** schematic, symbol, config, and an ADE state view (`maestro`-like: tests, analyses, variables,
  corners, outputs, results history). On-disk format: see the decisions below.
- **Schematic editor:**
  - Place instances from PDK or user symbols; wire with auto-junction; net labels and buses; pins.
  - Hierarchy: descend/ascend, edit in place. Inherited nets (net expressions); CDF-style parameter forms with
    callbacks evaluated in a sandbox.
  - Check and save (connectivity, ERC); netlist to SPICE through the config view.
- **Symbol editor:** shapes, pins and labels; generate from the schematic's pins.
- **ADE Explorer:**
  - Tests with analyses (op, dc, ac, tran, noise, sens, tf, pz).
  - Design variables, parametric sweeps, corners (PDK sections plus temperature and supply), and Monte Carlo
    (process and mismatch from the PDK).
  - Outputs as expressions, specs.
  - Run on the worker; results history.
- **Waveforms:** multi-trace plots, cursors, a calculator (dB, phase, bandwidth, slew, settling, …) and
  cross-probing back to the schematic.
- **PDK:** IHP SG13G2 first (open, already used by the RTL flow), then sky130.
- **Commands and MCP:** every M1 operation above, exposed and covered by command-level tests.
- **macOS app:** a signed, notarized `.app` bundle that ships its own Qt plugins and engine; no Homebrew paths.

**Gate (run, not claimed):** `tools/m1_gate.py --bundle` (2026-10-25: 28 of 28 checks pass). Step 5 stands on the
automated check: the bundle links nothing outside itself and the system, and the app and its simulator run in an empty
environment. The owner has no clean account or Apple Developer ID, so the bundle is signed ad hoc: it runs here, and on
another Mac it needs right-click → Open until a Developer ID signature and notarization are possible.
1. From an empty workspace, build a two-stage Miller op-amp on SG13G2: schematic, symbol, and a testbench using
   the symbol.
2. In ADE, run op, ac, tran (step response) and noise, over 5 corners and a temperature sweep, plus a 200-sample
   Monte Carlo.
3. Every result matches ngspice on the same exported netlist within run_diff's tolerances. The Monte Carlo is compared
   by distribution (a two-sample Kolmogorov–Smirnov test per output): the simulators' random generators differ, so
   their samples do.
4. All of steps 1–3 is driven once more through the MCP server alone, by a script, with the same results.
5. The bundle runs on a clean macOS user account (`cmake --build build --target bundle`; the gate's `--bundle` checks it
   is self-contained and starts in an empty environment, and the owner runs it once on a clean account).

## Milestone 2: assembler and RF

Owner's order (2026-10-25): ADE Assembler first, then the RF analyses in TreeSPICE, then Spectre X.

**Slices:**
1. **ADE Assembler** (the app, on M1's ADE):
   - a summary across a history: per output its range, its spec's pass count, and the worst point with its
     parameters and margin;
   - yield: per spec and overall, a sample passing only when every spec does;
   - comparing two histories point by point;
   - variables per test;
   - a run plan of several runs.
2. **stb** (TreeSPICE): loop gain and stability margins at a probe, without breaking the loop (Middlebrook/Tian, as
   Spectre's stb). Checked against breaking the loop by hand (the M1 gate's 1 GΩ/1 F trick) and analytic loops.
3. **PSS** (TreeSPICE): periodic steady state by shooting Newton, driven and autonomous (oscillators). Checked
   against a long transient's last period, and ngspice's experimental .pss where it applies.
4. **PAC and PNoise** (TreeSPICE): small signal and noise about the periodic steady state (conversion gain, phase
   noise). Checked against analytic LPTV circuits and a transient with a small tone.
5. **HB** (TreeSPICE): harmonic balance, for strongly nonlinear circuits with many tones. Checked against PSS on the
   same circuits, and the Xyce gold outputs (datasets/xyce) that have HB.
6. **Spectre X**: the multithreaded solve and the S3 quotient arrays, reachable from ADE.

Each RF analysis gets an ADE analysis type, calculator access (harmonics, loop gain, conversion gain, phase noise) and
a waveform view.

**Gate (to be run):** through MCP alone, on SG13G2:
1. An LC VCO and a mixer:
   - PSS gives the frequency and harmonics, matching a long transient;
   - PAC gives the conversion gain and PNoise the phase noise, matching their brute-force references;
   - HB agrees with PSS.
2. The M1 op-amp: stb's phase margin matches the loop broken by hand.
3. Assembler: the yield over corners and a Monte Carlo, with the worst point named for every spec.
4. Spectre X: a large array runs, and its speedup is measured and reported.

## Salvage and retirement

**Carried over:**
- The TreeSPICE submodule.
- From `compute/orchestration/`: the gRPC worker server and client, the subprocess runner, the ORFS/OpenROAD/
  KLayout/Yosys report parsers, `rtl_flow` and DEF parsing (for M5).
- `compute/soc/` (the RV32I SoC and firmware co-simulation) for the digital milestones.
- PDK discovery (`gui/orchestration/pdk_manager`), reworked onto oadb.
- Small Qt widgets that fit: the command palette, the breadcrumb.

**Retired:** the current GUI shell, the schematic editor and simulation workspace (rebuilt on oadb and commands),
`ai_api/` (replaced by the command layer and MCP), the Verilog-A netlist transport (`veriloga_parser`: the new
netlister writes SPICE for TreeSPICE), and the WASM/web-specific code (stubs, storage manager, wasm worker
client) until the web phase.

## Decisions (owner)

2026-10-24:
- A new app that salvages working parts, built on a branch of OpenVLSI that starts by deleting the old app, so
  history and CI carry over.
- Qt 6 + C++.
- Our own OA-style database, stored as one SQLite file per library, with a text export for diffs and review.
- Milestone 1 is the analog loop.
- **Scripting:** embedded Python over the command API. Each app (tool) can bring its own venv, so a tool's Python
  dependencies never collide with another's or with the system's.
- **TreeSPICE is the simulation tool for everything:** analog, RF, mixed-signal, post-layout and large-array
  simulation. Simulation work in TreeSPICE is not limited to what M1 needs.
- **The other tools are not reworked.** OpenROAD, Yosys, KLayout, Netgen, OpenRCX and OpenSTA stay the backends for
  synthesis, place and route, extraction, timing and physical verification. OpenVLSI is a convenient frontend that
  ties everything together.
