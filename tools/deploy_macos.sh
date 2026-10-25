#!/bin/sh
# tools/deploy_macos.sh <macdeployqt> <OpenVLSI.app>: make the bundle self-contained (cmake --build build --target bundle)
#   macdeployqt copies the Qt frameworks and plugins in; then what it misses with Homebrew's Qt:
#   - libbrotlicommon, which the libbrotlidec it copies names by @rpath
#   - LC_RPATHs that point out of the bundle (the build's Qt, the Cellar paths Homebrew's libraries carry)
#   and an ad-hoc signature over the result (ponytail: Developer ID and notarization are the owner's identity)
set -eu
deployqt=$1 app=$2
"$deployqt" "$app" -always-overwrite
brotli=/opt/homebrew/opt/brotli/lib/libbrotlicommon.1.dylib
if [ -f "$app/Contents/Frameworks/libbrotlidec.1.dylib" ] && [ -f "$brotli" ]; then
    cp -f "$brotli" "$app/Contents/Frameworks/"
    chmod u+w "$app/Contents/Frameworks/libbrotlicommon.1.dylib"
    install_name_tool -id @rpath/libbrotlicommon.1.dylib "$app/Contents/Frameworks/libbrotlicommon.1.dylib"
fi
find "$app" -type f \( -perm -u+x -o -name '*.dylib' \) | while read -r f; do
    otool -l "$f" 2>/dev/null | awk '/cmd LC_RPATH/ { getline; getline; print $2 }' | grep -v '^@' | while read -r r; do
        install_name_tool -delete_rpath "$r" "$f"
    done
done
codesign --force --deep --sign - "$app"
