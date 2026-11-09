#!/bin/sh
# Rebuilds packaging/icon/WakeDeck.icns from make-icon.swift.
# Needs the Xcode command line tools (swift, sips, iconutil). The .icns is
# committed, so a normal build never runs this.
#
# Usage: sh packaging/icon/make-icns.sh [source.png]
#   With a PNG argument (1024 x 1024), that image is used instead of drawing one.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

if [ $# -ge 1 ]; then
    cp "$1" "$WORK/source.png"
else
    swift "$HERE/make-icon.swift" "$WORK/source.png"
fi

SET="$WORK/WakeDeck.iconset"
mkdir "$SET"
for size in 16 32 128 256 512; do
    double=$((size * 2))
    sips -z "$size" "$size" "$WORK/source.png" --out "$SET/icon_${size}x${size}.png" >/dev/null
    sips -z "$double" "$double" "$WORK/source.png" --out "$SET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$SET" -o "$HERE/WakeDeck.icns"
echo "wrote $HERE/WakeDeck.icns"
