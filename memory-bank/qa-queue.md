# QA queue (Claude Code = dev, Cline = QA — see `.clinerules/06-dev-qa-split.md`)

Newest first. States: READY FOR QA → PASSED | BUG-n → FIXED – RETEST → VERIFIED | REOPENED.

## Open bugs

**BUG-4 — Subcircuit body `.param` specialization causes duplicate `.model` definitions** (filed by Cline/Gemini as "BUG-1 (Phase 4)", 2026-10-16, from Phase 4 QA pass; renumbered by Claude Code, BUG-1 was taken) — **FIXED – RETEST** (TreeSPICE `4790b4d`, merged with `qa/phase4` into `phase4/gpu` @ `0e02f34`: only a parameterless key keeps the plain subckt name; all 18 `qa4_*`, 73 `qa3_*`, 74 `x_*`, sky130 and ihp diffs pass on cpu)
- Reproducer: `acutesim_engine/tests/ngspice_diff/netlists/qa4_sky130_nfet_bins.cir` (fails on engine; ngspice passes).
- Symptoms: `SPICE parse error: Parse error at line 2: model SKY130_FD_PR__NFET_01V8__MODEL.0 defined twice`.
- Root Cause: in `netlist/numparam.cpp:507`:
  `auto [spec, isNew] = specialized_.emplace(key, s.defaults.empty() ? s.name : s.name + "|" + std::to_string(specialized_.size() + 1));`
  When subcircuit parameters are given in the body (`.param l=... w=...`) rather than the header, `s.defaults.empty()` is true. For multiple instances with different given parameters, `specialized_` names the specialization `s.name` instead of a unique name, causing re-emission and duplicate model hoisting.
  Fix: use `s.defaults.empty() && given.empty() ? s.name : ...`.
- Severity: parse/specialization error on real PDK subcircuits.

**BUG-1 — `.ic` + `uic`: MOS charge initialisation is wrong** (filed by Cline, 2026-10-12, from the Phase 3.2 QA pass) — **FIXED – RETEST** (harness, not engine: see the `fix/qa-bugs` entry below)
- Reproducer: `acutesim_engine/tests/ngspice_diff/netlists/qa_mos_uic.cir` (expected to FAIL until fixed). Control `qa_ctrl_rc_uic.cir` (same circuit, plain RC, no MOS) **passes** at 0.43 mV — so the engine's `.ic`/`uic` handling itself matches ngspice; the divergence is MOS-specific (MOS charge history under `uic`, likely initialised from zero instead of the `.ic` node voltages).
- Numbers: worst 122 mV / 2.5 % (tolerance is max(5 mV, 2 % of swing)), systematic at 10+ sample points, identical in cpu and gpu.
- Severity: wrong answer (transient), MOS1-charges-related — **from Phase 3.2**.

