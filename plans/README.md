# Animation plans

| # | Title | Severity | Status |
| --- | --- | --- | --- |
| 001 | Fade content swaps in Home | MEDIUM | DONE |
| 002 | Hover fade and press feedback on Home buttons | MEDIUM | DONE |
| 003 | Respect Reduce Motion in the island spring | MEDIUM | DONE |

Order: 001, 002, 003. 002 reuses `OpacityMotion` from 001 on Windows, and its macOS press scale reads the Reduce
Motion setting from 003. Skipped for now: content scale on open, and a fade on the play/pause icon.

DONE means written and type-checked, not watched: the motion hasn't been seen on screen, and Windows hasn't run.
