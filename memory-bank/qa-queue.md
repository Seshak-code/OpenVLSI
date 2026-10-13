# QA queue (Claude Code = dev, Cline = QA — see `.clinerules/06-dev-qa-split.md`)

Newest first. States: READY FOR QA → PASSED | BUG-n → FIXED – RETEST → VERIFIED | REOPENED.

## Open bugs

**BUG-1 — `.ic` + `uic`: MOS charge initialisation is wrong** (filed by Cline, 2026-10-12, from the Phase 3.2 QA pass) — **FIXED – RETEST** (harness, not engine: see the `fix/qa-bugs` entry below)
- Reproducer: `tests/ngspice_diff/netlists/qa_mos_uic.cir` (expected to FAIL until fixed). Control `qa_ctrl_rc_uic.cir` (same circuit, plain RC, no MOS) **passes** at 0.43 mV — so the engine's `.ic`/`uic` handling itself matches ngspice; the divergence is MOS-specific (MOS charge history under `uic`, likely initialised from zero instead of the `.ic` node voltages).
- Numbers: worst 122 mV / 2.5 % (tolerance is max(5 mV, 2 % of swing)), systematic at 10+ sample points, identical in cpu and gpu.
- Severity: wrong answer (transient), MOS1-charges-related — **from Phase 3.2**.

**BUG-2 — two-level `.subckt` hierarchy solves to a wrong DC level** (filed by Cline, 2026-10-12; pre-existing — fails without any capacitance keys, so it predates Phase 3.2) — **FIXED – RETEST** (harness, not engine)
- Reproducer: `tests/ngspice_diff/netlists/qa_diag_nest_nocaps.cir` (X inside a `.subckt`, plain MOS1, no caps; expected to FAIL). With caps: `qa_mos_nest.cir` (1.38 V).
- Numbers: the output node is **flat at a wrong level** — engine 1.18863 V vs ngspice 2.328 V, constant across the transient (1.14 V / 23 %). Single-level hierarchy passes (`qa_mos_subckt.cir`, 26 mV).
- Note: *nested `.subckt definitions* are a separate, documented parse error ("nested .subckt definitions are not supported") — that one is fail-closed, not a bug.
- Severity: wrong answer (DC operating point) — worst of the three.

**BUG-3 — coarse `.tran` tstep: LTE/corner sub-stepping leaves large mid-edge error** (filed by Cline, 2026-10-12; pre-existing — fails without caps too) — **FIXED – RETEST** (harness, not engine)
- Reproducer: `tests/ngspice_diff/netlists/qa_diag_coarse_nocaps.cir` (`.tran 25u`, plain MOS; expected to FAIL). With caps: `qa_mos_coarse.cir` (646 mV).
- Numbers: up to 452 mV / 8.9 % mid-edge with **no** capacitance keys (646 mV / 12.9 % with); 140+ sample points; identical cpu/gpu. Fine tsteps (0.5–2 u) pass at ≤ 26 mV — the failure appears only when the engine's own LTE/corner logic must sub-step. The reference resolves the edges (tmax = tstep/10).
- Severity: wrong answer (transient accuracy at coarse steps).

## Entries

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
- `tests/ngspice_diff/run_diff.py`: 104/104. New: `x_mos_inv_caps`, `x_mos_ring`, `x_mos_pass_caps`, `x_mos_ac_caps`, `x_mos_subckt_caps`.
- `tests/xyce_diff.py`: 0 FAIL.
- `tests/ibmpg_diff.py -k ibmpg2`: PASS.
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
**Dev checks:** all suites; `tests/ngspice_diff/run_diff.py` 94/94 (new: `x_mos_swap`, `x_mos_body`,
`x_pmos_body`, `x_mos_bulk_fwd`, `x_mos_mult_ld`); `tests/xyce_diff.py` 0 FAIL; `tests/ibmpg_diff.py -k ibmpg2`.
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
netlists (`tests/ngspice_diff/netlists/qa_mos_*.cir`, committed): PMOS all regions with body at
VDD / source / floating-R, NMOS pass gate and transmission gate with Vds sign changes, stacked-NMOS
NAND body effect, subthreshold edge (3e-28), `M=`1000/0.5, `LD` just below L/2, MOSFETs in `.subckt`
with inside-subckt models, `.dc`/`.tran`/`.ac`/`.noise`, cpu+gpu — all within tolerance, re-verified
again on the Phase 3.2 build. Perf item blocked by the known ≥100-stage `.op` singular issue.
Two QA-side netlist revisions were harness artifacts, not engine bugs (1 n PULSE edges smear in the
ngspice reference interpolation; use edges ≥ 10×tmax).

