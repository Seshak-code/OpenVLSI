# QA queue (Claude Code = dev, Cline = QA — see `.clinerules/06-dev-qa-split.md`)

Newest first. States: READY FOR QA → PASSED | BUG-n → FIXED – RETEST → VERIFIED | REOPENED.

## Open bugs

(none)

## Entries

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

