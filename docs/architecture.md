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
| Claude limits | Undocumented usage endpoint, Keychain login, only while the usage page shows | Same endpoint, `.credentials.json` login |
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
  Noitch/Features/         Home, NowPlaying, Lyrics, Usage, Settings
  Vendor/MediaRemoteAdapter/ Third-party adapter, embedded in the app bundle
  NoitchTests/             Swift Testing, pure logic
  scripts/                 Budget and hover checks against the real app
  benchmarks/ui-stack/     The experiment behind the macOS stack decision
windows/
  CMakeLists.txt
  src/main.cpp             Process entry and message loop
  src/island/              The island: window, compositor, geometry, spring
  src/features/            home, now_playing, lyrics, usage, settings
  tests/                   Pure logic tests; run on any host
  scripts/                 Budget check against the real app
```

Each platform folder is self-contained: it holds its own tooling, and nothing in one refers to the
other. Only documentation is shared.

## Module responsibilities

The two apps have the same shape, with three kinds of module:

| Role | macOS | Windows | Rule |
| --- | --- | --- | --- |
| Controller: owns state, reacts to events | `IslandController`, `PageController`, `HomeController`, `UsageController` | `IslandWindow`, `PageController`, `HomeController`, `UsageController` | The only places state changes |
| Scene: owns compositor objects | `IslandView`, `IslandPanel`, `PagerScene`, `HomeScene`, `UsageScene` | `Compositor`, `Painter`, `HeaderScene`, `HomeScene`, `UsageScene` | Makes no decisions |
| Pure logic: values in, values out | `IslandGeometry`, `IslandState`, `IslandPage`, `HomeLayout`, `HomeModel`, `NowPlaying`, `Lyrics`, `AIUsage`, `UsageModel`, `UsageLayout` | `island_geometry.h`, `spring.h`, `island_page.h`, `home_layout.h`, `home_model.h`, `now_playing.h`, `lyrics.h`, `ai_usage.h`, `usage_model.h`, `usage_layout.h` | No OS headers, fully tested |

`PageController` owns which page is showing. The island tells it when it opens and closes and
forwards pointer and scroll events; it passes them on to the current page. Each feature controller
owns what its page knows, and is told "open" only while its page is the one on screen.

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

## Pages

The expanded island has pages, in tab order: Home, then Usage. A shared header in the band beside the
notch holds a tab per page at the left and settings at the right; pages keep their own buttons clear
of it (`HeaderLayout` and `island_page.h`, tested against Home's controls).

- **Switching.** Click a tab, or swipe two fingers sideways: fingers moving left bring the next page.
  `PageSwipe` turns a gesture into at most one step, so momentum can't skip a page. A pause of
  0.25 s starts a new gesture. macOS ignores momentum events; Windows reads `WM_MOUSEHWHEEL`.
- **Animation.** Slide plus crossfade, all in the compositor. The old page fades out in 0.2 s while
  sliding 24 pt toward its side in 0.32 s; the new one slides in from the other side over 0.42 s and
  fades in over 0.3 s after 0.04 s, on the shared ease-out curve. Each starts from its on-screen
  value, so a quick second switch reverses smoothly. On macOS the tab plate follows with a short
  spring; on Windows the header is redrawn with the plate in place. Reduce Motion drops the slide on
  macOS.
- **Lifecycle.** Only the current page is open. Leaving a page closes it exactly as closing the
  island would, so its source stops and it is released 10 s later. The header and, on Windows, the
  shared Direct2D painter are released 11 s after the island closes.
- On Windows the content is a container visual inside the pill's clip with one child per layer:
  the header on top, pages below, each with its own opacity effect and X offset.

## Home

Home is the first page: large artwork on the left, and beside it the title and artist, the lyric line
being sung (bright) with the next one below it (dim and smaller), a progress slider and transport
buttons. Behind it, a glow of the artwork's average colour spreads from the artwork in an ellipse that has
faded out by the top edge, so the bends where the pill meets the black flares show no seam (numbers
in `HomeLayout.glowRadii` and `home_layout.h`). "Glow behind the page" and "Tint the progress bar"
turn the glow and the bar's colour on or off.

- **Lyrics.** When the song moves on one line, the bright line rises and fades, the dim line grows
  into its place and the following line rises in below: three text layers take turns, and only
  position, scale and opacity move, so no text is redrawn during the motion. Any other change,
  such as a seek or a new track, crossfades. Before the first line and on empty lines, "♪" holds the
  place. On Windows the surface is redrawn in place, without the slide.
- **A new track** rises a few points into place as its title and artist crossfade.
- **Next and previous** answer at once. Until the player reports a different title, the old title,
  artist and artwork stay at 40% and the clock reads 0:00; after 4 s without a new title it ends
  anyway, for a track that restarted. Players, browsers especially, can take a second or more to
  report the new track, and the adapter adds no delay of its own (`stream` runs without debounce).
- **Buttons** sink to 88% while pressed on macOS.
- The island's content is clipped to the pill on macOS, by a layer whose bounds follow the pill's
  spring, so nothing shows outside the black while it opens or closes. On Windows the content
  already sits inside the pill's clip.

Nothing about it runs at rest. On each platform:

1. Opening the island starts the source and builds the scene.
2. While open and playing, one one-shot timer fires at the next whole second or lyric, whichever
   comes first. Paused or closed, nothing is scheduled.
3. Closing stops that timer and starts a 10 second one-shot. If the island doesn't reopen, the source
   stops and the scene, artwork, text and lyrics are released.

The grace period means hovering in and out doesn't restart the source each time.

- On macOS the progress bar is a linear `CABasicAnimation`, set once per playback change, so the
  render server moves it.
- On Windows the bar is redrawn with the time label, once per second.

Opening reveals the current page after a 0.06 s head start for the pill: it fades in over 0.3 s and drops
8 pt out of the notch over 0.42 s, on an ease-out curve. Closing fades it out in 0.14 s. Reopening
mid-fade continues from what is on screen.

- macOS reveals the header first, then the page's groups 0.045 s apart (Home: artwork and lyrics
  button, text, slider, transport; Usage: one per column), and scales the whole of it from 0.94
  with the opening spring. Reduce Motion drops the slide.
- Windows draws each layer into one surface, so the content fades and slides as a whole: no
  stagger and no scale. The
  ease-out is an exact cubic, one `IDCompositionAnimation` segment each for an effect group's
  opacity and the visual's Y offset.

Home has no data while the island is closed, so there is no now playing indicator on the collapsed
pill. Adding one would mean keeping the source running at rest, which needs measuring first.

### Now playing on macOS

macOS has no public API for this, and since 15.4 MediaRemote only answers platform binaries. Noitch
uses [MediaRemoteAdapter](https://github.com/ungive/mediaremote-adapter), the same as Boring Notch: a
framework loaded by `/usr/bin/perl`. This is a deliberate, approved exception to the single-process,
no-private-API and no-dependency rules, limited as follows.

- `stream` runs while the island is open, plus the grace period, and, when "Show what's playing" is
  on, while a media player is running (approved on 2026-11-11). Players are a fixed list of music
  apps and browsers in `MediaPlayers`; launches and quits arrive as `NSWorkspace` notifications, so
  nothing polls. With no player running, or the setting off, there is no child at rest. A browser
  open all day keeps the child running all day: about 26 MB of its own, outside our budgets.
- `NowPlayingMonitor` owns the one stream; Home and the closed island both read it, so there is
  never a second child.
- Commands are short-lived `perl` children too, about 90 ms each.
- Seeking is the exception. The adapter's `seek` reports failure on macOS 26.6, so `seek` calls
  `MRMediaRemoteSetElapsedTime` directly, as Boring Notch does. MediaRemote enters our process only
  when the slider is first used.
- The pipe is read by a `DispatchSource` on the main queue, so no thread is added.
- If the child exits it isn't restarted until the next opening, so a broken adapter can't spin.

A macOS update could break the adapter. Home then shows "Nothing playing"; nothing else is affected.

### Closed island while music plays

While something plays, the closed pill widens into a wing on each side of the notch, with the same
kind of spring as opening (response 0.42 s, bounce 0.16): the artwork on the left, four level bars on
the right, tinted with the artwork's colour when "Tint the progress bar" is on. Numbers are in
`LiveLayout` (macOS) and `live_layout.h` (Windows). Hovering the wings opens the island as the notch
does.

- **Bars.** Each is a looping compositor animation with its own period (0.47, 0.61, 0.39, 0.53 s),
  so they never line up. They are a rhythm, not the audio: reading real levels would need an
  audio-capture permission and work on every frame. Paused, they rest low and nothing animates.
- **Pause.** The wings stay 10 s after a pause, so a quick pause doesn't flicker the pill, then
  narrow away. One one-shot timer.
- **Track change.** The artwork crossfades; the pill doesn't move.
- **Resizing.** As with opening, the window (macOS) or hit region (Windows) grows before widening
  and shrinks after narrowing, never per frame.
- **Lifetime.** The layers are made when the wings first show and released 2 s after they go.
- **Windows.** The media session subscription now runs from launch while the setting is on, so WinRT
  loads at start instead of on first opening. It needs no other process.

### Usage

The second page answers one question per tool: how much of my five-hour and weekly limit is left,
and will it last? For Claude Code and Codex side by side:

- **Gauge.** Two rings: five hours inside, the week outside, in the tool's colour (Claude coral, Codex
  blue; the week lighter). A white dot on each ring marks how much of that window's *time* has
  passed, so fill running past the dot means usage is ahead of the clock. The middle shows the
  five-hour percentage. On macOS the rings sweep up from empty as the page appears.
- **Rows.** Percentage, pace (▲ ahead, ● even, ▼ under, comparing use with time gone, ±10 points)
  and when each window resets: a countdown for five hours, a day and time for the week.
- **Insight.** The one thing worth knowing, most urgent first: a limit reached and when it comes
  back; a limit that runs out before its reset at the current pace, and how long before the reset
  it does; otherwise the five-hour use projected at its reset ("Plenty of room", "On track",
  "Cutting it close"). Rings and text turn yellow, then red, as it gets urgent.
- **Pace.** The five-hour projection uses the last half hour, the week its average so far. Codex's
  recent pace comes from the percentages it logs with every turn. Claude reports only a percentage,
  so its recent pace is that percentage times the share of the window's cost (from local logs)
  spent in the last half hour.
- **Today.** Tokens and estimated cost today, under each name, from the local logs.
- **Remembered.** The last good numbers per tool are kept in a small cache file (`Caches/Noitch/limits`
  on macOS, `%LOCALAPPDATA%\Noitch\limits` on Windows, one line per tool in the same format). They
  show at once when the page opens, while the request is out, and in place of "Limits unavailable"
  when offline and under "Login expired"; Codex's are used when its logs have aged out of the day the page reads. Remembered
  numbers say "As of" and when in place of today's totals. A window whose reset has passed shows
  0%. Written only when the numbers change or are ten minutes older, and only while the page shows.

Where the numbers come from:

- **Codex** writes `rate_limits` (five hours and a week, with reset times) into its session logs,
  `~/.codex/sessions` or `CODEX_HOME`. Nothing is sent anywhere.
- **Claude Code** keeps no percentages on disk. Its logs record a limit only when one is hit. The
  numbers come from the endpoint its own `/usage` command calls, `GET
  https://api.anthropic.com/api/oauth/usage` with `anthropic-beta: oauth-2025-04-20`, using Claude
  Code's login: the Keychain item "Claude Code-credentials" on macOS, `.credentials.json` in the
  Claude folder on Windows. This is an undocumented API, a network request and a read of another
  app's credential, approved by the owner on 2026-11-10 with these limits:
  - One request when the page is shown, and one a minute while it stays shown. None otherwise.
  - The token is read for each request and never stored, logged or refreshed. Refreshing would
    replace Claude Code's own login and sign it out, so an expired token shows "Login expired"
    and "Run claude in a terminal" over the remembered numbers. Only the `claude` CLI renews this
    login: the Claude desktop app passes its sessions a token of its own and leaves it alone, so
    someone who only uses the desktop app sees this about eight hours after the last CLI run.
  - macOS asks once, through the system's Keychain prompt. After a refusal Noitch stops asking
    until it is opened again.
  - If Anthropic changes the endpoint, the page says "Limits unavailable"; nothing else breaks.
