#!/bin/bash
set -euo pipefail
: "${RELEASE_MODE:?}" "${RELEASE_VERSION:?}" "${MACOS_BUNDLE_ID:?}"
case "$RELEASE_MODE" in
    signed) : "${MACOS_TEAM_ID:?}" ;;
    unsigned) ;;
    *) echo 'RELEASE_MODE must be signed or unsigned' >&2; exit 1 ;;
esac
verify_signature() {
    codesign --verify --strict --verbose=2 "$1"
    if [[ $RELEASE_MODE == signed ]]; then
        codesign -dvv "$1" 2>&1 | grep -Fx "TeamIdentifier=$MACOS_TEAM_ID"
    else
        codesign -dvv "$1" 2>&1 | grep -Fx 'Signature=adhoc'
    fi
}
dmg=$1
hdiutil verify "$dmg"
verify_signature "$dmg"
if [[ $RELEASE_MODE == signed ]]; then xcrun stapler validate "$dmg"; fi
mount=$(mktemp -d)
trap 'hdiutil detach "$mount" >/dev/null 2>&1 || true; rmdir "$mount"' EXIT
hdiutil attach "$dmg" -readonly -nobrowse -mountpoint "$mount"
app="$mount/Noitch.app"
[[ $(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$app/Contents/Info.plist") == "$RELEASE_VERSION" ]]
[[ $(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$app/Contents/Info.plist") == "$MACOS_BUNDLE_ID" ]]
[[ $(lipo -archs "$app/Contents/MacOS/Noitch") == arm64 ]]
test -L "$mount/Applications"
[[ $(readlink "$mount/Applications") == /Applications ]]
test -s "$app/Contents/Resources/MediaRemoteAdapter-LICENSE.txt"
test -s "$app/Contents/Resources/mediaremote-adapter.pl"
verify_signature "$app/Contents/Frameworks/MediaRemoteAdapter.framework"
verify_signature "$app"
lipo -verify_arch arm64 "$app/Contents/Frameworks/MediaRemoteAdapter.framework/MediaRemoteAdapter"
if [[ $RELEASE_MODE == signed ]]; then
    xcrun stapler validate "$app"
    spctl --assess --type execute --verbose=2 "$app"
else
    echo 'Ad-hoc signed, not notarized. Publisher identity and Gatekeeper acceptance are not verified.'
fi
