#!/bin/sh
# tools/deploy_macos.sh <macdeployqt> <OpenVLSI.app>: make the bundle self-contained (cmake --build build --target bundle)
#   macdeployqt copies the Qt frameworks and plugins in. With Homebrew's Qt it misses what a copied file names by
#   @rpath (QtGui's QtDBus, libbrotlidec's libbrotlicommon): it resolves those from the copy, where they are not yet.
#   So: every @rpath framework or library a file in the bundle needs and the bundle lacks is copied in from Qt's or
#   Homebrew's lib, until none is missing; LC_RPATHs that point out of the bundle are removed; and the result is signed
#   ad hoc (ponytail: Developer ID and notarization are the owner's identity).
set -eu
deployqt=$1 app=$2
qtlib=$(cd "$(dirname "$deployqt")/../lib" && pwd)
fw=$app/Contents/Frameworks
"$deployqt" "$app" -always-overwrite

# every Mach-O file in the bundle (framework binaries carry no executable bit, so by content)
machos() { find "$app" -type f | while read -r f; do if file -b "$f" | grep -q '^Mach-O'; then echo "$f"; fi; done; }
external() { case $1 in /System/*|/usr/lib/*|@*) return 1 ;; *) return 0 ;; esac; }

# copy a dependency in: a framework (by its name) or a library, from Qt's lib, Homebrew's, or the path it was named by
bring() {
    dep=$1
    case $dep in
        *.framework/*)
            name=${dep##*/}
            src=
            for d in "$qtlib" "$(dirname "$dep" | sed 's|/[^/]*\.framework/.*||')"; do [ -d "$d/$name.framework" ] && src=$d/$name.framework && break; done
            [ -n "$src" ] || { echo "deploy: $dep is needed and no lib directory has $name.framework" >&2; exit 1; }
            cp -R "$src" "$fw/"
            rm -rf "$fw/$name.framework/Headers" "$fw/$name.framework/Versions/A/Headers"
            chmod -R u+w "$fw/$name.framework" ;;
        *)
            base=${dep##*/}
            src=
            for d in "$(dirname "$dep")" "$qtlib" /opt/homebrew/lib /usr/local/lib; do [ -f "$d/$base" ] && src=$d/$base && break; done
            [ -n "$src" ] || { echo "deploy: $dep is needed and no lib directory has it" >&2; exit 1; }
            cp -f "$src" "$fw/$base"
            chmod u+w "$fw/$base"
            install_name_tool -id "@rpath/$base" "$fw/$base" ;;
    esac
}

# until nothing is missing: an @rpath dependency the bundle lacks is brought in; one named by an absolute path outside
# the system is brought in and renamed @rpath/... in the file that names it (the executable's rpath reaches Frameworks)
list=$(mktemp)
while :; do
    changed=0
    machos > "$list"
    while read -r f; do  # read from a file, not a pipe: the loop runs in this shell, so `changed` survives it
        own=$(otool -D "$f" 2>/dev/null | sed -n 2p)
        for dep in $(otool -L "$f" 2>/dev/null | awk 'NR > 1 { print $1 }'); do
            [ "$dep" = "$own" ] && continue
            case $dep in
                @rpath/*)
                    rel=${dep#@rpath/}
                    [ -e "$fw/$rel" ] || { bring "$rel"; changed=1; } ;;
                *)
                    external "$dep" || continue
                    case $dep in *.framework/*) rel=${dep##*/}.framework/${dep#*.framework/} ;; *) rel=${dep##*/} ;; esac
                    [ -e "$fw/$rel" ] || bring "$dep"
                    install_name_tool -change "$dep" "@rpath/$rel" "$f"
                    changed=1 ;;
            esac
        done
    done < "$list"
    [ $changed = 0 ] && break
done
rm -f "$list"

machos | while read -r f; do
    otool -l "$f" 2>/dev/null | awk '/cmd LC_RPATH/ { getline; getline; print $2 }' | grep -v '^@' | while read -r r; do
        install_name_tool -delete_rpath "$r" "$f"
    done
done
codesign --force --deep --sign - "$app"
