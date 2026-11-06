# 002 — Hover fade and press feedback on Home buttons

- **Status**: DONE
- **Commit**: 96056dc
- **Severity**: MEDIUM
- **Category**: Physicality / Easing
- **Estimated scope**: macOS 1 file, Windows 4 files

## Problem

The hover plate appears and moves instantly, and pressing only brightens it.

```swift
// macos/Noitch/Features/Home/HomeScene.swift:setHover — current
hoverPlate.frame = frame
hoverPlate.opacity = isPressed ? 1 : 0.55
hoverPlate.isHidden = false      // and isHidden = true when leaving
```

## Target

| Property | Value |
| --- | --- |
| Plate fade in or out | 100 ms, ease-out |
| Moving between buttons | Instant, no glide: hover happens tens of times a day |
| Press scale of the icon | 0.92, 100 ms ease-out going down, 160 ms ease-out coming back |

0.92 sits below the usual 0.95 to 0.98 because these icons are 20 pt, and 0.95 would be about one pixel.

- **macOS:** the plate stays one layer. Set its frame in a transaction with actions disabled, then set its
  `opacity` in another with `setAnimationDuration` and `setAnimationTimingFunction(.easeOut)`. Scale the icon
  layer's `transform` the same way. Skip the scale when Reduce Motion is on (see plan 003).
- **Windows:** move the plate out of the shared surface into its own visual and surface, a white circle sized to
  one button. Animate its opacity 0 to 0.55 for hover and to 1 for pressed. **Gap:** no press scale, because the
  icons live in the shared surface and scaling one needs its own visual.

## Repo conventions to follow

- Exemplar: `HomeScene.fade(to:duration:delay:)` starts from `presentation()`, so interruptions don't jump.
- Windows: reuse `OpacityMotion` from plan 001.

## Steps

1. macOS: replace `isHidden` on `hoverPlate` with an opacity fade, starting hidden at opacity 0.
2. macOS: track the pressed icon and animate its scale to 0.92, then back to identity.
3. Windows: add `Compositor::CreatePlate`, `ShowPlate(x, y, opacity, now)` and `HidePlate(now)`.
4. Windows: remove plate drawing from `HomeScene::Paint`. Add `HomeScene::SetHover`, called by `HomeController`.

## Boundaries

- Do NOT animate the plate's position or size.
- Do NOT use `transition: all` equivalents: only `opacity` and `transform`.
- If the code no longer matches, STOP and report.

## Verification

- **Mechanical**: tests and `measure.sh --hover` as in plan 001.
- **Feel check**: sweep across the transport buttons quickly. The plate never flickers or lags behind the cursor.
  Press and hold: the icon shrinks, and releases smoothly.
- **Done when**: no plate pops in or out on macOS.
