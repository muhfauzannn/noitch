# Performance

Performance is the first priority. These budgets are enforced by scripts, and a change that exceeds
one needs an explicit decision, not a quiet increase.

## Budgets

| Metric | Budget | macOS now | Windows now |
| --- | --- | --- | --- |
| Memory at rest | 12 MB macOS, 30 MB Windows | 9.14 MB, 10.13 MB after Home and seek were used | Not measured |
| CPU at rest | 20 ms per minute | 1.48–2.87 ms | Not measured |
| Wakeups at rest | 0.5 per second | 0.13–0.17 | Not measured |
| CPU per transition | 10 ms | 4.83 ms | Not measured |
| Threads at rest | None runnable | Holds, per the CPU figures | Not measured |
| Per-frame work in our process | None | By design | By design |
| Third-party dependencies | 1, approved: MediaRemoteAdapter | See left | 0 |

With the shelf redesign (2026-11-12, same M5, Spotify running): 11.94 MB, 11.88 ms/min and 1.50
wakeups/s, against 11.92 MB, 10.58 ms/min and 1.63 wakeups/s for `main` just before it on the same
machine, so the same within noise. The wakeups were already over budget on `main`, from the
approved one-second clipboard check and the closed island's lyric line. The hover check, run on its
own with Hover delay at None, passed all 23 at 5.06 ms per transition. The binary grew from 1763 KB
to 1793 KB. Showing the Shelf page itself was not timed.

With the pin and its shortcut, Usage's marks, the calendar's day tiles and the lyric fix
(2026-11-12, same M5): 9.83, 9.89 and 9.89 MB, 1.33 to 2.33 ms/min, 0.13 to 0.17 wakeups/s, 4.31 and
4.67 ms per transition, all 23 hover checks passing on two runs; the binary is 1576 KB. Before the fix
below, the first two hover checks failed on every run, here and on main: the first opening took 92 ms
of main thread, against the check's 80, because attaching the calendar drew it at once (51 ms of a
Debug build's 86). It now draws at its first look, 0.2 s in, as the docs always said. The commit
before the three-day calendar passed; the one with it did not.

With the tasks page's day bars and indigo palette, Home's three-day calendar with deadlines and the
site choice (2026-11-12, same M5), signed out: 9.74 and 9.74 MB, 1.72 and 2.62 ms/min, 0.13 wakeups/s;
the binary is 1524 KB. A first run read 9.89 MB, 18.21 ms/min and 0.47 wakeups/s, close to the
budgets, and did not recur.

With the tasks page and its closed-island alert (2026-11-12, same M5), signed out, so no check runs:
10.50 and 10.45 MB, 2.38 and 1.91 ms/min, 0.13 and 0.10 wakeups/s, 4.28 ms per transition, all 23
hover checks passing with Hover delay at None. Just before, on main: 9.97 MB, 2.89 ms/min, 0.10
wakeups/s. The binary grew from 1135 KB to 1499 KB. Two runs in between read 40.27 and 16.84 MB with
hundreds of ms/min; the app was idle at 0% CPU when checked straight after, so the pointer or other
work was the likely cause. **Signed in, not yet measured.** One check is one HTTPS request: measured
apart from the app, 25.9 ms of CPU the first time and 10.6 to 13.1 ms after, and the networking
frameworks added 2.9 to 3.6 MB that stayed. At one check per 30 minutes that is under 1 ms/min and
0.0006 wakeups/s, but the memory may take the app over 12 MB at rest, unless the lyric lookup has
loaded the same frameworks already. Whether to accept that is the owner's call once it is measured
signed in.

With the closed island's lyric line, rising lyrics and the stage's new background (2026-11-11, same
M5), measured while Spotify or Music played a track with synced lyrics, so the wings and the line
were up: with "Show the lyric line" off, 10.52 MB, 9.12 ms/min and 0.20 wakeups/s; with it on,
11.53, 12.09 and 12.52 MB, 38.22, 44.05 and 23.82 ms/min (the last over a 60 s window) and 0.43,
1.80 and 0.47 wakeups/s. **Over the memory and CPU budgets while a line shows. Accepted by the
owner on 2026-11-11, with the setting on by default; the budgets still hold with nothing playing.** The cost is one text render and one wakeup per lyric line, plus the networking frameworks the
lookup loads while closed. With nothing playing nothing of this runs. The binary is 1136 KB. The
hover check, run on its own against the Release app with the line showing and Hover delay at None,
failed its first two checks on one run (the island opened late) and passed all 23 on the repeat,
5.77 ms per transition; treat it as flaky until it recurs.

