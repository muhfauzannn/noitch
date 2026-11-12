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
passing or to frames drawn. The spec for "at rest" is simple: **zero threads runnable**. The explicitly approved clipboard
recording exception adds a one-second pasteboard check while enabled; Pause restores that idle
contract. See [Shelf and clipboard](#shelf-and-clipboard).

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
| Moodle tasks | Moodle web service, key in the Keychain, one check every 30 min | Same service, key in Credential Manager |
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
  Noitch/Features/         Home, NowPlaying, Lyrics, Usage, Tasks, Shelf, Settings
  Vendor/MediaRemoteAdapter/ Third-party adapter, embedded in the app bundle
  NoitchTests/             Swift Testing, pure logic
  scripts/                 Budget and hover checks against the real app
  benchmarks/ui-stack/     The experiment behind the macOS stack decision
windows/
  CMakeLists.txt
  src/main.cpp             Process entry and message loop
  src/island/              The island: window, compositor, geometry, spring
  src/features/            home, now_playing, lyrics, usage, tasks, settings
  tests/                   Pure logic tests; run on any host
  scripts/                 Budget check against the real app
```

Each platform folder is self-contained: it holds its own tooling, and nothing in one refers to the
other. Only documentation is shared.

## Module responsibilities

The two apps have the same shape, with three kinds of module:

| Role | macOS | Windows | Rule |
| --- | --- | --- | --- |
| Controller: owns state, reacts to events | `IslandController`, `PageController`, `HomeController`, `UsageController`, `TasksController`, `TaskWatch`, `TaskAlertController`, `ShelfController` | `IslandWindow`, `PageController`, `HomeController`, `UsageController`, `TasksController`, `TaskWatch`, `TaskAlertController`, `ShelfController` | The only places state changes |
| Scene: owns compositor objects | `IslandView`, `IslandPanel`, `PagerScene`, `HomeScene`, `UsageScene`, `TasksScene`, `TaskAlertScene`, `ShelfScene` | `Compositor`, `Painter`, `HeaderScene`, `HomeScene`, `UsageScene`, `TasksScene`, `TaskAlertScene`, `ShelfScene` | Makes no decisions |
| Pure logic: values in, values out | `IslandGeometry`, `IslandState`, `IslandPage`, `ShelfItem`, `ShelfCollection`, `ShelfLayout`, `HomeLayout`, `HomeModel`, `NowPlaying`, `Lyrics`, `AIUsage`, `UsageModel`, `UsageLayout`, `MoodleFormat`, `TaskModel`, `TasksLayout` | `island_geometry.h`, `spring.h`, `island_page.h`, `shelf_model.h`, `shelf_thumbnail.h`, `home_layout.h`, `home_model.h`, `now_playing.h`, `lyrics.h`, `ai_usage.h`, `usage_model.h`, `usage_layout.h`, `task_model.h`, `tasks_layout.h` | No OS headers, fully tested |

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

The expanded island has pages, in tab order: Home, Usage, Tasks, then Shelf. A shared header in the band beside the
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

## Shelf and clipboard

Shelf is the fourth page, containing Files and Clipboard modes. It keeps the native 580 × 180
island: mode buttons beneath the header, three cards, and paging arrows plus a footer. Cards use
quiet white surfaces, blue file-type labels and a blue hover outline; hovering reveals pin and
remove controls. Empty Files invites a drop. Clipboard has Pause/Resume; Clear preserves pins.
Settings can hide Shelf or choose it as the opening page. Explicit pager routing remains adequate
for four pages; no registry or shared platform layer was introduced.

- **Shelf.** Drop files onto the closed notch to open Shelf, or onto the expanded island. Each file
  becomes a card holding its original path, never a moved or copied original. Drop batches are
  capped at 50 on macOS and 16 on Windows. Clicking a card copies its contents; dragging it supplies
  files, text or clipboard images to another application. Missing original files cannot be copied.
- **Clipboard.** Plain text, http(s) links, file selections and images are recorded locally. Rich-text
  formatting is not retained. macOS accepts
  PNG/TIFF; Windows accepts CF_DIB (also offered as CF_DIB to compatible drag destinations). Payloads stay on disk; only metadata and up to three 128 px
  thumbnails are held for rendering. Windows previews uncompressed 24/32-bit RGB DIBs; other DIBs
  still copy back without modification and show a type card. No network service is used.
- **Bounds.** Up to 50 items in each mode, text up to 64 KB UTF-8, each image up to 4 MB. Duplicate
  copies move to the front and retain pins. The oldest unpinned item is evicted when full; an
  entirely pinned collection rejects new items. Clear removes unpinned items, not original files.
  Individual remove also releases pinned history items. Unreferenced payload files are pruned.
- **Storage.** macOS uses Application Support/Noitch/Shelf (`items.json` and hashed payload names);
  Windows uses LocalAppData/Noitch/Shelf (`items.bin` and payload names). Manifest writes are atomic
  replacements. Paths and previews persist, with no promise that referenced files cannot be moved
  by another application. Clipboard secrets marked concealed/transient by their source are skipped,
  as are our own clipboard writes; unmarked secrets cannot be reliably identified. History is
  ordinary local application data, not an encrypted vault.
- **Lifetime.** macOS's public pasteboard API has no change notification. The owner explicitly
  approved one 1-second `changeCount` check while recording is enabled on 2026-11-12. The timer runs
  on the existing main run loop and stops on Pause, when Shelf is disabled, and on quit. Unchanged
  counts cause no data reads or redraws. The setting persists. This is a narrow exception to the
  no-polling-at-rest rule and necessarily changes the idle wakeup expectation while enabled.
  Clipboard changes between checks may be missed. Windows uses `AddClipboardFormatListener` and
  `WM_CLIPBOARDUPDATE`, without polling. Pause unregisters the listener.
- **Drag and rendering.** AppKit dragging registration and OLE IDropTarget/IDropSource supply native
  events. Windows initializes OLE on the existing UI thread's STA before other COM use, required by
  drag-and-drop; it adds no application thread. Surfaces/layers are built on showing Shelf, animate
  with the existing pager compositor and are released after its outgoing fade. Thumbnails are
  released on hide. No per-frame work, new helpers, dependencies or private APIs were added.

## Home

Home is the first page: large artwork on the left, and beside it the title and artist with the lyrics
button at the end of their row, the lyric line being sung (bright) with the next one below it (dim and
smaller), a progress slider and transport buttons. Behind it, a glow of the artwork's average colour spreads from the artwork in an ellipse that has
faded out by the top edge, so the bends where the pill meets the black flares show no seam (numbers
in `HomeLayout.glowRadii` and `home_layout.h`). "Glow behind the page" and "Tint the progress bar"
turn the glow and the bar's colour on or off.

- **Lyrics.** When the song moves on one line, the bright line rises and fades, the dim line grows
  into its place and the following line rises in below: three text layers take turns, and only
  position, scale and opacity move, so no text is redrawn during the motion. Any other change,
  such as a seek or a new track, fades in as it rises 6 pt into place. Lyrics always enter from
  below, never from the notch. Before the first line and on empty lines, "♪" holds the
  place. On Windows the surface is redrawn in place, without the slide.
- **Lyrics stage.** The lyrics button turns the whole page into lyrics, and back. See
  [Lyrics stage](#lyrics-stage).
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

- macOS reveals the header first, then the page's groups 0.045 s apart (Home: artwork, lyrics
  button, title, lyric lines, slider, transport, calendar; on the lyrics stage, the lyrics then the
  button; Usage: one per column). Lyric groups rise 8 pt from below; the rest drop 8 pt, and scales the whole of it from 0.94
  with the opening spring. Reduce Motion drops the slide.
- The page grows from its top edge, so on its own the spring would carry the lyrics a few points
  down and back while they rose: down, then up. The lyric groups are scaled the other way on the
  same spring (from 1/0.94), anchored at the same edge, so they only rise. Windows has no scale, so
  nothing to undo there.
- Windows draws each layer into one surface, so the content fades and slides as a whole: no
  stagger and no scale. The
  ease-out is an exact cubic, one `IDCompositionAnimation` segment each for an effect group's
  opacity and the visual's Y offset.

Home has no data while the island is closed, so there is no now playing indicator on the collapsed
pill. Adding one would mean keeping the source running at rest, which needs measuring first.

### Lyrics stage

A 22 pt button with a speech bubble sits at the end of the title row. Clicking it gives the whole of
Home to the lyrics; clicking it again brings Home back. Numbers are in `HomeLayout` (macOS) and
`home_layout.h` (Windows).

- **Lines.** Four lines, centred across the page with 30 pt clear at each side: the one before (30%),
  the one being sung (20 pt bold, a little above the middle), and the two after (55% and 26%). Lines
  other than the sung one are drawn at 70% of its size. Empty lines and the intro show "♪". Without
  synced lyrics the middle shows the same status as Home, smaller and dimmer.
- **Romanization.** A non-Latin sung line has a smaller Latin reading below its original. The
  following slots sit 10 pt lower to leave room. Readings are prepared once per lyric lookup,
  reused for repeated lines, and released with the lyrics. Japanese is detected from kana anywhere
  in the synced track, including for kanji-only lines; entirely kanji tracks cannot be reliably
  distinguished from Chinese. macOS uses the public Core Foundation tokenizer and Foundation
  transforms. Windows uses system ICU and the in-process Japanese IME; Japanese kanji readings
  are omitted if that optional IME is absent or cannot supply a reading. No network service, bundled
  dictionary, helper, new thread or third-party package is added. Automatic readings can differ
  from an artist's pronunciation.
- **Background.** With "Glow behind the page" on, one soft wash of the artwork's average colour
  rises from the bottom edge: none at the top (where the pill meets the flares), 10% at 40% of the
  height, 32% at the bottom. A single gradient, on both platforms. With the setting off the stage is
  black. Earlier tries were a radial glow under the sung line (a blob) and the cover's colours
  stretched under black shades (too busy).
- **Motion (macOS).** When the song moves on one line, every line rises one slot: the sung line grows
  into the middle, the one before drifts up and away, and the next rises in from below. Five text
  layers take turns. Each owns its Latin reading as a child layer, so both travel and scale
  together. A reading fades in as its line becomes current and fades out with that same line;
  it is never replaced in a fixed layer at the centre. Only position, scale and opacity move.
  A seek or new track fades the lines
  in as they rise 8 pt, one after another.
  Windows redraws the surface in place, without the slide.
- **Switching (macOS).** Home's own layers fade out and shrink to 96% as the lyrics fade in and rise
  12 pt; the artwork glow fades into the stage's background; the button glides to the page's
  top-right corner (the same place unless the calendar is showing) and turns into a white disc
  with a black bubble. Every part starts from what is on screen, so a quick second click reverses.
  Windows redraws in one step.
- **Jumping.** Clicking a line seeks to its start, shifted by the timing setting. Hovering one
  brightens it to 85%. On the stage the only other control is the button.
- **Rest.** The stage shows no clock, so its one-shot timer fires only at the next line, not every
  second. With no line left, nothing is scheduled.
- **Memory.** The choice lasts until the app quits, through closing and the release after it, so the
  island reopens on the stage if it was left there. It isn't saved. Turning "Show lyrics" off hides the
  button and leaves the stage.

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

### Calendar in Home (macOS)

With "Show calendar" on, Home gains a 172 pt panel on the right, past a thin divider, and the music
column narrows to make room (numbers in `HomeLayout`). The island stays 580 pt wide: a 720 pt island
was tried and found too long.

- **Days.** Three days set in type between two thin chevrons, centred on today (yesterday, today,
  tomorrow, with today chosen), with no boxes: the
  weekday ("THU") over a 20 pt date, then a dot for events and an indigo ring for a Moodle deadline.
  Everything is white: the chosen day bright, the others at half, and the heading under them says
  "Today" (today was red at first; the owner asked for neutral white). A short white line over the
  chosen day springs to a clicked day (it sat under the date first; the owner moved
  it up). Hovering a day lifts it toward bright. The
  chevrons move three days, and the new days push in from the side they point to, clipped to the
  days' row so they never slide past the chevrons. Before this the
  owner picked it from four mockups (2026-11-12), after seven days in a row (too many for the 150 pt the panel had then),
  three bare dates between text arrows (untidy) and three filled tiles (disliked). The panel then
  grew to 172 pt with hairlines between the days, so the three read as a grid.
- **Deadlines.** Tasks from the tasks page (signed in, page on) show among the day's events: a ring
  instead of a bar, in the tasks' indigo, with the due time in indigo and "Tugas 3 · PBP". Today drops
  the ones that have passed. They show even without calendar access; a day with none then offers
  access as before. Windows has no calendar, so no deadlines there either.
- **Day.** "Today", "Tomorrow", "Yesterday" or "Fri 13", with "+2 more" on the same line when the day
  has more than three events. Then up to three rows: a bar in the calendar's colour, the time ("All
  day", "Now", "14:00"), the title. Today drops what has ended and makes the current event bold.
- **Access.** EventKit, read only while Home shows and released with Home's layers. Noitch never
  prompts on its own: the panel shows "Show your calendar", and the system prompt appears when that is
  clicked. After a refusal the same place opens System Settings. The app is hardened, so it carries
  the `com.apple.security.personal-information.calendars` entitlement and a usage description.
- **Opening.** The store, the read and the first text wait 0.2 s after Home opens, when the panel
  reveals anyway, so opening costs no more than before. That includes a new scene's first draw:
  drawing it on attach took about 50 ms of the opening and made the hover check's first opening late. A first hover run without this was slow
  enough to fail the hover check.
- **Events.** `EKEventStoreChanged` notifications reload the three days; one one-shot timer fires at
  the next start, end, deadline or midnight so rows change on time. A change in the task list redraws. Nothing polls. Each opening is centred on today.
- **Windows.** Not supported: Windows' calendar API (`AppointmentStore`) is only available to packaged
  (MSIX) apps, and the Windows app isn't packaged.

### Closed island while music plays

While something plays, the closed pill widens into a wing on each side of the notch, with the same
kind of spring as opening (response 0.42 s, bounce 0.16): the artwork on the left, six thin level bars
on the right, tinted with the artwork's colour when "Tint the progress bar" is on. Numbers are in
`LiveLayout` (macOS) and `live_layout.h` (Windows). Hovering the wings opens the island as the notch
does.

- **Bars.** Six thin bars (1.5 pt, 1.5 pt apart), each rising and falling from its middle on its
  own looping compositor animation (0.40 to 0.61 s), so they never line up. The outer bars take the
  artwork's colour and the middle ones are mixed toward white, so the group reads as one glow. They
  are a rhythm, not the audio: reading real levels would need an audio-capture permission and work
  on every frame. Paused, they rest low and nothing animates. Our own design: four plain white bars
  were too close to Boring Notch, and a drifting wave was tried and dropped.
- **Pause.** The wings stay 10 s after a pause, so a quick pause doesn't flicker the pill, then
  narrow away. One one-shot timer.
- **Track change.** The artwork crossfades; the pill doesn't move.
- **Resizing.** As with opening, the window (macOS) or hit region (Windows) grows before widening
  and shrinks after narrowing, never per frame.
- **Lifetime.** The layers are made when the wings first show and released 2 s after they go.
- **Windows.** The media session subscription now runs from launch while the setting is on, so WinRT
  loads at start instead of on first opening. It needs no other process.
- **Lyric line.** With "Show the lyric line" on (and "Show lyrics"), a track with synced lyrics
  also grows the pill down by a 20 pt strip under the notch, holding the line being sung in 12 pt
  semibold, centred and cut short with "…" when long. The wings stay level with the notch. On macOS
  a new line rises 7 pt in from below as the old one rises away and fades; Windows redraws it in
  place. Before the first line and in gaps it shows "♪". A track without synced lyrics never grows
  the strip. The window or hit region grows before the strip appears and shrinks after it goes.
- **Lyric timing.** One one-shot timer at the next line, only while the island is closed, playing
  and showing a line. Opening the island stops it; closing restarts it from the current position.
- **Lyric lookup.** The closed island looks the track up itself, as soon as it plays, so lyrics
  reach LRCLIB without the island being opened. A cached track reads the disk instead. Home's lookup
  for the same track then reads the file. On Windows the two share `LyricsSource::kMessage`; the
  closed island's requests carry the top bit of the generation.
- **Cost.** While a line shows, the app wakes once per line and draws its text, and the lookup's
  networking stays loaded: about 12.5 MB and 24 ms/min with a track playing, over the at-rest
  budgets. Accepted by the owner on 2026-11-11, on by default. See [performance.md](performance.md).

### Usage

The second page answers one question per tool: how much of my five-hour and weekly limit is left,
and will it last? For Claude Code and Codex side by side:

- **Marks.** Each column's name has its tool's mark before it: Claude's in its coral, OpenAI's in
  white for Codex. The shapes are Simple Icons' (CC0), turned into absolute moves, lines and cubics in
  `BrandLogo.swift` and `brand_logo.h` from the same text, and drawn as a shape layer on macOS and a
  Direct2D path on Windows. They are their owners' trademarks, shown only to name each tool.
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

### Tasks

The third page tracks what is still due on a Moodle site: by default Scele, the Fasilkom UI site
(`https://scele.cs.ui.ac.id`). The design was approved on 2026-11-12, and its
[design page](https://claude.ai/artifact/HpTrJWPxU23AkJzVzJr8Ae) has the reasoning and the mockups.

- **Page.** On the left, the next task as a countdown ("1d 2h"), white, or red within a day or
  overdue, over its title, course and due time, with "Open in Scele" (or EMAS3, or Moodle for another
  site). On the right, the next seven days as bars of how much is due each day, from today: the
  busiest day in the accent, a day with something due within a day in red, the rest grey, a new task's
  day outlined; beside them "Busiest: Thu · 3" over "5 due in 7 days". On macOS the bars rise out of
  their line as the page appears. Under them three rows: icon, title and course, and when it is due
  ("Tomorrow 23:55", "Fri 10:00"). Scroll up and down to move through the rest. A 14-day line with a
  dot per task came first and was replaced: it said when, not how much. Hovering a row widens its
  day's bar; clicking a row or the button opens the task in the browser, only for
  links on the signed-in site.
- **Overdue.** Work past its deadline (kept up to a week) is hidden by default: the page is about
  what can still be done. "Show 2 overdue", in red at the right end of the line under the list, adds
  it to the top of the list and the countdown ("Overdue 2d"); "Hide overdue" takes it away again, and
  every new showing of the page starts hidden. With only overdue work left, the page says "All clear ·
  Nothing else due" with the same button.
- **Other states.** Signed out, the page offers "Sign in…", which opens settings. With nothing due it
  says "All clear" under a tick that sits below the header band (it once reached into the notch), and
  on macOS the tick draws itself.
- **Colour.** One accent, indigo, on white and greys: the busiest day, the course chip, the row
  icons, new tasks and the tick. Red only for what is due within a day or overdue. Amber, blue and a
  red-amber-white scale came first and were too busy. Home's calendar uses the same indigo for
  deadlines.
- **Closed island.** When a check finds a task that wasn't there before, the pill takes the music's
  wing and strip shape (the live spring) for 8 s: the module's icon in the left wing, "+1" in the right
  and "New · Fri 23:55 · PBP · Tugas 3" under the notch. With music playing the wings keep the artwork
  and bars, and the alert takes the lyric strip for those 8 s. Then the pill narrows to a 6 pt blue dot
  in a 14 pt wing on each side, which stays, with nothing running, until the page has been seen.
  Hovering or clicking opens the island straight onto Tasks. Numbers are in `TaskAlertLayout`
  (macOS) and `tasks_layout.h` (Windows). Windows draws the alert into one surface and redraws it in
  place, in one colour.
- **Reminders.** The same slip in red, a day and an hour before a deadline, from one one-shot timer
  at the next reminder time. No request: the list is already known. A reminder that fell while the Mac
  slept for more than 10 minutes is dropped, not shown late.
- **New.** A task is new if its id isn't among those already announced and it is still ahead. The
  first list after signing in is taken as already seen, so signing in announces nothing. Opening the
  page marks everything seen.
- **Remembered.** The last list, the ids announced and those not yet seen are kept in a small cache
  file (`Caches/Noitch/tasks` on macOS, `%LOCALAPPDATA%\Noitch\tasks` on Windows, the same format),
  so the page shows at once and a restart neither empties it nor announces everything again. Signing
  out deletes it.

Where the tasks come from: the site's web service, the documented API Moodle's own app uses
(`webservice/rest/server.php`), approved by the owner on 2026-11-12 with these limits:

- **Signing in.** The username and password go once, in a POST over HTTPS, to `login/token.php` with
  `service=moodle_mobile_app`; the password is never stored, logged or kept in a field. Scele allows
  this: its public config (`tool_mobile_get_public_config`) has web services and the mobile service on
  and in-app login (`typeoflogin` 1), checked on 2026-11-11. Or a key pasted from the site's
  Preferences › Security keys. Then `core_webservice_get_site_info` checks the key and gives the name
  for settings.
- **The key** is kept in the Keychain (a generic password, service `app.noitch.moodle`, this device
  only) on macOS, and in Credential Manager (`Noitch/Moodle/<host>`, this user on this PC) on Windows.
  It is read for each request and dropped after it, sent in the request body, never in a URL, and never
  logged. A rebuilt macOS app has a new signature, so macOS asks once whether it may still read it.
- **The request.** `core_calendar_get_action_events_by_timesort` from a week ago, at most 50: what the
  Dashboard's Timeline block shows. Moodle drops an item once it is done, so the list is what is still
  to do. One request, a few kilobytes. `mod_assign_get_submission_status` was in the design for an
  assignment's details on click; it isn't built.
- **When.** Moodle can't push to other apps, so this is the one feature that checks on a timer
  while the island is closed, approved at once every 30 minutes at most: one one-shot timer with up to 5
  minutes of tolerance (`Timer` on macOS, `SetCoalescableTimer` on Windows), re-armed after each check.
  Also when the island opens (at most every 10 minutes), when the page shows (every 2), and when the
  computer wakes. "Every hour" and "Only when I open the island" check less. A refused key stops the
  timer until the next sign-in. Signed out, or with the page off, nothing is scheduled.
- **Cost.** Each check is one HTTPS request: 11 to 26 ms of CPU on the M5. The networking frameworks it
  loads stay loaded; see [performance.md](performance.md).
- If the site changes or is down, the page says so over the remembered list; nothing else breaks.

### Lyrics

Both apps look up the track on [LRCLIB](https://lrclib.net), which sends the track, artist and album
names to that service. A track with no artist is skipped, because that is usually a browser tab whose
title shouldn't be sent. Results are cached on disk, and misses aren't, so a track that gains lyrics is
found later. "Show lyrics" in settings turns off both the display and the lookup.

## Settings

A window with a sidebar of pages: General, Pages, then one page per feature (Home, AI Usage, Tasks), and
About. Each feature owns its part of the settings and its page, next to its code:

| Part | macOS | Windows |
| --- | --- | --- |
| Values | `AppSettings` holds `GeneralSettings`, `PageSettings`, `HomeSettings`, `UsageSettings`, `TasksSettings` | `app_settings.h`, `home_settings.h`, `usage_settings.h`, `tasks_settings.h` |
| Page | `SettingsPane` built by `SettingsPanes`, `HomeSettingsPane`, `UsageSettingsPane`, `TasksSettingsPane` | `SettingsPane` data from `settings_panes.h`, `home_settings_pane.h`, `usage_settings_pane.h`, `tasks_settings_pane.h`; the account block is built by `SettingsWindow` |
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
| General | Pin shortcut | ⌃⌥N on macOS, Ctrl+Alt+N on Windows | Pins the island open, or lets it go, from anywhere. Needs ⌘, ⌥ or ⌃ (Ctrl or Alt); Delete or Backspace turns it off. `RegisterEventHotKey` on macOS, `RegisterHotKey` on Windows: no permission, and an event only on the press. If another app holds the keys, macOS settings say so |
| Pages | AI Usage | On | Off removes its tab and swipe stop. Home is always there |
| Pages | Tasks | On | Off removes its tab, swipe stop, checks and alerts |
| Pages | Open on | The page used last | Or always Home, AI Usage or Tasks |
| Home | Show lyrics | On | The lyric lines and the LRCLIB lookup |
| Home | Show the next line | On | The dimmer second line |
| Home | Timing | As published | Shifts which line is sung by −1 to +1 s |
| Home | Show shuffle and repeat | On | The two outer transport buttons |
| Home | Tint the progress bar | On | Average artwork color, lifted so dark covers stay visible |
| Home | Glow behind the page | On | The wash of that color behind the page |
| Home | Show calendar | On | macOS only: this week and the chosen day's events beside the music, which narrows for it. See [Calendar in Home](#calendar-in-home-macos) |
| Home | Show what's playing | On | The closed island's wings with artwork and level bars. On macOS this keeps the now playing helper running while a player app is open |
| Home | Show the lyric line | On | The line being sung under the notch while the wings show. Needs Show lyrics. Looks lyrics up when a track plays, not only when Home opens |
| AI Usage | Claude Code, Codex | On | One column each; one alone is centered. With both off, Claude stays |
| AI Usage | Warn at | 70% | Amber from here; red from 90%, or 5 points above when that is higher |
| AI Usage | Check Claude every | Minute | Or 5 or 15 minutes. Never more often than the approved once a minute; logs are still read every minute |
| Tasks | Account | Signed out | Site: Scele (`scele.cs.ui.ac.id`, default), EMAS3 (`emas3.ui.ac.id`) or Other with an address (HTTPS only); username and password, or a security key; signed in, the name and Sign out |
| Tasks | Check for new tasks | Every 30 minutes | Or every hour, or only when the island opens. Never more often than the approved 30 minutes |
| Tasks | Alert on the closed island | On | The slip, then the dot |
| Tasks | Remind before due | 1 day and 1 hour | Or 1 hour, or off |
| Tasks | Show | Assignments and quizzes | Or everything with a due date, such as forums |

Open settings with the header's gear or the secondary-click menu. The window is created on first use
and released on close, and only the page on screen has controls. macOS uses AppKit, not SwiftUI,
because [the first SwiftUI window](tech-stack.md#swiftui) would use most of the footprint budget for
good. Sidebar badges are drawn from paths rather than SF Symbols for the same reason.

## Pinning

The header has a pin left of settings, and a shortcut does the same from anywhere (⌃⌥N on macOS,
Ctrl+Alt+N on Windows, set in General). Pinned, leaving the island doesn't close it, and neither
does a slider drag that ends outside; pinning a closed island opens it on the usual page. Letting go
closes it, unless the pointer is over it, in which case leaving closes it as usual. The shortcut is
`RegisterEventHotKey` on macOS and `RegisterHotKey` on Windows: public, no permission, and nothing
runs until the keys are pressed.

Pinned is not at rest: the page on screen runs as it does whenever the island is open (Home's
one-shot timers and now playing, Usage's minute, Tasks' minute), until it is let go. The window
covers its expanded area for as long as it stays pinned.

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
| Pages | Home, Usage, Tasks. Tabs left in the header band, settings right; a page turned off gives its slot to the next; header numbers in `HeaderLayout` and `island_page.h` |
| Page switch | Tab click, or a sideways swipe of 40 pt (one wheel notch on Windows); one page per gesture; slide 24 pt with a crossfade, see [Pages](#pages) |
| Usage layout | Numbers in `UsageLayout` (macOS) and `usage_layout.h` (Windows), which must match |
| Usage insight | Rules in `AILimits` (macOS) and `ai_limits.h` (Windows): pace ±10 points, recent pace over 30 min, warnings in yellow and red |
| Tasks layout | Numbers in `TasksLayout` and `TaskAlertLayout` (macOS) and `tasks_layout.h` (Windows), which must match; seven day bars, three rows; one indigo accent, red for within a day |
| Task rules | `TaskRule` (macOS) and `task_model.h` (Windows): amber within 3 days, red within 1; overdue kept a week and hidden until asked for; slip 8 s; reminders 1 day and 1 hour before; checks every 30 min at most |
| Closed island with music | Wings of `artSide + 17` pt each side, artwork `notch height − 14` pt, six 1.5 pt bars shaded toward white in the middle; spring 0.42 s, bounce 0.16; 10 s after a pause; a 20 pt lyric strip under the notch, 12 pt text. See [Closed island while music plays](#closed-island-while-music-plays) |
| Content reveal | See [Home](#home): 0.06 s delay, 0.3 s fade, 8 pt drop over 0.42 s; 0.14 s fade out |
| Expand / collapse | Expand when the pointer reaches the real notch, including at the screen top edge, at once or after the hover delay setting; collapse immediately on exit. The real notch can reopen during collapse; the former expanded area cannot |
| Secondary click | Menu with Settings and Quit |
| Pin | A pin in the header left of settings, and the pin shortcut. Pinned, the island stays open when the pointer leaves; pinning a closed island opens it. Letting go closes it unless the pointer is over it. The pin shows as a white disc. Not kept across launches. See [Pinning](#pinning) |
| Settings | Pages, values and defaults in [Settings](#settings); both apps keep the same pages and rows |
| Home layout | Numbers in `HomeLayout` (macOS) and `home_layout.h` (Windows), which must match |
| Shelf | Files / Clipboard modes, three 77 pt cards, 50 items per mode, pin/remove/copy/drag, local payload storage; see [Shelf and clipboard](#shelf-and-clipboard) |
| Lyrics stage | 22 pt button at the title row's end, moving to the top-right corner on the stage; four centred lines with a smaller Latin reading beneath the sung non-Latin line, the sung one 20 pt bold, others 70% at 30%, 55%, 26%; click a line to seek; a wash of the artwork's colour rising from the bottom. See [Lyrics stage](#lyrics-stage) |
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
