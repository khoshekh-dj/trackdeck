#!/bin/bash
# Build a self-contained <Name>.app + <Name>.dmg from an already-built app bundle
# Usage: package-mac.sh <path/to/built/TrackDeck.app> <output dir>
# Uses Homebrew's libraries by default; set PKG_PREFIX=/opt/local for a
# MacPorts build (see scripts/build-mac-macports.sh, for older macOS).
set -euo pipefail
SRC=${1:?path to built app bundle}
SRC=${SRC%/}
NAME=$(basename "$SRC" .app)
BIN=$(/usr/libexec/PlistBuddy -c "Print CFBundleExecutable" "$SRC/Contents/Info.plist")
OUT=${2:?output dir}
if [ -n "${PKG_PREFIX:-}" ]; then # MacPorts
    PREFIX=$PKG_PREFIX
    QTBIN=$PREFIX/libexec/qt5/bin
    SCANNER=$PREFIX/libexec/gstreamer-1.0/gst-plugin-scanner
else # Homebrew
    PREFIX=$(brew --prefix)
    QTBIN=$(brew --prefix qt@5)/bin
    SCANNER=$(brew --prefix gstreamer)/libexec/gstreamer-1.0/gst-plugin-scanner
fi
GSTLIB=$PREFIX/lib/gstreamer-1.0
# library locations that must not remain referenced from the bundle
OUTSIDE='^(/opt/homebrew|/usr/local|/opt/local)'

rm -rf "$OUT"
mkdir -p "$OUT"
APP="$OUT/$NAME.app"
ditto "$SRC" "$APP"
# strip local user data / caches that live next to the binary
rm -f "$APP/Contents/MacOS/$BIN".{cfg,m3u,peaks,bpm} "$APP/Contents/MacOS/gstreamer-1.0.registry.bin"

# audio-only GStreamer plugin set
PLUGINS="coreelements typefindfunctions playback autodetect osxaudio app
 audioconvert audioresample volume audioparsers id3demux apetag icydemux
 isomp4 mpg123 flac ogg vorbis opus opusparse wavparse aiff faad musepack
 libav asf spectrum level"
mkdir -p "$APP/Contents/PlugIns/gstreamer"
EXES=()
FOUND_PLUGINS=""
for p in $PLUGINS; do
    if [ ! -f "$GSTLIB/libgst$p.dylib" ]; then
        echo "note: GStreamer plugin '$p' not installed, skipping"
        continue
    fi
    cp "$GSTLIB/libgst$p.dylib" "$APP/Contents/PlugIns/gstreamer/"
    EXES+=("-executable=$APP/Contents/PlugIns/gstreamer/libgst$p.dylib")
    FOUND_PLUGINS+=" $p"
