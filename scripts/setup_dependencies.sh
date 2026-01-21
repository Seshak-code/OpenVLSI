#!/bin/bash

# AcuteSim Dependency Setup Script
# Installs vcpkg and bootstraps the orchestration layer dependencies.

set -e

echo "=== AcuteSim Dependency Setup ==="

# 1. Check for Homebrew (macOS)
if [[ "$OSTYPE" == "darwin"* ]]; then
    if ! command -v brew &> /dev/null; then
        echo "Error: Homebrew not found. Please install explicitly."
        exit 1
    fi
    echo "[OK] Homebrew found."
    
    # Install system-level build tools
    echo "Installing system build tools..."
    brew install cmake ninja pkg-config autoconf automake libtool
fi

# 2. Check for VCPKG
VCPKG_ROOT="$HOME/vcpkg"
if [ -d "$VCPKG_ROOT" ]; then
    echo "[OK] vcpkg found at $VCPKG_ROOT"
else
    echo "Cloning vcpkg to $VCPKG_ROOT..."
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
    "$VCPKG_ROOT/bootstrap-vcpkg.sh"
fi

# 3. Install Dependencies defined in vcpkg.json
echo "Installing project dependencies (this may take 15+ minutes on first run)..."
"$VCPKG_ROOT/vcpkg" install --triplet=arm64-osx

echo "=== Setup Complete ==="
echo "To build the project, run:"
echo "cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
echo "cmake --build build"
