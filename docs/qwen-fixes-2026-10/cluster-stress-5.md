FIXED

# Cluster stress-5: idle headless instance keeps its main thread at 80-95% CPU

Verdict: **FIXED**. The busy loop is not in `GuiApplication.cpp:104` (the finding's pointer, `a11y::installAccessibility()`, is not in the stack). It is a repaint feedback loop in the arranger's range overlay, `src/gui/ArrangerOverlays.cpp:91-96` (the filter), fixed with the change at `:96` and `:165`.

## Measured before the fix (unfixed build, `build/zene`)

- Idle instance started with the shared harness (`tests/control_socket_harness.py` `start_instance` + `wait_ready`), main thread sampled from `/proc/<pid>/task/<pid>/stat` over 5 s windows: **86.0-89.6%** (`idle-sample-2.log`, EXIT=0). The audio thread (`lmms::AudioDumm`) was at about 2%.
- Main-thread stack (gdb, `gdb-bt-3.log`, Thread 1): `QCoreApplication::exec` → `QWidget::event` → `QWidgetRepaintManager::paintAndFlush` → background `fillRect`. The main thread was repainting, not polling.
- Callers of `QWidget::update` in steady state (breakpoints enabled after startup with SIGUSR1, `gdb-upd-3.log`): `lmms::gui::ArrangerRangeOverlay::eventFilter` 2915 hits, `ReadoutField::refresh` 277, `TransportReadouts::tick` 275, `Navigator::follow` 35.

## Root cause

`ArrangerRangeOverlay` is a transparent child of the viewport it watches (`ArrangerOverlays.cpp:54-60`, `viewport->installEventFilter(this)`). Its filter called `update()` on **every** `QEvent::Paint` of that viewport (`ArrangerOverlays.cpp:94` before the fix). Repainting a transparent child also repaints the parent beneath it, so the viewport sent another `Paint`, which scheduled another overlay repaint. The loop had no exit and ran with no content and no range, which is the idle state of the finding.

Sibling check: `VideoLane.cpp:126` and `ArrangerRulerLanes.cpp:204` have the same `Paint → update()` shape, but they watch `editor->timeLine()`, which is not their ancestor, so their repaint does not repaint the watched widget and cannot feed back. The steady-state trace shows no hits for either. They are unchanged.

## The change

- `src/gui/ArrangerOverlays.cpp:96`: a viewport `Paint` schedules the overlay only when `rangeRows() != m_paintedRows`, i.e. when the rows it draws have moved.
- `src/gui/ArrangerOverlays.cpp:165`: `paintEvent` records the rows it drew in `m_paintedRows`.
- `include/ArrangerOverlays.h:97`: `QVector<QRect> m_paintedRows;` (declaration with its definition in the same commit; 12 includers rebuilt).

The overlay's painted output is a function of `rangeRows()` (and the overlay's size), so an unchanged row set means the repaint would draw the same pixels.

## Regression test (`tests/src/gui/ArrangerOverlaysVideoLaneTest.cpp`)

Added in the family that already covers this overlay (`ArrangerOverlaysVideoLaneTest`). The three families named in the brief cover `GuiApplication`, not the arranger overlay, so the arranger file is the right home.

- `anIdleRangeOverlayStopsRepaintingItself` (line 198): counts the overlay's `Paint` events over 30 event-loop passes with no range. Asserts at most 2.
- `theRangeOverlayRepaintsWhenItsRowsMove` (line 213): guard. With a range shown, zooming moves the rows (`rangeRect()` changes), and the overlay must repaint at least once.
- `PaintCounter` (line 74) and `pumpEvents` (line 88) are helpers in the same file.

Note on the guard: the brief's "scroll" path cannot move the rows here, because an empty project has no horizontal scroll range (`horizontalScroll()->maximum()` was 0 even at 200 px per bar; first attempt failed for that reason). The guard therefore moves the rows by zoom. It does not exercise the scroll path.

## Commands and exit codes (all logs in the LOG FOLDER `…/runs/stress-5.r1/logs/`)

