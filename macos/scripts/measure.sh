#!/bin/bash
# Builds Release, runs the real app and checks it against docs/performance.md.
# Usage: macos/scripts/measure.sh [--clipboard-paused] [--hover] [idle seconds]   Exits non-zero if a budget is exceeded.
# --hover also runs hover-check.swift, which moves the real cursor for about 25 seconds.
set -euo pipefail
cd "$(dirname "$0")/.."

clipboard_paused=0
if [[ "${1:-}" == "--clipboard-paused" ]]; then clipboard_paused=1; shift; fi
hover=0
if [[ "${1:-}" == "--hover" ]]; then hover=1; shift; fi
window=${1:-30}
settle=10
max_footprint_mb=12
max_cpu_ms_per_min=20
max_wakeups_per_s=0.5

build=build
xcodebuild -project Noitch.xcodeproj -scheme Noitch -configuration Release \
    -derivedDataPath "$build" CODE_SIGN_IDENTITY=- -quiet build
app="$build/Build/Products/Release/Noitch.app"
# Not grep -q: exiting early could kill nm, and pipefail would then read that as "not found".
if nm "$app/Contents/MacOS/Noitch" | grep __llvm_profile >/dev/null; then
    echo "Release build is coverage-instrumented; its numbers would be wrong" >&2
    exit 1
fi
probe="$build/probe"
clang -O2 -o "$probe" scripts/probe.c

pkill -x Noitch 2>/dev/null || true
sleep 1
if [[ "$clipboard_paused" == 1 ]]; then
    "$app/Contents/MacOS/Noitch" -shelfRecording NO &
else
    "$app/Contents/MacOS/Noitch" &
fi
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
sleep "$settle"
before=$("$probe" "$pid")
sleep "$window"
after=$("$probe" "$pid")

size=$(stat -f %z "$app/Contents/MacOS/Noitch")
awk -v a="$before" -v b="$after" -v w="$window" -v size="$size" \
    -v mf="$max_footprint_mb" -v mc="$max_cpu_ms_per_min" -v mw="$max_wakeups_per_s" 'BEGIN {
    split(a, x, " "); split(b, y, " ");
    footprint = y[1] / 1048576
    cpu = (y[2] - x[2]) / 1e6 * 60 / w
    wakeups = ((y[3] - x[3]) + (y[4] - x[4])) / w
    printf "binary        %d KB\n", size / 1024
    printf "footprint     %.2f MB        (budget %s)\n", footprint, mf
    printf "idle CPU      %.2f ms/min    (budget %s)\n", cpu, mc
    printf "idle wakeups  %.2f /s        (budget %s)\n", wakeups, mw
    exit (footprint > mf || cpu > mc || wakeups > mw) ? 1 : 0
}'

if [[ "$hover" == 1 ]]; then
    swiftc -O -o "$build/hover-check" scripts/hover-check.swift
    "$build/hover-check" "$pid" "$probe"
fi
