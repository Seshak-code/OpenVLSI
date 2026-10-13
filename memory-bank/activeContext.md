# Active Context

**Last session: Claude Code (dev), 2026-10-13 — QA bugs, repo split, headless crash fix, renames**

Owner's rule: **Claude Code develops, Cline does QA** (`.clinerules/06-dev-qa-split.md`; hand-off channel
`memory-bank/qa-queue.md`).

- **Renamed (2026-10-13):** this app is **OpenVLSI** (repo Seshak-code/OpenVLSI, formerly CuteApp), and the
  engine is **TreeSPICE** (Seshak-code/TreeSPICE, formerly Karam; TREE = Tensor Recursive Elimination Engine).
  GitHub redirects the old URLs. Code identifiers (`acutesim::`, `ACUTESIM_`, `acutesim_engine`, the `AcuteSim`
  binary) keep their old names for now. The local folders are still `CuteApp*/` and `Karam/`.
- **Merged:** #6 (QA bugs 1–3 were harness artifacts; ngspice dynamic gmin stepping), TreeSPICE #2 (engine is its
  own repo), #7 (`acutesim_engine/` is the TreeSPICE submodule), TreeSPICE #3 (Dawn procs before the GPU probe).
- **Open for review:** #8 headless segfault fix (bumps the submodule + `test_capabilities_probe`), TreeSPICE #4
  and the OpenVLSI rename PR (branding).
- Engine changes are committed in TreeSPICE first; then the submodule pointer is bumped here. New worktrees
  need `git submodule update --init`.

Next dev increment: diode RS/CJO/TT/BV, then Gummel–Poon BJT.

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
