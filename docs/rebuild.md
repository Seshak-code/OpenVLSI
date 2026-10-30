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
| PDKs | IHP SG13G2 (M1); FinFET and GAA: ASAP7, ASAP5 (devices M2, layout and LVS M3, digital flow M5) | the PDKs' BSIM-CMG cards; ORFS's asap7 platform | M1–M5 |
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
   Done (2026-10-25): `tools/m2_gate.py` step 2. On the op-amp over 15 points, stb equals Middlebrook's two injections
   done by hand to 8.6e-15. Its margins match the loop broken at the same node to 0.165°, and to 0.014° once the
   output's gate load is buffered away. A loop broken at the other input (M1's `tb_ol`) is another path, through the
   mirror's pole, about 0.5° away.
3. **PSS** (TreeSPICE): periodic steady state by shooting Newton, driven and autonomous (oscillators). Checked
   against a long transient's last period (TreeSPICE's and ngspice's).
   Done (2026-10-25): `tools/m2_gate.py` steps 3 and 4.
   - The op-amp follower driven by a 0.3 V, 1 MHz sine over 15 points (THD 1.0 to 6.8 %): harmonics 0 to 5 match both
     transients' last period to 4e-6 of the first.
   - A cross-coupled LC VCO on SG13G2, tuned 3.42 to 4.69 GHz over three corners: frequency to 5e-5 of both
     transients, swing to 4e-5 of TreeSPICE's. ngspice's swing is first order in its step (0.95 mV high at 1 ps,
     0.27 mV at 0.25 ps).
   - ngspice's own `.pss` is no reference. It treats every circuit as an oscillator, so a driven RC came out at
     600 kHz instead of 1 MHz. On an LC oscillator it reported convergence at 4.03 MHz; the oscillator runs at 5.03 MHz.
4. **PAC and PNoise** (TreeSPICE): small signal and noise about the periodic steady state (conversion gain, phase
   noise). Checked against analytic LPTV circuits and a transient with a small tone.
   Done (2026-10-25): the conversion matrix. G(t) and C(t) at the PSS's ticks become harmonics; the sideband system
   is Y_ml = G_(m-l) + j(w + m w0) C_(m-l). It is also HB's Jacobian. `tools/m2_gate.py` steps 5 and 6.
   - **Analytic and LTI checks:**
     - an ideal multiplier converts by exactly ±j/2;
     - with nothing periodic, PAC equals .ac and PNoise equals .noise;
     - a switched RC's noise integrates to kT/C (0.9985 extrapolated);
     - an LC oscillator's phase noise matches linear theory to 0.9 %.
   - **On SG13G2:**
     - an NMOS passive mixer's sidebands −1, 0 and +1 match TreeSPICE's and ngspice's transients with a 1 mV tone
       to 2e-5;
     - the op-amp's PNoise with nothing periodic equals .noise to 8.5e-6;
     - the VCO reads −120.3 dBc/Hz at 1 MHz, 20.06 dB/decade, converged in its sidebands to 0.11 dB.
   - **Limits:**
     - hard switching converges in sidebands as 1/K;
     - near an oscillator's carrier the cut-off sidebands move the carrier: use 40 or more;
     - a mode far slower than the period (a 1 GΩ / 1 F feedback) is refused.
   - **Open:** a brute-force phase-noise reference needs transient noise sources (TRNOISE) in TreeSPICE.
5. **FinFET devices** (TreeSPICE and the app), for the predictive FinFET and GAA PDKs OpenROAD supports. Both use
   BSIM-CMG (their HSPICE cards are `level = 72`, version 107). ASAP7 has fins (`geomod = 1`), ASAP5 nanowires
   (`geomod = 3`).
   - TreeSPICE routes a `level = 72` card to its BSIM-CMG (OSDI, CMC 111.0), as it routes BSIM4 to its OSDI model.
     Each v107 parameter the 111 model does not know is an error, not a drop. The instance takes `nfin`, `nrs` and
     `nrd`; a CDL `w` (ASAP7: `w=81n nfin=3`) only when it equals nfin times the fin pitch.
   - Checked against ngspice running the same cards through OSDI: inverters, a ring oscillator and the 6T SRAM cell
     of each PDK, over its corners; the cases join the parity matrix.
   - `pdk_install asap7` and `pdk_install asap5`: `nmos`/`pmos` × `slvt`/`lvt`/`rvt`/`sram` cells. `nfin` is a whole
     number of at least 1, `l` the PDK's drawn length (ASAP7's CDL: 20 nm; ASAP5's to be read from its cells). The
     TT/SS/FF model files (ASAP5 also FS, SF) are ADE corners.
   - ASAP5 is analog only: it ships no LEF or Liberty and four standard cells, its Calibre decks are unreleased, and
     ORFS has no asap5 platform.
   - PDKs: `datasets/pdk/asap7/asap7_pdk_r1p7` and `datasets/pdk/asap5/asap5` (blobless clones of
     The-OpenROAD-Project's repositories, BSD-3).
   Done (2026-10-26), with one change from the plan, by the owner's choice: the cards run on **BSIM-CMG 107** (their
   own version), not 111.
   - **Why 107:** 111 lacks five of the cards' parameters (CAPMOD, COREMOD, NSEG; ASAP5's K1SI, LRSD). On one card
     the two versions differ by up to 12 % (Xyce's 107 and 111 gold outputs).
   - **The 107 model:** compiled from the BSIM team's 107.0.0 Verilog-A as Xyce carries it (`tests/build_osdi.sh`,
     `BSIMCMG107_DIR`; the attribute syntax made standard). It matches Xyce's native 107 to 3e-9 in DC current and
     2.5e-4 in AC.
   - **Routing:** VERSION 107 runs on it and 111 on `bsimcmg.osdi`; another VERSION, or none, is an error.
   - **Card fixes, both explicit:**
     - ASAP5's `LRSD = 0` goes only where nothing reads it (RGEOMOD 0, CGEOMOD != 2);
     - 107's EOTACC bound is written as the 1e-10 the cards give (`0.1n` was a hair above it in floating point).
   - **Checks:**
     - parity: 12 new cases (inverters at every corner, ring oscillators, the SRAM read inverter) match ngspice on
       the same cards through OSDI to 1e-3; matrix 629 PASS;
     - `tools/m2_gate.py` step 7, in ADE: chain delay to print precision, ring frequency to 1.6e-4 (ASAP7
       27–36 GHz, ASAP5 52–76 GHz).
   - **ASAP5's L:** its cards' 14 nm (its cells are Cadence views).
   - **Open:** Xyce's 107 inverter-chain transient switches up to 4.3 ns (of a 1 µs period) from ours, while
     111's matches to µV. Not understood yet.
