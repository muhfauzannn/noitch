# Development

## macOS

Requires Xcode 26 or later (tested on 27.2). No other tools.

```sh
open macos/Noitch.xcodeproj                 # Then Run

# Or from the command line
xcodebuild -project macos/Noitch.xcodeproj -scheme Noitch -configuration Release -derivedDataPath macos/build build
open macos/build/Build/Products/Release/Noitch.app

# Tests
xcodebuild test -project macos/Noitch.xcodeproj -scheme Noitch -destination 'platform=macOS,arch=arm64' -derivedDataPath macos/build
```

Noitch has no Dock icon or menu bar item. **Right-click the island to quit**, or run `pkill -x Noitch`.

Quit other notch apps such as Boring Notch while testing. They compete for the same screen area and
the same mouse events.

New Swift files are picked up automatically; don't edit the project file to add them.

## Windows

Requires Visual Studio 2022 or later with the Desktop development with C++ workload, and CMake 3.25
or later. No other tools.

```powershell
cd windows
cmake --preset default
cmake --build --preset release              # build\Release\Noitch.exe
cmake --build --preset debug
ctest --preset debug
```

New source files must be added to `windows/CMakeLists.txt`.

The pure logic tests also run on macOS:

```sh
clang++ -std=c++20 -Iwindows/src windows/tests/island_tests.cpp -o /tmp/island_tests && /tmp/island_tests
```

To check that the Windows code still compiles without a Windows machine, use Zig's bundled mingw
headers. This catches type errors only, and it is not a substitute for building with MSVC:

```sh
cd windows
zig c++ -target x86_64-windows-gnu -std=c++20 -fno-exceptions -fno-rtti -DUNICODE -D_UNICODE \
  -DWIN32_LEAN_AND_MEAN -DNOMINMAX -municode -Wl,--subsystem,windows \
  src/main.cpp src/island/compositor.cpp src/island/island_window.cpp \
  -ld3d11 -ldcomp -lshcore -luser32 -lgdi32 -luuid -o /tmp/Noitch.exe
```

## Verification status

Keep this honest and up to date.

### macOS

| Check | Status |
| --- | --- |
| Builds; 5 unit tests | Passing |
| Budgets at rest | Passing |
| Hover, reversal, click area sizing (8 automated checks) | Passing on a notched MacBook, with Boring Notch running. Last run was before coverage was turned off in Release, with the same source |
| Pill shape | Checked by screenshot |
| Right-click quit menu | **Not tested** |
| Space swipes, full screen | **Not tested** |
| External display, display without a notch | **Not tested** (geometry is unit tested) |
| Sleep, wake, lid close, display changes | **Not tested** |
| macOS 14 and 15 | **Not tested**; only run on 26.6 |
| Intel | **Not tested**; local Release builds are arm64 only |

### Windows

| Check | Status |
| --- | --- |
| Spring and geometry tests | Passing on macOS, under ASan and UBSan |
| Compiles and links | With Zig and mingw only |
| CMake configures; resolved MSVC flags inspected | Done, without MSVC itself |
| **Built with MSVC** | **Never** |
| **Run on Windows** | **Never** |
| Rendering, hover, click-through, DPI | **Unverified** |

Treat the Windows app as untested code. Start with
[First run on Windows](performance.md#first-run-on-windows).

## Before distributing

- `app.noitch.mac` is a placeholder bundle identifier.
- macOS builds are ad-hoc signed. Distribution needs a Developer ID and notarization.
- The Windows binary is unsigned, which will trigger SmartScreen warnings.
