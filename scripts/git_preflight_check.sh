#!/bin/bash
# Preflight check to prevent committing build artifacts
# Usage: ./scripts/git_preflight_check.sh [--ci]

CI_MODE=0
if [[ "$1" == "--ci" ]]; then
    CI_MODE=1
fi

echo "🔍 Running Git Preflight Check..."

# Define forbidden paths relative to the repo root
FORBIDDEN_PATHS=("build" "build_wasm" "build_desktop" "bin" "out")
FOUND_VIOLATIONS=0
REPO_ROOT=$(git rev-parse --show-toplevel)

for path in "${FORBIDDEN_PATHS[@]}"; do
    # Check if files in this path are tracked by git
    if git ls-files "$path" | grep -q .; then
        echo "❌ ERROR: Tracked files found in forbidden directory: $path"
        git ls-files "$path" | head -n 5 | sed 's/^/   - /'
        if [[ $(git ls-files "$path" | wc -l) -gt 5 ]]; then
            echo "   ... and more."
        fi
        
        if [[ $CI_MODE -eq 0 ]]; then
            echo "💡 Remediation: Run 'git rm --cached -r $path' to untrack these files."
        fi
        FOUND_VIOLATIONS=1
    fi
done

if [[ $FOUND_VIOLATIONS -eq 1 ]]; then
    echo "🚫 Preflight check failed! Build artifacts are tracked by git."
    exit 1
fi

echo "✅ Preflight check passed. No build artifacts tracked."
exit 0
