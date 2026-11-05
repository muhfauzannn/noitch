# Performance

Performance is the first priority. These budgets are enforced by scripts, and a change that exceeds
one needs an explicit decision, not a quiet increase.

## Budgets

| Metric | Budget | macOS now | Windows now |
| --- | --- | --- | --- |
| Memory at rest | 12 MB macOS, 30 MB Windows | 8.7–9.0 MB | Not measured |
| CPU at rest | 20 ms per minute | 0.9–1.8 ms | Not measured |
| Wakeups at rest | 0.5 per second | 0.10–0.17 | Not measured |
| CPU per transition | 10 ms | 5.73 ms | Not measured |
| Threads at rest | None runnable | Holds, per the CPU figures | Not measured |
| Per-frame work in our process | None | By design | By design |
| Third-party dependencies | 0 | 0 | 0 |

macOS figures are from the Release app on an Apple M5, macOS 26.6, with only the foundation in place.
The app bundle is 152 KB.

- The rest figures cover four clean runs. A fifth run showed 145 ms per minute and 6.1 wakeups per
  second. It did not recur in three more runs, during which the cursor never entered the island. Its
  cause is unknown; real hover is one possibility, as cursor position was not logged in that run.
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
| Does EcoQoS (`PROCESS_POWER_THROTTLING`) help without hurting hover latency? | Possible battery gain |
