# Engine Handoff — Karam (acutesim_engine)

*Written 2026-10-12 at the end of the engine-review session. Read this first, then `CLAUDE.md`,
then the two Roadmap sections it points to. Everything below was verified by running it.*

## 0. Where things stand

- **Branch:** `engine/spice-correctness` (10 commits on top of `97a0735`, **not pushed**, tree clean).
- **Next work, in order:** (1) GUI large-design performance / `NetlistCompiler::compile` slowdown,
  (2) Roadmap **Phase 2 — Analysis parity**.
- **Status of the plan:** Roadmap Phase 1 (Foundations) is done. See `Roadmap.md` →
  "⚙️ ENGINE FEATURE LIST & VERIFIED SUPPORT STATUS" (what works, by device/analysis, with ✅/⚠️/❌/🚫/❓)
  and "🎯 END-TO-END GOAL & ENGINE SCALE PLAN" (phases and gates).

```
0bce71c perf(engine): run independent contraction fronts on worker threads
a90e4af fix(engine): apply MOSFET channel-length modulation in the linear region
0b90505 feat(engine): MNA branch currents for V sources, inductors, E/F/H and K
a200bd7 feat(engine): tensor-contraction direct solver as the default linear solver
04d4b95 test: ngspice differential harness and engine waveform dump tool
1b7b760 fix(engine): working subcircuits — local node scoping and instance-aware solver
2d858fa docs(roadmap): add end-to-end SoC goal and phased engine scale plan
cada846 docs(roadmap): add verified engine feature list and support status
4e16c99 fix(engine): SPICE correctness — fail-closed convergence, parser, waveforms, E/G/J
fe2c077 chore: ignore graphify-out code-graph artifacts
```
`git log 97a0735..HEAD` has the full messages.

## 1. The owner's direction (do not drift from this)

- **Goal:** one application to design an SoC, connect RAM, run firmware on it and see the program's
  output, covering every step from schematic/RTL to sign-off.
- **Core bet: tensor contraction.** Simulating up to 10⁹ transistors on a local GPU via contraction
  and the parallelism superposition gives. Superposition holds *inside each Newton step* (the
  linearised system): child fronts contract independently and their Schur complements **add** into the
  parent. It does **not** parallelise across Newton iterations or timesteps.
- **Physics reality (measured):** M4 Pro, 26 GB unified memory. Flat SPICE-accurate 10⁹ devices does
  not fit (>150 GB). Billion-scale needs FastSPICE techniques on top of contraction: identical cells
  share tensors (isomorphism reuse), activity bypass, GPU-batched fronts. Firmware runs on RTL/gate
  models; SPICE is for the blocks that need it, co-simulated.
- The owner likes delegating focused, well-specified tasks to **Sonnet subagents**, and wants
  verification by **running** things (the ngspice harness), not by reading checklists.

## 2. Engine architecture as of now

```
SPICE text ──► SPICEParser (netlist/spice_parser.*) ──► TensorNetlist (netlist/circuit.h)
                 • local node scoping in .subckt, X expanded after parse (recursive)
                 • .model/.param/F-H control names resolved after the whole file
                 • unknown/unsupported cards = hard parse error (never silently dropped)
TensorNetlist ─► CircuitSim::solveDC / stepTransient (solvers/circuitsim_solver.cpp)
                 unknowns x = [N node voltages | B branch currents]   (systemSize())
                 per Newton iteration:
                   stampAllElements (physics/circuitsim.h)
                     ├─ SoA path (default): stampSoABlock = R, I, C, D, M, Q tensors
                     │    + stampUncovered = G (VCCS), J (JFET) from the netlist
                     ├─ AoS path (LEGACY_AOS, used by ai_api speculative runner)
                     └─ stampBranchSystem = V, L(+K), E, F, H  ◄─ ONE function for both paths
                   solveLinear(A, b) ─► contraction solver (math/contraction_solver.*)
                   kclExcess(...)   ─► audit residual beyond KCL_RELTOL·(|A||x|) (scale-aware)
                   checkConvergence (fail-closed on NaN)
Session API: engine_impl.cpp (ISimulationEngine) — parse errors returned, transient loop retries
GUI path:   compute/orchestration/driver/local_worker_driver.cpp calls CircuitSim directly
            (its own transient loop: fixed step, stops at first failure, no UIC)
```

Key invariants (each was a real bug when broken):
- **Stamps and the KCL audit must use the same formulas.** Branch elements guarantee it by construction
  (`buildBranchSystem` feeds both). Other devices: if you change a stamp, change
  `calculatePhysicalResiduals` (global AND instance sections) too.
- **Branch order:** all V sources, then inductors, then E, then H; each over the global block, then
  instances in `tensorizeNetlist` order (`branchSlots()`). History updates walk the same order.
