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
