#!/bin/bash
# Safe build script that avoids overwhelming the system
# Usage: ./scripts/build_safe.sh [target]

# Determine safe core count (leave 2 cores for system/UI/clangd)
if [[ "$OSTYPE" == "darwin"* ]]; then
    TOTAL_CORES=$(sysctl -n hw.ncpu)
else
    TOTAL_CORES=$(nproc)
fi

SAFE_CORES=$((TOTAL_CORES - 2))
if [[ $SAFE_CORES -lt 1 ]]; then
    SAFE_CORES=1
fi

echo "🛡️  Safe Build: Using $SAFE_CORES jobs (Total Hardware Cores: $TOTAL_CORES)"

# Change to build directory
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

# Ensure configuration exists
if [ ! -f "build/CMakeCache.txt" ]; then
    echo "Configuring project..."
    cmake -B build -S . -DACUTESIM_DEPS_NOISY_WARNINGS=OFF
fi

# Build
echo "🚀 Building with -j$SAFE_CORES..."
cmake --build build -j $SAFE_CORES "$@"