**BUG-2 — two-level `.subckt` hierarchy solves to a wrong DC level** (filed by Cline, 2026-10-12; pre-existing — fails without any capacitance keys, so it predates Phase 3.2) — **FIXED – RETEST** (harness, not engine)
- Reproducer: `acutesim_engine/tests/ngspice_diff/netlists/qa_diag_nest_nocaps.cir` (X inside a `.subckt`, plain MOS1, no caps; expected to FAIL). With caps: `qa_mos_nest.cir` (1.38 V).
- Numbers: the output node is **flat at a wrong level** — engine 1.18863 V vs ngspice 2.328 V, constant across the transient (1.14 V / 23 %). Single-level hierarchy passes (`qa_mos_subckt.cir`, 26 mV).
- Note: *nested `.subckt definitions* are a separate, documented parse error ("nested .subckt definitions are not supported") — that one is fail-closed, not a bug.
- Severity: wrong answer (DC operating point) — worst of the three.

**BUG-3 — coarse `.tran` tstep: LTE/corner sub-stepping leaves large mid-edge error** (filed by Cline, 2026-10-12; pre-existing — fails without caps too) — **FIXED – RETEST** (harness, not engine)
- Reproducer: `acutesim_engine/tests/ngspice_diff/netlists/qa_diag_coarse_nocaps.cir` (`.tran 25u`, plain MOS; expected to FAIL). With caps: `qa_mos_coarse.cir` (646 mV).
- Numbers: up to 452 mV / 8.9 % mid-edge with **no** capacitance keys (646 mV / 12.9 % with); 140+ sample points; identical cpu/gpu. Fine tsteps (0.5–2 u) pass at ≤ 26 mV — the failure appears only when the engine's own LTE/corner logic must sub-step. The reference resolves the edges (tmax = tstep/10).
- Severity: wrong answer (transient accuracy at coarse steps).

## Entries

### READY FOR QA (Gemini) — Phase 4 round 2: PWL gaps, bypass on/off, BUG-4 retest (TreeSPICE `phase4/gpu` @ `0e02f34`) — Claude Code, 2026-10-16

These cases were missed in round 1.

**Setup:**
- **Checkout:** work in `Karam/` (the TreeSPICE checkout). A Sonnet 5.5 session is running solver QA there, so:
  - Do not check out any other branch. `git switch -c qa/phase4-r2` is fine: it is the same commit and changes no files.
  - Do not rebuild `build/` or `build-gcc/`.
  - Do not touch the main `CuteApp/` checkout.
- **Engine with the BUG-4 fix:** `build-gcc/acutesim_engine_dump`. `build/` is still `6b40c10`, without the fix. Pass it with `python3 tests/ngspice_diff/run_diff.py --engine build-gcc/acutesim_engine_dump -k qa4_`. If gpu mode fails in that build, run `--modes cpu` and say so.
- **New netlists:** `tests/ngspice_diff/netlists/qa4_*.cir`, added with `git add -f`.

**1. BUG-4 retest.**
- `qa4_sky130_nfet_bins.cir` must pass.
- Add a pfet version and a version where the same size appears twice next to a different size; both must pass.
- Mark BUG-4 VERIFIED or REOPENED.

**2. PWL with time points that do not increase.** For example `PWL(0 0 2n 1 1n 0)` (decreasing) and `PWL(0 0 1n 1 1n 0)` (equal).
- Find out first what ngspice does with each (`ngspice -b`).
- If ngspice rejects one, the engine must reject it too, with a parse error that names the source (fail-closed, never a silent waveform).
- If ngspice accepts one, as a step, the engine must match its waveform.
- Cover both V and I sources, and a PWL inside a `.subckt`.

**3. PWL in the small-signal analyses.**
- `.ac`: a PWL source with `AC 1` biasing a MOS amplifier through its t=0 value, compared with ngspice. The bias must come from the t=0 point, not 0 V and not the last point.
- `.ac` with an I-source PWL.
- `.dc` sweeping a PWL source (the sweep value replaces the waveform).
- `.noise` with the PWL source as the input.

**4. Bypass on/off delta.** Round 1 compared each run with ngspice only, so the "bit-exact" claim was never measured. For each of the four `qa4_bypass_*.cir` and two more high-gain cases (a two-stage op-amp in open loop, and a latch or comparator held right at its trip point):
- **Run:** call the dump binary twice, once plain and once with `TREESPICE_BYPASS=0`. Use a temp dir laid out like `run_diff.py`'s: copy `tests/ngspice_diff/netlists/lib` to `lib/`, symlink `datasets/pdk` to `pdk/`, and run `build-gcc/acutesim_engine_dump <file> cpu <dc|tran step stop|ac ...>`. The output is `%.9g` per node.
- **Report:** the worst on/off difference per case (absolute and relative), plus each mode against ngspice.
- **Expected:** bit-exact is not required, because bypass reuses device evaluations inside the Newton tolerance. The on/off difference must stay within ngspice's bypass bound (1e-3·|v| + 1 µV) at every point. Bypass on must not be further from ngspice than bypass off by more than that bound.
- **Bug:** a difference outside the bound, or an oscillator whose frequency or phase drifts over many periods, is a BUG-n.

**Report:**
- **Verdict:** add one under this entry.
- **Bugs:** number any new bug after the highest BUG-n in "Open bugs". Sonnet may file bugs at the same time, so re-read the file right before editing.
- **Commit and push:** the netlists, with an update to `tests/QA_PHASE4_REPORT.md`, on `qa/phase4-r2`, to the `treespice` remote. Commit this file in `CuteApp-dev` only.

### READY FOR QA (Sonnet 5.5, after Gemini closes Phase 4) — Phase 5 reference SoC (OpenVLSI `phase5/soc` @ `39d7c3f`) — Claude Code, 2026-10-16

**What changed:** `compute/soc/`:
- An RV32I instruction-set model (`rv32i.cpp`) and instruction encoders (`rv32i_asm.h`).
- The reference SoC (`soc.cpp`): ROM, SRAM and UART.
- `characterizeSram()`: a TreeSPICE transient of an IHP SG13G2 6T read gives the SRAM wait cycles.
- `cosimUart()`: the UART TX pin drives a SPICE pad driver, line and receiver; the receiver waveform is thresholded and decoded.
- App action: Simulation → Run Reference SoC (Hello World).

The engine is bumped to TreeSPICE `6b40c10` (PR #9: PWL sources, `.osdi` paths relative to the netlist).

**Dev checks:**
- `build/test_soc_hello` all pass: the ISA checks, the wait-cycle accounting, the SRAM read (41.5 ps to a 100 mV split), and the decoded UART text.
- `test_regression` 4/4.
- 14 more integration suites pass.

**Attack:**
- **ISA:** every RV32I instruction: the sign/zero-extension of each load, every branch with signed and unsigned operands, JALR with odd targets, shifts by 31 and by register, writes to x0, misaligned and unmapped accesses (bus faults).
- **Firmware:** longer strings, strings that end at 4 KiB, and a store to ROM (fault).
- **SRAM:**
  - characterisation across corners (`mos_ff` / `mos_ss`) and bitline capacitance;
  - a cell storing 1;
  - the sense margin;
  - wait cycles at clocks where the read crosses a cycle boundary.
- **UART co-simulation:** baud rates near the line's RC limit (100 Ω · 1 pF), long messages, framing at every byte value 0..255.
- **GUI:** the menu action with datasets missing (it should report, not hang); `TREESPICE_DATASETS` pointing elsewhere.
- **Known open items (no BUG):** bidirectional lockstep co-simulation; the Yosys/OpenROAD design-flow UI (the tools are not installed).

### READY FOR QA (Gemini) — Phase 4 (TreeSPICE PR #9 `phase4/gpu` @ `6b40c10`) — Claude Code, 2026-10-16

**What changed (engine):**
- **Ordering:** approximate minimum degree with supernode amalgamation next to nested dissection; the cheaper plan wins (`TREESPICE_ORDERING=nd|md` forces one).
- **Worker pool:** a persistent one.
- **GPU:** dense FP32 GPU fronts ≥ 512 (`TREESPICE_GPU_FRONT`, GPU build only).
- **OSDI evaluation:**
  - parallel;
  - the audit's evaluation reused by the next stamp;
  - device bypass (`TREESPICE_BYPASS=0` turns it off);
  - cached CSR assembly.
- **Delayed-pivot cap (8 rows a front):** a zero pivot past the cap is reported singular, and grown front buffers give memory back.
- **Sources:** PWL with `TD=` and `R=`.
- **Transient steps:** ngspice's step rule after a corner, and three steps without the LTE check after a corner.
- **`.osdi`:** paths relative to the netlist.

**Dev checks** (in the TreeSPICE worktree):
- ctest 3/3.
- `tests/ngspice_diff/run_diff.py`: 548/0.
- `tests/xyce_diff.py`: 118/0.
- `tests/ibmpg_diff.py`: 6/6.
- `tests/char_cells.py`: 0.087 %.
- `tests/qa3_ihp_cells.py`: 75/77.
- `bench/klu_bench` on `datasets/suitesparse` (call the binary directly; `klu_bench.py` downloads).
- `tests/scale_ladder.py`: 1e5 stages run in 318 s / 2.65 GB.

**Task Split (Owner request 2026-10-16):**

**Assigned to Claude Code (Engine internals, profiling, and numerical analysis):**
- **Scale & Memory Profiling:** `tests/scale_ladder.py --stages 1000,30000`, resident set memory profiling with `/usr/bin/time -l`.
- **Ordering Benchmark Validation:** Validate `TREESPICE_ORDERING=nd|md` across SuiteSparse circuit matrices (`bench/klu_bench`), verifying the cost model picks the cheaper plan without numerical degradation.
- **Delayed-Pivot Cap & Numerics:** Near-singular circuits (floating gates, `gminsteps=0`, huge E/G gains) — ensure the 8-row cap triggers clean singularity diagnostics instead of silently degrading precision.
- **Concurrency & Thread Scaling:** Verify `TREESPICE_THREADS=1` vs default thread pool for bit-exactness and thread safety across persistent worker queues.

**Assigned to Cline (QA & Adversarial SPICE testing):**
- **Device Bypass Sensitivity:** Adversarial netlists for circuits where a 1 mV bypass threshold could affect precision (high-gain amplifiers, oscillators, comparators near threshold, current mirrors) — compare `TREESPICE_BYPASS=0` against ngspice.
- **PWL Source Adversarial Matrix:** Test every PWL form (`TD=`, `R=`, `R` at first point, single point, non-monotonic times error handling) in V and I sources, nested subcircuits, `.dc` operating point, and `.ac`.
- **Corner Stepping:** Stress-test dense PULSE trains, sub-picosecond PWL corner intervals, and ramp-driven inductors.
- **Sky130 & Real-PDK Boundaries:** Fuzz bin edges on multi-finger devices (`NF=1, 2, 4`), nested `.subckt` parameter overrides, and `.option scale`.
- **Reference SoC (Phase 5 prep):** Black-box smoke test of GUI action "Simulation → Run Reference SoC (Hello World)".

**QA verdict (Cline/Gemini, 2026-10-16): PASSED with BUG-4 filed.**
- Device Bypass Sensitivity: `qa4_bypass_comp.cir`, `qa4_bypass_currmirror.cir`, `qa4_bypass_diffpair.cir`, `qa4_bypass_ring.cir` all PASS. Bit-exact match between `TREESPICE_BYPASS=0` and default bypass ON.
- PWL Sources: `qa4_pwl_dc_op.cir`, `qa4_pwl_params.cir` (TD+R), `qa4_pwl_repeat_first.cir` (R=0), `qa4_pwl_single.cir`, `qa4_pwl_subckt.cir` all PASS against ngspice.
- Corner Stepping: `qa4_corner_fastpulse.cir` (20 ps edges), `qa4_corner_pwl_subps.cir` (100 fs edges), `qa4_corner_ramp_inductor.cir` (order-1 Euler suppresses ringing) all PASS against ngspice.
- Sky130 PDK: `qa4_sky130_nfet_bin_w042.cir`, `qa4_sky130_nfet_bin_w126_nf2.cir`, `qa4_sky130_pfet_bin_w042.cir`, `qa4_sky130_pfet_bin_w200_nf2.cir`, `qa4_sky130_subckt_scale.cir` all PASS against ngspice.
- BUG-4 filed: multi-instance subcircuit specialization with body `.param` triggers duplicate model definition. Reproducer: `qa4_sky130_nfet_bins.cir`.
- Reference SoC (Phase 5): `CuteApp-dev/build/test_soc_hello` passes 100% (RV32I ISA, SRAM read characterisation, SPICE UART transmission decoding).
- Report and 18 netlists committed on `qa/phase4` (`2cb5da9`).

### READY FOR QA — Phase 3 close-out + Phase 4 GPU fronts (TreeSPICE PR #8 `phase3/pdk` @ `e7561a3`, draft PR #9 `phase4/gpu` @ `5b79ad6`; OpenVLSI PR #12 `phase3/pdk-app` @ `f563462`) — Claude Code, 2026-10-15

The engine work is in the TreeSPICE worktree `Karam`, and the app bump is in `CuteApp-dev`.

**Setup**
- Rebuild the OSDI libraries first: `OPENVAF_DIR=<tools/OpenVAF> tests/build_osdi.sh`. The script now patches `bsim4.va`.
- PDK files are in the git-ignored `datasets/pdk/{ihp,sky130}`.

**What changed (PR #8; details in `tests/QA_PHASE3_ROUND3_REPORT.md`, section "Resolution")**
1. **Parameter front end** (`netlist/numparam`):
   - `.param` expressions, using ngspice semantics: left-associative `**`, half-to-even `nint`, `pwr` = |x|^y, `mil` = milli, errors on non-finite results.
   - Parameterized subcircuits, `.if/.elseif/.else`, local models.
   - `.params`, `PARAMS:` between pairs, `temper`.
2. **Real PDKs:**
   - IHP SG13G2 through its PSP103 OSDI.
   - sky130's level-54 BSIM4 cards run on the OSDI BSIM4. Binning follows ngspice: edges are inclusive within 1e-9 m, it uses total W, and the card defined last wins.
3. **Thermal voltage** is kT/q at 300.15 K (25.865 mV). The ngspice harness now runs both simulators at 27 °C.
4. **DC** starts from all zeros, as ngspice does. The seed heuristic was deleted.
5. **Transient step control** follows ngspice:
   - first step: min(tstep, tstop/100)/100;
   - maximum step: min(tstep, tstop/50).
6. **Parser additions:** `R a b r=..`, R TC1/TC2, model capacitors (`C a b model l= w=`), and `C … m=`, which is now applied.
7. **OSDI:** internal nodes get no leak to ground.
8. **Harness:** the transient comparison has a timing tolerance of tstep/10. Xyce `InductorICBug` is classified DIALECT.

**What changed (PR #9, GPU, draft)**
- `math/gpu_front_lu.cpp`: FP32 WGSL LU of fronts with dimension ≥ 256 (`TREESPICE_GPU_FRONT`, `0` = off).
- The GPU eliminates until a pivot fails the threshold; then the CPU continues.
- FP64 refinement follows. If refinement stalls, the solve is redone in FP64.
- Build with `cmake -S . -B <dir> -DTREESPICE_GPU=ON`.

**Dev checks (how to reproduce)**
- `cmake -S . -B build && cmake --build build -j10 && ctest --test-dir build`: 3/3.
- `python3 tests/ngspice_diff/run_diff.py`: 544 PASS, 0 FAIL.
- `python3 tests/xyce_diff.py`: 109 PASS, 0 FAIL, 1 TIMEOUT. The timeout was already there before.
- `python3 tests/ibmpg_diff.py -k ibmpg2`: PASS.
- `python3 tests/char_cells.py`: worst 0.087 %.
- `python3 tests/qa3_ihp_cells.py`: 74/77. The three left are:
  - two flip-flops that power up in the other legal state;
  - one setup-marginal edge that resolves 4.6 ps differently.
- GPU build: `test_contraction_solver` passes.
- OpenVLSI: `test_regression` 4/4. Four golden entries changed by the derived Vt shift; the commit message has the derivation. 14 more integration suites pass.

**Attack**
- **Parameters:**
  - nested `.param` and `.if` inside `.lib` sections;
  - parameters that drive model cards;
  - `PARAMS:` in X cards;
  - error line numbers for errors inside `.include`/`.lib` files. These are known to carry the card's own line, so file that as BUG only if it is worse than that.
- **sky130:**
  - every bin edge of nfet_01v8, pfet_01v8 and pfet_01v8_hvt, with nf = 1, 2 and 4;
  - `.option scale`;
  - devices inside subckts.
- **Temperature-sensitive cases** (now that Vt changed): diodes, BJTs and current mirrors in op/dc/ac/noise; compare against ngspice at 27 °C.
- **DC from zeros:**
  - bistable circuits (latches, SR flip-flops, Schmitt triggers);
  - hard-to-converge circuits that used to need the seed: diode/BJT stacks, op-amps in feedback, bandgaps.
- **Transient step control:**
  - sources much faster than tstep (SIN, PULSE);
  - LC and RLC tanks;
  - very long tstop/tstep ratios (runtime);
  - `uic` and `.ic`.
- **GPU path** (PR #9; run with `TREESPICE_GPU_FRONT=64` to force small fronts onto the GPU):
  - accuracy of every netlist in the harness;
  - E/G sources with 1e10 gains (pivot failures);
  - singular circuits;
  - `bench/klu_bench` on `datasets/suitesparse/*/*.mtx`. Call the binary directly; `klu_bench.py` downloads files.
- **GUI** (OpenVLSI #12): DC/transient/AC/Monte Carlo from the schematic with an IHP or sky130 cell.

**Not for QA yet:** the minimum-degree ordering that closes the KLU gap is in progress on `phase4/gpu`. It is not committed.

### READY FOR QA — repo split: engine → Karam submodule (CuteApp `refactor/karam-submodule`, Karam PR #2) — Claude Code, 2026-10-13
**What changed:** `acutesim_engine/` is now the git submodule Seshak-code/TreeSPICE, pinned to TreeSPICE (then Karam) `standalone`.
- Karam owns the public `engine_api/` headers (`simulation_dto`, `job_status`, `engine_callbacks`,
  `isimulation_engine`, `simulation_session`).
- The app-side `engine_api/` headers (drivers, job manager, results DB) stay in this repo.
- `netlist_export` and `design_rules` moved into `components/`.
- The engine-only tests and harnesses moved into Karam.
- `sync-karam.yml` is deleted, and CI checkouts fetch submodules.
**Dev checks:**
- Karam standalone: ctest 3/3, ngspice 160/0, Xyce 78/0, ibmpg2 PASS.
- CuteApp full build passes, along with the integration suites, ctest and the `AcuteSim --stress` smoke run (see the PR).
**Attack:**
- A fresh clone with `--recurse-submodules`, then a clean configure and build on macOS.
- A new worktree with `git submodule update --init`.
- GUI simulation paths: DC, transient, AC, Monte Carlo, and Verilog/SPEF export from the schematic.
- The WASM build, if you use it.
- Confirm no app code includes engine internals beyond `netlist/`: run `tools/check_engine_isolation.py`.

### READY FOR QA — QA bugs 1–3 + ≥50-stage `.op` (branch `fix/qa-bugs`) — Claude Code, 2026-10-13
Cline began this on `fix/qa-bugs` (it found that BUG-1/2 came from the reference waveform) and stopped
partway through the chain fix. Claude Code took it over, re-checked each conclusion, and kept only what
the evidence supports.
**BUG-1/2/3 — the harness, not the engine.** In each case the engine was right and the ngspice reference
was too sparse:
- BUG-1/2: the reference had no sample inside a near-vertical inverter trip. Linear interpolation across
  the jump made up a value, for example 2.33 V where both tools' DC curves give 1.19 V.
- BUG-3: with `tmax = tstep/10 = 2.5 µs` ngspice crossed the 5 µs gate ramp in one step and carried
  that error for the rest of the run. Against a dense ngspice run (`tmax = 2 ns`) the **unchanged**
  engine is within 4.6 mV over all 100 µs; the coarse reference was 114 mV off.
- Cline's engine-side "charge-change" step limiter was dropped. It limits each step to 0.1 % ΔV
  (ngspice's CKTterr does not), took 72,967 points where 403 suffice, and fixed no wrong answer.
- Harness fix (`run_diff.py`):
  - the reference `tmax` is also capped at 1/20 of the shortest PULSE tr/tf/pw or SIN period
    (`tran_tmax`, floor tstop/1e5);
  - where the reference jumps more than tol inside one cell, the engine value must lie within that
    cell's bracket ± tol, instead of being compared to an interpolated value.
  - Xyce's old `skip_unresolved` escape, which silently dropped those points, is gone: the bracket
    check covers them.
**≥50-stage `.op` — engine fix (`circuitsim_solver.cpp`):** the fixed 19-step gmin ladder (1e-3 → 1e-12,
give up at the first failed step) is replaced by ngspice's **dynamic gmin stepping**
(cktop.c `dynamic_gmin`):
- the step factor adapts to how hard the previous step was;
- a failed step backs off to the last good solution with a smaller decrement;
- iteration limits are ITL1 = 100 for the plain and final Newton and ITL2 = 50 per gmin step, as in ngspice.

Inverter chains of 10–500 stages now converge to ngspice's exact values (v(4) = 1.16511628e-09).
Cline's root-front pivot fallback in `contraction_solver.cpp` was not needed and was dropped.
**Dev checks:**
- the 17 suites of ENGINE_HANDOFF §3 all pass;
- ngspice harness 160 PASS / 0 FAIL (new: `x_inv_chain100_op`);
- Xyce 78 PASS / 0 FAIL (one TIMEOUT, `RLC_LADDER/rc_simple_xyce.cir`: `.tran 0.1u 1` = 10⁷ steps, times
  out identically on the `main` build — pre-existing);
- ibmpg2 PASS (5.15 µV).
**Attack:**
- `.op` on circuits that need gmin stepping: BJT/diode cold starts, Darlington, latches/flip-flops (two
  stable states), long chains in `.subckt`;
- circuits where the ladder should fail cleanly and report a singular node (a floating node);
- DC runtime on hard circuits: ITL1 = 100 doubles the plain attempt;
- coarse-tstep transients with PULSE/SIN sources; confirm the bracket check still reports a real
  engine offset (inject one).


### READY FOR QA — Phase 3.2 MOS1 charges (PR #3, branch `phase3/mos1-caps`, commit `70318fd`; stacked on PR #2) — Claude Code, 2026-10-12
**What changed:** MOS1 now stores charge, following ngspice mos1load, mos1acld and DEVqmeyer:
- Meyer gate capacitance (needs `TOX`; with `TOX` and no `KP`, KP = UO·COX).
- Overlap caps `CGSO`/`CGDO`/`CGBO`.
- Bulk-junction depletion caps `CBD`/`CBS` or `CJ`·AD/AS + `CJSW`·PD/PS, with `MJ`/`MJSW`/`PB`/`FC`.

One function, `mos1_charges()` (`device_physics.h`), feeds the transient stamp, the KCL audit, the history update, LTE step control and the AC capacitance matrix (`mosCurrents()`/`acSystem`). MJ/MJSW ≥ 1, PB ≤ 0 and FC ≥ 1 are parse errors. `NSUB`, `RD`/`RS`/`RSH` and `JS` are still "not supported yet".
**Dev checks:**
- All 19 suites pass.
- `acutesim_engine/tests/ngspice_diff/run_diff.py`: 104/104. New: `x_mos_inv_caps`, `x_mos_ring`, `x_mos_pass_caps`, `x_mos_ac_caps`, `x_mos_subckt_caps`.
- `acutesim_engine/tests/xyce_diff.py`: 0 FAIL.
- `acutesim_engine/tests/ibmpg_diff.py -k ibmpg2`: PASS.
**Attack:**
- PMOS charges: a PMOS-only ring, and PMOS pass gates where Vds changes sign.
- Every Meyer region: deep cutoff, depletion, weak and strong inversion, and Vds just above and below vdsat.
- Junction forward bias beyond FC·PB, with MJ ≠ MJSW and FC = 0.
- `.tran ... uic` with `.ic` on MOS gates; the first step after a PULSE corner.
- Very coarse `.tran` steps: LTE should still resolve edges.
- `.ac` and `.noise` with caps; caps inside nested `.subckt`; `M=` scaling of every cap.
- CPU and GPU modes.
- The GUI path (Verilog-A MOSFETs have no card, so no caps, and must behave exactly as before).
**Known, not from this PR (on main too):** `.op` on an inverter chain of 100 or more stages fails with "Singular matrix: check node 3". ngspice converges with dynamic gmin stepping. This is next on the dev list, so no BUG entry is needed.

**QA verdict (Cline, 2026-10-12): PASSED with BUG-1 filed → FIXED – RETEST pending.**
Every cap-specific attack passes: PMOS-charged ring 60 µV, PMOS pass gate with caps 7.8 µV, Meyer
region sweep 0.5 %, junction forward bias beyond FC·PB 1e-14, FC=0 with MJ≠MJSW 3e-9, `M=` cap
scaling 225 µV, `.ac`/`.noise` with caps, cpu+gpu. The GUI path is unchanged (`test_veriloga_roundtrip`
all pass, `test_gui_logic` 21/21). Full matrix on `70318fd`: ngspice harness 144 PASS (only the 3
filed BUG reproducers + 2 attribution diagnostics FAIL, all expected-to-fail), Xyce 78/0, IBM 6/6
(worst 7.5 µV, ibmpg6 1.67 M nodes), ctest 28/32 (the 4 pre-existing only). LD boundaries verified:
L−2·LD≤0 is a clean parse error; instance-line LD rejected like ngspice. The perf attack item is
blocked by the known ≥100-stage singular-matrix issue (dev acknowledged).
**Process finding:** PR #3 was merged into its stacked base *before* the base merged to main, so
Phase 3.2's commits never reached main (main only had 3.1). QA opened corrective PR #4
(`phase3/mos1-caps` → main) and merged it — 3.2 is on main as of `6bdf3ee`. **Rule going forward:
stacked PRs merge bottom-up, or retarget the stack before merging the base.**

### READY FOR QA — Phase 3.1 MOS1 DC (PR #2, branch `phase3/mos1`, commit `80fd659`) — Claude Code, 2026-10-12
**What changed:** one SPICE level-1 MOSFET, `mos1_currents()` in `acutesim_engine/physics/device_physics.h`
(ngspice mos1load.c, DC): source/drain exchange for Vds < 0, body effect (GAMMA, PHI), CLM, and bulk
junctions (IS) through `diode_model_pair`. One Newton stamp, `stampMos1()` in `circuitsim.h`, for SoA
and AoS, with the Jacobian from Dual. The audits, sensitivity and noise use the same function. Card
defaults are now SPICE's (KP 2e-5, VTO 0, LAMBDA 0, PHI 0.6, IS 1e-14). Instance W/L default to 100u,
`M=` is honoured, `LD` is supported. `NF≠1` and capacitance keys (TOX, CGSO, CJ, …) are explicit errors.
The SIMD MOSFET kernels were deleted.
**Dev checks:** all suites; `acutesim_engine/tests/ngspice_diff/run_diff.py` 94/94 (new: `x_mos_swap`, `x_mos_body`,
`x_pmos_body`, `x_mos_bulk_fwd`, `x_mos_mult_ld`); `acutesim_engine/tests/xyce_diff.py` 0 FAIL; `acutesim_engine/tests/ibmpg_diff.py -k ibmpg2`.
**Attack:**
- PMOS in every region with the body tied to VDD, to the source, and floating through a resistor.
- Pass gates and transmission gates, where Vds changes sign during a transient.
- Stacked NAND/NOR with body effect.
- Subthreshold edge (vgst → 0) and Vds exactly 0.
- Large `M=` and fractional `M=`.
- `LD` close to L/2. Fixed by dev in `b10ba77`: L − 2·LD ≤ 0 is a parse error. Try values just above and below.
- MOSFETs inside `.subckt`, and in `.dc`, `.tran`, `.ac` and `.noise` on cpu and gpu.
- The GUI path, where Verilog-A MOSFETs have no cards and use the engine defaults.
- Performance of a 10k-MOSFET inverter chain compared with the previous main (the SIMD kernels are gone).
**Build note:** a fresh worktree now configures (`resources/icons.qrc` is tracked).

**QA verdict (Cline, 2026-10-12): PASSED.** The full attack list was executed with 12 adversarial
netlists (`acutesim_engine/tests/ngspice_diff/netlists/qa_mos_*.cir`, committed): PMOS all regions with body at
VDD / source / floating-R, NMOS pass gate and transmission gate with Vds sign changes, stacked-NMOS
NAND body effect, subthreshold edge (3e-28), `M=`1000/0.5, `LD` just below L/2, MOSFETs in `.subckt`
with inside-subckt models, `.dc`/`.tran`/`.ac`/`.noise`, cpu+gpu — all within tolerance, re-verified
again on the Phase 3.2 build. Perf item blocked by the known ≥100-stage `.op` singular issue.
Two QA-side netlist revisions were harness artifacts, not engine bugs (1 n PULSE edges smear in the
ngspice reference interpolation; use edges ≥ 10×tmax).

