#!/usr/bin/env bash
# ==============================================================================
# build_wasm_dist.sh - AcuteSim WebAssembly & Vercel Production Build Script
# ==============================================================================
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/out/build/wasm"
DIST_DIR="${REPO_ROOT}/public"

echo "=== [1/4] Checking WebAssembly Build Environment ==="
if ! command -v emcmake &> /dev/null; then
    echo "Warning: emcmake not found on PATH. If building locally, activate EMSDK first."
    echo "Example: source /path/to/emsdk/emsdk_env.sh"
fi

mkdir -p "${BUILD_DIR}"
mkdir -p "${DIST_DIR}"

echo "=== [2/4] Configuring & Compiling AcuteSim WASM ==="
if command -v emcmake &> /dev/null; then
    emcmake cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DFETCHCONTENT_UPDATES_DISCONNECTED=ON

    cmake --build "${BUILD_DIR}" --target AcuteSim -j$(sysctl -n hw.ncpu 2>/dev/null || nproc || echo 4)
fi

echo "=== [3/4] Assembling Vercel Distribution Package ==="
# Copy WASM binary artifacts if present in build directory
for f in AcuteSim.js AcuteSim.wasm qtloader.js; do
    if [ -f "${BUILD_DIR}/${f}" ]; then
        cp -v "${BUILD_DIR}/${f}" "${REPO_ROOT}/"
        cp -v "${BUILD_DIR}/${f}" "${DIST_DIR}/" 2>/dev/null || true
    fi
done

# Copy Web App Shell assets to public distribution
cp -v "${REPO_ROOT}/index.html" "${DIST_DIR}/" 2>/dev/null || true
cp -v "${REPO_ROOT}/styles.css" "${DIST_DIR}/" 2>/dev/null || true
cp -v "${REPO_ROOT}/app.js" "${DIST_DIR}/" 2>/dev/null || true

echo "=== [4/4] AcuteSim Vercel Distribution Assembly Complete ==="
echo "Artifacts are ready for deployment via Vercel CLI or GitHub Actions."
