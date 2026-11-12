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
window. `--demo-tasks` fills the tasks page with sample tasks, with no account or network, and
announces a new one four seconds after launch, to see the alert; add `-openOn tasks` to open on it.
With `--demo-all-clear` only an overdue sample is left, for the empty state. With `--demo-media` as
well nothing is unseen, so the island opens on Home and the calendar shows the samples' deadlines.
Add `--demo-romanization` to `--demo-media --open` to show a Japanese sample directly on the
lyrics stage, including a kanji-only line and mixed Latin text, without any network lookup.
The demo track has a coloured cover, so the artwork tint shows, and the calendar shows sample events
without asking for access.

Quit other notch apps such as Boring Notch while testing. They compete for the same screen area and
the same mouse events.

New Swift files are picked up automatically; don't edit the project file to add them.

`--demo-shelf --open -openOn shelf` opens Shelf with sample file cards and disables recording for
that process. `macos/scripts/render-shelf.sh` renders Files, Clipboard and empty-state previews
without reading the real clipboard or opening windows; generated PNGs live in `macos/build/`.
`measure.sh --clipboard-paused [--hover]` pauses recording only for the measured process, so the
original no-polling idle budget can be checked without changing the saved setting.

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
clang++ -std=c++20 -Iwindows/src windows/tests/island_tests.cpp windows/tests/windows_sdk_compatibility.cpp -o /tmp/island_tests && /tmp/island_tests
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

When a Windows CI build succeeds, its `Noitch-windows-x64` artifact lets you try the app without
Visual Studio. The latest native build failed before producing this artifact.

## Verification status

Keep this honest and up to date.

### macOS

| Check | Status |
| --- | --- |
| Builds; 117 unit tests | Passing locally on Xcode 27.2, including shelf, romanization and paired lyric animation regression tests. Xcode 26.6 CI retry remains pending after the callback fix below |
| Home glow fading out before the top bends | Checked by window screenshots (`--demo-media --open`, Debug only, with a coloured demo cover) |
| Calendar in Home: three-day strip with arrows, deadline rings, rows, narrower music column | Checked by a window screenshot with `--demo-tasks --demo-media --open` (sample events and tasks, no EventKit); the arrows by click, a deadline row, the access prompt, real calendars, change notifications and the timers **not checked on screen** |
| Pin in the header, General's shortcut field, Usage's marks, calendar dates (type with an underline) | Checked by window screenshots (`--demo-media --open --settings`); pinning by click or shortcut, recording a shortcut, the days' push, the underline's spring and the lyric fix during opening **not checked on screen** |
| Tasks page (day bars, indigo palette, overdue toggle, all clear) and the closed-island slip and dot | Checked by window screenshots with `--demo-tasks`, `--demo-all-clear` and `--open`; clicks, scrolling, hover and the bars' reveal **not checked on screen** |
| Tasks against a real Moodle site, sign-in, Keychain, the 30-minute check, reminders | **Not run**: needs a signed-in account |
| Closed island with music (wings, six moving bars) | Checked by window screenshots with `--demo-media`; real playback, artwork, pause timing and the player-app watching **not checked on screen** |
| Settings window with sidebar | General page checked by a window screenshot (`--settings`, Debug only); other pages and every control's effect **not checked on screen** |
| Usage page read against real Claude Code and Codex logs | Totals checked by a one-off probe, not against ccusage |
| Usage page ring design | Checked by an offscreen render with sample numbers |
| Claude limits from the usage endpoint and Keychain | **Not run**: needs the Keychain prompt answered in the real app |
| Tab switch, swipe, page animation, usage page layout | **Not checked on screen** |
| Budgets at rest | Latest romanization runs exceed budgets, including the pre-change baseline; see performance.md |
| Immediate hover, top-edge entry, real-notch re-entry, fast scroll, invisible-area rejection (23 automated checks) | Previously passing; latest shelf run is blocked by Window Server/loginwindow covering the test point. The checker now reports BLOCKED for obstructed sessions. See performance.md |
| Pill and flare shape, collapsed and expanded | Checked by screenshot |
| Home layout before the redesign, header buttons, opening Settings by click | Checked with `--demo-media` and real clicks |
| Redesigned Home (wash, two lyric lines, skip state) | Checked by an offscreen render; the lyric slide and skip timing **not checked on screen** |
| Content clipped to the pill while opening | **Not checked on screen** |
| Adapter stream, play/pause and seek by real clicks | Passing with a browser player on macOS 26.6 |
| Adapter stops after closing, and on quit | Passing |
| Shelf / Clipboard | 116 tests pass, including shelf bounds, deduplication, pinned eviction, isolated text/image/file pasteboard round trips, private-marker exclusion, payload pruning and keeping originals. Native previews inspected offscreen. Idle budgets pass with recording paused; active wakeups use the approved exception. Hover/real drag/drop and after-use memory blocked by the locked session; Windows UI remains unverified |
| Lyrics stage romanization | Japanese sample checked again with a window-only screenshot after pairing each reading with its original; paired Latin animation has a regression test for line advancement and seek; the earlier standalone hover checks passed with 6.57 ms/transition; kanji reading, kanji-only lines with track context, Latin passthrough, Chinese, Korean and Cyrillic covered by tests. Real playback and retained memory after tokenizer use remain unverified |
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

