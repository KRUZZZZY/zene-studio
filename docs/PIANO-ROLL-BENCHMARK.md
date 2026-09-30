# Piano-roll and engine benchmarks (R7.3, R7.4)

`tests/src/gui/PianoRollBenchmarkTest.cpp` times the real `PianoRollWindow`, offscreen, over
clips of 1 000, 5 000, 10 000 and 50 000 notes: one frame is a `grab()` of the window at
1600 x 900 (the roll, its keyboard and its note-property area), and each size reports the
median of five frames after one warm-up.

## Numbers on the development box (2026-09-30, RelWithDebInfo, offscreen)

| Notes | Frame at the default zoom (ms, median) | Frame at 12.5%, the widest zoom (ms, median) |
| ---: | ---: | ---: |
| 1 000 | 4.25 | 13.37 |
| 5 000 | 4.30 | 13.37 |
| 10 000 | 4.46 | 13.41 |
| 50 000 | 4.57 | 13.49 |

(The default-zoom column was 4.31 / 4.27 / 4.26 / 4.51 on the first run; run-to-run noise here
is a few percent.)

**The frame time is flat in the note count at both zooms.** Paint cost follows the notes ON
SCREEN, not the clip's size: the whole 50 000-note clip costs 0.1-0.3 ms more per frame than a
1 000-note one, so the off-screen notes are all but free. Zoomed all the way out a frame costs
about three times the default. **What the harness cannot yet show** is a frame with tens of
thousands of notes on screen: at 12.5% the 1600-pixel window holds about 64 bars, which is
about 1 000 of this fill's sixteenth notes - so every size above 1 000 puts the same ~1 000
notes on screen. A successor editor (R8.8) that allows a wider zoom will have to be measured
at it; this one has none. The test finds the zoom through the toolbar's own combo box (the
model is private to the editor), so it drives what a user drives.

## Engine render (R7.4)

`tests/src/core/EngineRenderBenchmarkTest.cpp` times `AudioEngine::renderNextPeriod()` itself,
with the Dummy device's thread stopped: sixteen sample tracks of noise into the master, over a
two-bar loop inside the clips, for 2 000 periods of 256 frames at 44.1 kHz. It asserts the
session is audible and that the automated variant is audibly quieter where its fader starts
(0.2). **Those two checks are not decoration: the first draft measured silence** - the clips
ended a third of the way into the run and the peak was read from a stale, double-buffered
period - and reported a number that meant nothing.

| Session | us per period (three runs) | Realtime factor |
| --- | ---: | ---: |
| 16 sample tracks | 78.0 / 81.7 / 75.9 | about x74 |
| the same, master fader on a Linear (sample-accurate, R1.2) automation clip | 81.3 / 82.6 / 79.2 | about x71 |

The per-sample automation ramp costs about 3-4% of this session's render. The same opt-in gate
applies (below); the keys are `plain` and `automated`.

## The gate

A timing only compares on the machine that took it, so the +20% regression gate is opt-in:

- `ZENE_BENCH_WRITE=<file>` writes `key<TAB>value` lines (a baseline for this machine);
- `ZENE_BENCH_BASELINE=<file>` fails the test when any key is more than 20% slower.

Both benchmarks share the gate (`tests/src/core/BenchmarkBaseline.h`).

Without either, the test measures and asserts only a sanity bound (a frame under five seconds -
a hang, not a slowdown). Negative control, recorded: a baseline of half the measured times
fails the gate (`1000 notes: 5.15 ms against a 2.16 ms baseline - more than 20% slower`), and so
does the engine's (`automated: 79.2014 against a 41.19 baseline - more than 20% slower`); its own
baseline passes.

## What the harness found

Building the roll without the full `GuiApplication` crashed twice in the
`BUGS_FOUND` 10.4 class - `SimpleTextFloat`'s constructor and `PianoRoll::paintEvent` both
dereferenced a null `getGUI()`. Both are guarded (the application's behaviour is unchanged).
