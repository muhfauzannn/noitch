# Adding features

How to grow Noitch without giving up what makes it light. Read [architecture.md](architecture.md)
first. The rules here are requirements, and [performance.md](performance.md) has the budgets.

## The shape of a feature

Each feature has up to two parts, in its own folder (`macos/Noitch/Features/<Name>/`,
`windows/src/features/<name>/`):

- **Source:** turns OS events into plain values. Owns the subscription.
- **Content:** turns values into layers or visuals. Owns the compositor objects.

The island's controller connects the two and remains the only owner of state.

`HomeController` also sits between the island and its sources, so the island's own controller stays
about the window.

Don't add a `Feature` protocol, plugin registry, event bus or dependency injection for the first
feature. Write it concretely. Extract an abstraction when a second feature shows what is actually
shared.

A feature that needs its own page adds a case to `IslandPage` (both platforms), a tab in the header,
and a branch in `PageController`'s show, hide and reveal switches. With two pages that is still
simpler than a protocol; reconsider at the third.

A feature with preferences adds a settings part and a settings page in its own folder (see
[Settings](architecture.md#settings)): a `<Name>Settings` value added to `AppSettings`, and a pane
listed in the settings window. Both apps get the same rows.

## Checklist

1. **Find the event.** Identify the OS notification or callback before writing anything. Polling is
   only acceptable if the OS offers no event, and then see the rules below.
2. **Design for rest.** What runs when nothing is happening? The answer should be nothing.
3. **Subscribe late, release early.** Start a source when its data can be seen, and stop it when it
   can't.
4. **Create content on demand.** Build layers on first expansion. Free them on collapse if they hold
   images or text.
5. **Animate with the compositor.** See below.
6. **Keep logic pure.** Parsing and layout go in pure types with tests.
7. **Implement both platforms in the same change**, unless the request names one platform. If an OS
   can't support the feature, record why. Update "Shared behavior" if behavior is shared.
8. **Measure.** Run the budget script before and after, and report both.

## Rendering content

**macOS**

- Use `CALayer`, `CATextLayer`, and image `contents`. Set `contentsScale` from the screen.
- Plain `NSView`s are fine for controls.
- Animate layer properties with `CAAnimation`, never per-frame code.
- Avoid `CAShapeLayer` path animation, blurs, shadows and masks until measured. They are the
  expensive end of Core Animation. Prefer `cornerRadius` and `opacity`.

**Windows**

- Text and images are drawn once with Direct2D and DirectWrite into an `IDCompositionSurface`, then
  shown by a visual. Redraw only when the content changes.
- Animate visual properties with `IDCompositionAnimation`. Build curves with `ScalarSpring`.
- D2D and DirectWrite are delay-loaded, so they cost nothing until the first text is drawn.
- Surfaces cost GPU memory for their whole area: size them to the content, not the window.

**Continuous motion** (visualizers, progress) needs care on both platforms, because it is the one
case where something must change over time.

- Prefer a repeating compositor animation over updating values from code.
- If live data truly must drive it, run a timer only while visible, cap it at 30 Hz, and stop it the
  moment the island collapses.

## SwiftUI

SwiftUI is not allowed in the island, where it cost about 39× more CPU per animation. It may be used
in **separate, rarely opened windows** such as settings, because linking it costs nothing measurable
until it is used.

The first SwiftUI window adds about 8.4 MB that stays **after the window closes**, so opening settings
once uses most of the footprint budget's headroom until the app restarts. Weigh that before choosing
SwiftUI over a small AppKit window. The numbers are in [tech-stack.md](tech-stack.md#swiftui).

## Where the events come from

Starting points for likely features. Verify each against current OS documentation before relying on
it. Only now playing, AI usage and launch at login are implemented so far.

| Feature | macOS | Windows |
| --- | --- | --- |
| Battery, charging | `IOPSNotificationCreateRunLoopSource` | `RegisterPowerSettingNotification` |
| Volume | CoreAudio property listeners | `IAudioEndpointVolumeCallback` |
| Network | `NWPathMonitor` | `INetworkListManager` events |
| Timers (focus, reminders) | One-shot `DispatchSourceTimer`, tolerance set | One-shot waitable timer |
| File shelf | `NSDraggingDestination`; keep URLs, not file data | `IDropTarget`; keep paths |
| Now playing | [Implemented](architecture.md#now-playing-on-macos) | `GlobalSystemMediaTransportControlsSessionManager` |
| AI usage | [Implemented](architecture.md#usage): reads local logs when shown | Same |
| Launch at login | `SMAppService` | Run registry key or Startup task |
| Full-screen detection | `NSWorkspace` and Space notifications | `SHQueryUserNotificationState` on foreground change |

### Windows WinRT

Now playing is the first WinRT use in the app. It uses the raw ABI headers with WRL, because
C++/WinRT needs exceptions. It loads lazily; what it adds to private memory has not been measured.

## Things that need a discussion first

Stop and ask before adding any of these:

- A third-party dependency, of any size
- A second process, helper, daemon or login item
- A private or undocumented API
- A new always-running thread
- A web view, or any embedded runtime
- A polling loop or periodic timer at rest
