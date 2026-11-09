#!/bin/sh
# Builds the WakeDeck disk image: release build against Qt's official binaries,
# every test, Qt frameworks copied into the bundle, Developer ID signature with
# the hardened runtime, notarization by Apple, stapled tickets.
#
# Usage (from the WakeDeck folder):
#   QT_DIR=/path/to/Qt/6.11.3/macos sh packaging/make-release.sh
#
# Settings (environment variables):
#   QT_DIR          required: Qt's official macOS binaries (see packaging/get-qt.sh)
#   SIGN_IDENTITY   default: the one "Developer ID Application" identity in the keychain
#   NOTARY_PROFILE  default: WakeDeckNotary, stored once with `xcrun notarytool store-credentials`
#   BUILD_DIR       default: $TMPDIR/wakedeck-release (keep it out of iCloud-synced folders)
#   OUT_DIR         default: $BUILD_DIR/out; the .dmg and its checksum land here
#   ARCHS           default: arm64 (Apple Silicon); "arm64;x86_64" builds a universal app
#   SKIP_NOTARIZE   set to 1 for a signed but not notarized image (for testing the script)
#
# Nothing personal is written into the repository: the identity comes from the
# keychain and the notarization credentials stay in it.
set -eu

die() { echo "error: $*" >&2; exit 1; }
step() { printf '\n==> %s\n' "$*"; }

[ -f CMakeLists.txt ] && [ -d src/app ] || die "run this from the WakeDeck folder"
SRC=$(pwd)
QT_DIR=${QT_DIR:-}
[ -n "$QT_DIR" ] || die "set QT_DIR to Qt's official macOS binaries (sh packaging/get-qt.sh <folder>)"
[ -x "$QT_DIR/bin/macdeployqt" ] || die "$QT_DIR/bin/macdeployqt not found"
NOTARY_PROFILE=${NOTARY_PROFILE:-WakeDeckNotary}
BUILD_DIR=${BUILD_DIR:-${TMPDIR:-/tmp}/wakedeck-release}
OUT_DIR=${OUT_DIR:-$BUILD_DIR/out}
ARCHS=${ARCHS:-arm64}
SKIP_NOTARIZE=${SKIP_NOTARIZE:-0}
MIN_MACOS=13.0

CMAKE=$(command -v cmake || echo /opt/homebrew/bin/cmake)
CTEST=$(dirname "$CMAKE")/ctest
NINJA=$(command -v ninja || echo /opt/homebrew/bin/ninja)
[ -x "$CMAKE" ] || die "cmake not found"
[ -x "$NINJA" ] || die "ninja not found"

