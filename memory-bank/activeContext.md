# Active Context

**Last session: Claude Code, 2026-10-12 (review of the junction-gmin commit `7332f62`)** —
kept the design, fixed what review found: BJT SoA kernels double-counted GMIN (+ stale floors);
the contraction solver silently "solved" singular systems (floating nodes reported converged with
garbage) — now DC fails with "Singular matrix: check node N" like ngspice, and element-free nodes
(dangling subckt ports) are pinned to 0 V; ladder end/gminSteppingUsed/sensitivity gmin fixed;
vacuous Darlington test fixed. Verified: 19 suites, ngspice 84/84, Xyce 0 FAIL/78 PASS, IBM 6/6.
Details: `ENGINE_HANDOFF.md` §0 (sixth session).

**This file is the live cross-agent state. Claude Code auto-imports it at every
session start (`@memory-bank/activeContext.md` in CLAUDE.md). Update it at
every session end and stamp your agent.**

## Where things stand

- **Branch:** `engine/spice-correctness` — pushed after the sixth session. The third
  session's uncommitted stretch is committed and verified (clean build; ngspice
  82/83, only `aac_10_bridge`; full ctest 28/32 with 4 pre-existing failures —
  identical at baseline `3453182`).
- Done today: legacy TN/MPO/Schur/sparse-LU stack deleted (~40k lines); Phase 2
  analysis parity (real AC/noise, `.dc` sweeps, transient ICs/uic, PULSE/SIN
  corner stepping); fail-closed parser; GUI Verilog-A transport round-trips
  PULSE/SIN/AC; IBM power-grid datasets in `datasets/` + harnesses
  `tests/ibmpg_diff.py`, `tests/xyce_diff.py`.

## Next, in order (from `ENGINE_HANDOFF.md` §0)

1. **Golden datasets into the harness** — wire `tests/ibmpg_diff.py` and
   `tests/xyce_diff.py` into regular verification; IBM power-grid for solver
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
  Full backlog: `ENGINE_HANDOFF.md` §6.

## Agent coordination

Both Claude Code and Cline work this repo. Session start: sweep
(`.clinerules/00-authoritative-source.md`). Session end: update this file +
`ENGINE_HANDOFF.md` §0, stamp, commit, push (`.clinerules/01-collab-handoff.md`).
Never assume another agent's "isolated" background task was actually isolated —
check `git status` first.
