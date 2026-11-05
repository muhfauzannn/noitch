# Noitch

A lightweight Dynamic Island style overlay for macOS and Windows. Two separate native apps, built for
the lowest possible memory, CPU and battery use.

A black island at the top of the screen that expands on hover into Home: the current track, artwork,
synced lyrics, a progress slider and playback controls. Open settings from the gear or by right-clicking.

| | macOS | Windows |
| --- | --- | --- |
| Stack | Swift, AppKit, Core Animation | C++20, Win32, DirectComposition |
| Memory at rest | About 9 MB | Not measured |
| CPU at rest | About 1 ms per minute | Not measured |
| App size | 816 KB | Not measured |
| Dependencies | MediaRemoteAdapter | None |
| Status | Runs and passes its checks | **Never run**; now playing never compiled |

## Quick start

**macOS** (Xcode 26+)

```sh
xcodebuild -project macos/Noitch.xcodeproj -scheme Noitch -configuration Release -derivedDataPath macos/build build
open macos/build/Build/Products/Release/Noitch.app
```

**Windows** (Visual Studio 2022+, CMake 3.25+)

```powershell
cd windows
cmake --preset default
cmake --build --preset release
build\Release\Noitch.exe
```

Hover over the island to expand it, and right-click it for Settings and Quit. Quit other notch apps first.

## Documentation

- [Architecture](docs/architecture.md): how it works and why
- [Tech stack](docs/tech-stack.md): the choices, with measurements
- [Performance](docs/performance.md): budgets and how to measure
- [Adding features](docs/adding-features.md): growing it without getting heavier
- [Development](docs/development.md): building, testing, verification status
- [AGENTS.md](AGENTS.md): rules for coding agents
