"""
tools/check_connectivity_isolation.py
======================================
Bidirectional isolation enforcer for the Connectivity Layer.

RULES:
  connectivity/ → Qt headers           ❌  VIOLATION
  connectivity/ → compute/ internals   ❌  VIOLATION
  connectivity/ → physics/ internals   ❌  VIOLATION
  connectivity/ → std::filesystem      ❌  VIOLATION (paths must be DTO-provided)
  connectivity/ → engine_api/          ✅  ALLOWED
  connectivity/ → STL (<...>)          ✅  ALLOWED

Run before every PR merge:
  python3 tools/check_connectivity_isolation.py
"""

import os
import re
import sys
from pathlib import Path

ROOT        = Path(__file__).parent.parent
CONN_DIR    = ROOT / "connectivity"

FORBIDDEN_PATTERNS = [
    # Qt types
    (re.compile(r'#include\s+[<"]Q[A-Z]'), "Qt include"),
    (re.compile(r'\bQObject\b|\bQString\b|\bQVector\b'), "Qt type"),
    # Engine/compute internals
    (re.compile(r'#include.*compute/(solvers|tensors|math|infrastructure)'),
     "compute internal"),
    (re.compile(r'#include.*physics/'), "physics internal"),
    # Filesystem path assumptions
    (re.compile(r'\bstd::filesystem\b'), "std::filesystem direct use"),
    (re.compile(r'\bgetenv\s*\('), "getenv (filesystem assumption)"),
    (re.compile(r'"(?:\./|\.\./).*\.(sch|gds|v|json)"'),
     "hardcoded file extension path"),
]

ALLOWED_OVERRIDES = [
    # engine_api and STL are always OK
    re.compile(r'#include.*engine_api/'),
    re.compile(r'#include\s+<[a-z_]+>'),   # pure STL
]


def strip_comments(text: str) -> str:
    """Remove C/C++ line and block comments."""
    text = re.sub(r'//[^\n]*', '', text)
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    return text


def check_file(path: Path) -> list[str]:
    violations = []
    try:
        raw = path.read_text(encoding="utf-8", errors="ignore")
        text = strip_comments(raw)
        for pattern, label in FORBIDDEN_PATTERNS:
            for match in pattern.finditer(text):
                line_num = raw[:match.start()].count('\n') + 1
                violations.append(
                    f"  {path.relative_to(ROOT)}:{line_num}  [{label}]  {match.group().strip()}"
                )
    except Exception as e:
        violations.append(f"  {path}: ERROR reading file: {e}")
    return violations


def run() -> int:
    if not CONN_DIR.exists():
        print(f"[INFO] No connectivity/ directory found at {CONN_DIR} — skipping check.")
        return 0

    all_violations: list[str] = []
    files_checked = 0

    for ext in ("*.h", "*.cpp", "*.cc", "*.cxx"):
        for f in CONN_DIR.rglob(ext):
            all_violations.extend(check_file(f))
            files_checked += 1

    if all_violations:
        print(f"❌  Connectivity isolation violations ({len(all_violations)}):")
        for v in all_violations:
            print(v)
        return 1
    else:
        print(f"✅  Connectivity isolation: {files_checked} files checked, 0 violations.")
        return 0


if __name__ == "__main__":
    sys.exit(run())
