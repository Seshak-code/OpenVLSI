# Active Context

**Last session: Claude Code (dev), 2026-10-15 — Phase 3 close-out, Phase 4 GPU, ordering**

**QA is back with Cline/Gemini (owner, 2026-10-15).** This replaces the 2026-10-13 rule that Claude Code tests everything itself. Pick up the READY FOR QA entry at the top of `memory-bank/qa-queue.md`. QA does not edit engine source.

Open PRs:
- **TreeSPICE #8 (`phase3/pdk`):** Phase 3 close-out with real PDKs (IHP, sky130) and the QA round 3 fixes.
- **OpenVLSI #12 (`phase3/pdk-app`):** the submodule bump plus golden-DB entries updated by derived amounts. Merge #8 first.
- **TreeSPICE #9 (draft, `phase4/gpu`):** FP32 GPU fronts with FP64 refinement. The GPU is 2–7× faster on large fronts but did not close the KLU gap.

**In progress** (Claude Code, `phase4/gpu`, not committed yet; the owner approved it):
- A minimum-degree ordering (AMD) with relaxed supernodes, next to nested dissection. The planner keeps whichever plan is cheaper.
- First numbers: TreeSPICE is now within 1.4–5× of KLU on small circuit matrices, down from 100–2000×, and faster than KLU on ASIC_100k, G2_circuit, rajat15 and scircuit.
- **Next:**
  - the per-front cost model for choosing between the two plans;
  - faster AMD planning on meshes;
  - then the owner's GPU direction: CPU symbolic, and batched per-level small fronts on the GPU (superposition / extend-add on the device), resident on the GPU for dGPUs.

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
