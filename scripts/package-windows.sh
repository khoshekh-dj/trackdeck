#!/bin/bash
# Build a portable TrackDeck folder + .zip from a MinGW (MSYS2) build.
# Run inside an MSYS2 shell (UCRT64/MINGW64) from the project root after `make`.
# Usage: package-windows.sh <output dir>   ->  <output dir>/TrackDeck + <output dir>.zip
set -euo pipefail
OUT=${1:?output dir}
PREFIX=${MINGW_PREFIX:?run inside an MSYS2 MinGW shell}
PROJECT=$(pwd)
APP="$OUT/TrackDeck"

rm -rf "$OUT"
mkdir -p "$APP"

# --- the app, its plugins, skins and translations ---------------------------
cp TrackDeck.exe "$APP/"
mkdir -p "$APP/plugins" "$APP/skins" "$APP/i18n"
cp Plugins/*.dll "$APP/plugins/"
cp Skins/*.nzs "$APP/skins/" 2>/dev/null || cp skins/*.nzs "$APP/skins/"
cp i18n/*.qm "$APP/i18n/" 2>/dev/null || true

# --- GStreamer: audio-only plugin set + scanner -----------------------------
GSTLIB="$PREFIX/lib/gstreamer-1.0"
PLUGINS="coreelements typefindfunctions playback autodetect app audioconvert
 audioresample volume audioparsers id3demux apetag icydemux isomp4 mpg123
 flac ogg vorbis opus opusparse wavparse aiff faad musepack libav
 asf wavpack spectrum level wasapi wasapi2 directsound"
mkdir -p "$APP/gstreamer"
for p in $PLUGINS; do
    if [ -f "$GSTLIB/libgst$p.dll" ]; then
        cp "$GSTLIB/libgst$p.dll" "$APP/gstreamer/"
    else
        echo "note: GStreamer plugin '$p' not installed, skipping"
    fi
done
cp "$PREFIX/libexec/gstreamer-1.0/gst-plugin-scanner.exe" "$APP/"

# --- Qt runtime (platform plugin, styles, image formats, ...) ---------------
WINDEPLOYQT=$(command -v windeployqt-qt5 || command -v windeployqt)
"$WINDEPLOYQT" --no-translations --no-compiler-runtime --no-opengl-sw --no-angle --no-system-d3d-compiler \
    --dir "$APP" "$APP/TrackDeck.exe" "$APP"/plugins/*.dll > "$OUT.windeployqt.log" 2>&1 || {
    cat "$OUT.windeployqt.log"; exit 1; }

# --- every other DLL dependency, resolved recursively from the MinGW prefix --
# (windeployqt doesn't copy MinGW runtime or non-Qt libraries)
copy_deps() {
    ntldd -R "$@" 2>/dev/null | tr -d '\r' | awk '{print $3}' | while read -r dll; do
        [ -z "$dll" ] && continue
        unix=$(cygpath -u "$dll" 2>/dev/null || echo "$dll")
        case "$unix" in
            "$PREFIX"/bin/*.dll)
                [ -f "$APP/$(basename "$unix")" ] || cp "$unix" "$APP/" ;;
        esac
    done
}
for pass in 1 2; do  # second pass picks up dependencies of newly copied DLLs
    mapfile -t bins < <(find "$APP" -name "*.exe" -o -name "*.dll")
    copy_deps "${bins[@]}"
done

# --- licences ----------------------------------------------------------------
LIC="$APP/licenses"
mkdir -p "$LIC/TrackDeck"
cp "$PROJECT/LICENSE.GPL3" "$LIC/TrackDeck/"
printf '%s\n' "TrackDeck, based on Nulloy by Sergey Vlasov (https://github.com/nulloy/nulloy)" \
    "License: GPL-3.0. Source: https://github.com/khoshekh-dj/trackdeck" "Skins: CC-BY-SA 3.0." \
    > "$LIC/TrackDeck/INFO.txt"
find "$APP" -name "*.dll" -o -name "*.exe" | while read -r f; do
    src="$PREFIX/bin/$(basename "$f")"
    [ -f "$src" ] || src="$GSTLIB/$(basename "$f")"
    [ -f "$src" ] || src="$PREFIX/libexec/gstreamer-1.0/$(basename "$f")"
    [ -f "$src" ] || continue
    pacman -Qoq "$src" 2>/dev/null || true
done | sort -u | while read -r pkg; do
    name=${pkg#"${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}"-}
    mkdir -p "$LIC/$name"
    [ -d "$PREFIX/share/licenses/$name" ] && cp -r "$PREFIX/share/licenses/$name/." "$LIC/$name/"
    pacman -Qi "$pkg" | grep -E "^(Name|Version|Licenses|URL)" > "$LIC/$name/INFO.txt" || true
done
echo "licences collected for $(ls "$LIC" | wc -l) packages"

# --- zip -----------------------------------------------------------------------
(cd "$OUT" && zip -qr "../$(basename "$OUT").zip" TrackDeck)
du -sh "$APP" "$OUT.zip"
