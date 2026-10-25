#!/usr/bin/env python3
"""
Engine Isolation Enforcement — Bidirectional Check
===================================================
Direction 1 (Engine → Qt):  Files in engine dirs must NOT import Qt headers or GUI types.
Direction 2 (GUI → Engine): Files in gui/ must NOT include deep engine internals
                             (tensors, solvers, math, infrastructure, physics, autodiff)
                             directly — they must go through engine_api/ or
                             compute/orchestration/ (the service interface layer).
"""

import os
import re
import sys

# ─── Direction 1: Engine dirs must be Qt-free ──────────────────────────────
ENGINE_DIRS = [
    'acutesim_engine',   # NEW canonical engine location
    'compute',           # orchestration layer still here
    'engine_api',
]

FORBIDDEN_INCLUDES_IN_ENGINE = [
    re.compile(r'^\s*#\s*include\s*[<"]\s*Qt'),
    re.compile(r'^\s*#\s*include\s*[<"]\s*gui/'),
    # Qt lowercase headers: "qstring.h", "qobject.h" etc.
    # Must NOT have underscores (which would match local files like "quick_analysis.h").
    re.compile(r'^\s*#\s*include\s*[<"]\s*q[a-zA-Z]+\.h'),
]

FORBIDDEN_TYPES_IN_ENGINE = [
    'QWidget', 'QObject', 'QString', 'QVector', 'QMap', 'QVariant', 'SIGNAL', 'SLOT',
]

# Engine files that are ALLOWED to use Qt (conditionally guarded, or transitional)
ENGINE_QT_ALLOWLIST = {
    # corner_config.h: Qt types are inside #if defined(QT_CORE_LIB) guards
    # Valid: engine builds without Qt define, GUI gets Qt helpers when linked with Qt
    'acutesim_engine/physics/corner_config.h',
}

# ─── Direction 2: GUI dirs must NOT include deep engine internals ───────────
GUI_DIRS = ['app', 'oadb', 'cmd', 'mcp', 'flow', 'soc']  # the app's code: the engine's public API only

# Patterns that flag a gui/ file bypassing the engine_api/ contract.
# compute/orchestration/ (DTOs, drivers, gRPC client) is ALLOWED from GUI —
# it is the service-interface layer designed for GUI consumption.
# Block the deep implementation layers only.
ENGINE_INTERNAL_INCLUDE_PATTERNS = [
    # The engine's internals: GUI must NEVER bypass engine_api/
    re.compile(r'^\s*#\s*include\s*[<"]acutesim_engine/physics/'),
    re.compile(r'^\s*#\s*include\s*[<"]acutesim_engine/solvers/'),
    re.compile(r'^\s*#\s*include\s*[<"]acutesim_engine/math/'),
    re.compile(r'^\s*#\s*include\s*[<"]acutesim_engine/tensors/'),
    re.compile(r'^\s*#\s*include\s*[<"]acutesim_engine/infrastructure/'),
    re.compile(r'^\s*#\s*include\s*[<"]acutesim_engine/autodiff/'),
    re.compile(r'^\s*#\s*include\s*[<"]acutesim_engine/netlist/'),
    re.compile(r'^\s*#\s*include\s*[<"]acutesim_engine/internal/'),
    # Relative back-traversal
    re.compile(r'^\s*#\s*include\s*"\.\./(\.\./)*acutesim_engine/(physics|solvers|math|tensors|infrastructure|autodiff|netlist|internal)/'),
]

# Allowlist: files PERMITTED to include engine internals (adapter pattern).
ENGINE_INTERNAL_ALLOWLIST = set()

# ─── Helpers ────────────────────────────────────────────────────────────────
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


