# Progress

**Snapshot: 2026-10-12 (evening), by Cline** (see `docs/claude-archive/session-log.md`
for the full session history and `ENGINE_HANDOFF.md` §0–§5 for engine detail).

## What works (verified by running it)

- **ngspice differential harness: 84 PASS / 0 FAIL** (cpu+gpu) — `aac_10_bridge`
  fixed by junction gmin (was 82/83). `x_pwl_unsupported` correctly errors
  "not supported yet".
- Clean build; full ctest **28/32** — the 4 failures are pre-existing (identical
  at baseline `3453182`); test_regression 4/4, test_engine_fixes,
  test_ad_integration 19/19, test_std_cells all pass.
- Phase 2 analysis parity: `.dc` sweeps, transient `.ic`/`uic`, PULSE/SIN corner
  stepping, real AC + noise, shared `CircuitSim` entry for session and GUI.
- GUI Verilog-A transport: PULSE/SIN/AC round-trip with per-field regression
  checks.
- Harnesses ready: `tests/ibmpg_diff.py` (IBM power grids, data in `datasets/`),
  `tests/xyce_diff.py`.

## Junction gmin (2026-10-12, Cline — see `ENGINE_HANDOFF.md` §0)

gmin moved from node-to-ground to across-each-junction (diode/BJT/zener models
in `device_physics.h` — one edit feeds AoS stamps and both audits — plus the
generic/NEON/AVX2/AVX-512 SoA diode kernels and generic/NEON BJT kernels:
`g = g_base + GMIN` additive, `i += GMIN·v_j`). Node gmin dropped at the DC
ladder's final step and removed from transient. **Awaiting Claude Code review.**

## What's left (order per §0)

1. Wire the golden-dataset harnesses into regular verification.
2. Roadmap Phase 3 — see `Roadmap.md` → "🎯 END-TO-END GOAL & ENGINE SCALE PLAN".

(The two queued GUI issues were already fixed — Verilog-A transport in `c7faeb0`,
the two GUI tests in `b1d9fd2`; both verified passing 2026-10-12, offscreen Qt env.)
