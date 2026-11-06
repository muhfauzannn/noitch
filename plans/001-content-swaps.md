# 001 — Fade content swaps in Home

- **Status**: DONE
- **Commit**: 96056dc
- **Severity**: MEDIUM
- **Category**: Missed opportunities / Cohesion
- **Estimated scope**: macOS 1 file, Windows 3 files plus a pure header

## Problem

Home swaps content with no transition. Artwork and the lyric line fade, so the rest looks broken
next to them.

```swift
// macos/Noitch/Features/Home/HomeScene.swift:apply — current
title.string = model.title
artist.string = model.artist
...
for layer in [title, artist, lyricLine, elapsed, remaining, track] { layer.isHidden = showsPanel }
for layer in panelLines { layer.isHidden = !showsPanel }
```

Windows repaints one Direct2D surface on every change, so every swap is instant there.

## Target

Easing is `ease-out` (entering content). All values are final.

| Swap | Duration | Easing |
| --- | --- | --- |
| Lyrics panel toggled | 150 ms | ease-out |
| Title or artist changed (new track) | 200 ms | ease-out, same as the existing lyric fade |

- **macOS:** `CATransition` of type `.fade`. The panel toggle is added to `root`, the parent of every layer that
  changes `isHidden`. Title and artist get one each. The per-second time text never fades.
- **Windows:** repaint first, then ramp the content visual's opacity from 0.35 to 1 over 150 ms with an
  ease-out cubic, through `IDCompositionVisual3::SetOpacity(IDCompositionAnimation*)`. It is a dip rather
  than a crossfade, because a crossfade needs two surfaces. If the visual doesn't support
  `IDCompositionVisual3`, skip the animation.
- Reduce Motion keeps these: opacity aids comprehension and moves nothing.

## Repo conventions to follow

- Exemplar: `HomeScene.addFade(to:)` for the macOS fade.
- Windows animation curves are cubic segments, see `CubicSegment` in `windows/src/island/spring.h`. The
  curve math goes in a pure header with tests: `windows/src/island/opacity_motion.h`.

## Steps

1. macOS: in `apply()`, when `previous?.showsLyricsPanel != model.showsLyricsPanel`, call
   `Self.addFade(to: root, duration: 0.15)`. Give `addFade` a `duration` parameter, default 0.2, and set
   `timingFunction` to `.easeOut`.
2. macOS: when `previous?.title != model.title`, fade `title`. When the artist changed, fade `artist`.
3. Windows: add `opacity_motion.h`, with `OpacityMotion` giving `Value(now)` and cubic segments for an ease-out
   ramp that can start from the current value.
4. Windows: add `Compositor::AnimateContentOpacity(const OpacityMotion&)`.
5. Windows: in `HomeController::Refresh`, compare against the previously painted panel flag and title. If either
   changed, paint, then start the 0.35 to 1 ramp.

## Boundaries

- Do NOT fade `elapsed` or `remaining`: they change every second.
- Do NOT add timers or per-frame work.
- If the code no longer matches, STOP and report.

## Verification

- **Mechanical**: `xcodebuild test` passes, `island_tests` passes, and `measure.sh --hover` stays within budget.
- **Feel check**: toggle the lyrics button and change tracks. Nothing pops, and spamming the toggle never
  leaves a layer half visible.
- **Done when**: both swaps fade on macOS, and the Windows code type-checks.
