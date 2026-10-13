# Dev / QA split (owner's rule, 2026-10-12)

**Claude Code develops. Cline does QA and bug testing.** Neither does the other's job.

## Claude Code (development)

- Implements Roadmap items (currently Phase 3), with the unit checks that prove each
  change (`tests/test_engine_fixes.cpp`, harness netlists for new behavior).
- Before handing to QA: clean build, every suite in `ENGINE_HANDOFF.md` §3, ngspice
  harness, `tests/xyce_diff.py` (0 FAIL), `tests/ibmpg_diff.py -k ibmpg2`.
- Commits, pushes, then adds a **READY FOR QA** entry to `memory-bank/qa-queue.md`:
  commit, feature, what changed, what to attack, how to reproduce Claude's checks.
- Fixes every **BUG** Cline files, links the fix commit in the entry, and moves it to
  **FIXED – RETEST**.

## Cline (QA)

- Picks up READY FOR QA entries. Does NOT edit engine/GUI source (`acutesim_engine/`,
  `compute/`, `components/`, `gui/`, `engine_api/`): findings go to the queue, fixes
  come from Claude Code.
- May add/modify tests: new harness netlists in `tests/ngspice_diff/netlists/`
  (ngspice is the oracle), checks in test files, Xyce/IBM classifications in
  `tests/xyce_diff.py`. New files under `tests/` need `git add -f`.
- For each entry: run the full verification matrix, then go adversarial — edge
  values (0, negative, huge), polarity (NMOS/PMOS, NPN/PNP), hierarchy (inside
  `.subckt`), every analysis (op/dc/tran/ac/noise), cpu and gpu modes, the GUI path
  (Verilog-A transport) where relevant.
- Files each defect as **BUG-n** in the queue: netlist (or test) that reproduces it,
  engine vs ngspice numbers, severity (wrong answer > crash/hang > error message).
  Commit the reproducer test (expected-to-fail checks are fine; say so).
- Verifies FIXED – RETEST entries; closes them as **VERIFIED** or reopens.

## Queue states

READY FOR QA → (QA) → PASSED | BUG-n filed → (dev) FIXED – RETEST → (QA) VERIFIED | REOPENED

Both agents still follow `01-collab-handoff.md` (stamp, update §0 / activeContext,
commit, push) — the queue is the only hand-off channel between dev and QA.
