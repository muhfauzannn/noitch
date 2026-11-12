#!/bin/bash
set -euo pipefail
: "${RELEASE_VERSION:?}" "${MACOS_BUNDLE_ID:?}" "${MACOS_TEAM_ID:?}"
dmg=$1
hdiutil verify "$dmg"
codesign --verify --strict --verbose=2 "$dmg"
codesign -dvv "$dmg" 2>&1 | grep -Fx "TeamIdentifier=$MACOS_TEAM_ID"
xcrun stapler validate "$dmg"
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
for code in "$app/Contents/Frameworks/MediaRemoteAdapter.framework" "$app"; do
    codesign --verify --strict --verbose=2 "$code"
    codesign -dvv "$code" 2>&1 | grep -Fx "TeamIdentifier=$MACOS_TEAM_ID"
done
lipo -verify_arch arm64 "$app/Contents/Frameworks/MediaRemoteAdapter.framework/MediaRemoteAdapter"
xcrun stapler validate "$app"
spctl --assess --type execute --verbose=2 "$app"