6. **HB** (TreeSPICE): harmonic balance, for strongly nonlinear circuits with many tones. Checked against PSS on the
   same circuits, and the Xyce gold outputs (datasets/xyce) that have HB.
   Done (2026-10-30): Xyce's cards, `.hb f1 [f2 ...]` with `.options hbint numfreq=k1[,k2 ...] [selectharms=diamond
   intmodmax=n]`; ADE analysis `hb {tones, harms [, intmodmax]}`, the calculator's `VHB`/`IHB` (spectrum) and
   `VHBT`/`IHBT` (one tone's period).
   - **Method:** Newton on the harmonics, its Jacobian PAC's conversion matrix (`lptv`), so HB and PAC share one
     linearisation. Several tones fold into one artificial period by the frequency map (box truncation, or a diamond).
     Residual from each tick's static stamp (sources off) and charges q(x); the sources' spectra (one tone: any
     waveform repeating at 1/f1, sampled; several: DC and SIN).
   - **Checks:** 12 Xyce HB golds pass to 2.3e-5 of each column's peak (BJT amplifiers, a PULSE-driven RTL inverter,
     diode clippers, the RC exactly); a rectifier's harmonics converge to its PSS (1.2e-4 V at 40 harmonics, 4.5e-7 at
     80); two tones 10k + 11k converge to one tone at their 1 kHz GCD (1e-9 V at 13 harmonics each).
   - **M2 gate step 8 (SG13G2, PSP; nominal, ss, ff):** the follower's harmonics 0..5 at 16 harmonics match its PSS
     to 1.0e-6 of the first; the passive mixer's LO (100 MHz, 20 harmonics) and a 1 mV RF (110 MHz) as two tones give
     a 10 MHz IF matching PAC's conversion to 2.1e-4 (-10.9 .. -10.4 dB). Gate: 35 of 35.
   - **Refused (explicit):** MOS levels 1–3 with Meyer capacitances (no charge function: Xyce's invert_hb), BJT
     excess phase (gilbert_cell_hb), transmission lines, XSPICE, time-dependent B sources, switch hysteresis.
   - **Limits:** dense DFTs and a direct solve of the conversion matrix: two tones of 13 harmonics on 8 unknowns take
     11 s. Oscillator HB (an unknown frequency) is not done; PSS covers oscillators.
7. **Spectre X**: the multithreaded solve and the S3 quotient arrays, reachable from ADE.

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
5. FinFET: on ASAP7 and ASAP5, an inverter chain and a ring oscillator over the PDKs' corners in ADE match ngspice
   (the same BSIM-CMG cards through OSDI).

**FinFET after M2:**
- **M3 (layout):**
  - snapping to the fin and gate-pitch grids;
  - the ASAP7 layer technology (ORFS's `asap7.lyt`);
  - a FinFET PCell (fins, gate, active, contacts), from the public design-rule manual;
  - DRC through ORFS's KLayout deck (`asap7.lydrc`).
  - **LVS:** no open ASAP7 deck exists (ORFS, the PDK and `lambdapdk` have none; the PDK's own is Calibre). M3 writes
    a KLayout LVS deck that recognises FinFETs and counts their fins. It is the one piece of new groundwork here.
- **M5 (digital):** `rtl_flow` reads a platform description instead of IHP's paths (DRC and LVS decks, Liberty, cell
  models, `setRC`). ASAP7 runs ORFS from synthesis to GDS, with KLayout DRC and OpenRCX extraction. Until M3's deck
  lands, its LVS step reports that no open deck exists, rather than passing. ORFS's `gt2n` (2 nm nanosheets) follows
  the same path once its models are checked.

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

2026-10-25:
- The Cadence suite is a comparison target, not a replication list. Open-source groundwork is reused, and OpenVLSI
  builds the seams between tools.
- FinFET PDKs, as OpenROAD supports them: ASAP7 and ASAP5 (M2 slice 5, then M3 and M5).