- Local logs: `~/.claude/projects` (and `~/.config/claude/projects`, or `CLAUDE_CONFIG_DIR`) for
  today's totals and Claude's recent pace.

Reading the logs:

- **Counting.** Claude Code repeats a request's usage on each content block, so entries are
  deduplicated by message and request id. Codex repeats a usage event when only its limits change,
  so an event counts only when the running total grows; its cached input is split out of input.
- **Cost.** A built-in price list (`ModelPrice` / `FindPrice`), Anthropic list prices as of 2026-11
  and the OpenAI models whose price is known. A model without a price adds tokens but no cost, and
  the cost then shows as a lower bound (`$4.20+`), or a dash if nothing is priced.
- **When.** There is no event to wait for, so it reads when the page is shown and at each whole
  minute while it stays shown, then nothing. Only `.jsonl` files changed in the last 24 hours are
  opened, each mapped, and only bytes appended since the last read are parsed. Lines are found with
  `memchr` and `memmem`; because quotes inside JSON strings are escaped, a `"key":` pattern only
  matches a real key, so the fields are found without parsing the large response text.
- It runs on the main thread. On an M5 the first read of a day's logs (21 MB) took 24 to 59 ms in a
  Release build; later reads took about 3 ms. The request runs off the main thread: a Swift task on
  macOS, a short-lived pool thread on Windows, as lyrics do.

