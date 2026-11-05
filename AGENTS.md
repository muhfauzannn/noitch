# Noitch: agent guidelines

Noitch is a Dynamic Island style overlay with **two separate native apps**: `macos/` (Swift, AppKit,
Core Animation) and `windows/` (C++20, Win32, DirectComposition). They share no code, on purpose.

**Performance is the first priority**, ahead of developer convenience and code sharing.

## Feature requests apply to both platforms

**Any request to add or change a feature applies to macOS and Windows**, even if it doesn't mention
a platform. Implement it in both apps in the same change.

- Work on one platform only when the request says so explicitly, such as "macOS only".
- If one OS has no way to support the feature, implement the other, then state the gap and the reason
  in your summary. Don't drop it silently.
- Being unable to run Windows here is not a reason to skip it. Write the Windows code, run the checks
  you can, and report it as not run.

## Before you change anything

Read these first. They are requirements, not background.

1. [docs/architecture.md](docs/architecture.md): always
2. [docs/performance.md](docs/performance.md): always
3. [docs/adding-features.md](docs/adding-features.md): before any new feature
4. [docs/tech-stack.md](docs/tech-stack.md): before questioning or changing a technology choice
5. [docs/development.md](docs/development.md): for build, test and verification status

If a request conflicts with these docs, say so before writing code. If you change how the system
works, update the docs in the same change.

## The rules that matter most

The app must have **no runnable threads at rest**, and our process must do **no per-frame work**.

- **Everything is event-driven.** No polling, no periodic timers, no display links at rest. One-shot
  timers that fire once and end are fine.
- **Animation belongs to the compositor.** Use `CAAnimation` and `IDCompositionAnimation`. Never
  compute frames in a timer or per-frame callback.
- **Never resize a window per frame.** Click areas change once at each end of a transition.
- **One process, one main thread.** Add neither without a measured reason and explicit approval.
- **Lazy by default.** Create things when first needed, and release them when hidden.
- **Measure, don't assume.** A claim that something is faster needs a number from this hardware.

## Stop and ask first

- Third-party dependencies, of any size
- Extra processes, helpers, daemons, login items
- Private or undocumented APIs
- New always-running threads
- Web views or embedded runtimes
- SwiftUI inside the island (it is allowed only in separate, lazily created windows)

Moving to a shared cross-platform layer is out of scope. The duplication is deliberate.

## Project structure

- Keep the three roles apart: **controller** (owns state), **scene** (owns compositor objects, makes
  no decisions), and **pure logic** (values in, values out, no OS headers, tested).
- Data flows one way: OS event → controller → state → scene.
- New features go in `macos/Noitch/Features/<Name>/` and `windows/src/features/<name>/`, as a source
  and a content part. See [docs/adding-features.md](docs/adding-features.md).
- Don't add a protocol, base class, registry or event bus until two real features need it.
- `macos/Noitch.xcodeproj` uses synchronized folders. Adding a Swift file needs no project change.
- Windows sources are listed in `windows/CMakeLists.txt`.
- Each platform folder is self-contained. Tooling lives in `macos/scripts/` and `windows/scripts/`;
  nothing in one platform folder may refer to the other.
- Generated files stay out of the repository. Add new ones to `.gitignore`.

## Shared behavior

Sizes, radii, spring parameters and interaction rules are listed in
[docs/architecture.md](docs/architecture.md#shared-behavior). A behavior change updates that table
and both apps together.

## Swift conventions

- Swift 6 language mode. The app target uses main-actor default isolation and Approachable
  Concurrency, so don't add `@MainActor` by hand.
- Stay on the main thread. Move work off it only after profiling shows a hang.
- `struct` and `enum` by default; `final class` for identity, such as windows and controllers.
- Log with `Logger` from `os`, never `print`.
- Tests use Swift Testing (`@Test`, `#expect`).
- Match the existing style: comments explain why, not what.

## C++ conventions

- C++20. Exceptions and RTTI are off. Report errors with `HRESULT`.
- Use `ComPtr` for COM objects. No raw `AddRef` or `Release`.
- `PascalCase` types and methods, `camelCase` locals, trailing `_` for members, `kName` for constants.
- Everything lives in `namespace noitch`.
- Avoid heavy standard headers in the app: no `<iostream>`, `<regex>`, `<thread>` or `<filesystem>`.
- Keep pure logic free of `windows.h`, so its tests run on any host.

## Before you finish

macOS changes:

```sh
xcodebuild test -project macos/Noitch.xcodeproj -scheme Noitch -destination 'platform=macOS,arch=arm64' -derivedDataPath macos/build
macos/scripts/measure.sh
```

If you changed the window, hover or animation code, also run `macos/scripts/measure.sh --hover`. It
moves the real cursor for about 25 seconds, so tell the user first. It has caught bugs that unit tests
cannot see.

Windows changes, on Windows:

```powershell
cd windows
cmake --preset default
cmake --build --preset debug
ctest --preset debug
cmake --build --preset release
scripts\measure.ps1
```

Windows pure logic can also be tested on macOS:

```sh
clang++ -std=c++20 -Iwindows/src windows/tests/island_tests.cpp -o /tmp/island_tests && /tmp/island_tests
```

In your summary, report the budget numbers before and after, and state plainly what you could not
run. **Never describe Windows code as working unless it was run on Windows.** Compiling is not running.

CI (`.github/workflows/`) runs builds and unit tests only. A green run says nothing about budgets or
hover, so it never replaces the checks above.

The scripts open real windows and can move the cursor, so tell the user before running them. Never
capture the whole screen: use `screencapture -l <window id>`, or personal data can end up in your
context.
