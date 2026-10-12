# Active Context

**Last session: Cline, 2026-10-12** (migration/handoff session; the code work
below was authored by the Claude Code sessions of the same day and verified,
committed and pushed by Cline — `b85e404`…`f98f835`).

**This file is the live cross-agent state. Claude Code auto-imports it at every
session start (`@memory-bank/activeContext.md` in CLAUDE.md). Update it at
every session end and stamp your agent.**

## Where things stand

- **Branch:** `engine/spice-correctness` — pushed, tree clean. The third
  session's uncommitted stretch is committed and verified (clean build; ngspice
  82/83, only `aac_10_bridge`; full ctest 28/32 with 4 pre-existing failures —
  identical at baseline `3453182`).
- Done today: legacy TN/MPO/Schur/sparse-LU stack deleted (~40k lines); Phase 2
  analysis parity (real AC/noise, `.dc` sweeps, transient ICs/uic, PULSE/SIN
  corner stepping); fail-closed parser; GUI Verilog-A transport round-trips
  PULSE/SIN/AC; IBM power-grid datasets in `datasets/` + harnesses
  `tests/ibmpg_diff.py`, `tests/xyce_diff.py`.

## Next, in order (from `ENGINE_HANDOFF.md` §0)

1. **Junction gmin** — add gmin to diode/BJT junction stamps (SoA kernels:
   generic/NEON/AVX2/AVX-512, AoS, both audits), drop node gmin at convergence.
   Fixes `aac_10_bridge`. AC already drops it.
2. **Golden datasets into the harness** — wire `tests/ibmpg_diff.py` /
   `tests/xyce_diff.py` into regular verification; IBM power-grid for solver
   scale, Xyce regression subset for devices.
3. **GUI issues queued as separate tasks** (from session 75afa6b2).
4. **Roadmap Phase 3.**

## Work around (known, do not chase)

- `aac_10_bridge` ngspice FAIL — the junction-gmin task above.
- ctest pre-existing failures: `test_gpu_parity`, `test_kernel_dispatch`,
  `test_numerical_intelligence`, `test_grpc_orchestrator` (identical at
  baseline).
- `mcSeed` never applied (every Monte Carlo run identical); temperature: Vt
  fixed at 25.85 mV vs `ambient_temp_K` default 300.15 K; MOSFET level-1 gaps
  (no Vds<0 swap, no body effect); `.include/.lib/.options/.ic/.print` silently
  ignored. Full backlog: `ENGINE_HANDOFF.md` §6.

## Agent coordination

Both Claude Code and Cline work this repo. Session start: sweep
(`.clinerules/00-authoritative-source.md`). Session end: update this file +
`ENGINE_HANDOFF.md` §0, stamp, commit, push (`.clinerules/01-collab-handoff.md`).
Never assume another agent's "isolated" background task was actually isolated —
check `git status` first.
