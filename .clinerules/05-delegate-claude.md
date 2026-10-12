# Delegating to Claude Code (subagent pattern)

Claude Code can be run headlessly as a one-shot subagent from this repo. Use it
for focused, well-specified tasks (the owner's preference) — especially
analysis/review work where Claude's model choice matters.

## Read-only analysis (default, safe)

```
cd "/Users/seshak/Documents/Projects/Coding Projects/CuteApp" && \
claude -p "<task>" --allowed-tools "Read,Grep,Glob"
```

## With write access (only when the user explicitly approves)

Prefer a dedicated worktree so the main checkout (which the other agent may be
using — collisions are proven history here) stays untouched:

```
git worktree add ../CuteApp-claude-delegation -b claude/delegated && \
claude -p "<task>" --allowed-tools "Read,Write,Edit,Bash(git *),Grep,Glob" -c ../CuteApp-claude-delegation
```
…then merge back per `.clinerules/01-collab-handoff.md` and remove the worktree.

## Rules

1. The prompt must be **self-contained**: absolute file paths, the exact
   symptom/expected behavior, how to verify (build + `ctest` +
   `tests/ngspice_diff/run_diff.py` — see `.clinerules/03-development-workflow.md`).
   There is no shared context window.
2. Read the printed result and report it to the user as a subagent report.
3. **Never let the delegated Claude delegate further** (no recursion, no it
   calling Cline back).
4. Record the delegation in `memory-bank/activeContext.md` (what, where,
   result) per the collaboration protocol.
5. Honest limits: separate billing/context; the result is text, not a structured
   report; it cannot see this conversation.
