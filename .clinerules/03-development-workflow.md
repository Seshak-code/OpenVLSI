# Development workflow (distilled from the cognitive-foundry-dev agent)

Distilled from `.claude/agents/cognitive-foundry-dev.md` (which stays for Claude
Code) so both agents follow the same plan-first workflow.

## Before implementing

1. **Read `Roadmap.md`** — identify the phase/roadmap item this work supports;
   classify the change (New Feature | Refactor | Bug Fix | Performance |
   Architecture | Documentation).
2. **Read `context.md`** — the master interconnection map. The dependency
   direction `gui/ → ai_api/ → compute/ → physics/ → autodiff/` is **DOWNWARD
   ONLY**; lower layers never import from higher layers.
3. **For every folder you touch, read its `flow.md`** (local logic, guardrails,
   SME checklists — most directories have one).
4. **Plan before code.** If a `flow.md` guardrail conflicts with the approach,
   stop, document the conflict, do not proceed past it silently.

## After implementing (verify by running, per the owner's standing rule)

1. `cmake --build build` then `cd build && ctest`.
   Known **pre-existing** failures (identical at baseline `3453182`, do NOT
   chase them): `test_gpu_parity`, `test_kernel_dispatch`,
   `test_numerical_intelligence`, `test_grpc_orchestrator`.
2. `python3 tests/ngspice_diff/run_diff.py` — expect **82/83 PASS**; only
   `aac_10_bridge` fails (junction gmin, the queued next engine task).
3. Layering touched? `python3 scripts/check_layering.py` and
   `python3 tools/check_engine_isolation.py`.
4. Session end: follow `.clinerules/01-collab-handoff.md` (update §0 +
   `activeContext.md`, stamp, commit, push).