#`--demo-shelf --open -openOn shelf` opens Shelf with sample file cards and disables recording for
that process. `macos/scripts/render-shelf.sh` renders Files, Clipboard and empty-state previews
without reading the real clipboard or opening windows; generated PNGs live in `macos/build/`.
`measure.sh --clipboard-paused [--hover]` pauses recording only for the measured process, so the
original no-polling idle budget can be checked without changing the saved setting.

## Windows

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
| **Built with MSVC** | CI configured successfully, but Debug compilation failed on a Windows SDK macro collision and two `ComPtr` cleanup loops. Source fixes are local; a new native build is pending |
| Lyrics stage romanization using system ICU and Japanese IME | Pure model tests, including reading selection across line advancement, pass on macOS; Windows source has **not been compiled or run** |
| Shelf / Clipboard | Pure collection/layout and bounded DIB thumbnail tests pass on macOS. New Windows UI/OLE/clipboard code has **not been compiled or run** here |
| **Run on Windows** | **Never** |
| Rendering, hover, click-through, DPI, Home, Settings | **Unverified** |

Treat the Windows app as untested code. Start with
[First run on Windows](performance.md#first-run-on-windows).

## CI build repair (2026-11-12)

The [macOS run](https://github.com/muhfauzannn/noitch/actions/runs/43251892954) failed during
compilation, before tests ran: Xcode 26.6 could not diagnose the Shelf timer expression returning
`ShelfItem?` through `MainActor.assumeIsolated`. The callback now explicitly discards that value,
so its actor-isolated closure returns `Void`. Recording interval, run-loop mode and lifetime stay
the same. Xcode 27.2 locally builds and passes all 117 tests; Xcode 26.6 is not installed locally,
so that exact compiler still needs a fresh CI run.

The [Windows run](https://github.com/muhfauzannn/noitch/actions/runs/43251893235) configured with
MSVC, then failed in Debug compilation. `task_model.h` used `small`, a macro supplied by the Windows
SDK, as a local variable name. It is now `isSmallWord`. Shelf and Tasks tried to take addresses of
`ComPtr` values in a cleanup initializer list; WRL overloads that operator for COM output parameters.
Both scenes now reset each owned format directly, in the same order. No raw COM reference counting
was introduced. A separate test translation unit recreates the SDK macro and is included in the
CMake test target; it failed before the rename and passes with the fix on macOS.

These repairs do not establish a successful MSVC build or native Windows runtime behavior. No CI
job was skipped or test disabled. A push containing the platform source changes will trigger both
existing platform workflows. Release creation still requires a `v*` tag and configured signing.

## Releases

Tag-based CD builds signed macOS arm64 DMGs and Windows x64 portable executables into a private
GitHub draft. Explicit publication verifies the original build artifacts again and uses a protected
environment. See [releases.md](releases.md) for certificates, variables, secrets, approval and recovery.
The workflow has not yet been run with production signing credentials. Local CD verification passed
117 macOS tests, Windows pure-logic tests on macOS, five release-contract tests, shell syntax checks
and YAML parsing. PowerShell/MSVC and end-to-end signing/publication have not been run. The two
idle budget runs exceed CPU/wakeup limits; see performance.md. CD does not replace native smoke tests
or local budgets.

## Before distributing

- `app.noitch.mac` is a placeholder bundle identifier.
- macOS builds are ad-hoc signed. Distribution needs a Developer ID and notarization.
- The Windows binary is unsigned, which will trigger SmartScreen warnings.
