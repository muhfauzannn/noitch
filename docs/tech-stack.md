# Tech stack

| | macOS | Windows |
| --- | --- | --- |
| Language | Swift 6 | C++20 |
| UI | AppKit, no SwiftUI in the island | Win32 |
| Rendering | Core Animation | DirectComposition, Direct3D 11 |
| Build | Xcode project | CMake, MSVC |
| Minimum OS | macOS 14 | Windows 10 1809 (intended, untested) |
| Third-party dependencies | MediaRemoteAdapter | None |
| Evidence | **Measured on real hardware** | **Documentation only; never run** |

The confidence behind these two columns is very different, and the rest of this page keeps that
visible. Anything marked *unmeasured* is a reasoned choice, not a result.

## The target to beat

[Nusa](https://rama2555.github.io/nusa) is Windows-only. Its site says it is written in Rust, uses
about 30 MB of RAM (45 MB with a full shelf), 0% CPU at idle, and is a 3 MB download. These are
marketing figures with no stated method, and its public repository holds only the landing page. They
are a reference point, not a verified benchmark.

## macOS: measured

Four versions of the same panel, each animating the same spring. Source and raw data are in
[`macos/benchmarks/ui-stack/`](../macos/benchmarks/ui-stack/).

Apple M5, macOS 26.6, Swift 6.4.2, optimized builds. Medians of 3 runs. Idle is a 30 s window; animate
is 20 s of continuous toggling.

| Candidate | Footprint | Idle CPU | Animation CPU | Animation wakeups |
| --- | --- | --- | --- | --- |
| Swift, AppKit, Core Animation | 8.27 MB | 0.3 ms | **15.9 ms** | 4.2/s |
| Objective-C, AppKit, Core Animation | 8.25 MB | 0.4 ms | 15.7 ms | 3.4/s |
| Same as first, SwiftUI linked but unused | 8.08 MB | 0.4 ms | 18.9 ms | 5.0/s |
| SwiftUI hosted in `NSPanel` | 9.86 MB | 0.6 ms | **619 ms** | 65.6/s |

What this shows:

- **Swift costs nothing here** compared with Objective-C, so Swift wins on safety and maintainability.
- **Idle cost is the same for all of them.** The run loop sleeps regardless of framework.
- **Animation is the real difference.** SwiftUI used about 39× the CPU, and about 65 wakeups per
  second suggests in-process work on every frame. Core Animation hands the curve to the render server.
- **Memory barely differs.** A widely repeated anecdote of 56 MB against 13 MB did not reproduce:
  the gap was 1.6 MB.

### SwiftUI

Linking SwiftUI without using it had no measurable cost. Opening and then closing one small SwiftUI
window left the app at **16.7 MB**, about 8.4 MB higher, and that memory did not return.

So SwiftUI is ruled out for the island and allowed, with care, in separate windows.

### Limits of this test

- One machine, one OS version, three runs.
- The scene is a single shape. Real content will change the absolute numbers.
- One SwiftUI implementation was tested. A different approach might animate more cheaply; if someone
  wants SwiftUI in the island, that is the experiment to run.
- Launch time is not reported as a comparison. Typical runs took 75–110 ms for every candidate, the
  first run of each batch took up to 890 ms, and other builds were running during the test.

## Windows: reasoned, unmeasured

No Windows machine was available, so nothing here comes from a measurement of this app.

### UI framework: Win32

| Option | Why not |
| --- | --- |
| Electron, Tauri, WebView2 | Each WebView2 control starts its own browser, renderer and GPU processes, per Microsoft. One project measured an idle Tauri app at about 175 MB. |
| WinUI 3 | Adds the Windows App SDK runtime. One issue report describes about 70 MB at start, and Microsoft has said it is still working on memory use. Weak evidence, and a heavy dependency for one shape. |
| WPF, WinForms, .NET | Brings a managed runtime into a process meant to sit idle. Not measured here. |
| Qt, Flutter | Ship their own rendering stack and a large dependency. Not measured here. |

### Language: C++ (a close call)

**No evidence was found that C++ is faster than Rust for this app**, and Nusa suggests Rust does well.
C++ was chosen for simplicity, not speed: the Windows SDK ships headers for everything needed, so
there is nothing to depend on, whereas Rust needs the `windows` crate. Rust's memory safety is a real
advantage given up here. If the team prefers Rust, this app is small enough to switch.

### Rendering: DirectComposition

Microsoft's documentation recommends `Windows.UI.Composition` over DirectComposition for Windows 10
apps, so this is a deliberate departure. Neither page gives a performance comparison, so this choice
is based on what each one requires.

| | DirectComposition | Windows.UI.Composition |
| --- | --- | --- |
| Animation runs in DWM, not our process | Yes | Yes |
| Requires | COM headers | C++/WinRT, `windowsapp.lib`, a DispatcherQueue |
| Springs | Built here from cubic segments, about 150 lines | Native |
| Interrupting a spring | Computed here | Native |
| Rounded clip, animatable | Yes | Yes |

The cost of the choice is `spring.h`, which is written and tested. The benefit is one less runtime
layer, which is plausible but **unmeasured**. All the composition code is in `compositor.cpp`, so
switching later is contained. [performance.md](performance.md#first-run-on-windows) lists the
comparison to run.

## macOS: the adapter

Now playing on macOS uses MediaRemoteAdapter, vendored in `macos/Vendor/MediaRemoteAdapter/`, because
macOS offers no public alternative that covers every player. Per-app events from Spotify and Music were
the other option; they need no extra process, but miss browsers and every other player. The choice was
made to match Boring Notch, and its limits are in
[architecture.md](architecture.md#now-playing-on-macos).

The adapter's binary is prebuilt, not built from source here. Boring Notch itself is GPL-3, so Noitch
reimplements its behavior and copies none of its code. Only the BSD-licensed adapter is reused.

## Trade-offs accepted

- Two codebases, so every feature is built twice.
- More code than SwiftUI: animations, layout and hit testing are done by hand.
- Text on Windows will need Direct2D and DirectWrite, which are lower level than any UI framework.

## Sources

- [Nusa site](https://rama2555.github.io/nusa), [AlternativeTo listing](https://alternativeto.net/software/nusa/about/)
- [SwiftUI vs AppKit anecdote](https://dev.to/woojinahn/a-13mb-cursor-monitor-appkit-undocumented-apis-zero-dependencies-1k72)
- [DirectComposition architecture](https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components), [animation](https://learn.microsoft.com/en-us/windows/win32/directcomp/animation), [rectangle clip](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nn-dcomp-idcompositionrectangleclip), [AddCubic](https://learn.microsoft.com/en-us/windows/win32/api/dcompanimation/nf-dcompanimation-idcompositionanimation-addcubic)
- [Visual layer in Win32](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/using-the-visual-layer-with-win32), [spring animation](https://learn.microsoft.com/en-us/uwp/api/windows.ui.composition.springscalarnaturalmotionanimation)
- [WebView2 performance](https://learn.microsoft.com/microsoft-edge/webview2/concepts/performance), [Tauri measurement](https://github.com/Fledge-Software/Syrinx-app/wiki/Background-09-phase-0-verdict)
- [WinUI 3 memory report](https://github.com/microsoft/Win2D/issues/996), [WinUI 3 performance work](https://www.techzine.eu/news/devops/141345/microsoft-promises-significant-speed-improvements-for-winui-3/)
- [mediaremote-adapter](https://github.com/ungive/mediaremote-adapter), [spitfiresb/notch](https://github.com/spitfiresb/notch), [Boring Notch](https://github.com/TheBoredTeam/boring.notch)

## Shelf and clipboard

Shelf uses the existing AppKit/Core Animation and Win32/DirectComposition stacks. macOS registers
public AppKit drag destinations and dragging sources. Windows OLE drag-and-drop requires an STA,
so the UI thread now calls OleInitialize before creating the island and uninitializes after
controllers and compositor objects are destroyed. The media source already tolerates an existing
COM apartment. No extra application thread is used; Windows behavior and memory remain unmeasured.
Clipboard notifications are public on Windows. macOS recording uses the owner's narrow, approved
one-second polling exception, with Pause, rather than a private pasteboard notification API.
