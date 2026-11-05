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
| Animation | `CASpringAnimation` | Spring sampled into cubic `IDCompositionAnimation` segments |
| Idle wait | `NSApplication` run loop | `GetMessageW` |
| Hover | `NSTrackingArea` | `WM_MOUSEMOVE` + one-shot `TrackMouseEvent` |
| Hit region | Window frame, two sizes | `SetWindowRgn` on a fixed window |
| Transition end | `CATransaction` completion | One-shot `SetTimer` |
| Dependencies | None beyond the OS | None beyond the OS and Windows SDK |

Why each technology was chosen, and the evidence, is in [tech-stack.md](tech-stack.md).

## Repository layout

```text
AGENTS.md                  Rules for coding agents
docs/                      This documentation
macos/
  Noitch.xcodeproj         Synchronized folders: new files need no project edits
  Noitch/App/              Process entry and lifecycle
  Noitch/Island/           The island: window, layers, geometry, state
  NoitchTests/             Swift Testing, pure logic
windows/
  CMakeLists.txt
  src/main.cpp             Process entry and message loop
  src/island/              The island: window, compositor, geometry, spring
  tests/                   Pure logic tests; run on any host
scripts/                   Budget checks against the real apps
benchmarks/macos-ui-stack/ The experiment behind the macOS stack decision
```

## Module responsibilities

The two apps have the same shape, with three kinds of module:

| Role | macOS | Windows | Rule |
| --- | --- | --- | --- |
| Controller: owns state, reacts to events | `IslandController` | `IslandWindow` | The only place state changes |
| Scene: owns compositor objects | `IslandView`, `IslandPanel` | `Compositor` | Makes no decisions |
| Pure logic: values in, values out | `IslandGeometry`, `IslandState` | `island_geometry.h`, `spring.h` | No OS headers, fully tested |

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

The expanded area includes `overshootMargin`, because a spring passes its target briefly.

Neither window ever takes focus. Typing, the frontmost app and Alt-Tab are unaffected.

## Interruption

A hover can reverse mid-transition, and the shape must not jump.

- macOS reads the on-screen value from `layer.presentation()` and starts a new spring from it.
- Windows cannot read values back from DWM. `PillMotion` knows the curve analytically, so it computes
  the current value and velocity from elapsed time and starts from those.

An interrupted transition also fires its completion, so completion handlers check the current state
before shrinking the click area.

## Shared behavior

The spec both apps follow. Change it here and in both apps together.

| Property | Value |
| --- | --- |
| Expanded size | 440 × 140 pt |
| Collapsed size | Physical notch if present, otherwise 180 pt wide |
| Collapsed height without a notch | macOS: menu bar height. Windows: 32 |
| Corner radius | 10 collapsed, 24 expanded, bottom corners only |
| Corner shape | macOS: continuous. Windows: circular (DirectComposition has no continuous corner) |
| Overshoot margin | 16 pt |
| Spring | Response 0.38 s, bounce 0.18 |
| Expand / collapse | On hover enter / exit, immediately |
| Quit | Secondary click on the island |

The Windows spring is the same closed-form curve that these parameters describe, so the two should
feel alike. This has not yet been compared side by side.

## Patterns worth keeping

- **Value-based geometry.** Layout is a pure function of display traits and state, so edge cases are
  unit tests rather than manual checks.
- **Black over a black notch.** The collapsed pill matches the camera housing, so it looks like the
  hardware itself is expanding.
- **Solid shapes, not bitmaps.** Neither app's pill has any pixels of its own to allocate or redraw.
- **Lazy everything.** Nothing exists until first needed, and content is released on collapse.

## Known limitations

- **Windows has compiled but never run.** See [development.md](development.md#verification-status).
- **Space swipes on macOS.** A standard panel can disappear during a Space swipe or full-screen
  transition. One open-source notch app solves this with a private CGS overlay space. Noitch uses
  public API only so far; check this by hand and decide before adopting anything private.
- **Full screen.** Neither app hides itself for full-screen video or games yet.
- **One display.** The island appears on the notched display, or the main one on macOS, and on the
  primary monitor on Windows.
- **GPU device loss on Windows** is checked on display changes only, not when it happens.
