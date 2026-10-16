#!/usr/bin/env bash
# RTL -> GDS tools for the app's flow (Roadmap Phase 6), macOS / Apple silicon. Everything goes
# to ~/eda: OpenROAD's Bazel build and ORFS's make flow break on paths with spaces.
# RtlFlowTools::detect() (compute/orchestration/drivers/rtl_flow.cpp) finds them there.
#
# Downloads (about 3 GB of sources, 1 h of builds on 14 cores):
#   Homebrew: yosys, verilator, make (GNU make 4; ORFS), ruby (KLayout's DRC engine), bazelisk
#   git: OpenROAD (2.2 GB with submodules), KLayout v0.30.12 (380 MB),
#        OpenROAD-flow-scripts, sparse: flow scripts + the IHP SG13G2 platform (115 MB)
set -euo pipefail
EDA="$HOME/eda"
mkdir -p "$EDA/bin"
cd "$EDA"

HOMEBREW_NO_AUTO_UPDATE=1 brew install yosys verilator make ruby qt@5

# OpenROAD (CLI; the app has its own layout view)
[ -d OpenROAD ] || git clone --recursive --depth 1 --shallow-submodules https://github.com/The-OpenROAD-Project/OpenROAD.git
(cd OpenROAD && ./etc/DependencyInstaller.sh -no-gui && ./etc/Build.sh -no-gui -prefix="$EDA/openroad")
ln -sf "$EDA/openroad/bin/openroad" "$EDA/bin/openroad"

# KLayout from source: Homebrew's cask was disabled on 2026-10-12 (fails the Gatekeeper check).
# - Qt 5: the Qt 6 install lacks its qmake specs (macx-clang)
# - AGL: Qt 5 still links Apple's AGL framework, which the macOS 26 SDK no longer has; link a
#   stub and drop it again (-dead_strip_dylibs), nothing references it
# - no libgit2 (package manager only), no Qt script bindings (this Qt lacks QtNetwork DTLS):
#   DRC, LVS and GDS export need neither
[ -d klayout ] || git clone --depth 1 --branch v0.30.12 https://github.com/KLayout/klayout.git
if [ ! -f stubs/AGL.framework/AGL ]; then
  mkdir -p stubs/AGL.framework && echo 'void agl_stub(void){}' > stubs/agl.c
  clang -dynamiclib -arch arm64 -o stubs/AGL.framework/AGL -install_name /System/Library/Frameworks/AGL.framework/AGL stubs/agl.c
fi
grep -q "eda/stubs" klayout/build.sh || python3 - "$EDA" <<'PY'
import sys
p = sys.argv[1] + "/klayout/build.sh"; s = open(p).read()
open(p, "w").write(s.replace("qmake_options=(\n", "qmake_options=(\n  \"QMAKE_LFLAGS+=-F" + sys.argv[1] + "/stubs -Wl,-dead_strip_dylibs\"\n", 1))
PY
PYINC=$(python3 -c "import sysconfig; print(sysconfig.get_paths()['include'])")
PYLIB=$(python3 -c "import sysconfig, os; print(os.path.join(sysconfig.get_config_var('PYTHONFRAMEWORKPREFIX'), sysconfig.get_config_var('PYTHONFRAMEWORK') + '.framework/Versions', sysconfig.get_config_var('py_version_short'), 'Python'))")
(cd klayout && ./build.sh -qmake "$(brew --prefix qt@5)/bin/qmake" -ruby "$(brew --prefix ruby)/bin/ruby" \
  -python "$(command -v python3)" -pylib "$PYLIB" -pyinc "$PYINC" -nolibgit2 -without-qtbinding -j8 -prefix "$EDA/klayout-bin")
# Its dylibs are linked by bare name
cat > "$EDA/bin/klayout" <<'EOF'
#!/bin/sh
export DYLD_LIBRARY_PATH="$HOME/eda/klayout-bin${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
exec "$HOME/eda/klayout-bin/klayout.app/Contents/MacOS/klayout" "$@"
EOF
# IHP's LVS deck logs memory with Linux pmap
cat > "$EDA/bin/pmap" <<'EOF'
#!/bin/sh
printf ' total    %12sK\n' "$(ps -o rss= -p "$1" | tr -d ' ')"
EOF
chmod +x "$EDA/bin/klayout" "$EDA/bin/pmap"

# OpenROAD-flow-scripts: the flow and the IHP SG13G2 platform (LEF, Liberty, GDS, DRC/LVS decks)
[ -d orfs ] || git clone --depth 1 --filter=blob:none --sparse https://github.com/The-OpenROAD-Project/OpenROAD-flow-scripts.git orfs
(cd orfs && git sparse-checkout set --no-cone /flow/Makefile '/flow/scripts/' '/flow/util/' '/flow/platforms/common/' \
  '/flow/platforms/ihp-sg13g2/' '/flow/designs/ihp-sg13g2/' '/flow/designs/src/gcd/' '/flow/designs/src/riscv32i/')

echo "RTL flow tools in $EDA. Check: build/test_rtl_flow"
