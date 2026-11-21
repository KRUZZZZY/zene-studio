# Cluster new-p9-303-port

Verdict: **FIXED** (round 4; rounds 1-3 are already on this branch and Luna's round-3 review is addressed here).

Finding: a fast paint stroke skips the grid cells between mouse events. A drag that crosses several
cells can be delivered as only a press, one sparse move and a release; `extendPaint()` paints only the
cell under an event and the release did not close its final segment.

## Round 4 change (Luna's review of round 3)

`qa/reviews/new-p9-303-port.r3.md` (DISPUTE) found the remaining boundary-start defect: the walk
seeded each axis from its floor cell while `boundaryParam` excluded `t == 0`. A stroke that starts
exactly on a time or row boundary and moves backwards therefore kept the cell it had just left while
the other axis crossed boundaries: it painted an uncrossed cell and missed the cell it actually
entered. The review asked for the immediate interior cell on a backward boundary start, endpoint
preservation, and permanent backward time-boundary and row-boundary regressions.

The walk now:

- `src/gui/noteeditor/NoteCanvasGestures.cpp:65` `cellAtStart` seeds the cell the path is in just
  after `t = 0`: the floor cell, or the cell below it when `from` is exactly on a boundary (within
  `1e-9`) and `delta < 0`. This is the interior cell the path enters immediately.
- `src/gui/noteeditor/NoteCanvasGestures.cpp:76` `nextBoundaryParam` returns the parameter
  (`0 < t < 1`) at which the current cell is left through its next boundary. It is computed from the
  current interior cell (not from the old boundary), and `t = 1` is excluded so an end point that is
  exactly on a boundary keeps the floor cell that `extendPaint` paints, instead of also painting the
  cell beyond it.
- `src/gui/noteeditor/NoteCanvasGestures.cpp:115` `paintAlong` seeds that interior cell
  (`:128-131`), steps whichever boundary comes first (`:133-142`, both on a corner) and closes the
  segment with `extendPaint(to)` (`:143`). Per-cell dedupe (`appendPaintCell`, `:54`) is unchanged.

Regressions added next to the paint tests:

- `tests/src/gui/NoteEditorTest.cpp:527` `aBackwardTimeBoundaryPaintStrokePaintsTheCellsItEnters` -
  starts exactly on tick 300, moves left and up to tick 288/key 64. Asserts cell 288 on key 63 (the
  immediate interior cell, missed before) is painted and cell 300 on key 63 (never entered) is not.
- `tests/src/gui/NoteEditorTest.cpp:547` `aBackwardRowBoundaryPaintStrokePaintsTheCellsItEnters` -
  starts exactly on the top of key 60's row, moves left to tick 283/key 61. Asserts cell 288 on key
  61 (crossed after the row boundary, missed before) is painted and cell 276 on key 60 (never
  entered) is not.
- `tests/src/gui/NoteEditorTest.cpp:566`
  `aBackwardPaintStrokeEndingOnACellBoundaryDoesNotPaintTheCellBeyondIt` - ends exactly on tick 288;
  asserts the release cell is painted and cell 276, beyond the release, is not.
- The four earlier paint regressions are retained and pass: horizontal dense (`,:448`), narrow zoom
  through the release (`:466`), sparse vertical in one column (`:489`) and sparse diagonal
  (`:508`).

## Earlier rounds on this branch

- Round 1 (`794014865`) added the path walk and the release call
  (`src/gui/noteeditor/NoteCanvasInput.cpp:241,262`); round 2 (`24de290ad`) added the vertical walk;
  round 3 (`e52fb726c`) replaced the per-axis sampling with the 2-D DDA over tick and row
  boundaries. Their logs remain under `runs/new-p9-303-port.r1`, `.r2` and `.r3`.
- No new control commands, no new source files, no audio-thread code, no header signature change
  (`include/NoteCanvas.h:182` already declared `paintAlong`).
- The release call site still runs only for `Drag::Paint`; `strokeAlong` (mute/erase) is untouched.

## Evidence (r5 logs)

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-303-port.r5/logs`.
Builds run from the worktree; `ctest` runs from `build/tests`.

| Step | Command | Exit |
|---|---|---:|
| Build, new tests + old product code | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r5 cmake --build build --target zene NoteEditorTest -j12 > $LOGS/build-1-red.log 2>&1; echo EXIT=$?` | 0 |
| RED - old product code | `ctest -R '^NoteEditorTest$' --output-on-failure > $LOGS/test-1-red.log 2>&1; echo EXIT=$?` | **8** (36 passed, 3 failed) |
| Build with the fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r5 cmake --build build --target zene NoteEditorTest -j12 > $LOGS/build-2-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN | `ctest -R '^NoteEditorTest$' --output-on-failure > $LOGS/test-2-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with totals | `ctest -R '^NoteEditorTest$' -V > $LOGS/test-3-green-verbose.log 2>&1; echo EXIT=$?` | 0 (39 passed, 0 failed) |
| Save the product patch | `git diff -- src/gui/noteeditor/NoteCanvasGestures.cpp > $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Reverse the product patch | `git apply -R $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Build without the product change | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r5 cmake --build build --target zene NoteEditorTest -j12 > $LOGS/build-3-reverted.log 2>&1; echo EXIT=$?` | 0 |
| RED without the product change | `ctest -R '^NoteEditorTest$' --output-on-failure > $LOGS/test-4-reverted-red.log 2>&1; echo EXIT=$?` | **8** (36 passed, 3 failed) |
| Re-apply the product patch | `git apply $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Build with the change again | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r5 cmake --build build --target zene NoteEditorTest -j12 > $LOGS/build-4-reapplied.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after re-apply | `ctest -R '^NoteEditorTest$' --output-on-failure > $LOGS/test-5-reapplied-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after re-apply with totals | `ctest -R '^NoteEditorTest$' -V > $LOGS/test-5b-reapplied-verbose.log 2>&1; echo EXIT=$?` | 0 (39 passed, 0 failed) |
| Build the family's other target | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r5 cmake --build build --target NoteEditorFlTest -j12 > $LOGS/build-5b-family.log 2>&1; echo EXIT=$?` | 0 |
| Family run | `ctest -R "NoteEditor" --output-on-failure > $LOGS/test-7-family.log 2>&1; echo EXIT=$?` | 0 (3 tests passed) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $LOGS/gate-feature-check.log 2>&1; echo EXIT=$?` | 0 (PASS) |
| Complexity gate | `bash tests/complexity-gate.sh --check > $LOGS/gate-complexity.log 2>&1; echo EXIT=$?` | **1** (6 pre-existing) |
| File-length gate | `bash tests/file-length-gate.sh --check > $LOGS/gate-filelength.log 2>&1; echo EXIT=$?` | **1** (14 pre-existing) |
| Whitespace | `git diff --check; echo EXIT=$?` | 0 |

What the RED/GREEN runs say:

- RED (`test-1-red.log`, `test-4-reverted-red.log`): `Totals: 36 passed, 3 failed`; the three failures
  are exactly the new assertions at `NoteEditorTest.cpp:539`, `:558` and `:577`
  (`noteAt(m_clip, 288, 63) != nullptr`, `noteAt(m_clip, 288, 61) != nullptr`,
  `noteAt(m_clip, 276, 62) == nullptr`). They fail both before any product change and with the
  product patch reverse-applied.
- GREEN (`test-2-green.log`, `test-3-green-verbose.log`, `test-5-*.log`): exit 0,
  `Totals: 39 passed, 0 failed` (36 earlier QTest functions + 3 new).
- Family: `ctest -R "NoteEditor"` ran 3 tests - `NoteEditorTest`, `NoteEditorFlTest` and
  `ControlNoteEditor` (python) - all passed; zero tests would be a failure.

## Gates

| Gate | This tree | Base tree |
|---|---:|---:|
| `feature_check.py` | 0 (PASS) | - |
| `bash tests/complexity-gate.sh --check` | 1 | 1 (`runs/new-p9-303-port.r1/logs/gate-complexity-base.log`) |
| `bash tests/file-length-gate.sh --check` | 1 | 1 (`runs/new-p9-303-port.r1/logs/gate-filelength-base.log`) |

- Complexity: 6 `REGRESSION` lines (pre-existing: `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`,
  `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`,
  `ControlCommandsShortcuts.cpp`). A sorted `diff` against the base log exits 0; neither
  `cellAtStart`, `nextBoundaryParam`, `paintAlong` nor `keyOfRow` is listed.
- File length: 14 `REGRESSION` lines, sorted `diff` against the base log exits 0; neither
  `src/gui/noteeditor/NoteCanvasGestures.cpp` nor `tests/src/gui/NoteEditorTest.cpp` is listed.
- No gate script, baseline or ratchet was edited.

## Files registered

- `tests/fork-sources.txt` - `src/gui/noteeditor/NoteCanvasGestures.cpp` and this report are listed.
- `tests/upstream-modifications.txt` - `tests/src/gui/NoteEditorTest.cpp` is already declared there
  (inherited file); the product files are fork-new.

## Known cost, not changed

`appendPaintCell` dedupes with a linear scan, so a single event sweeping a very long distance at
narrow zoom is quadratic in cells crossed. This is the same cost the earlier rounds and the accepted
commit `31076ea56` have; the regressions here cross at most 17 cells. No cap was added because a cap
would silently drop cells the fix is meant to paint.

## Self-check

- The regression tests FAIL with the product change reverted and PASS with it:
  `test-4-reverted-red.log` exits 8 with the three new assertions failing; `test-5-reapplied-green.log`
  exits 0 and `test-5b-reapplied-verbose.log` records `Totals: 39 passed, 0 failed`. The product patch
  was saved with `git diff -- src/gui/noteeditor/NoteCanvasGestures.cpp`, reverse-applied (exit 0)
  and re-applied (exit 0).
- The tests are deterministic: `QTest::mousePress/mouseMove/mouseRelease` with fixed canvas points
  computed from `grid.xOfTick`, `grid.yOfKey` and `grid.rowHeight()`; no sleeps, timers, wall clock or
  threads. The exact-boundary cases use the grid's own pixel/tick mapping at the fixed zoom 1.0.
- The fix is at the cause and complete: `cellAtStart` now chooses the interior cell for a backward
  exact-boundary start on both axes, `nextBoundaryParam` computes the next boundary from that cell
  and excludes `t = 1` so the release cell (painted by `extendPaint`) is not overshot. Callers checked:
  `NoteCanvasInput.cpp:241` (move) and `:262` (release) both pass the same segment endpoints;
  `pressPaint` still paints the press cell; `extendPaint`/`appendPaintCell` are unchanged; the
  mute/erase `strokeAlong` does not use this walk and is untouched.
- No collateral damage: no project or save/load code touched; dedupe stays per `(key, start)`; the
  four earlier paint regressions still pass (horizontal, narrow zoom, vertical, diagonal), so strokes
  that do not start or end on a boundary behave exactly as before.
- Verdict is FIXED (not already-fixed/false-positive); the evidence is the RED/GREEN runs and the
  line references above.
