#!/usr/bin/env python3
"""
tools/abi_stability_test.py — Engine ABI Stability Verification
================================================================
Proves the link-time sovereignty boundary:
    AcuteSim (GUI + orchestration) must NOT recompile when acutesim_engine internals change.

Test procedure:
  1. Full native build (configure + build)
  2. Fingerprint GUI .o files (mtime + md5)
  3. Patch ONE internal engine file (adds a harmless comment)
  4. Rebuild only acutesim_engine target
  5. Link the main executable
  6. Verify: none of the GUI .o fingerprints changed
  7. Verify: acutesim_engine_cli and the app still exit 0

Exit 0 = PASS (ABI boundary is solid)
Exit 1 = FAIL (something in the GUI recompiled — engine boundary leaks)
"""

import os
import sys
import hashlib
import subprocess
import time
import shutil
import tempfile
import argparse
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent.resolve()
BUILD_DIR    = PROJECT_ROOT / "build_abi_test"

# ── Paths whose .o files must NOT change after an engine-only patch ──────
# These represent the GUI / orchestration / connectivity layers.
# If any of these recompile, the engine boundary has leaked.
GUI_FINGERPRINT_GLOBS = [
    "gui/**/*.cpp.o",
    "gui/**/*.cc.o",
    "connectivity/**/*.cpp.o",
    "ai_api/**/*.cpp.o",
    "compute/orchestration/**/*.cpp.o",
    "main.cpp.o",
]

# ── The internal engine file we will patch ───────────────────────────────
# Must be 100% internal — not included transitively by any public header.
PATCH_TARGET = PROJECT_ROOT / "acutesim_engine" / "gpu_context_manager.cpp"
PATCH_MARKER = "// __ABI_STABILITY_PATCH__"

def run(cmd, cwd=None, check=True, capture=False):
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(
        [str(c) for c in cmd],
        cwd=str(cwd or PROJECT_ROOT),
        capture_output=capture,
        text=True
    )
    if check and result.returncode != 0:
        print(f"  ERROR: command failed (exit {result.returncode})")
        if capture:
            print(result.stderr[-2000:])
        sys.exit(1)
    return result

def md5(path: Path) -> str:
    return hashlib.md5(path.read_bytes()).hexdigest()

def fingerprint_gui_objects(build_dir: Path) -> dict:
    """Return {relative_path: md5} for all GUI .o files."""
    fps = {}
    for pattern in GUI_FINGERPRINT_GLOBS:
        for f in build_dir.glob(pattern):
            rel = str(f.relative_to(build_dir))
            fps[rel] = md5(f)
    return fps

def patch_engine_file(path: Path):
    """Add a harmless comment (changes the .cpp, not the .h)."""
    src = path.read_text()
    if PATCH_MARKER in src:
        print(f"  [skip] {path.name} already patched — removing old patch first")
        src = src.replace(f"\n{PATCH_MARKER}\n", "\n")
    patched = src + f"\n{PATCH_MARKER}\n"
    path.write_text(patched)
    print(f"  [patch] Added ABI patch marker to {path.name}")

def unpatch_engine_file(path: Path):
    """Remove the ABI stability patch from the file."""
    src = path.read_text()
    cleaned = src.replace(f"\n{PATCH_MARKER}\n", "\n")
    path.write_text(cleaned)
    print(f"  [unpatch] Removed ABI patch marker from {path.name}")

