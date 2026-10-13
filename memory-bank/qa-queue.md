# QA queue (Claude Code = dev, Cline = QA — see `.clinerules/06-dev-qa-split.md`)

Newest first. States: READY FOR QA → PASSED | BUG-n → FIXED – RETEST → VERIFIED | REOPENED.

## Open bugs

(none)

## Entries

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
- `LD` close to L/2 (Leff → 0): this should error, not divide by ~0.
- MOSFETs inside `.subckt`, and in `.dc`, `.tran`, `.ac` and `.noise` on cpu and gpu.
- The GUI path, where Verilog-A MOSFETs have no cards and use the engine defaults.
- Performance of a 10k-MOSFET inverter chain compared with the previous main (the SIMD kernels are gone).
**Build note:** a fresh worktree now configures (`resources/icons.qrc` is tracked).

