# Piano-roll benchmark (R7.3)

`tests/src/gui/PianoRollBenchmarkTest.cpp` times the real `PianoRollWindow`, offscreen, over
clips of 1 000, 5 000, 10 000 and 50 000 notes: one frame is a `grab()` of the window at
1600 x 900 (the roll, its keyboard and its note-property area), and each size reports the
median of five frames after one warm-up.

## Numbers on the development box (2026-09-30, RelWithDebInfo, offscreen)

| Notes | Frame (ms, median) |
| ---: | ---: |
| 1 000 | 4.31 |
| 5 000 | 4.27 |
| 10 000 | 4.26 |
| 50 000 | 4.51 |

**The frame time is flat in the note count at the default zoom.** The view shows the first
bars only, so paint cost follows the notes ON SCREEN, not the clip's size - nothing in the
paint path scales with the total. That is the finding, and also the harness's current limit:
a zoomed-out frame (tens of thousands of notes visible) is the case a successor editor (R8.8)
must beat, and measuring it needs the roll's zoom, which is private to `PianoRollWindow`
today. It is the next slice.

## The gate

A timing only compares on the machine that took it, so the +20% regression gate is opt-in:

- `ZENE_BENCH_WRITE=<file>` writes `notes<TAB>ms` lines (a baseline for this machine);
- `ZENE_BENCH_BASELINE=<file>` fails the test when any size is more than 20% slower.

Without either, the test measures and asserts only a sanity bound (a frame under five seconds -
a hang, not a slowdown). Negative control, recorded: a baseline of half the measured times
fails the gate (`1000 notes: 5.15 ms against a 2.16 ms baseline - more than 20% slower`).

## What the harness found

Building the roll without the full `GuiApplication` crashed twice in the
`BUGS_FOUND` 10.4 class - `SimpleTextFloat`'s constructor and `PianoRoll::paintEvent` both
dereferenced a null `getGUI()`. Both are guarded (the application's behaviour is unchanged).