done
cp "$SCANNER" "$APP/Contents/MacOS/"
chmod u+w "$APP"/Contents/MacOS/gst-plugin-scanner "$APP"/Contents/PlugIns/gstreamer/*
EXES+=("-executable=$APP/Contents/MacOS/gst-plugin-scanner")
for p in "$APP"/Contents/MacOS/plugins/*.dylib; do
    EXES+=("-executable=$p")
done

"$QTBIN/macdeployqt" "$APP" "${EXES[@]}" -always-overwrite > "$OUT/../macdeployqt.log" 2>&1

# copy in @rpath dependencies macdeployqt missed and verify everything resolves
python3 "$(dirname "$0")/mac-fix-deps.py" "$APP" "$PREFIX"

if [ -z "${PKG_PREFIX:-}" ]; then
    # licence texts for every bundled Homebrew library: dependency closure of the
    # original (not rewritten) binaries plus everything that ended up in the bundle
    python3 "$(dirname "$0")/mac-collect-licenses.py" "$APP" "$PREFIX" "$(cd "$(dirname "$0")/.." && pwd)" \
        "$SRC/Contents/MacOS/$BIN" "$SRC"/Contents/MacOS/plugins/*.dylib \
        "$SCANNER" $(for p in $FOUND_PLUGINS; do echo "$GSTLIB/libgst$p.dylib"; done)
else
    # MacPorts: the port that installed each bundled library, with its licence
    # and homepage, plus any licence files the port installed
    LIC="$APP/Contents/Resources/Licenses"
    mkdir -p "$LIC/TrackDeck"
    cp "$(dirname "$0")/../LICENSE.GPL3" "$LIC/TrackDeck/"
    for f in "$APP"/Contents/Frameworks/*.dylib "$APP"/Contents/PlugIns/gstreamer/*.dylib; do
        orig="$PREFIX/lib/$(basename "$f")"
        [ -f "$orig" ] || orig="$GSTLIB/$(basename "$f")"
        port provides "$orig" 2>/dev/null | sed -n 's/.* is provided by: //p'
    done | sort -u | while read -r portname; do
        [ -n "$portname" ] || continue
        mkdir -p "$LIC/$portname"
        port info --license --homepage "$portname" > "$LIC/$portname/INFO.txt" 2>/dev/null || true
        for l in "$PREFIX/share/doc/$portname"/{COPYING,LICENSE,LICENCE}*; do
            # an unmatched pattern stays literal; with "set -e" a bare
            # "[ -f ] && cp" would end the script silently, so use if
            if [ -f "$l" ]; then
                cp "$l" "$LIC/$portname/"
            fi
        done
    done
    echo "licences collected for $(ls "$LIC" | wc -l | tr -d ' ') MacPorts packages"
fi

# drop leftover rpaths that point into Homebrew
while IFS= read -r -d '' f; do
    file "$f" | grep -q Mach-O || continue
    for r in $(otool -l "$f" | grep -A2 LC_RPATH | awk '/ path /{print $2}' | grep -E "$OUTSIDE" || true); do
        install_name_tool -delete_rpath "$r" "$f"
    done
done < <(find "$APP" -type f -print0)

# verify no binary still depends on Homebrew (ignoring each dylib's own install name)
LEFT=""
while IFS= read -r -d '' f; do
    file "$f" | grep -q Mach-O || continue
    id=$(otool -D "$f" | tail -n +2)
    deps=$(otool -L "$f" | tail -n +2 | awk '{print $1}' | grep -E "$OUTSIDE" | grep -vxF "$id" || true)
    rpaths=$(otool -l "$f" | grep -A2 LC_RPATH | awk '/ path /{print $2}' | grep -E "$OUTSIDE" || true)
    [ -n "$deps$rpaths" ] && LEFT+="$f: $deps $rpaths"$'\n'
done < <(find "$APP" -type f -print0)
if [ -n "$LEFT" ]; then
    echo "UNRESOLVED references to Homebrew/MacPorts:"
    echo "$LEFT"
    exit 1
fi

# codesign requires Contents/MacOS to hold only code: move data to Resources
# and leave symlinks where the app looks for it (next to the binary)
mkdir -p "$APP/Contents/Resources"
for d in i18n skins; do
    if [ -d "$APP/Contents/MacOS/$d" ]; then
        mv "$APP/Contents/MacOS/$d" "$APP/Contents/Resources/$d"
        ln -s "../Resources/$d" "$APP/Contents/MacOS/$d"
    fi
done

# ad-hoc sign inside-out: every Mach-O file, then frameworks, then the app
while IFS= read -r -d '' f; do
    file "$f" | grep -q Mach-O || continue
    [ "$f" = "$APP/Contents/MacOS/$BIN" ] && continue
    codesign --force --sign - "$f" 2>/dev/null
done < <(find "$APP/Contents" -type f -print0)
for fw in "$APP"/Contents/Frameworks/*.framework; do
    codesign --force --sign - "$fw"
done
codesign --force --sign - "$APP"
codesign --verify --strict "$APP"

ln -s /Applications "$OUT/Applications"
# hdiutil sometimes fails on CI machines ("Resource busy" while Spotlight or
# the disk-image helper still holds the folder); wait and try again
for attempt in 1 2 3 4 5; do
    if hdiutil create -volname "$NAME" -srcfolder "$OUT" -ov -format UDZO "$OUT/../$NAME.dmg"; then
        break
    fi
    if [ "$attempt" -eq 5 ]; then
        echo "hdiutil create failed 5 times" >&2
        exit 1
    fi
    echo "hdiutil create failed (attempt $attempt), retrying in $((attempt * 5)) s" >&2
    sleep $((attempt * 5))
done
rm "$OUT/Applications"
du -sh "$APP" "$OUT/../$NAME.dmg"
