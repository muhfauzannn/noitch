#!/bin/bash
# Builds each candidate and records memory, idle cost, animation cost and launch time.
# Usage: ./run.sh [reps]    Writes results.tsv next to this script. Takes ~4 minutes per rep.
set -euo pipefail
cd "$(dirname "$0")"
reps=${1:-3}
out=build
mkdir -p "$out"

clang -O2 -o "$out/probe" ../../scripts/probe.c
swiftc -O -wmo -swift-version 6 -o "$out/appkit_ca" appkit_ca.swift
swiftc -O -wmo -swift-version 6 -o "$out/swiftui_hosted" swiftui_hosted.swift
swiftc -O -wmo -swift-version 6 -o "$out/appkit_swiftui_linked" appkit_swiftui_linked.swift
clang -O2 -fobjc-arc -framework AppKit -framework QuartzCore -o "$out/appkit_ca_objc" appkit_ca_objc.m

now() { perl -MTime::HiRes=time -e 'printf "%.6f\n", time'; }
field() { awk -v n="$1" '{print $n}'; }

# run <binary> <mode> <settle s> <window s>  -> launch_ms footprint_mb cpu_ms wakeups_per_s
measure() {
    local bin=$1 mode=$2 settle=$3 window=$4 log t0 pid ready a b
    log=$(mktemp)
    t0=$(now)
    BENCH_MODE=$mode "$out/$bin" >"$log" 2>/dev/null &
    pid=$!
    sleep "$settle"
    a=$("$out/probe" "$pid")
    sleep "$window"
    b=$("$out/probe" "$pid")
    kill "$pid"; wait "$pid" 2>/dev/null || true
    ready=$(awk '/READY/{print $2; exit}' "$log")
    rm -f "$log"
    awk -v t0="$t0" -v r="$ready" -v a="$a" -v b="$b" -v w="$window" 'BEGIN {
        split(a, x, " "); split(b, y, " ");
        printf "%.1f\t%.2f\t%.1f\t%.2f\n", (r - t0) * 1000, y[1] / 1048576, (y[2] - x[2]) / 1e6, ((y[3] - x[3]) + (y[4] - x[4])) / w
    }'
}

printf 'rep\tcandidate\tmode\tlaunch_ms\tfootprint_mb\tcpu_ms_in_window\twakeups_per_s\n' > results.tsv
for rep in $(seq 1 "$reps"); do
    for bin in appkit_ca appkit_ca_objc appkit_swiftui_linked swiftui_hosted; do
        printf '%s\t%s\tidle\t%s\n' "$rep" "$bin" "$(measure "$bin" idle 10 30)" >> results.tsv
        printf '%s\t%s\tanimate\t%s\n' "$rep" "$bin" "$(measure "$bin" animate 5 20)" >> results.tsv
    done
    printf '%s\t%s\tlazy\t%s\n' "$rep" appkit_swiftui_linked "$(measure appkit_swiftui_linked lazy 10 30)" >> results.tsv
done
ls -l "$out" | awk 'NR>1 {print $5, $9}' > binary-sizes.txt
