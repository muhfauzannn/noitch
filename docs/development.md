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

Noitch has no Dock icon or menu bar item. **Right-click the island for Settings and Quit**, or run
`pkill -x Noitch`.

To check Home's layout without the adapter or the network, run a Debug build with sample data:

```sh
macos/build/Build/Products/Debug/Noitch.app/Contents/MacOS/Noitch --demo-media
```

`measure.sh --hover` expects the island to open at once, so set Hover delay to None in settings
first (`defaults write app.noitch.mac hoverDelay -float 0`), and restore it after.

Debug builds also take `--open`, which opens the island at launch so it can be captured with
`screencapture -l <window id>` without moving the cursor, and `--settings`, which opens the settings
window. The demo track has a coloured cover, so the artwork tint shows, and the calendar shows sample events
without asking for access.

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

This no longer links, because mingw has no `windows.media.control.h`. To type-check everything else,
add `-Isrc -c`, compile one file at a time, and put a stub of that header on the include
path. `media_session_source.cpp` can only be checked by MSVC.

## CI

GitHub Actions runs on pushes to `main` and on pull requests, one workflow per platform, each only
when its own folder changes.

| Workflow | Runner | What it does |
| --- | --- | --- |
| `macos.yml` | `macos-26`, Xcode 26.6 | Unit tests, Release build, no coverage in Release |
| `windows.yml` | `windows-2025`, Visual Studio 2026 | Debug and Release builds with MSVC, unit tests, uploads `Noitch.exe` |

**CI proves the code builds and the logic tests pass. It does not prove the app works.** Budgets and
the hover check stay local, because shared runners are too noisy for performance numbers and hover
needs a real cursor. Run them before merging anything that touches the window, hover or animation
code.

To try the Windows app without Visual Studio, download `Noitch-windows-x64` from the workflow run.

## Verification status

Keep this honest and up to date.

### macOS

| Check | Status |
| --- | --- |
| Builds; 64 unit tests | Passing |
| Home glow fading out before the top bends | Checked by window screenshots (`--demo-media --open`, Debug only, with a coloured demo cover) |
| Calendar in Home: week strip, rows, narrower music column | Checked by a window screenshot with `--demo-media --open` (sample events, no EventKit); the access prompt, real calendars, change notifications and the timers **not checked on screen** |
| Closed island with music (wings, six moving bars) | Checked by window screenshots with `--demo-media`; real playback, artwork, pause timing and the player-app watching **not checked on screen** |
| Settings window with sidebar | General page checked by a window screenshot (`--settings`, Debug only); other pages and every control's effect **not checked on screen** |
| Usage page read against real Claude Code and Codex logs | Totals checked by a one-off probe, not against ccusage |
| Usage page ring design | Checked by an offscreen render with sample numbers |
| Claude limits from the usage endpoint and Keychain | **Not run**: needs the Keychain prompt answered in the real app |
| Tab switch, swipe, page animation, usage page layout | **Not checked on screen** |
| Budgets at rest | Passing |
| Immediate hover, top-edge entry, real-notch re-entry, fast scroll, invisible-area rejection (23 automated checks) | Passing on a notched MacBook, on the clean Release build |
| Pill and flare shape, collapsed and expanded | Checked by screenshot |
| Home layout before the redesign, header buttons, opening Settings by click | Checked with `--demo-media` and real clicks |
| Redesigned Home (wash, two lyric lines, skip state) | Checked by an offscreen render; the lyric slide and skip timing **not checked on screen** |
| Content clipped to the pill while opening | **Not checked on screen** |
| Adapter stream, play/pause and seek by real clicks | Passing with a browser player on macOS 26.6 |
| Adapter stops after closing, and on quit | Passing |
| Lyrics lookup from LRCLIB | Passing for one known track, outside a real playback session |
| Artwork, next, previous, shuffle, repeat on a real player | **Not tested**; the player used sent no artwork |
| Spotify and Music | **Not tested** |
| Launch at login, Open on hover turned off | **Not tested** |
| Right-click quit menu | **Not tested** |
| Space swipes, full screen | **Not tested** |
| External display, display without a notch | **Not tested** (geometry is unit tested) |
| Sleep, wake, lid close, display changes | **Not tested** |
| macOS 14 and 15 | **Not tested**; only run on 26.6 |
| Intel | **Not tested**; local Release builds are arm64 only |

### Windows

| Check | Status |
| --- | --- |
| Spring, geometry, Home, lyrics and JSON tests | Passing on macOS, under ASan and UBSan |
| Usage parser, five-hour block, pager and swipe tests | Passing on macOS with clang; not run under ASan/UBSan |
| Pages, header, usage page with rings, shared painter, Claude limit request, `WM_MOUSEHWHEEL` swipe | **Never compiled**: no Windows toolchain was available |
| Settings values, panes, page and usage settings tests | Passing on macOS with clang |
| Settings window with sidebar, registry store, hover delay, page settings in the pager | **Never compiled** |
| Live layout, bar curves and pixels | Passing on macOS with clang |
| Closed island with music: shared media monitor, `LiveScene`, `AddRepeat` bar animations | **Never compiled** |
| Compiles | With Zig and mingw, using a stub for the WinRT header. It no longer links there |
| `media_session_source.cpp` | **Never compiled**: only the Windows SDK has its headers |
| CMake configures; resolved MSVC flags inspected | Done, without MSVC itself |
| **Built with MSVC** | **Never**; `windows.yml` will do this on its first run |
| **Run on Windows** | **Never** |
| Rendering, hover, click-through, DPI, Home, Settings | **Unverified** |

Treat the Windows app as untested code. Start with
[First run on Windows](performance.md#first-run-on-windows).

## Before distributing

- `app.noitch.mac` is a placeholder bundle identifier.
- macOS builds are ad-hoc signed. Distribution needs a Developer ID and notarization.
- The Windows binary is unsigned, which will trigger SmartScreen warnings.