- **Outputs are node voltages only:** `SolverStep::nodeVoltages` is truncated to N. The full x lives in
  `CircuitSim::lastSolution_` (seeds transient Newton and sensitivity).
- **Waveforms:** a single `waveformValue()` (netlist/circuit.h) is used by parser, stamps and audit.
- **Junction exponent cap:** `PhysicsConstants::JUNCTION_EXP_ARG_MAX = 50`, shared by every diode/BJT
  kernel, the WGSL shader and `pnjlim` (keep them in sync).
- **Contraction solver:** plan once per sparsity pattern (MC64-style matching → hubs to root → nested
  dissection → fronts grouped in levels); factor every iteration (assemble from CSR, add children,
  threshold partial pivoting u = 0.01, delayed pivots to parent); solve + iterative refinement. Levels run
  on `std::thread` workers (deterministic). The old `sparse_lu.cpp` is dense internally; the legacy
  TN/MPO/Schur executor and the WebGPU "hybrid" loop are no longer called by `solveDC`.

## 3. How to build and verify (run all of these after any engine change)

```bash
cd CuteApp
cmake --build build --target acutesim_engine acutesim_engine_dump -j10
for t in test_engine_fixes test_contraction_solver test_regression test_engine_parity \
         test_hierarchy_headless test_std_cells test_soa_bridge test_convergence_engine \
         test_physics_kernels test_veriloga_parser test_sparse_lu test_schur_solver test_graph_partitioner; do
  cmake --build build --target $t -j10 && ./build/$t > /tmp/$t.log 2>&1; echo "$t exit=$?"; done
# test_regression MUST run from the repo root (it reads ./regression_golden.db); it is the CI gate.
TMPDIR=/private/tmp/claude-502 python3 tests/ngspice_diff/run_diff.py   # ~90 s; needs `brew install ngspice` (installed)
```
Expected today: every suite passes; ngspice harness (34 netlists, each run in cpu and gpu mode) =
27 of 28 compared netlists PASS in both modes; the only FAIL is `aac_10_bridge` (a published netlist
with floating outputs, ill-posed; `x_bridge_loaded` is its well-posed twin and passes); 5 SKIPPED (.ac);
1 ENGINE-UNSUPPORTED (`x_pwl_unsupported`, PWL rejected by design).

- `tests/test_engine_fixes.cpp` = one regression check per fix (analytic/hand-derived references,
  CPU and GPU modes). Add a check there for every engine fix.
- `build/acutesim_engine/acutesim_engine_dump <netlist> <cpu|gpu> <dc|tran> [step stop]` prints
  machine-readable waveforms (node order = first appearance, subckt bodies excluded).
- **Golden DB updates:** never accept `--update` wholesale. Run it in a scratch copy, splice only the
  entries whose change you have derived physically, and say why in the commit (see commits
  `4e16c99`-series diode entry and `0b90505`).

## 4. Task 1 — GUI large-design performance (`NetlistCompiler::compile`)

**What is known (verified):**
- `NetlistCompiler::compile()` (`acutesim_engine/infrastructure/netlist_compiler.cpp`, ctx overload in
  `netlist_compiler_ctx.cpp`) runs: tensorize → GraphPartitioner → FactorGraph → **TreewidthAnalyzer
  (Min-Fill, hash-set elimination, ~quadratic: 100+ s at 10⁴ nodes)** → TNCompiler. The WebGPU upload
  used to call it on every solve; that call was replaced by `tensorizeNetlist()` (commit `a200bd7`).
- Since `0b90505` the solver no longer reads `tnAnalysis` / `tnProgram` / `partitions`. The only
  remaining reader is `WebGPUSolver`'s legacy contraction path (`renderGraph`), itself unused by `solveDC`.
- Callers of `compile()`: `compute/orchestration/batch_runner.cpp` (once per sweep point),
  `tests/test_hierarchy_headless.cpp`, `tests/test_hierarchy_gui.cpp`, `tests/test_virtuoso_parity.cpp`.
- **Correction to the previous session's summary:** the GUI's normal path
  (`LocalWorkerDriver` → `BatchExecutionEngine` → `CircuitSim::solveDC/stepTransient`) does **not**
  call `compile()`. `gui/analysis/parametric_sweep_dialog.h` includes `batch_runner.h` but no GUI code
  was found instantiating `BatchRunner`. So the "GUI compile slowdown" may not be what users hit.

**Do this:**
1. Measure first. Drive the real app path headlessly with a generated 10⁴–10⁵-node netlist (grids:
   see `Roadmap` measurements; generator = a few lines of Python) through `LocalWorkerDriver` and profile
   (`sample <pid>` on macOS). Find where time actually goes (schematic→netlist conversion, design DB,
   `BatchExecutionEngine`, Dawn/GPU context start-up ~0.5–1 s even in CPU mode, …). Fix what dominates.
