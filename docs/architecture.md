# Architecture

Noitch is a Dynamic Island style overlay for macOS and Windows: a small black shape at the top of
the screen that expands on hover. There are two independent native apps. They share a design and a
behavior spec, and no code.

## The core idea

Three rules produce almost all of the performance, and both apps are built on them.

1. **Retained layers.** The UI is a small tree of compositor objects, not something repainted.
2. **Out-of-process animation.** A transition is described once and handed to the OS compositor,
   which evaluates it in its own process. Our process does no per-frame work.
3. **Event-driven everything.** The main thread sleeps in the OS until something happens. There are
   no timers, polling loops or display links at rest.

The result is that the app's cost is proportional to the number of events it handles, not to time
passing or to frames drawn. The spec for "at rest" is simple: **zero threads runnable**.

| Concern | macOS | Windows |
| --- | --- | --- |
| Language | Swift 6 | C++20 |
| Window | `NSPanel`, borderless, non-activating | `WS_POPUP`, `WS_EX_NOACTIVATE`, `WS_EX_NOREDIRECTIONBITMAP` |
| Scene | `CALayer` tree | DirectComposition visual tree |
| Compositor | Render server (out of process) | DWM, `dwm.exe` (out of process) |
| Pill shape | Layer `bounds` and `cornerRadius` | Rounded `IDCompositionRectangleClip` over a stretched 1×1 surface |
| Top flares | Two fixed `CAShapeLayer`s that only move | Two small fixed surfaces, pixels computed once per DPI |
| Animation | `CASpringAnimation` | Spring sampled into cubic `IDCompositionAnimation` segments |
| Idle wait | `NSApplication` run loop | `GetMessageW` |
| Hover | `NSTrackingArea` | `WM_MOUSEMOVE` + one-shot `TrackMouseEvent` |
| Hit region | Window frame, two sizes | `SetWindowRgn` on a fixed window |
| Transition end | `CATransaction` completion | One-shot `SetTimer` |
| Now playing | MediaRemote adapter child process, only while open | `GlobalSystemMediaTransportControlsSessionManager`, only while open |
| Text and images | `CATextLayer`, layer `contents` | One Direct2D + DirectWrite surface |
| Settings | AppKit window, `UserDefaults` | Win32 window, `HKCU\Software\Noitch` |
| Dependencies | MediaRemoteAdapter (BSD 3-Clause, vendored) | None beyond the OS and Windows SDK |

Why each technology was chosen, and the evidence, is in [tech-stack.md](tech-stack.md).

## Repository layout

```text
AGENTS.md                  Rules for coding agents
README.md                  Overview and quick start
.github/workflows/         CI, one workflow per platform
docs/                      This documentation
macos/
  Noitch.xcodeproj         Synchronized folders: new files need no project edits
  Noitch/App/              Process entry and lifecycle
  Noitch/Island/           The island: window, layers, geometry, state
  Noitch/Features/         Home, NowPlaying, Lyrics, Settings
  Vendor/MediaRemoteAdapter/ Third-party adapter, embedded in the app bundle
  NoitchTests/             Swift Testing, pure logic
  scripts/                 Budget and hover checks against the real app
  benchmarks/ui-stack/     The experiment behind the macOS stack decision
windows/
  CMakeLists.txt
  src/main.cpp             Process entry and message loop
  src/island/              The island: window, compositor, geometry, spring
  src/features/            home, now_playing, lyrics, settings
  tests/                   Pure logic tests; run on any host
  scripts/                 Budget check against the real app
```

Each platform folder is self-contained: it holds its own tooling, and nothing in one refers to the
other. Only documentation is shared.

## Module responsibilities

The two apps have the same shape, with three kinds of module:

| Role | macOS | Windows | Rule |
| --- | --- | --- | --- |
| Controller: owns state, reacts to events | `IslandController`, `HomeController` | `IslandWindow`, `HomeController` | The only places state changes |
| Scene: owns compositor objects | `IslandView`, `IslandPanel`, `HomeScene` | `Compositor`, `HomeScene` | Makes no decisions |
| Pure logic: values in, values out | `IslandGeometry`, `IslandState`, `HomeLayout`, `HomeModel`, `NowPlaying`, `Lyrics` | `island_geometry.h`, `spring.h`, `home_layout.h`, `home_model.h`, `now_playing.h`, `lyrics.h` | No OS headers, fully tested |

