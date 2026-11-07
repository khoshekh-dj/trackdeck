#!/bin/bash
# Build TrackDeck on an older Mac (e.g. macOS 12 Monterey) using MacPorts.
#
# The official release builds use Homebrew's libraries, which only support
# recent macOS versions. MacPorts compiles everything for the Mac it runs on,
# so the .dmg this produces runs on this Mac's macOS version and newer.
#
# Before running:
#   1. Install the Xcode Command Line Tools:   xcode-select --install
#   2. Install MacPorts for your macOS version: https://www.macports.org/install.php
#
# Usage (from the TrackDeck source folder):   scripts/build-mac-macports.sh
# The first run compiles Qt, GStreamer and friends and can take a few hours.
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v port >/dev/null; then
    echo "MacPorts isn't installed. Get it from https://www.macports.org/install.php"
    echo "(it needs the Xcode Command Line Tools first: xcode-select --install)"
    exit 1
fi

PORTS="pkgconfig qt5-qtbase qt5-qtscript qt5-qtsvg qt5-qttools
 gstreamer1 gstreamer1-gst-plugins-base gstreamer1-gst-plugins-good
 gstreamer1-gst-plugins-bad gstreamer1-gst-plugins-ugly gstreamer1-gst-libav
 taglib aubio ImageMagick librsvg"

# only install what's missing, so later builds (and a GitHub Actions runner,
# which can't type a password) don't need sudo
MISSING=""
for p in $PORTS; do
    port -q installed "$p" 2>/dev/null | grep -q "(active)" || MISSING="$MISSING $p"
done
if [ -n "$MISSING" ]; then
    if [ -n "${CI:-}" ]; then
        echo "MacPorts packages missing on this runner:$MISSING"
        echo "Install them once by hand: sudo port -N install$MISSING"
        exit 1
    fi
    echo "==> Installing build dependencies with MacPorts (asks for your password):$MISSING"
    sudo port -N install $MISSING
else
    echo "==> MacPorts build dependencies already installed"
fi

export PATH="/opt/local/libexec/qt5/bin:/opt/local/bin:$PATH"

echo "==> Configuring and building"
./configure
make -j"$(sysctl -n hw.ncpu)"

echo "==> Packaging"
PKG_PREFIX=/opt/local scripts/package-mac.sh "$PWD/TrackDeck.app" "$PWD/dist/TrackDeck"

version=$(tr -d '[:space:]' < VERSION)
macos=$(sw_vers -productVersion | cut -d. -f1)
if [ "$(uname -m)" = "x86_64" ]; then chip=Intel; else chip=AppleSilicon; fi
name="TrackDeck-$version-macOS$macos-$chip.dmg"
mv dist/TrackDeck.dmg "dist/$name"
shasum -a 256 "dist/$name" | tee "dist/$name.sha256"

echo
echo "Built dist/$name"
echo "It runs on macOS $macos and newer on $chip Macs."
echo "Quick check: open dist/TrackDeck/TrackDeck.app and play a track."