With the lyrics stage (2026-11-11, same M5): 9.80, 10.08 and 10.05 MB, 1.42, 6.08 and 0.86 ms/min,
0.10 to 0.13 wakeups/s, 4.71 to 5.61 ms per transition, all 23 hover checks passing on the run with
Hover delay at None; the binary is 1130 KB, against 1090 KB last recorded with the calendar. One idle
run read 15.11 MB because the cursor had been left on the notch, so the island was open while it
measured; with the cursor moved away first it read 10.05 MB. Keep the cursor off the notch before
measuring. Switching to the stage was not timed.

With the calendar in Home at 580 pt (2026-11-11, same M5): 9.59 MB, 0.89 and 1.04 ms/min, 0.10
wakeups/s, 5.03 and 5.22 ms per transition. The first hover run failed "stays expanded while the
pointer remains over the notch" and the check after it; the repeat passed all 23. Treat it as flaky
until it recurs.

With the calendar in Home and a 720 pt island, since narrowed back to 580 (2026-11-11, same M5): 9.77 MB, 2.88 ms/min, 0.10
wakeups/s, 5.10 ms per transition, all 23 hover checks passing; the binary grew from 981 KB to 1090
KB. A first run failed the first two checks because the opening also built and read the calendar;
the calendar's first read now waits 0.2 s, and the checks passed. Runs with the owner's hover delay
of 0.3 s fail most opening checks by design; the hover check needs Hover delay set to None.

With six thin graded bars replacing the wave (2026-11-11, same M5): 9.74 MB, 1.74 ms/min, 0.17
wakeups/s; the binary is 981 KB.

With the wave replacing the bars (2026-11-11, same M5): 10.13 MB, 1.70 ms/min, 0.17 wakeups/s; the
binary is 983 KB. The run before, with the Home glow change, read 9.59 MB, 1.26 ms/min and 0.10
wakeups/s. Whether music played during this run, and so whether the wings were up, is not known.

With the closed island's music wings (2026-11-11, same M5): 9.77 and 9.72 MB, 2.72 and 3.45 ms/min,
0.13 wakeups/s, 4.18 ms per transition, all 23 hover checks passing. Nothing was playing, so the
hover run never widened the pill, and the moving bars are not in these figures; they run in the
render server. A browser was open, so the now playing helper may have been running beside the app;
its memory is not counted. The binary grew from 920 KB to 981 KB.

With the settings window split into per-feature pages (2026-11-11, same M5): 9.47 and 9.39 MB, 1.43
and 2.10 ms/min, 0.13 wakeups/s, 4.12 ms per transition, all 23 hover checks passing. Just before,
the same machine read 9.36 MB, 1.68 ms/min and 0.13 wakeups/s. The binary grew from 776 KB to 920
KB. Opening the settings window is not in these figures.

With the Home redesign and the content clip (2026-11-10, same M5): 9.33 MB, 0.86 ms/min, 0.10
wakeups/s and 3.20 ms per transition; two further runs read 4.33 and 5.25 ms/min and 3.01 and 3.53 ms
per transition with all hover checks passing. The first run failed one check, "a brief visit closes
after the pointer leaves", which then passed twice; treat it as flaky until it recurs. The binary
is 776 KB.

With the limits page (2026-11-10, same M5): two idle runs read 9.16 and 9.27 MB, 1.84 and 4.89
ms/min, 0.10 and 0.07 wakeups/s. Two runs started straight after a test run or another measurement
read 10.09 and 10.58 MB, 24.15 and 37.56 ms/min, over budget, so their hover checks did not run; the
cursor was in use on another display at the time. Repeat with the machine idle before trusting
either. The binary is 751 KB.

