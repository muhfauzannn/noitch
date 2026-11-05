# Noitch

A lightweight Dynamic Island style overlay for macOS and Windows. Two separate native apps, built for
the lowest possible memory, CPU and battery use.

This is the foundation: a black island at the top of the screen that expands on hover. Features come
next.

| | macOS | Windows |
| --- | --- | --- |
| Stack | Swift, AppKit, Core Animation | C++20, Win32, DirectComposition |
| Memory at rest | About 9 MB | Not measured |
| CPU at rest | About 1 ms per minute | Not measured |
| App size | 136 KB | Not measured |
| Dependencies | None | None |
| Status | Runs and passes its checks | **Compiles; never run** |

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

Hover over the island to expand it, and right-click it to quit. Quit other notch apps first.

## Documentation

- [Architecture](docs/architecture.md): how it works and why
- [Tech stack](docs/tech-stack.md): the choices, with measurements
- [Performance](docs/performance.md): budgets and how to measure
- [Adding features](docs/adding-features.md): growing it without getting heavier
- [Development](docs/development.md): building, testing, verification status
- [AGENTS.md](AGENTS.md): rules for coding agents