2. Make `compile()` cheap regardless: drop or lazily compute the stages nobody consumes
   (factor graph, Min-Fill, TN compile, partitioning). Check each consumer with grep before removing;
   keep `tensors`, metadata, `structural_`, interface pins. Expected: `test_hierarchy_headless` (97) still
   passes; compile of a 10⁴-node netlist drops from minutes to ~ms.
3. Decide (ask the owner) whether to delete the now-dead legacy stack: `sparse_lu.cpp` (dense),
   `contraction_executor`, `schur_solver`, `mpo_builder`, `tn_compiler`, `treewidth_analyzer`,
   `factor_graph`, the WebGPU hybrid NR loop. They have tests of their own; deletion is a scope call.
- **Gate:** a 10⁴-node design runs DC end-to-end through the app path in < 1 s; batch sweeps do not
  re-run quadratic analysis per point.

## 5. Task 2 — Roadmap Phase 2 (Analysis parity)

From `Roadmap.md` Phase 2, with pointers:
1. **DC operating point as transient initial state.** Today transient always starts from zero state even
   without `uic` (`engine_impl.cpp` solves DC only as a gate; `stepTransient` history starts empty).
   Seed capacitor voltages / inductor currents from the DC solution unless `uic`; honour `.ic`.
   Add `uic`/IC to the DTO (`engine_api/simulation_dto.h`) so the GUI path can request it.
2. **Breakpoints** at PULSE corners (and SIN start/delay): the step controller must land on them
   (`waveformValue` has a `ponytail:` note). Square-wave integrator (#17) converges only first-order now.
3. **`.dc` sweeps** in the parser/session (the ngspice harness emulates them today).
4. **AC + noise through `ISimulationEngine`.** `SessionImpl::runAC` currently returns a DC result
   labelled AC 🚫; `runNoise` aliases it. The GUI path has a real `ACSolver::solveAC`
   (`solvers/ac_solver.h`, unverified). It is voltage-only (Norton sources): it must get the same MNA
   branch-current treatment, then be wired to the session and checked against ngspice (`.ac` netlists
   `aac_05..09` already in the harness, currently SKIPPED).
5. **One transient loop** shared by `SessionImpl::dispatch_tran_internal` and
   `LocalWorkerDriver` (the latter has fixed step, stops at first failure, no UIC).
- **Gate:** every analysis in the feature list ✅ against ngspice (extend `run_diff.py` to `.ac`).

## 6. Known issues / backlog (not yet fixed)

- Monte Carlo: `mcSeed` never applied (every run identical) 🚫.
- Hard nonlinear DC: plain-Newton-first costs extra iterations (Darlington 35 → ~115); consider a lower
  iteration cap on the direct attempt.
- Instance KCL audit: diodes use the unclamped equation and MOSFETs hard-coded params (global audit uses
  model cards) — inconsistent for model-card MOSFETs inside subcircuits.
- MOSFET level 1: no source/drain swap for Vds < 0, no body effect. Diode: no RS/CJO/TT. BJT: no VAF, RB/RC/RE,
  junction caps. AREA, EXP/PWL/SFFM/AM, T lines: explicit errors.
- Subcircuit parameters (`params:`) unsupported. `.include/.lib/.options/.ic/.print` silently ignored.
- Floating high-impedance nodes: the "KCL < 1e-9 A ⇒ converged" shortcut can accept stale voltages.
- Junction-cap shortcut and other simplifications are marked `ponytail:` in code (grep for it).
- AVX2/AVX-512 kernels are x86-only and were not built on this Mac; they mirror the NEON code.
- Engine CLI (`acutesim_engine_cli`) parses `.tran 10u` with `istringstream` (no SI suffixes).

## 7. Gotchas learned the hard way

- **Qt macros:** never name engine identifiers `slots`, `signals`, `emit`, `foreach` — engine headers are
  compiled into Qt targets (`test_std_cells` broke on a local `slots`).
- **Stale binaries:** `cmake --build build --target <x>` before trusting a result; a stale CLI once
  showed the pre-fix dense solver. New targets need `cmake -S . -B build -DFETCHCONTENT_UPDATES_DISCONNECTED=ON`.
- **macOS `sed` has no `\b`:** use `perl -pi -e`.
- **SPICE line 1 is the title** — test netlists need a title line or the first element vanishes.
- **No `-ffast-math`** (it deletes NaN checks; CLAUDE.md requires `-fno-fast-math`).
- **Absolute tolerances lie at stiff nodes:** use the scale-aware metrics (`kclExcess`, backward error),
  and justify any tolerance change in the commit.
- `graphify-out/` (code graph: `graphify update .`) is git-ignored; regenerate as needed.
- Commits: conventional prefix, explain *why*, end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
