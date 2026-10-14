# Active Context

**Last session: Claude Code (dev), 2026-10-14 — Phase 3 devices (diode + Gummel–Poon BJT)**

**Owner's rule (2026-10-13), overrides the dev/QA split for now:** Claude Code does ALL testing itself and
does not hand QA to Cline; it tests once per phase, after implementing everything in that phase.

- **Phase 3 devices done** (TreeSPICE PR #6, OpenVLSI companion PR): SPICE diode (RS, CJO, TT, BV, AREA)
  and Gummel–Poon BJT (VAF/IKF/ISE/RB/RC/RE/CJE/CJC/XCJC/CJS/ISS/TF/TR, substrate node); one device path
  (`junctionCurrents` → `stampJunction`); SIMD kernels and KernelDispatcher deleted; ngspice junction
  limiting; convergence fixes. Verified: ngspice 190/0, Xyce 92/0, ibmpg2, OpenVLSI suites + ctest.
- KLU baseline recorded (TreeSPICE PR #5): 3–1,900× slower than KLU on transistor-level matrices; owner
  accepted the gap for now.
- Repos: OpenVLSI (this app) and TreeSPICE (engine, submodule `acutesim_engine/`). Engine changes land in
  TreeSPICE first; then bump the pointer here. New worktrees: `git submodule update --init`.

Next: Phase 3 remainder — BSIM4/BSIM-CMG via OpenVAF/OSDI, `.lib` sections and corners, source stepping.

**This file is the live cross-agent state. Claude Code auto-imports it at every
session start (`@memory-bank/activeContext.md` in CLAUDE.md). Update it at
every session end and stamp your agent.**

## Where things stand

- **Branch:** `main` — **PR #1 merged 2026-10-12 by Cline** (merge commit `43003d5`;
  `engine/spice-correctness` deleted after merge, local + remote). New work starts
  on `main` or a fresh branch off it. Stale leftover: `engine/tensor-mna`
  (8 months old, superseded). Tree clean.
- Done today: legacy TN/MPO/Schur/sparse-LU stack deleted (~40k lines); Phase 2
  analysis parity (real AC/noise, `.dc` sweeps, transient ICs/uic, PULSE/SIN
  corner stepping); fail-closed parser; GUI Verilog-A transport round-trips
  PULSE/SIN/AC; IBM power-grid datasets in `datasets/` + harnesses
  `acutesim_engine/tests/ibmpg_diff.py`, `acutesim_engine/tests/xyce_diff.py`.

## Next, in order (from `acutesim_engine/ENGINE_HANDOFF.md` §0)

1. **Golden datasets into the harness** — wire `acutesim_engine/tests/ibmpg_diff.py` and
   `acutesim_engine/tests/xyce_diff.py` into regular verification; IBM power-grid for solver
   scale, Xyce regression subset for devices.
2. **Roadmap Phase 3.**

(The two queued GUI issues were already fixed — Verilog-A transport in `c7faeb0`,
the two GUI tests in `b1d9fd2`; both verified passing 2026-10-12, offscreen Qt env.)

## Work around (known, do not chase)

- ctest pre-existing failures: `test_gpu_parity`, `test_kernel_dispatch`,
  `test_numerical_intelligence`, `test_grpc_orchestrator` (identical at
  baseline; `test_kernel_dispatch` = stale diode-clamp expectation).
- `mcSeed` never applied (every Monte Carlo run identical); temperature: Vt
  fixed at 25.85 mV vs `ambient_temp_K` default 300.15 K; MOSFET level-1 gaps
  (no Vds<0 swap, no body effect); `.include/.lib` are explicit "not supported yet"
  errors (only output cards `.options/.print/.plot/.save/.probe/.meas/.four` are ignored).
  Full backlog: `acutesim_engine/ENGINE_HANDOFF.md` §6.

## Agent coordination

Both Claude Code and Cline work this repo. Session start: sweep
(`.clinerules/00-authoritative-source.md`). Session end: update this file +
`acutesim_engine/ENGINE_HANDOFF.md` §0, stamp, commit, push (`.clinerules/01-collab-handoff.md`).
Never assume another agent's "isolated" background task was actually isolated —
check `git status` first.
