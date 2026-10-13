# System Patterns

The master interconnection map is **`context.md`** — read it in full before
multi-module work. This file only adds the rules that most often bite.

## Dependency direction (strict, never violate)

```
gui/ → ai_api/ → compute/ → physics/ → autodiff/
                 ↓            ↓
           components/ ←────┘
```

Dependencies flow **downward only**. Lower layers never import from higher
layers. Verify with `python3 scripts/check_layering.py` and
`python3 tools/check_engine_isolation.py`.

## Patterns that most often bite

- Every folder has a `flow.md` (local logic + guardrails + SME checklists) —
  read it before touching the folder.
- AI actions go through `AiActionEnvelope`, never direct `QGraphicsScene`.
- gRPC/orchestration code guarded by `#ifdef ACUTESIM_ENABLE_ORCHESTRATION`.
- FP determinism mandatory (`-fno-fast-math`, `/fp:strict`, `FE_TONEAREST`).
- Solver tier routing and the fail-closed convergence/parser behavior:
  `ENGINE_HANDOFF.md` §2 (architecture as of now).

**Do not duplicate `context.md`'s maps here — it drifts. Read the source.**
