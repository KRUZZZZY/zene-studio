# Cluster new-p9-303-port

Verdict: **FIXED** (round 3, this port branch, after Luna's REWORK of round 2).

Finding: a fast paint stroke skips the grid cells between mouse events.

## What round 3 changed

Luna's review of round 2 (`qa/reviews/new-p9-303-port.r2.md`) returned REWORK: `paintAlong` walked
time columns and rounded the interpolated `y` at each column edge, and sampled only integer `y`
values for pitch. A stroke from `point(300, 66)` to `point(402, 61)` therefore missed the cell at
tick 312 on key 66 - the path leaves key 66's row a hair after tick 310, so it enters cell 312 on
key 66 and crosses into key 65's row before the next column edge. The rework asked for the cells of
the continuous 2-D path, including short segments between a time boundary and a pitch boundary, plus
a regression for exactly that drag.

The port is now one DDA over the grid's own boundaries:

- `src/gui/noteeditor/NoteCanvasGestures.cpp:103` `NoteCanvas::paintAlong` walks the segment in
  tick space (`grid.tickAtX`, `:112-115`) and in row space (`y + scrollY`, rows every
  `grid.rowHeight()` pixels). It paints the cell it starts in (`:118-120`), then, for as long as
  either axis has a boundary left inside the segment (`:125`), steps whichever boundary comes first
  in path-parameter order - both on a corner (`:127-130`) - and paints the cell it enters (`:131`).
  `extendPaint(to)` (`:133`) still closes the last segment at release.
  No floating-point position is rounded and no axis is walked alone, so a cell narrower than a
  pixel, a stroke inside one column and a diagonal are all covered by the same walk.
- `src/gui/noteeditor/NoteCanvasGestures.cpp:64` `boundaryParam` - the parameter (0 < t <= 1) of the
  first multiple of `step` after `from`, or 2 when the axis does not move or crosses none inside the
  segment (an axis that does not move is never stepped, so the walk cannot divide by zero).
- `src/gui/noteeditor/NoteCanvasGestures.cpp:74` `keyOfRow` - the key on a row index, -1 outside the
  rows. It reuses `NoteGrid::keyAtY`, which maps a row index back exactly (`y + scrollY` divided by
  the row height), so the walk's rows and the pixels agree.
- Per-cell dedupe (`appendPaintCell`, `:54`) and the release call site
  (`src/gui/noteeditor/NoteCanvasInput.cpp:241,262`) are unchanged from round 1. No header change:
  `include/NoteCanvas.h:182` already declares `paintAlong`, and no target needed a header rebuild.
- No new control commands, no new source files, no audio-thread code.

Regression: `tests/src/gui/NoteEditorTest.cpp:508` `aSparseDiagonalPaintStrokePaintsEveryCellItCrosses`
- `drag(point(300, 66), point(402, 61))`: a press, two moves and a release along that diagonal. It
  asserts notes at (312, 66) - the cell Luna named, which no event lands in - at (312, 65), (324, 65),
  (324, 64), (336, 64), (372, 62) and (396, 61), 16 notes in total (14 painted + the clip's 2), and 2
  after one undo. The three earlier paint regressions stay: horizontal dense (`:448`), narrow zoom
  through the release (`:466`), sparse vertical in one column (`:489`).

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-303-port.r1/logs`.
Builds run from the worktree; `ctest` runs from `build/tests`.

| Step | Command | Exit |
|---|---|---:|
| Build with the new test, old product code | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r1 cmake --build build --target zene NoteEditorTest -j8 > LOGS/build-10-red-test.log 2>&1; echo EXIT=$?` | 0 |
| RED - new test on round-2 product code | `ctest -R '^NoteEditorTest$' -V > LOGS/test-10-red.log 2>&1; echo EXIT=$?` | **8** |
| Build, first version of the DDA | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r1 cmake --build build --target zene NoteEditorTest -j8 > LOGS/build-11-green.log 2>&1; echo EXIT=$?` | 0 |
| Test, first version of the DDA (see "The mistake I made first") | `ctest -R '^NoteEditorTest$' -V > LOGS/test-11-green.log 2>&1; echo EXIT=$?` | **8** |
| Build, corrected DDA | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r1 cmake --build build --target zene NoteEditorTest -j8 > LOGS/build-12-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN | `ctest -R '^NoteEditorTest$' -V > LOGS/test-12-green.log 2>&1; echo EXIT=$?` | 0 |
| Save the product patch and reverse it | `git diff -- src/gui/noteeditor/NoteCanvasGestures.cpp > LOGS/fix-r1.patch && git apply -R LOGS/fix-r1.patch; echo EXIT=$?` | 0 |
| Build without the product change | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r1 cmake --build build --target zene NoteEditorTest -j8 > LOGS/build-13-reverted.log 2>&1; echo EXIT=$?` | 0 |
| RED without the product change | `ctest -R '^NoteEditorTest$' -V > LOGS/test-13-reverted-red.log 2>&1; echo EXIT=$?` | **8** |
| Re-apply the product patch | `git apply LOGS/fix-r1.patch; echo EXIT=$?` | 0 |
| Build with the change again | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r1 cmake --build build --target zene NoteEditorTest -j8 > LOGS/build-14-reapplied.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after re-apply | `ctest -R '^NoteEditorTest$' -V > LOGS/test-14-reapplied-green.log 2>&1; echo EXIT=$?` | 0 |
| Build the family's other target | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-303r1 cmake --build build --target NoteEditorFlTest -j8 > LOGS/build-15-family.log 2>&1; echo EXIT=$?` | 0 |
| Family run | `ctest -R "NoteEditor" -V > LOGS/test-15-family.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-10.log 2>&1; echo EXIT=$?` | **1** |
| File-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-filelength-10.log 2>&1; echo EXIT=$?` | **1** |
| Upstream-declaration gate, after registering the report file | `bash tests/no-upstream-regression-gate.sh > LOGS/gate-upstream-10.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate, re-run after the last edit | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-11-final.log 2>&1; echo EXIT=$?` | **1** |
| File-length gate, re-run after the last edit | `bash tests/file-length-gate.sh --check > LOGS/gate-filelength-11-final.log 2>&1; echo EXIT=$?` | **1** |
| Whitespace check | `git diff --check` | 0 |

What the failures say:

- RED (`test-10-red.log`, `test-13-reverted-red.log`): `FAIL! :
  NoteEditorTest::aSparseDiagonalPaintStrokePaintsEveryCellThePathCrosses() 'noteAt(m_clip, cell.first,
  cell.second) != nullptr' returned FALSE. (no painted note at 312, key 66)` at
  `NoteEditorTest.cpp(520)`; `Totals: 35 passed, 1 failed`. This is Luna's assertion, reproduced
  twice: once before any product change, once with the product change reverse-applied.
- GREEN (`test-12-green.log`, `test-14-reapplied-green.log`, `test-15-family.log`): `Totals: 36
  passed, 0 failed`; the family run is `100% tests passed out of 3`.
- Family count: `ctest -R "NoteEditor"` registers 3 tests - `NoteEditorTest` (36 QTest functions),
  `NoteEditorFlTest` (16 QTest functions) and `ControlNoteEditor` (python) - and all 3 passed.

### The mistake I made first

`test-11-green.log` (exit 8) is kept because it is the honest record of my first version of the DDA:
after stepping a cell or a row it recomputed the next boundary from the *just crossed* one, so the
parameter never moved past the current boundary and the walk skipped every pitch crossing between two
columns (`Totals: 33 passed, 3 failed`: the diagonal, and one extra painted note in each of the two
horizontal tests). The fixed lines are `:129-130`, which step to `cell + cellStep` / `(row + rowStep)`.

## Gates

Both gates exit 1, and they exit 1 on the untouched base tree of this branch too, with the same
findings - they are pre-existing, not caused by this change.

| Gate | With the change | Base tree (`b02a0ff2b`) |
|---|---:|---:|
| `bash tests/complexity-gate.sh --check` | 1 | 1 (`runs/new-p9-303-port.r2/logs/gate-complexity-base.log`) |
| `bash tests/file-length-gate.sh --check` | 1 | 1 (`runs/new-p9-303-port.r2/logs/gate-filelength-base.log`) |

- Complexity: 6 `REGRESSION` lines, in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`,
  `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp` and
  `ControlCommandsShortcuts.cpp`. `diff` of the sorted `REGRESSION` lines against the base log exits
  0 (identical). Neither `paintAlong`, `boundaryParam` nor `keyOfRow` is listed.
- File length: 14 `REGRESSION` lines, identical to the base log (`diff` exits 0). Neither
  `src/gui/noteeditor/NoteCanvasGestures.cpp` (273 lines) nor `tests/src/gui/NoteEditorTest.cpp` is
  listed.
- I did not edit the gate scripts, their baselines, or any file they list. Clearing those 20 findings
  is outside this bug and needs its own change or a deliberate re-anchor by the owner.

## Files registered

- `tests/fork-sources.txt` - this report (`docs/qwen-fixes-2026-10/cluster-new-p9-303-port.md`) is a
  product-new file that the earlier rounds never registered; it is registered now, with a comment
  naming the cluster. The file still ends with a newline.
- `tests/upstream-modifications.txt` - unchanged. The only inherited file this change touches,
  `tests/src/gui/NoteEditorTest.cpp`, is already declared there; the product files
  (`src/gui/noteeditor/NoteCanvasGestures.cpp`, `include/NoteCanvas.h`,
  `src/gui/noteeditor/NoteCanvasInput.cpp`) are fork-new and already listed in
  `tests/fork-sources.txt`.

## Deliberate difference from the accepted commit `31076ea56`

`31076ea56` (`git show 31076ea56`) made the paint dedupe key-agnostic, so a stroke paints one note
per column rather than one per cell. This port keeps the per-(key, start-tick) dedupe: a
key-agnostic dedupe would collapse the vertical-stroke case that round 2's regression covers, which
is a behaviour change outside this bug. It is the owner's decision whether to take it.

## Known cost, not changed

The walk appends every cell it crosses, and `appendPaintCell` dedupes with a linear scan, so a single
event that sweeps a very long distance at narrow zoom with the grid off is quadratic in the number of
cells crossed. That is the same cost the accepted commit has, and it is not part of this bug; the
regressions here cross at most 17 cells. I did not add a cap, because a cap would silently drop
cells the fix is meant to paint.

## Files committed

- `src/gui/noteeditor/NoteCanvasGestures.cpp`
- `tests/src/gui/NoteEditorTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-303-port.md`