`HomeController` owns what Home knows. The island tells it when it opens and closes, and forwards
pointer events.

Data flows one way: **OS event → controller → new state → scene**. The scene never reads state back
from the compositor, except to start a transition from what is on screen.

## Window model

The window doubles as the click target, so it must not cover anything but the pill. A transparent
window would swallow clicks meant for whatever is underneath.

A transition therefore changes the click area exactly twice per round trip, never per frame:

1. **Expanding:** grow the click area, then start the animation.
2. **Collapsing:** start the animation, and shrink the click area when it finishes.

- macOS has no window regions, so the panel frame itself switches between two sizes.
- Windows keeps one fixed window and switches a `SetWindowRgn` region. The coordinate space never
  moves, so nothing has to stay in step with the animation.

The collapsed area includes the flares. The expanded area includes `overshootMargin`, which covers
the flares and the brief moment a spring passes its target.

Neither window ever takes focus. Typing, the frontmost app and Alt-Tab are unaffected.

## Interruption

An exit can reverse an expansion mid-transition. Returning to the real collapsed notch area can
reverse collapse immediately, even when the expanded window is still shrinking. The former
expanded area alone cannot open the island. No exit/re-entry lock is used.

Mouse movement and scroll events inside the overlay also reconcile the hover target. This recovers
fast approaches that span a tracking-area replacement without polling or a global event monitor.

- macOS reads the on-screen value from `layer.presentation()` and starts a new spring from it.
- Windows cannot read values back from DWM. `PillMotion` knows the curve analytically, so it computes
  the current value and velocity from elapsed time and starts from those.

On macOS, replacing a running animation fires its completion immediately, while the replacement is
still on screen. A state check cannot catch this, because the state is already correct. Each
transition therefore has a number, and only the latest may shrink the click area. On Windows the
single settle timer is simply reset, so a stale completion cannot occur.

## Home

Home is what the expanded island shows: artwork, title, artist, the current lyric line, a progress
slider and transport buttons, plus lyrics and settings buttons in the header band beside the notch.
The lyrics button swaps the text column for the previous, current and next line.

Nothing about it runs at rest. On each platform:

1. Opening the island starts the source and builds the scene.
2. While open and playing, one one-shot timer fires at the next whole second or lyric, whichever
   comes first. Paused or closed, nothing is scheduled.
3. Closing stops that timer and starts a 10 second one-shot. If the island doesn't reopen, the source
   stops and the scene, artwork, text and lyrics are released.

The grace period means hovering in and out doesn't restart the source each time.

- On macOS the progress bar is a linear `CABasicAnimation`, set once per playback change, so the
  render server moves it. Content fades in and out.
- On Windows the bar is redrawn with the time label, once per second. There is no fade: the surface
  sits inside the pill's clip, so the spring reveals and hides it in DWM.

Home has no data while the island is closed, so there is no now playing indicator on the collapsed
pill. Adding one would mean keeping the source running at rest, which needs measuring first.

### Now playing on macOS