if [ -z "${SIGN_IDENTITY:-}" ]; then
    count=$(security find-identity -v -p codesigning | grep -c '"Developer ID Application: ' || true)
    [ "$count" = 1 ] || die "found $count Developer ID Application identities; set SIGN_IDENTITY to the one to use"
    SIGN_IDENTITY=$(security find-identity -v -p codesigning | sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p')
fi
echo "Signing as: $SIGN_IDENTITY"
echo "Qt:         $QT_DIR"
echo "Build:      $BUILD_DIR"

step "Configure and build (Release, macOS $MIN_MACOS, $ARCHS)"
"$CMAKE" -S "$SRC" -B "$BUILD_DIR/build" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_DIR" -DQt6_DIR="$QT_DIR/lib/cmake/Qt6" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_MACOS" -DCMAKE_OSX_ARCHITECTURES="$ARCHS" >/dev/null
nice -n 19 "$CMAKE" --build "$BUILD_DIR/build" --parallel 2

step "Run every test against this build"
(cd "$BUILD_DIR/build" && nice -n 19 "$CTEST" --output-on-failure)

VERSION=$(plutil -extract CFBundleShortVersionString raw "$BUILD_DIR/build/WakeDeck.app/Contents/Info.plist")
STAGE="$BUILD_DIR/stage"
APP="$STAGE/WakeDeck.app"
step "Assemble WakeDeck $VERSION with its Qt frameworks"
rm -rf "$STAGE" && mkdir -p "$STAGE"
ditto "$BUILD_DIR/build/WakeDeck.app" "$APP"
cp "$BUILD_DIR/build/wakedeck-cli" "$APP/Contents/MacOS/wakedeck-cli"
"$QT_DIR/bin/macdeployqt" "$APP" -executable="$APP/Contents/MacOS/wakedeck-cli" -verbose=1

# Nothing may still point outside the bundle, at the build machine's Qt or Homebrew.
if find "$APP" -type f -perm -u+x -o -type f -name '*.dylib' | while read -r f; do
       otool -L "$f" 2>/dev/null | tail -n +2 | grep -E "$QT_DIR|/opt/homebrew|/usr/local" && echo "  in $f"
   done | grep -q .; then
    die "the bundle still links to libraries outside it (see above)"
fi
echo "All libraries resolve inside the bundle or to the system."

step "Sign inside out with the hardened runtime"
sign() { codesign --force --timestamp --options runtime --sign "$SIGN_IDENTITY" "$@"; }
find "$APP/Contents" -name '*.dylib' -type f | while read -r f; do sign "$f"; done
find "$APP/Contents/Frameworks" -maxdepth 1 -name '*.framework' | while read -r f; do sign "$f"; done
sign "$APP/Contents/MacOS/wakedeck-cli"
sign "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
codesign -dv "$APP" 2>&1 | grep -E 'Authority=Developer ID Application|flags=.*runtime|TeamIdentifier'

notarize() {
    # $1: file to submit. Prints Apple's log and stops on anything but Accepted.
    result=$(xcrun notarytool submit "$1" --keychain-profile "$NOTARY_PROFILE" --wait --output-format plist)
    status=$(printf '%s' "$result" | plutil -extract status raw - 2>/dev/null || echo unknown)
    id=$(printf '%s' "$result" | plutil -extract id raw - 2>/dev/null || echo "")
    echo "Notarization of $(basename "$1"): $status (submission $id)"
    if [ "$status" != "Accepted" ]; then
        [ -n "$id" ] && xcrun notarytool log "$id" --keychain-profile "$NOTARY_PROFILE"
        die "notarization was not accepted"
    fi
}

if [ "$SKIP_NOTARIZE" != 1 ]; then
    step "Notarize and staple the app"
    ditto -c -k --keepParent "$APP" "$BUILD_DIR/WakeDeck-app.zip"
    notarize "$BUILD_DIR/WakeDeck-app.zip"
    xcrun stapler staple "$APP"
    xcrun stapler validate "$APP"
fi

DMG_NAME="WakeDeck-$VERSION.dmg"
DMG="$BUILD_DIR/$DMG_NAME"
step "Build the disk image"
DMG_ROOT="$BUILD_DIR/dmg-root"
rm -rf "$DMG_ROOT" "$DMG" && mkdir -p "$DMG_ROOT"
ditto "$APP" "$DMG_ROOT/WakeDeck.app"
ln -s /Applications "$DMG_ROOT/Applications"
hdiutil create -volname "WakeDeck $VERSION" -srcfolder "$DMG_ROOT" -fs HFS+ -format UDZO -ov "$DMG" >/dev/null
codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG"

if [ "$SKIP_NOTARIZE" != 1 ]; then
    step "Notarize and staple the disk image"
    notarize "$DMG"
    xcrun stapler staple "$DMG"
    xcrun stapler validate "$DMG"
    step "Gatekeeper's verdict"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$DMG"
    spctl --assess --type execute --verbose=2 "$APP"
fi

mkdir -p "$OUT_DIR"
cp "$DMG" "$OUT_DIR/$DMG_NAME"
(cd "$OUT_DIR" && shasum -a 256 "$DMG_NAME" > "$DMG_NAME.sha256")
step "Done"
echo "$OUT_DIR/$DMG_NAME"
cat "$OUT_DIR/$DMG_NAME.sha256"