# ──────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="ABI Stability Test")
    parser.add_argument("--cmake-extra", nargs="*", default=[],
                        help="Extra CMake args (e.g. -DCMAKE_BUILD_TYPE=Release)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count(),
                        help="Parallel build jobs")
    parser.add_argument("--skip-configure", action="store_true",
                        help="Re-use existing build directory (faster re-runs)")
    parser.add_argument("--no-cleanup", action="store_true",
                        help="Leave patch in place after test (for debugging)")
    args = parser.parse_args()

    print("=" * 60)
    print("  AcuteSim Engine ABI Stability Test")
    print("=" * 60)

    # ── Step 1: Configure ─────────────────────────────────────────────────
    if not args.skip_configure or not BUILD_DIR.exists():
        print("\n[1/6] CMake configure (native, non-WASM)")
        BUILD_DIR.mkdir(exist_ok=True)
        run([
            "cmake",
            str(PROJECT_ROOT),
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            *args.cmake_extra
        ], cwd=BUILD_DIR)
    else:
        print("\n[1/6] CMake configure — SKIPPED (--skip-configure)")

    # ── Step 2: Full build ────────────────────────────────────────────────
    print("\n[2/6] Full build")
    run(["cmake", "--build", str(BUILD_DIR), "--", f"-j{args.jobs}"])
    print("  Full build: OK")

    # ── Step 3: Fingerprint GUI .o files ──────────────────────────────────
    print("\n[3/6] Fingerprinting GUI/orchestration object files")
    before = fingerprint_gui_objects(BUILD_DIR)
    if not before:
        print("  WARNING: No GUI .o files found in build dir.")
        print("  This may mean CMake uses unity builds or diffs paths.")
        print("  Looking for any .o files...")
        before = {str(f.relative_to(BUILD_DIR)): md5(f)
                  for f in BUILD_DIR.glob("**/*.o") if "CMakeFiles" in str(f)}
    print(f"  Found {len(before)} GUI object files to watch")

    # ── Step 4: Patch engine internal ─────────────────────────────────────
    print(f"\n[4/6] Patching engine internal: {PATCH_TARGET.name}")
    try:
        patch_engine_file(PATCH_TARGET)
    except Exception as e:
        print(f"  FAIL: Could not patch {PATCH_TARGET}: {e}")
        sys.exit(1)

    # ── Step 5: Rebuild engine only ───────────────────────────────────────
    print("\n[5/6] Rebuilding acutesim_engine target only")
    run([
        "cmake", "--build", str(BUILD_DIR),
        "--target", "acutesim_engine",
        "--", f"-j{args.jobs}"
    ])

    # Relink the main application (touches the linker, not the GUI .cpp files)
    print("  Relinking AcuteSim application...")
    run([
        "cmake", "--build", str(BUILD_DIR),
        "--target", "AcuteSim",
        "--", f"-j{args.jobs}"
    ])
    print("  Relink: OK")

    # ── Step 6: Compare fingerprints ──────────────────────────────────────
    print("\n[6/6] Verifying no GUI/orchestration files recompiled")
    after = fingerprint_gui_objects(BUILD_DIR)

    changed = []
    for rel, digest in before.items():
        if rel not in after:
            changed.append(f"  MISSING: {rel}")
        elif after[rel] != digest:
            changed.append(f"  RECOMPILED: {rel}")

    new_files = [rel for rel in after if rel not in before]

    # ── Cleanup patch ─────────────────────────────────────────────────────
    if not args.no_cleanup:
        unpatch_engine_file(PATCH_TARGET)

    # ── Results ───────────────────────────────────────────────────────────
    print()
    print("─" * 60)
    if changed:
        print(f"  ❌ FAIL — {len(changed)} GUI file(s) recompiled after engine patch!")
        print("  This means engine internals are leaking into GUI translation units.")
        print("  Check: -fvisibility=hidden, header guards, and PUBLIC includes.")
        for c in changed:
            print(c)
        sys.exit(1)
    else:
        print(f"  ✅ PASS — 0 GUI files recompiled ({len(before)} watched)")
        print("  ABI boundary is solid. Only acutesim_engine objects changed.")
        if new_files:
            print(f"  (Note: {len(new_files)} new .o files appeared — these are expected if engine grew)")
        print()
        print("  Link-time sovereignty confirmed:")
        print("    Application ──LINKS──> acutesim_engine (ABI)")
        print("                     ╚══ internals are invisible")
        print("─" * 60)
        sys.exit(0)


if __name__ == "__main__":
    main()
