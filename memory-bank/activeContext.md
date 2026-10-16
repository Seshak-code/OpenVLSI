# Active Context

**Last session: Claude Code (dev), 2026-10-16 — Phase 4 done (TreeSPICE PR #9), Phase 5 gate met (OpenVLSI `phase5/soc`)**

**QA hand-off (owner, 2026-10-15/16):**
- **Gemini/Cline** takes the Phase 4 entry in `memory-bank/qa-queue.md`.
- **A Sonnet 5.5 session** takes the Phase 5 entry once Gemini closes Phase 4.
- QA does not edit engine or GUI source.

PRs:
- **TreeSPICE #8 and OpenVLSI #12:** merged.
- **OpenVLSI #13:** docs, the QA hand-off.
- **TreeSPICE #9 (`phase4/gpu`, ready for review):** Phase 4.
  - The ordering closes the KLU gap: within 1.1–3× on the small matrices, faster on the large ones.
  - Parallel device evaluation and bypass: a BSIM4 chain transient runs 3.8× faster.
  - 1e5-stage circuits now run (they died at 133 GB).
  - PWL sources.
- **OpenVLSI `phase5/soc`:** the reference SoC.
  - An RV32I model, an SRAM characterised by TreeSPICE, and the UART line co-simulated in SPICE.
  - Simulation → Run Reference SoC (Hello World).
  - The engine submodule is bumped to `6b40c10`. Merge #9 first.

Open after Phases 4 and 5:
- Device evaluation on the GPU (OSDI models are host code).
- Isomorphism reuse beyond bypass, and multirate integration.
- Lockstep, bidirectional co-simulation.
- The Yosys/OpenROAD design-flow UI (needs the tools installed).

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
