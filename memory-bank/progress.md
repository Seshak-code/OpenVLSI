# Progress

**Snapshot: 2026-10-12, by Cline** (see `docs/claude-archive/session-log.md`
for the full session history and `ENGINE_HANDOFF.md` §0–§5 for engine detail).

## What works (verified by running it today)

- Clean build (exit 0) after cmake reconfigure; tree clean; branch pushed
  (`f98f835`).
- ngspice differential harness: **82/83 PASS** (`aac_10_bridge` is the known
  junction-gmin task; `x_pwl_unsupported` correctly errors "not supported yet").
- Full ctest: **28/32** — the 4 failures are pre-existing (identical at
  baseline `3453182`).
- Phase 2 analysis parity: `.dc` sweeps (8 netlists match ngspice), transient
  from DC solution with `.ic`/`uic`, PULSE/SIN corner stepping, real AC + noise
  (adjoint, per-frequency), shared `CircuitSim` entry for session and GUI.
- GUI Verilog-A transport: PULSE/SIN/AC mag+phase round-trip with per-field
  regression checks (`test_veriloga_roundtrip`).
- New harnesses ready: `tests/ibmpg_diff.py` (IBM power grids, data in
  `datasets/`), `tests/xyce_diff.py` (Xyce regression subset).

## What was just done (2026-10-12, commits b85e404…f98f835)

1. `refactor(engine)`: legacy TN/MPO/Schur/sparse-LU stack deleted (~40k lines).
2. `feat(engine)`: Phase 2 parity + fail-closed parser + dataset harnesses.
3. `feat(gui)`: Verilog-A transport + sweep parameter refactor.
4. `docs`: handoff/roadmap records + the tests/-gitignore trap documented.
5. Cross-agent bridge installed: `.clinerules/` rules, `memory-bank/`,
   `CLAUDE.md` auto-import of `activeContext.md`, Claude→Cline and
   Cline→Claude delegation commands, `docs/claude-archive/` (machine-local
   memory preserved in-repo).

## What's left (order per §0)

1. Junction gmin (fixes `aac_10_bridge`).
2. Wire the golden-dataset harnesses into regular verification.
3. The two queued GUI issues.
4. Roadmap Phase 3 — see `Roadmap.md` → "🎯 END-TO-END GOAL & ENGINE SCALE PLAN".