### Lyrics

Both apps look up the track on [LRCLIB](https://lrclib.net), which sends the track, artist and album
names to that service. A track with no artist is skipped, because that is usually a browser tab whose
title shouldn't be sent. Results are cached on disk, and misses aren't, so a track that gains lyrics is
found later. "Show lyrics" in settings turns off both the display and the lookup.

## Settings

A window with a sidebar of pages: General, Pages, then one page per feature (Home, AI Usage), and
About. Each feature owns its part of the settings and its page, next to its code:

| Part | macOS | Windows |
| --- | --- | --- |
| Values | `AppSettings` holds `GeneralSettings`, `PageSettings`, `HomeSettings`, `UsageSettings` | `app_settings.h`, `home_settings.h`, `usage_settings.h` |
| Page | `SettingsPane` built by `SettingsPanes`, `HomeSettingsPane`, `UsageSettingsPane` | `SettingsPane` data from `settings_panes.h`, `home_settings_pane.h`, `usage_settings_pane.h` |
| Controls | `SettingsForm` turns a pane into AppKit rows in rounded groups | `SettingsWindow` turns a pane into Win32 controls |
| Storage | `UserDefaults`, keys unchanged from before the split | `HKCU\Software\Noitch`; fractions stored in thousandths |

A new feature adds its values to `AppSettings` and its page to the window's list; nothing else.

| Page | Setting | Default | Effect |
| --- | --- | --- | --- |
| General | Launch at login | Off | `SMAppService` on macOS, the `Run` registry value on Windows |
| General | Open on hover | On | When off, a click opens the island. A click always opens it. Leaving still closes it |
| General | Hover delay | None | 0.15, 0.3 or 0.5 s of rest in the notch first: one one-shot timer, cancelled on leaving |
| General | Haptic feedback | Off | macOS only: a tap on a Force Touch trackpad as the island opens. Windows has no such API for desktop apps |
| General | Show the island on | Built-in display with a notch | macOS only: or the main (menu bar) display. Windows always uses the primary display |
| Pages | AI Usage | On | Off removes its tab and swipe stop. Home is always there |
| Pages | Open on | The page used last | Or always Home, or always AI Usage |
| Home | Show lyrics | On | The lyric lines and the LRCLIB lookup |
| Home | Show the next line | On | The dimmer second line |
| Home | Timing | As published | Shifts which line is sung by −1 to +1 s |
| Home | Show shuffle and repeat | On | The two outer transport buttons |
| Home | Tint the progress bar | On | Average artwork color, lifted so dark covers stay visible |
| Home | Glow behind the page | On | The wash of that color behind the page |
| Home | Show what's playing | On | The closed island's wings with artwork and level bars. On macOS this keeps the now playing helper running while a player app is open |
| AI Usage | Claude Code, Codex | On | One column each; one alone is centered. With both off, Claude stays |
| AI Usage | Warn at | 70% | Amber from here; red from 90%, or 5 points above when that is higher |
| AI Usage | Check Claude every | Minute | Or 5 or 15 minutes. Never more often than the approved once a minute; logs are still read every minute |

Open settings with the header's gear or the secondary-click menu. The window is created on first use
and released on close, and only the page on screen has controls. macOS uses AppKit, not SwiftUI,
because [the first SwiftUI window](tech-stack.md#swiftui) would use most of the footprint budget for
good. Sidebar badges are drawn from paths rather than SF Symbols for the same reason.

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
| Spring | Expand: response 0.5 s, bounce 0.14. Collapse: response 0.38 s, bounce 0.18 |
| Pages | Home, Usage. Tabs left in the header band, settings right; header numbers in `HeaderLayout` and `island_page.h` |
| Page switch | Tab click, or a sideways swipe of 40 pt (one wheel notch on Windows); one page per gesture; slide 24 pt with a crossfade, see [Pages](#pages) |
| Usage layout | Numbers in `UsageLayout` (macOS) and `usage_layout.h` (Windows), which must match |
| Usage insight | Rules in `AILimits` (macOS) and `ai_limits.h` (Windows): pace ±10 points, recent pace over 30 min, warnings in yellow and red |
| Closed island with music | Wings of `artSide + 17` pt each side, artwork `notch height − 14` pt, four 3 pt bars; spring 0.42 s, bounce 0.16; 10 s after a pause. See [Closed island while music plays](#closed-island-while-music-plays) |
| Content reveal | See [Home](#home): 0.06 s delay, 0.3 s fade, 8 pt drop over 0.42 s; 0.14 s fade out |
| Expand / collapse | Expand when the pointer reaches the real notch, including at the screen top edge, at once or after the hover delay setting; collapse immediately on exit. The real notch can reopen during collapse; the former expanded area cannot |
| Secondary click | Menu with Settings and Quit |
| Settings | Pages, values and defaults in [Settings](#settings); both apps keep the same pages and rows |
| Home layout | Numbers in `HomeLayout` (macOS) and `home_layout.h` (Windows), which must match |
| Release after closing | 10 s |

The Windows spring is computed from the same parameters. For the collapse spring its stiffness
(273.4) and damping (27.12) match the values `CASpringAnimation` reports, and it settles in 0.56 s
against 0.575 s. The expand spring has not been compared numerically, and neither has been compared
side by side on screen.

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
- **Plain lyrics** aren't shown; Home says they aren't synced.
- **Long titles** are truncated, not scrolled.
- **Full screen.** Neither app hides itself for full-screen video or games yet.
- **One display.** The island appears on the notched display, or the main one on macOS, and on the
  primary monitor on Windows.
- **GPU device loss on Windows** is checked on display changes only, not when it happens.
