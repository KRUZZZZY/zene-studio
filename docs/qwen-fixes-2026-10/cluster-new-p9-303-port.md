# Cluster new-p9-303-port

Verdict: **FIXED** (port of the accepted `bugs/qwen-new-p9-303` fix onto `050/m0-train-5`; the newest line only had a partial fix)

Finding: fast paint strokes skip the grid cells between mouse events.

## Evidence before the fix

- The newest line already had a pixel-stepped `NoteCanvas::paintAlong` (from `2c9a61050`), but it steps 2 px at a time, so a cell narrower than 2 px can lie between two samples and is skipped. At 0.02 px/tick a 12-tick cell is 0.24 px wide.
- `NoteCanvas::mouseReleaseEvent` (`src/gui/noteeditor/NoteCanvasInput.cpp:257`) did not paint the segment from the last move to the release point, so a stroke whose only move is sparse lost its final segment.
- The regression `aSparsePaintStrokeAtNarrowZoomPaintsEveryCellThroughTheRelease` (`tests/src/gui/NoteEditorTest.cpp:466`) fails on the unfixed code at tick 312 (`no painted note at 312`, `NoteEditorTest.cpp:482`).

## The change

- `src/gui/noteeditor/NoteCanvasGestures.cpp`
  - `appendPaintCell` (`:54`): the once-per-cell rule as a free helper, shared by `extendPaint` (`:78`) and `paintAlong`.
  - `paintAlong` (`:86`): walks the cells between `from` and `to` in tick space, one step per grid cell (`:98-105`). Each crossed cell is painted on the row where the path enters it; `from` and `to` are painted as before (`:96`, `:106`).
- `src/gui/noteeditor/NoteCanvasInput.cpp:262`: release calls `paintAlong(from, m_currentPos)` for `Drag::Paint` before `commitDrag`.
- `include/NoteCanvas.h`: comment on `paintAlong` updated to match (comment only).
- `tests/src/gui/NoteEditorTest.cpp:466`: new regression test. No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged.

Deliberate difference from the accepted commit `31076ea56`: that commit also made `extendPaint`'s dedup key-agnostic (one note per column per stroke). I did not port that. The current dedup is per key and per start, so a slow vertical stroke paints one note per row it crosses; the key-agnostic version would collapse it to one note per column. That is a behaviour change outside this bug, and no test covers it. If the owner wants one note per column, that is a separate decision.

Other notes:
- A fast move across many cells with the grid off (1-tick cells) at a very narrow zoom can create many notes in one event. The dedup is linear per cell, so the cost is quadratic in the cells crossed. The accepted commit has the same property. Not measured.
- The first build attempt failed with `EXIT=2` because ccache's temp directory under `/run/user/1000` is denied in this sandbox. Re-running with `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-303` fixed it (`build-1-red.log` vs `build-2-red.log`). No code change.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-303-port.r1/logs/`.

| Step | Exact command (run from the worktree / `build/tests`) | Exit |
|---|---|---:|
| Build, first try (ccache temp denied) | `cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-1-red.log 2>&1; echo EXIT=$?` | 2 |
| Build for RED | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-303 cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-2-red.log 2>&1; echo EXIT=$?` | 0 |
| RED (fix not applied) | `ctest -R '^NoteEditorTest$' --output-on-failure > LOGS/test-1-red.log 2>&1; echo EXIT=$?` | **8** |
| Build with fix | `CCACHE_TEMPDIR=... cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-3-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN | `ctest -R '^NoteEditorTest$' --output-on-failure > LOGS/test-2-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN, verbose count | `ctest -R '^NoteEditorTest$' -V > LOGS/test-3-green-verbose.log 2>&1; echo EXIT=$?` | 0 |
| Save product patch | `git diff -- include/NoteCanvas.h src/gui/noteeditor/NoteCanvasGestures.cpp src/gui/noteeditor/NoteCanvasInput.cpp > LOGS/fix.patch; echo EXIT=$?` | 0 |
| Reverse product patch | `git apply -R LOGS/fix.patch; echo EXIT=$?` | 0 |
| Build without fix | `CCACHE_TEMPDIR=... cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-4-unfixed.log 2>&1; echo EXIT=$?` | 0 |
| RED after reversal | `ctest -R '^NoteEditorTest$' --output-on-failure > LOGS/test-4-unfixed.log 2>&1; echo EXIT=$?` | **8** (`no painted note at 312`) |
| Re-apply product patch | `git apply LOGS/fix.patch; echo EXIT=$?` | 0 |
| Build with fix again | `CCACHE_TEMPDIR=... cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-5-refixed.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after re-apply | `ctest -R '^NoteEditorTest$' -V > LOGS/test-5-refixed-verbose.log 2>&1; echo EXIT=$?` | 0 |
| Header comment shortened, rebuild | `CCACHE_TEMPDIR=... cmake --build build --target zene NoteEditorTest -j2 > LOGS/build-6-final.log 2>&1; echo EXIT=$?` | 0 |
| Final GREEN | `ctest -R '^NoteEditorTest$' -V > LOGS/test-6-final-verbose.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate (worktree) | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-2.log 2>&1; echo EXIT=$?` | **1** |
| File-length gate (worktree) | `bash tests/file-length-gate.sh --check > LOGS/gate-filelength-2.log 2>&1; echo EXIT=$?` | **1** |
| Complexity gate (clean `git archive HEAD` export) | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-base.log 2>&1; echo EXIT=$?` | **1** |
| File-length gate (clean `git archive HEAD` export) | `bash tests/file-length-gate.sh --check > LOGS/gate-filelength-base.log 2>&1; echo EXIT=$?` | **1** |

Test counts: `ctest -R '^NoteEditorTest$'` registers 1 test; it executes 34 QTest functions. Final run: `Totals: 34 passed, 0 failed, 0 skipped` (`test-6-final-verbose.log`). RED run: `Totals: 33 passed, 1 failed`.

The RED/GREEN proof used `fix.patch` before the final header-comment edit. That edit changed one comment line only; the final build and GREEN run (`build-6-final.log`, `test-6-final-verbose.log`) were run after it.

## Gates: failing before and after this change

Both gates exit 1, but not because of this change. On a clean `git archive HEAD` export (the base commit, with no edits) they exit 1 with the same findings:

- Complexity: 6 `REGRESSION` lines, identical sets with and without the change. They are in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`. None of the touched functions (`paintAlong`, `appendPaintCell`, `extendPaint`, `mouseReleaseEvent`) is listed.
- File length: 14 `REGRESSION` lines, identical sets with and without the change. None of the touched files is listed. `NoteEditorTest.cpp` (725 physical lines, grew by 23) is not listed by the gate. I did not measure its code-line count myself.

I did not re-anchor or edit either baseline, as the rules require. Clearing these 20 findings is outside this bug. They need a separate cleanup or a deliberate re-anchor by the owner.

## Files committed

- `src/gui/noteeditor/NoteCanvasGestures.cpp`
- `src/gui/noteeditor/NoteCanvasInput.cpp`
- `include/NoteCanvas.h`
- `tests/src/gui/NoteEditorTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-303-port.md`

No new source or test files, so nothing to register in `tests/fork-sources.txt` or `tests/upstream-modifications.txt`. No control commands added.