def _strip_comments(lines):
    """Yield (line_num, code_part) with block/line comments removed."""
    in_block = False
    for line_num, line in enumerate(lines, 1):
        stripped = line.strip()
        if in_block:
            if '*/' in stripped:
                in_block = True
                # Reset after end-of-block
                idx = stripped.index('*/')
                rest = stripped[idx + 2:]
                in_block = '/*' in rest
            continue
        if '/*' in stripped:
            idx = stripped.index('/*')
            rest = stripped[idx + 2:]
            if '*/' in rest:
                # Block starts and ends on same line — treat as comment, emit pre-block part
                code = line[:line.index('/*')]
                yield line_num, code
                continue
            in_block = True
            code = line[:line.index('/*')]
            yield line_num, code
            continue
        if stripped.startswith('//'):
            continue
        code = line.split('//')[0]
        yield line_num, code


def check_engine_file(filepath, rel_path):
    """Direction 1: engine-side Qt-freedom check."""
    if rel_path in ENGINE_QT_ALLOWLIST:
        return []
    violations = []
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        for line_num, code in _strip_comments(lines):
            for pattern in FORBIDDEN_INCLUDES_IN_ENGINE:
                if pattern.search(code):
                    violations.append(f"L{line_num}: Forbidden include: {code.strip()}")
            words = re.findall(r'\b\w+\b', code)
            for word in words:
                if word in FORBIDDEN_TYPES_IN_ENGINE:
                    violations.append(f"L{line_num}: Forbidden Qt type: {word}")
    except Exception as e:
        print(f"Warning: Could not read {filepath}: {e}")
    return violations


def check_gui_file(filepath, rel_path):
    """Direction 2: GUI-side layering check (must not bypass engine_api/)."""
    if rel_path in ENGINE_INTERNAL_ALLOWLIST:
        return []
    violations = []
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        for line_num, code in _strip_comments(lines):
            for pattern in ENGINE_INTERNAL_INCLUDE_PATTERNS:
                if pattern.search(code):
                    violations.append(
                        f"L{line_num}: GUI file includes engine internal directly: {code.strip()}"
                    )
    except Exception as e:
        print(f"Warning: Could not read {filepath}: {e}")
    return violations


def run_check():
    print("─── Engine Isolation Enforcement (Bidirectional) ───")
    total = 0

    # ── Direction 1 ──────────────────────────────────────────────────────────
    print("\n[1/2] Engine dirs → Qt-free check")
    d1_clean = True
    for engine_dir in ENGINE_DIRS:
        dir_path = os.path.join(PROJECT_ROOT, engine_dir)
        if not os.path.exists(dir_path):
            continue
        for root, _dirs, files in os.walk(dir_path):
            for fname in files:
                if fname.endswith(('.h', '.cpp', '.hpp', '.cc')):
                    fp = os.path.join(root, fname)
                    rel = os.path.relpath(fp, PROJECT_ROOT).replace(os.sep, '/')  # the allowlists use '/'
                    violations = check_engine_file(fp, rel)
                    if violations:
                        print(f"  ❌ {rel}:")
                        for v in violations:
                            print(f"       {v}")
                        total += len(violations)
                        d1_clean = False
    if d1_clean:
        print("  ✅ All engine directories are Qt-free (or allowlisted).")

    # ── Direction 2 ──────────────────────────────────────────────────────────
    print("\n[2/2] GUI dirs → no direct deep-engine includes")
    d2_clean = True
    for gui_dir in GUI_DIRS:
        dir_path = os.path.join(PROJECT_ROOT, gui_dir)
        if not os.path.exists(dir_path):
            continue
        for root, _dirs, files in os.walk(dir_path):
            for fname in files:
                if fname.endswith(('.h', '.cpp', '.hpp', '.cc')):
                    fp = os.path.join(root, fname)
                    rel = os.path.relpath(fp, PROJECT_ROOT).replace(os.sep, '/')  # the allowlists use '/'
                    violations = check_gui_file(fp, rel)
                    if violations:
                        print(f"  ❌ {rel}:")
                        for v in violations:
                            print(f"       {v}")
                        total += len(violations)
                        d2_clean = False
    if d2_clean:
        print("  ✅ All GUI directories respect the engine_api/ boundary.")

    print()
    if total > 0:
        print(f"Total violations: {total}")
        return False

    print("✅ Engine isolation check passed (both directions).")
    return True


if __name__ == "__main__":
    if not run_check():
        sys.exit(1)
    sys.exit(0)
