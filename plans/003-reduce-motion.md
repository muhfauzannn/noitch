# 003 — Respect Reduce Motion in the island spring

- **Status**: DONE
- **Commit**: 96056dc
- **Severity**: MEDIUM
- **Category**: Accessibility
- **Estimated scope**: 4 files

## Problem

The pill's spring bounces at 0.18 whatever the user's setting.

```swift
// macos/Noitch/Island/IslandView.swift:addSpring — current
let spring = CASpringAnimation(perceptualDuration: 0.38, bounce: 0.18)
```

## Target

When Reduce Motion is on, the spring has no bounce and keeps its 0.38 s response. Fades stay, because they
move nothing. Press scale (plan 002) is dropped.

- **macOS:** `bounce: NSWorkspace.shared.accessibilityDisplayShouldReduceMotion ? 0 : 0.18`.
- **Windows:** `SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION)` returns false. The closed-form spring divides by
  zero at bounce 0, so use `kReducedBounce = 0.01`, which overshoots by less than a pixel.

## Repo conventions to follow

- Shared behavior lives in `docs/architecture.md`. Add the Reduce Motion rule to its table.
- Windows: `ScalarSpring` takes the bounce as a constructor argument, default `kSpringBounce`.

## Steps

1. macOS: read the setting in `addSpring` and pass the bounce.
2. Windows: make bounce a member of `ScalarSpring`, and add a `bounce` argument to `PillMotion::Retarget`.
3. Windows: `IslandWindow::TransitionTo` reads the setting and passes the bounce.
4. Add a test that a 0.01 bounce settles on target without overshooting by more than 0.5 px over 300 px.

## Boundaries

- Do NOT change the default spring or the shared 0.38 s response.
- If the code no longer matches, STOP and report.

## Verification

- **Mechanical**: `island_tests` passes.
- **Feel check**: with System Settings, Accessibility, Display, Reduce Motion on, the island opens and closes
  without a bounce, and still fades its content.
- **Done when**: both platforms read the setting at the start of each transition, never cached.