With pages and the usage page (2026-11-10, same M5): 8.91 MB, 1.43 ms/min, 0.10 wakeups/s and
3.91 ms per transition, all hover checks passing; the binary grew from 478 KB to 711 KB. One earlier
run of the same build failed every hover check with a collapsed window 177 pt wide instead of 195,
and a run with a 5 s window read 13.53 MB and 1007 ms/min; neither recurred, and their cause is
unknown. Reading the usage logs is not in these figures: it happens only while the usage page is
shown (24 to 59 ms for the first read of a day's logs, about 3 ms after).

Current macOS figures are from two Release runs on an Apple M5, macOS 26.6, with Home, Settings and
the adapter in place, while a browser played media. Immediately before this change, the same machine
read 9.20 MB, 1.27 ms/min and 0.10 wakeups/s. With the adapter held out, the same build read 1.01
ms/min and 4.23 ms per transition, so the difference in idle CPU between runs is unexplained.

After opening Home and seeking, once everything was released, the app read 10.13 MB, 0.19 ms/min and
0.03 wakeups/s, with no child left. These runs had no artwork, because the player sent none.

These budgets cover our process only. While the island is open, and for 10 to 12 seconds after, the
adapter's `perl` child adds about 26 MB of resident memory that the table doesn't show.

SF Symbols were replaced by path icons after measuring them: the symbol catalog kept about 2 MB after
Home was released, reading 11.84 MB against 9.86 MB, too close to the 12 MB budget.

Earlier figures, from Release runs after switching to immediate real-notch activation with
mouse/scroll event reconciliation and no reopening lock or hover timer. The latest idle reads
ranged from 8.53–8.59 MB, 1.05–1.19 ms/min and 0.10 wakeups/s. The hover run measured
4.03 ms per transition. A preceding idle run exceeded the budget at 566.36 ms/min and 14.63 wakeups/s;
its cause remains unknown, and subsequent runs passed.
The earlier foundation-only baseline was 8.7–9.0 MB, 0.9–1.8 ms/min, 0.10–0.17 wakeups/s and
5.73 ms per transition.

- The new idle reads were taken with the cursor away from the island. Two individual runs showed
  outliers (33.35 ms/min and 1.33 wakeups/s; 138.05 ms/min and 3.57 wakeups/s); repeated idle and
  hover runs passed, and the cause of those outliers is unknown.
- The earlier foundation baseline covered four clean runs; a fifth run showed 145 ms per minute and
  6.1 wakeups per second. It did not recur in three more runs, during which the cursor never entered
  the island.
- CPU per transition includes the two flare layers, which each add a spring and a position update.

The Windows memory budget is provisional, set to match Nusa's claimed 30 MB. Direct3D loads GPU driver
code, so the real number is unknown. Tighten it after the first run.

## Romanization verification (2026-11-12)

Local Release measurements before this change: 13.02 MB footprint, 44.47 ms/min idle CPU,
0.40 wakeups/s. After: 14.74 MB / 104.47 ms/min / 1.70 wakeups/s; repeats were
13.58 MB / 70.55 ms/min / 1.43 wakeups/s and 13.78 MB / 53.38 ms/min / 1.33 wakeups/s.
The memory and CPU budgets already failed before the change; these runs do **not** establish
that the change meets the budgets, and no budget has been raised. They measure launch idle,
not retained memory after using the new tokenizer. That latter measurement remains outstanding.
The standalone hover check with the required zero hover delay passes all interaction checks,
with 6.57 ms CPU per transition (budget 10 ms). The user's 0.3 s hover delay was restored.
Windows measurements remain unavailable. The Latin readings use native OS services, with no
bundled MeCab or ICU package and no additional idle timer.

## Paired lyric animation verification (2026-11-12)

The fixed central Latin layer is replaced by five readings, each attached to its original lyric
layer. Existing compositor animations move and scale the pair; only the reading's opacity is
animated separately. No timer or per-frame callback was added.

Before this fix: 13.13 MB, 33.48 ms/min, 1.17 wakeups/s. After: 20.39 MB,
1111.96 ms/min, 10.77 wakeups/s; repeat: 20.14 MB, 697.15 ms/min, 8.00 wakeups/s.
These runs exceed the budgets, including the baseline; the source of the increased activity has
not been isolated. They do not prove acceptable idle performance, and the budgets are unchanged.
The first standalone hover run recorded 6.43 ms per transition but failed interaction checks;
it was started without the normal ten-second settle. A repeat after settling recorded 5.89 ms
per transition but still failed the assertion that the window remains large during collapse.
Windows budgets remain unmeasured.

## Spike investigation with hover prevented (2026-11-12)

The preceding before/after measurements did not record playback state or confirm that the island
remained collapsed; they cannot establish that paired lyric animation caused the CPU spike.
A follow-up used `-openOnHover NO` as a process-only override, ten seconds to settle, then a
20-second counter interval (CPU normalized to one minute). No saved preference was changed.

| Run | Footprint MB | CPU ms/min | Wakeups/s |
| --- | --- | --- | --- |
| Current, live music and closed lyrics enabled | 14.27 | 17.97 | 0.70 |
| Current, closed lyrics disabled | 10.30 | 8.97 | 0.20 |
| Current, closed music disabled | 9.11 | 1.29 | 0.10 |
| Before romanization (`f95010c`), live and lyrics explicitly enabled | 10.52 | 1.26 | 0.15 |
| Current repeat (`3755f85`), live and lyrics explicitly enabled | 11.77 | 1.77 | 0.10 |

Each process was restarted between runs. Three-second, 10 ms CPU samples after the counter intervals
showed the main thread waiting in the event loop, without an observed romanization or animation
hot loop. The large CPU spike did not recur. The 1.25 MB difference in the final build comparison
is an observation from one pair, not a proven allocation attributable entirely to romanization:
real playback and network/cache state were not frozen. Closed music and closed lyric paths have
measurable costs, but these results do not identify the historical spike's exact cause or prove
all budgets in all playback states. Repeat under a fixed track and capture a profile during a
reproduced spike before attributing it to a particular feature. These are diagnostic 20-second
intervals, not replacements for the normal 30-second budget checks or after-use measurements.

## Shelf and clipboard verification (2026-11-12)

Same local hardware, Release, before: **10.36 MB, 1.00 ms CPU/min, 0.13 wakeups/s**.
After, recording active: **10.52 MB, 13.43 ms CPU/min, 1.50 wakeups/s**. After, recording paused
with `measure.sh --clipboard-paused --hover`: **10.34 MB, 1.66 ms CPU/min, 0.17 wakeups/s**.
A final paused run after storage-pruning fixes measured **10.95 MB, 1.25 ms CPU/min,
0.13 wakeups/s**. The binary grew from 1592 to 1763 KB. These are normal 30-second launch-idle intervals after the
script's ten-second settle, with the local media state and current stored history.

The recording-active wakeup count exceeds the original 0.5/s idle budget. The owner explicitly
approved the one-second clipboard poll; that narrow exception is enabled only while recording.
The original budgets are unchanged and pass with recording paused. CPU and footprint pass in
both measured states. This does not establish performance with 50 image entries or memory retained
after a real drag/drop session; those checks remain outstanding.

Hover could not be validated: the Window Server/loginwindow lock-screen surfaces cover the target.
The initial run's changing window sizes and failures are invalid as a hover regression test. The
checker now returns BLOCKED (exit 2) when an on-screen surface above the island covers the test
point, and never suggests quitting loginwindow or Window Server. A subsequent standalone attempt
without the observed lock surfaces still did not receive usable hover input; interaction remains
unverified rather than established as a production regression. A controlled build of pre-Shelf
`main` (`67f3d0d`), launched with hover explicitly enabled and zero delay, reproduced the same
non-opening assertions and 195 pt window width. The failure therefore is not isolated to this
feature change; no passing hover result is claimed for either build in this session. Real drag/drop, clipboard paste
into external applications and after-use memory require an unlocked interactive session.

Offscreen Files, Clipboard and empty-state native layer renders were inspected. The 116 macOS
tests cover bounds, de-duplication, pinned eviction, private clipboard markers, copy echoes,
text/image/file round trips on isolated named pasteboards, payload pruning and keeping originals.
Windows pure tests pass with AddressSanitizer and UndefinedBehaviorSanitizer, including bounded
DIB thumbnails. Windows app code has not been compiled or run here; its budgets remain unknown.

## Measuring

```sh
macos/scripts/measure.sh            # Build Release, check the budgets at rest
macos/scripts/measure.sh --hover    # Also drive the real cursor (about 25 seconds)
```

```powershell
windows\scripts\measure.ps1
```

Both exit non-zero if a budget is exceeded.

### What the numbers mean

- **Footprint (macOS)** is `ri_phys_footprint`, the number Activity Monitor shows as Memory. RSS
  overstates it by including shared system pages.
- **Private bytes (Windows)** is the comparable number. Working set includes shared pages, so it is
  reported but not budgeted.
- **Wakeups** matter more for battery than CPU percentage does: each one takes a core out of a low
  power state. A process can show 0.0% CPU and still drain the battery.

Measure at least 30 seconds after a 10 second settle. For anything close to a budget, repeat the run
and compare medians. Compare only on the same machine, and quit other notch apps first. Keep the
cursor away from the island, because hovering is real work and counts against the idle figures. If a
single run fails, repeat it and investigate; don't assume it was noise.

Don't reduce working set with `EmptyWorkingSet` or similar tricks. They improve the number while
making the app slower when it next needs those pages.

## What usually goes wrong

A public notch app [went from about 15% idle CPU to about 0%](https://github.com/spitfiresb/notch/pull/1)
by removing these. Each one looks harmless when written:

- An audio tap running from launch, rather than only during playback
- Polling a media player every few seconds on the main thread
- A 60 Hz timer publishing unchanged values
- A 16 Hz loop reading the cursor position
- Polling the window list to detect Mission Control

In this codebase:

- Replacing a running Core Animation animation fires its completion at once. This cut off collapse
  animations until the hover check caught it. Unit tests and idle measurements both missed it.
- Test interaction, not only rest. Budgets at rest say nothing about behavior under hover.
- Another notch app above the island takes its mouse events, which looks like a hover bug.
- Memory at rest must be measured after a feature has been used, not only at launch. SF Symbols passed
  the launch measurement and took 2 MB that never came back.
- Xcode enables code coverage by default, even in Release. It doubled the binary size and invalidated
  a round of measurements. Release now turns it off, and `measure.sh` refuses instrumented builds.

## First run on Windows

Nothing on Windows has been measured. In order:

1. Build with MSVC and fix what `zig c++` did not catch.
2. Confirm that the pill appears, expands, collapses and reverses mid-flight.
3. Confirm clicks pass through around the collapsed pill.
4. Run `windows\scripts\measure.ps1` and record the numbers here.
5. Measure a hover round trip. There is no Windows hover script yet.

Then these experiments, each of which could change a decision:

| Question | Why it matters |
| --- | --- |
| How much private memory does `D3D11CreateDevice` add? | Probably the largest single cost |
| Does `D3D_DRIVER_TYPE_WARP` work for a 1×1 surface, and is it lighter? | Could avoid loading the GPU driver at all |
| Does a Windows.UI.Composition version cost more? | Tests the [DirectComposition decision](tech-stack.md#rendering-directcomposition) |
| What do WinRT, Direct2D and DirectWrite add to private memory, and does it return after release? | They are the first things loaded after launch |
| Does EcoQoS (`PROCESS_POWER_THROTTLING`) help without hurting hover latency? | Possible battery gain |

## Release automation verification (2026-11-12)

The CD change edits packaging/workflows and Windows version resources, with no macOS runtime code
or build-optimization changes. Two sequential Release idle runs on this M5 with the normal saved
settings (`measure.sh`, no recording override) measured:

| Run | Footprint MB | CPU ms/min | Wakeups/s | Binary KB |
| --- | --- | --- | --- | --- |
| Baseline for CD verification | 11.25 | 34.23 | 1.20 | 1787 |
| After CD changes | 10.66 | 38.25 | 1.13 | 1787 |

Both exceed the CPU and wakeup budgets; neither establishes compliance or a performance improvement.
Playback was not controlled/recorded, and clipboard recording was not overridden. These numbers do
not isolate the source of activity. Budgets remain unchanged. Hover was not rerun because no window,
hover or animation code changed. Windows budgets and packaged/signed runtime behavior remain unmeasured.

## Native CI build repair verification (2026-11-12)

The Shelf callback now discards the capture result inside its main-actor closure, so it returns
`Void` on the older CI compiler. The clipboard interval, tolerance and stop conditions are unchanged.
Windows changes rename a local variable that conflicts with an SDK macro and reset the same COM
formats directly. No runtime service, timer, thread or animation behavior was added.

Two sequential local Release runs with the normal saved settings (`measure.sh`, no recording
override) on the same M5 measured:

| Run | Footprint MB | CPU ms/min | Wakeups/s | Binary KB |
| --- | --- | --- | --- | --- |
| Before CI fixes | 10.58 | 7.96 | 1.33 | 1787 |
| After CI fixes | 10.78 | 12.25 | 1.13 | 1786 |

Memory and CPU are within budget in both runs. Wakeups exceed the 0.5/s baseline budget in both;
clipboard recording was not overridden and playback was not controlled. These samples do not
establish a performance improvement or isolate the wakeup source. Budgets are unchanged. No window,
hover or animation code changed, so hover was not rerun. Native Windows measurements remain unavailable.

## Certificate-free release verification (2026-11-12)

This change adds explicit packaging modes, with no runtime source or optimization-setting changes.
A local unsigned pipeline run passed 117 tests and created/verified an ad-hoc arm64 DMG. Sequential
`measure.sh` runs on the same M5, with the normal saved settings and no recording override, read:

| Run | Footprint MB | CPU ms/min | Wakeups/s | Binary KB |
| --- | --- | --- | --- | --- |
| Before packaging-mode changes | 10.75 | 8.92 | 1.47 | 1786 |
| After local packaging validation | 17.78 | 226.96 | 1.63 | 1786 |

The baseline already exceeds the wakeup budget. The later run exceeds memory, CPU and wakeup budgets;
it does not establish acceptable idle performance. Playback/hover state was not controlled or recorded,
so these samples cannot isolate the source of the increased activity or attribute it to packaging.
No budget has been raised and no improvement is claimed. Hover was not rerun because no window,
hover or animation code changed. Windows native before/after measurements remain unavailable.
