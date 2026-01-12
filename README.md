Project AcuteSim - Cross-platform Qt6 + CMake

This repo is configured to build on macOS and Windows (MSVC) using CMake.

Quick start (macOS Homebrew Qt):
1. Install Qt: `brew install qt` or use the Qt Online Installer and install an appropriate Qt6 kit.
2. Configure and build with CMake presets:
   - `cmake --preset macos-clang`
   - `cmake --build out/build/macos-clang`

Quick start (Windows MSVC):
1. Install Visual Studio with the "Desktop development with C++" workload and the MSVC x64 toolset.
2. Install Qt via the Qt Online Installer (choose the MSVC kit) or use vcpkg.
3. Configure and build (example):
   - `cmake --preset windows-msvc -DQt6_DIR="C:/Qt/6.x/msvcYYYY_64/lib/cmake/Qt6Core"`
   - `cmake --build out/build/windows-msvc`

Notes for developers:
- Do not commit machine-specific files (use the provided `.gitignore`).
- If you need to set local-only CMake variables, create `CMakeUserPresets.json` (not committed).
- For CI, see `.github/workflows/ci.yml` which demonstrates a simple multi-platform build.