macOS has no public API for this, and since 15.4 MediaRemote only answers platform binaries. Noitch
uses [MediaRemoteAdapter](https://github.com/ungive/mediaremote-adapter), the same as Boring Notch: a
framework loaded by `/usr/bin/perl`. This is a deliberate, approved exception to the single-process,
no-private-API and no-dependency rules, limited as follows.

- `stream` runs only while the island is open, plus the grace period. At rest there is no child.
- Commands are short-lived `perl` children too, about 90 ms each.
- Seeking is the exception. The adapter's `seek` reports failure on macOS 26.6, so `seek` calls
  `MRMediaRemoteSetElapsedTime` directly, as Boring Notch does. MediaRemote enters our process only
  when the slider is first used.
- The pipe is read by a `DispatchSource` on the main queue, so no thread is added.
- If the child exits it isn't restarted until the next opening, so a broken adapter can't spin.

A macOS update could break the adapter. Home then shows "Nothing playing"; nothing else is affected.

### Lyrics

Both apps look up the track on [LRCLIB](https://lrclib.net), which sends the track, artist and album
names to that service. A track with no artist is skipped, because that is usually a browser tab whose
title shouldn't be sent. Results are cached on disk, and misses aren't, so a track that gains lyrics is
found later. "Show lyrics" in settings turns off both the display and the lookup.

## Settings

| Setting | Default | Effect |
| --- | --- | --- |
| Launch at login | Off | `SMAppService` on macOS, the `Run` registry value on Windows |
| Open on hover | On | When off, a click opens the island. Leaving still closes it |
| Show lyrics | On | Lyric line, lyrics button, and the LRCLIB lookup |
| Show shuffle and repeat | On | The two outer transport buttons |
| Tint progress bar with artwork | On | Average artwork color, lifted so dark covers stay visible |

Open settings with the header's gear or the secondary-click menu. The window is created on first use
and released on close. macOS uses AppKit, not SwiftUI, because
[the first SwiftUI window](tech-stack.md#swiftui) would use most of the footprint budget for good.

## Shared behavior

The spec both apps follow. Change it here and in both apps together.

| Property | Value |
| --- | --- |
| Expanded size | 580 × 180 pt |
| Collapsed size | Physical notch if present, otherwise 180 pt wide |
| Collapsed height without a notch | macOS: menu bar height. Windows: 32 |
| Corner radius | 10 collapsed, 24 expanded, bottom corners only |
| Top flares | Concave curve on each side where the island meets the screen edge, 8 pt, same size in both states |
| Corner shape | macOS: continuous. Windows: circular (DirectComposition has no continuous corner) |
| Overshoot margin | 16 pt |
| Spring | Response 0.38 s, bounce 0.18 |
| Expand / collapse | Expand immediately when the pointer reaches the real notch, including at the screen top edge; collapse immediately on exit. The real notch can reopen during collapse; the former expanded area cannot |
| Secondary click | Menu with Settings and Quit |
| Home layout | Numbers in `HomeLayout` (macOS) and `home_layout.h` (Windows), which must match |
| Release after closing | 10 s |

The Windows spring is computed from the same two parameters. Its stiffness (273.4) and damping
(27.12) match the values `CASpringAnimation` reports, and it settles in 0.56 s against 0.575 s. The
two have not yet been compared side by side on screen.

## Patterns worth keeping

- **Value-based geometry.** Layout is a pure function of display traits and state, so edge cases are
  unit tests rather than manual checks.
- **Black over a black notch.** The collapsed pill matches the camera housing, so it looks like the
  hardware itself is expanding.
- **Solid shapes, not bitmaps.** Neither app's pill has any pixels of its own to allocate or redraw.
- **Move shapes, don't reshape them.** The flares never change shape. Each follows a pill edge with
  the same spring, which avoids path animation entirely.
- **Lazy everything.** Nothing exists until first needed, and content is released on collapse.

## Known limitations

- **Windows has compiled but never run.** See [development.md](development.md#verification-status).
- **Other notch apps.** Two overlays cannot share the notch. The macOS panel sits one level above
  Boring Notch so that it receives mouse events, but both may still react. Quit other notch apps
  when testing.
- **Space swipes on macOS.** A standard panel can disappear during a Space swipe or full-screen
  transition. One open-source notch app solves this with a private CGS overlay space. Noitch uses
  public API only so far; check this by hand and decide before adopting anything private.
- **Now playing on macOS depends on undocumented behavior.** See [Now playing on macOS](#now-playing-on-macos).
- **Plain lyrics** show their first three lines in the panel, with no scrolling.
- **Long titles** are truncated, not scrolled.
- **Full screen.** Neither app hides itself for full-screen video or games yet.
- **One display.** The island appears on the notched display, or the main one on macOS, and on the
  primary monitor on Windows.
- **GPU device loss on Windows** is checked on display changes only, not when it happens.
