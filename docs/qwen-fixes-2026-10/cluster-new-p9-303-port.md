# Cluster new-p9-303-port

Verdict: **FIXED** (round 2, after Luna's REWORK of round 1). The gates are red, but they are red on the base tree too; see "Gates".

Finding: fast paint strokes skip the grid cells between mouse events.

## Round 2: what Luna's REWORK asked for

Luna (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/qa/reviews/new-p9-303-port.r1.md`): `paintAlong` walked time columns only. When `fromCell == toCell` its loop ran zero times, so a sparse vertical stroke within one time column painted only its two endpoint rows. The rework asked for every crossed grid cell in both directions, a deterministic vertical regression that fails without the change and passes with it, and a rebuild and rerun.

## The change

- `src/gui/noteeditor/NoteCanvasGestures.cpp:86-116` (`NoteCanvas::paintAlong`):
  - `:91-105` - the time-column walk, unchanged from round 1: one cell per grid column between `from` and `to`, each on the row the path has at the column's boundary.
  - `:106-115` - new pitch walk: for each pixel row strictly between `from.y()` and `to.y()`, the cell is the column the path is in at that row (`tick = fromTick + (toTick - fromTick) * i / rowSpan`), and the row is `keyAtY(from.y() + rowDir * i)`. Both go through `appendPaintCell`, so each cell is still painted once per stroke.
  - `:116` - `extendPaint(to)` still closes the last segment at release.
  - Why per pixel row: `NoteGrid::keyAtY` is integer division of an integer `y` (`src/gui/noteeditor/NoteGrid.cpp:221-226`), so a row boundary is always at an integer pixel. Sampling every integer `y` strictly between the endpoints hits every row the path enters, and the endpoints are covered by `extendPaint(from)` / `extendPaint(to)`.
- Diagonal paths run through both walks, so they are covered by construction. Only the vertical case has a regression test, because that is the regression Luna specified.
- No header change (`include/NoteCanvas.h:182` declaration and comment are unchanged from round 1). No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged. No control commands added. Paint runs on the GUI thread, not an audio path.
- Regression: `tests/src/gui/NoteEditorTest.cpp:489` `aSparseVerticalPaintStrokeInOneColumnPaintsEveryKeyRowItCrosses`. Press on key 60 at tick 300, one sparse move to key 66, release. It expects a note on each of keys 60-66 at tick 300, 9 notes in total (2 existing + 7), and 2 after one undo.

## Evidence

- RED (the round-1 product code, with the new test): `test-1-red.log` shows `FAIL! : NoteEditorTest::aSparseVerticalPaintStrokeInOneColumnPaintsEveryKeyRowItCrosses() 'noteAt(m_clip, 300, key) != nullptr' returned FALSE. (no painted note on key 61)` at `NoteEditorTest.cpp(501)`. `Totals: 34 passed, 1 failed`.
- GREEN: `test-3-green-verbose.log` and `test-5-reapplied-green.log`: `Totals: 35 passed, 0 failed`.
- Reverse-apply proof: with only the product hunk reversed (`test-4-reverted-red.log`), the same assertion fails again at key 61, `Totals: 34 passed, 1 failed`.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-303-port.r2/logs/`. Commands are run from the worktree, and ctest from `build/tests`.

| Step | Command | Exit |
|---|---|---:|
| Build, first try (ccache temp dir under `/run/user/1000` denied in this sandbox) | `cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-1-red.log 2>&1; echo EXIT=$?` | 2 |
| Build with ccache temp under /tmp (no code change) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9 cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-2-red.log 2>&1; echo EXIT=$?` | 0 |
| RED, round-1 product code | `ctest -R '^NoteEditorTest$' --output-on-failure > LOGS/test-1-red.log 2>&1; echo EXIT=$?` | **8** |
| Build with fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9 cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-3-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN | `ctest -R '^NoteEditorTest$' --output-on-failure > LOGS/test-2-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN, verbose for QTest totals | `ctest -R '^NoteEditorTest$' -V > LOGS/test-3-green-verbose.log 2>&1; echo EXIT=$?` | 0 |
| Save product patch and reverse it | `git diff -- src/gui/noteeditor/NoteCanvasGestures.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| Build without fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9 cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-4-reverted.log 2>&1; echo EXIT=$?` | 0 |
| RED after reversal | `ctest -R '^NoteEditorTest$' --output-on-failure > LOGS/test-4-reverted-red.log 2>&1; echo EXIT=$?` | **8** |
| Re-apply product patch | `git apply LOGS/fix.patch` | 0 |
| Build with fix again | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9 cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-5-reapplied.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after re-apply | `ctest -R '^NoteEditorTest$' -V > LOGS/test-5-reapplied-green.log 2>&1; echo EXIT=$?` | 0 |
| Family: build the family's other targets | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9 cmake --build build --target NoteEditorFlTest ControlNoteEditor -j2 > LOGS/build-6-family.log 2>&1; echo EXIT=$?` | 2 |
| Family run | `ctest -R "NoteEditor" -V > LOGS/test-6-family.log 2>&1; echo EXIT=$?` | 0 |
| Whitespace check | `git diff --check` | 0 |

Exit 2 on the family build is because `ControlNoteEditor` is a ctest entry that runs `tests/control-note-editor.py`, not a build target, so `make` stopped with `No rule to make target 'ControlNoteEditor'`. `build-6-family.log` shows `NoteEditorFlTest` linked fresh before that error. The family run passed.

Test counts: `ctest -R '^NoteEditorTest$'` registers 1 test that executes 35 QTest functions (`Totals: 35 passed, 0 failed` in `test-3-green-verbose.log`). The family run passed 3 of 3: `NoteEditorTest` 35/35, `NoteEditorFlTest` 16/16, `ControlNoteEditor` (python) passed.

## Gates

Both gates exit 1 with the change. They also exit 1 on the base tree, and the REGRESSION sets are identical.

| Step | Command | Exit |
|---|---|---:|
| Complexity gate, with the change | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-1.log 2>&1; echo EXIT=$?` | **1** |
| File-length gate, with the change | `bash tests/file-length-gate.sh --check > LOGS/gate-filelength-1.log 2>&1; echo EXIT=$?` | **1** |
| Save both changed files and reverse them to the base tree | `git diff -- src/gui/noteeditor/NoteCanvasGestures.cpp tests/src/gui/NoteEditorTest.cpp > LOGS/all-changes.patch && git apply -R LOGS/all-changes.patch` | 0 |
| Complexity gate, base tree | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-base.log 2>&1; echo EXIT=$?` | **1** |
| File-length gate, base tree | `bash tests/file-length-gate.sh --check > LOGS/gate-filelength-base.log 2>&1; echo EXIT=$?` | **1** |
| Restore the changes | `git apply LOGS/all-changes.patch` | 0 |

- Complexity: 6 `REGRESSION` lines in both logs, identical (`diff` of the REGRESSION lines is empty). They are in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp` and `ControlCommandsShortcuts.cpp`. None is `paintAlong` or any function this change touches, and `paintAlong` does not appear in the over-CCN-10 list.
- File length: 14 `REGRESSION` lines in both logs, identical. None is `NoteCanvasGestures.cpp` or `NoteEditorTest.cpp`.

I did not edit the gate scripts, their baselines, or any file they list. Clearing those 20 findings is outside this bug and needs its own change or a deliberate re-anchor by the owner.

## Carried over from round 1 (not re-checked in round 2)

Deliberate difference from the accepted commit `31076ea56` (`Fix narrow-zoom paint strokes (cluster new-p9-303)`, which exists in this repository but is not an ancestor of `HEAD`): round 1 did not port its key-agnostic `extendPaint` dedupe. The current dedupe is per key and per start cell, so a slow vertical stroke paints one note per row it crosses. A key-agnostic dedupe would collapse that to one note per column. That is a behaviour change outside this bug, and no test covers it. It is the owner's decision.

## Files committed

- `src/gui/noteeditor/NoteCanvasGestures.cpp`
- `tests/src/gui/NoteEditorTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-303-port.md`
