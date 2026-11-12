#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
swiftc -swift-version 6 -default-isolation MainActor -O \
  Noitch/Features/Shelf/ShelfModel.swift Noitch/Features/Shelf/ShelfScene.swift \
  Noitch/Features/Home/HomeIcon.swift Noitch/Island/IslandPage.swift \
  Noitch/Island/Motion.swift scripts/shelf-preview/main.swift \
  -o build/render-shelf
for mode in files clipboard empty dropping; do
  build/render-shelf "build/shelf-$mode.png" "$mode"
done
