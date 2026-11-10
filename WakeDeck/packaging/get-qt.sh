#!/bin/sh
# Downloads Qt's official macOS binaries for release builds, without a Qt
# account, using aqtinstall in a private Python environment.
#
# Usage: sh packaging/get-qt.sh <folder>
#   Installs Qt into <folder>/Qt/<version>/macos and prints the path to give
#   make-release.sh as QT_DIR. Choose a folder outside iCloud-synced locations
#   such as Documents: Qt is about 1.4 GB.
#
# Qt 6.11.3 is the newest Qt whose official binaries still run on macOS 13.0
# (checked with `vtool -show-build` on QtCore); 6.12 requires macOS 14.4.
set -eu
QT_VERSION=${QT_VERSION:-6.11.3}
[ $# -eq 1 ] || { echo "usage: sh packaging/get-qt.sh <folder>" >&2; exit 1; }
ROOT=$1
mkdir -p "$ROOT"
ROOT=$(cd "$ROOT" && pwd)

if [ ! -x "$ROOT/aqt-venv/bin/aqt" ]; then
    /usr/bin/python3 -m venv "$ROOT/aqt-venv"
    "$ROOT/aqt-venv/bin/pip" install --quiet --upgrade pip aqtinstall
fi
# aqt writes aqtinstall.log into the current folder: keep it next to Qt.
(cd "$ROOT" && "$ROOT/aqt-venv/bin/aqt" install-qt mac desktop "$QT_VERSION" clang_64 -O "$ROOT/Qt")

QT_DIR="$ROOT/Qt/$QT_VERSION/macos"
minos=$(vtool -show-build "$QT_DIR/lib/QtCore.framework/QtCore" | awk '/minos/{print $2}' | sort -u | tr '\n' ' ')
echo
echo "Qt $QT_VERSION installed. QtCore runs on macOS $minos"
echo "QT_DIR=$QT_DIR"