Build environment note: ccache could not write its temp file in the default dir (`build-red-1.log`, EXIT=2). Every later build used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp`. This changes only where ccache writes temp files.

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| RED build (final test, unfixed product) | `cmake --build build --target ArrangerOverlaysVideoLaneTest -j2` | `build-red-5.log` | 0 |
| RED run | `cd build/tests && ctest -R '^ArrangerOverlaysVideoLaneTest$' --output-on-failure` | `test-red-4.log` | **8** |
| → RED result | `anIdleRangeOverlayStopsRepaintingItself`: `'counter.paints <= 2' returned FALSE (… painted 30 times in 30 event passes)`; the other 11 slots pass; Totals 11 passed, 1 failed | | |
| 4b: save fix | `git diff -- src/gui/ArrangerOverlays.cpp include/ArrangerOverlays.h > fix.patch` | `fix.patch` | 0 |
| 4b: revert | `git apply -R fix.patch` | | 0 |
| 4b: unfixed build | `cmake --build build --target zene ArrangerOverlaysVideoLaneTest -j2` | `build-4b-unfixed.log` | 0 |
| 4b: unfixed run | `ctest -R '^ArrangerOverlaysVideoLaneTest$' --output-on-failure` | `test-4b-red.log` | **8** (same `30 times in 30 event passes` failure) |
| 4b: re-apply | `git apply fix.patch` | | 0 |
| 4b: fixed build | `cmake --build build --target zene ArrangerOverlaysVideoLaneTest -j2` | `build-4b-fixed.log` | 0 |
| GREEN run | `ctest -R '^ArrangerOverlaysVideoLaneTest$' --output-on-failure` | `test-4b-green.log` | **0** (ctest: `100% tests passed out of 1`; 0 `FAIL!` lines in the log; the test binary has 12 slots, 10 pre-existing + 2 new, all passing) |
| Family (`-R Arranger`, all 5 arranger tests, after building the two that were not built: `ArrangerTakePunchTest`, `ArrangerFileDropTest`) | `ctest -R 'Arranger'` | `test-family-2.log` | **0**, 5/5 passed (the first run, `test-family-1.log`, had those two "Not Run" and exited 8 for that reason only) |

The first GREEN log name `test-green-1.log` was produced before the 4b cycle and shows the same result (EXIT 0).

## Idle CPU after the fix (the finding's verify step)

- Headless, same sampler and harness: main thread **2.0%** in each of 3 windows (`idle-sample-fixed.log`, EXIT=0). Before: 86-90%.
- Windowed (`QT_QPA_PLATFORM=xcb`, `DISPLAY=:0`, fixed build, launched directly with the scratch config): main thread **2%** over 5 s (command output; the xcb run opened a window on the session display for about 25 s, then was killed by pid).
- **Not measured:** the unfixed windowed build. So I can say the windowed app is idle now, but not that it had the loop before. The mechanism is in widget code, not in the platform, so I expect the same loop, but that is unverified.

## Gates

- Official `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-1.log`): **EXIT=1 with `PermissionError`**. It creates `<worktree>-feature-check` beside the worktree, which is outside the write-jail. I did not edit the shared script.
- Same checks run through the script's own functions with logs in the LOG FOLDER (`gates_runner.py`, `gates-3.log`, EXIT=0):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

  The ratchet baseline for `7641e9493` was already cached in `gate-baselines/`; the runner read it and did not write there.
- Registration: both product files are already in `tests/fork-sources.txt` (`include/ArrangerOverlays.h:752`, `src/gui/ArrangerOverlays.cpp:1973`); the test file is at `tests/fork-sources.txt:2792`. No `upstream-modifications.txt` change is needed.

## Self-check

1. **RED fails / GREEN passes.** RED (unfixed, freshly built): `test-red-4.log` EXIT=8, `anIdleRangeOverlayStopsRepaintingItself` fails with 30 paints in 30 passes. 4b (revert via `fix.patch`, rebuild, run): `test-4b-red.log` EXIT=8, same failure. Re-applied, rebuilt: `test-4b-green.log` EXIT=0.
2. **Deterministic.** No wall-clock timing and no sleeps. The test counts `QEvent::Paint` events the overlay receives over a fixed number of `QCoreApplication::processEvents()` passes (`pumpEvents`, line 88). The seam is the event filter (`PaintCounter`). Assumption: Qt delivers the queued `UpdateRequest` within those passes. The RED result shows it does in this build. This is an empirical result, not a documented Qt guarantee.
3. **At the cause and complete.** The cause is the unconditional `update()` on viewport `Paint` (`ArrangerOverlays.cpp:94`, now `:96`). Callers: the overlay's own `update()` calls (`ArrangerRange::changed`, `pixelsPerBarChanged`, `setTool`) are unchanged and still repaint directly. Siblings: `VideoLane.cpp:126` and `ArrangerRulerLanes.cpp:204` have the same shape but watch a non-ancestor (`timeLine()`), and the steady-state trace shows no hits for them. Not changed.
4. **No collateral damage.** No data, file format, control command or limit is involved. Only the repaint schedule changes. Repaint-on-row-move is kept (guard test). Unverified assumption, stated plainly: the fix relies on Qt repainting overlapping children when a parent region repaints, so skipping the overlay's repaint when its rows are unchanged loses no pixels. I did not verify this against the Qt source. The arranger family passes (5/5), including the `grab()`-based pixel checks in `theRangeToolDragsOutATimeRangeOverTheTracksBetween`, which covers the painted output. The scroll path (rows moving under a scroll) is not covered by a test, because the empty project has no scroll range.
5. **Verdict basis.** FIXED is supported by the file:line evidence and the RED/GREEN runs above, not by the finding's pointer, which was wrong.

## Not done / open

- The official `feature_check.py` run fails on its log directory write (above). The substitute run passes, but it is not the official gate's exit code.
- The unfixed windowed (xcb) measurement is missing (above).
- The guard test does not exercise the scroll path; the scroll path depends on the unverified Qt child-repaint assumption (item 4).
