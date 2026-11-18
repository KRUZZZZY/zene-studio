FIXED

# Cluster new-p10-302: a non-finite or oversized position-line zoom reaches a float-to-int conversion

## Decision

REAL, latent, now FIXED. `PositionLine::zoomChange` converted `std::max(8.0 * zoom, 1.0)` straight to `int` through
`QWidget::resize(int, int)`. For a NaN, an infinite, or a finite value past the `int` range, that conversion is undefined
behaviour. The slot is public, so the defect is real in the code.

It is not reachable from any caller in this tree today. The verdict is FIXED as hardening; the finding's "can corrupt the
widget size" is not shown in the product:

- `src/gui/editors/SongEditor.cpp:138` passes `pixelsPerBar() / float(DEFAULT_PIXELS_PER_BAR)`. `pixelsPerBar()` is an int
  from `calculatePixelsPerBar()` (`SongEditor.cpp:751`), which is `exp2` of a slider value in `[0, ZOOM_STEPS]` over
  `[MIN_PIXELS_PER_BAR, MAX_PIXELS_PER_BAR] = [4, 400]` (`SongEditor.cpp:81-82`). So the zoom is always in `[0.03, 3.13]`.
- `src/gui/editors/PianoRoll.cpp:5083` passes `m_zoomLevels[...]`, a constant table `{0.125 ... 8.0}` (`PianoRoll.cpp:155-156`).

## The fix

`src/gui/editors/PositionLine.cpp:103-112`:

- A non-finite zoom returns at once (`:106`, the same guard style as `src/gui/noteeditor/NoteGrid.cpp:72`). The line keeps its
  width and place.
- The width is clamped to `[1, QWIDGETSIZE_MAX]` as a double (`:111`) before the one conversion to `int` (`:112`).

For finite zooms the width is computed exactly as before. The clamp changes nothing below `QWIDGETSIZE_MAX / 8` (about 2.1
million zoom), and no caller gets near it. The `<algorithm>` and `<cmath>` includes were added for `std::clamp` and
`std::isfinite`. The function is one `if`, so its complexity stays far under the CCN limit.

## Regression test

Added `positionLineZoomRefusesNonFiniteAndClampsOversized` to `tests/src/gui/ArrangerTimelineTest.cpp:1082`. It uses the
editor's own `positionLine()` accessor (`include/SongEditor.h:78`) on the real, shown song editor. Checks:

1. NaN, +inf, -inf: width stays 8 and `x` stays 40 (the zoom is refused).
2. 3.0e8 and `FLT_MAX`: width becomes `QWIDGETSIZE_MAX` (clamped, no undefined conversion).
3. At the end it restores the line with the editor's own zoom path (`pixelsPerBar() / 128`, as `SongEditor.cpp:138`).

Finite-zoom behaviour is covered by the existing ArrangerTimelineTest slots and by the arranger, piano-roll and song-editor
family (see the table below).

Why this file and not a new one: a new test source needs a CMake reconfigure. This lane's build tree is configured and the
`cmake` shim refuses configuring (`bunny-guard: configuring is the parent's job`), so a new test target could not be built
here. The procedure asks for the existing area test file when one exists. `ArrangerTimelineTest` already drives the
song-editor zoom into this position line. Nothing in CMake changed.

Determinism: no clock, sleep or thread. The inputs are fixed literals and the starting state is set in the test.

## Evidence (commands run from the worktree or `build/tests`; logs in the LOG FOLDER)

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target ArrangerTimelineTest -j4` | 2 | `build-2-red.log` (ccache `Permission denied` on `qrc_lmms.cpp.o`; no compiler error) |
| Build with ccache off | `CCACHE_DISABLE=1 cmake --build build --target ArrangerTimelineTest -j4` | 0 | `build-3-red.log` |
| **RED** (test only, no product fix) | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` | **8** | `test-1-red.log`: `FAIL! ... positionLineZoomRefusesNonFiniteAndClampsOversized()`, `Actual (line->width()): 0`, `Expected (8): 8`, `ArrangerTimelineTest.cpp(1099)` |
| Build after fix | `CCACHE_DISABLE=1 cmake --build build --target zene -j4`; same with `--target ArrangerTimelineTest` | 0; 0 | `build-4-green-zene.log`, `build-5-green-test.log` |
| **GREEN** | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` | **0** | `test-2-green.log` |
| GREEN, verbose | `ctest -R '^ArrangerTimelineTest$' -V` | 0 | `test-3-green-verbose.log`: `PASS : ...positionLineZoomRefusesNonFiniteAndClampsOversized()`; `Totals: 39 passed, 0 failed` |
| **4b**: patch saved | `git diff -- src/gui/editors/PositionLine.cpp > .../logs/fix.patch` | 0 | |
| 4b: fix reverted | `git apply -R .../logs/fix.patch` | 0 | |
| 4b: rebuild without fix | `CCACHE_DISABLE=1 cmake --build build --target zene` / `--target ArrangerTimelineTest` | 0 / 0 | `build-6-4b-nofix-zene.log`, `build-7-4b-nofix-test.log` |
| **4b: test without fix** | `cd build/tests && ctest -R '^ArrangerTimelineTest$' -V` | **8** | `test-4-4b-nofix.log`: `FAIL!`, `Actual 0`, `Totals: 38 passed, 1 failed` |
| 4b: fix reapplied | `git apply .../logs/fix.patch` | 0 | |
| 4b: rebuild with fix | `CCACHE_DISABLE=1 cmake --build build --target zene` / `--target ArrangerTimelineTest` | 0 / 0 | `build-8-4b-fix-zene.log`, `build-9-4b-fix-test.log` |
| **4b: test with fix** | `cd build/tests && ctest -R '^ArrangerTimelineTest$' -V` | **0** | `test-5-4b-fix.log`: `PASS`, `Totals: 39 passed, 0 failed` |
| Family, first run | `cd build/tests && ctest -R 'Arranger\|PianoRoll\|SongEditor\|TimeLine' -j2 --output-on-failure` | 8 | `test-6-family.log`: 6 of 7 `Not Run` (binaries not built in this lane) |
| Family: build the 6 | `CCACHE_DISABLE=1 cmake --build build --target ArrangerTakePunchTest --target ArrangerFileDropTest --target ArrangerKeysNavigatorTest --target ArrangerOverlaysVideoLaneTest --target PianoRollBenchmarkTest --target PianoRollRecordTest -j4` | 0 | `build-10-family.log` |
| **Family: affected tests** | `cd build/tests && ctest -R 'Arranger\|PianoRoll\|SongEditor\|TimeLine' -j2 --output-on-failure` | **0** | `test-7-family.log`: **7 of 7 executed and passed** (ArrangerTimelineTest, ArrangerTakePunchTest, ArrangerFileDropTest, ArrangerKeysNavigatorTest, ArrangerOverlaysVideoLaneTest, PianoRollBenchmarkTest, PianoRollRecordTest) |

Gates:

| Gate | Command | EXIT | Note |
| --- | --- | --- | --- |
| `feature_check.py` | `python3 .../qwen/feature_check.py <worktree>` | **1** | Did not run: `PermissionError` creating `<worktree-parent>/zene-qwen-new-p10-302-feature-check` (`gate-1-feature-check.log`). That directory is outside this lane's write area. I did not edit the shared script. |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| complexity | `bash tests/complexity-gate.sh --check` | **1** | `gate-complexity.log`: 6 `REGRESSION` lines, none in a file I changed |
| file-length | `bash tests/file-length-gate.sh --check` | **1** | `gate-file-length.log`: 23 lines; one is in a file I changed, see Self-check |

## Files committed

- `src/gui/editors/PositionLine.cpp` (fix, +9/-1)
- `tests/src/gui/ArrangerTimelineTest.cpp` (regression slot, +36)
- `tests/upstream-modifications.txt` (one entry for the inherited product file, with reason)
- `docs/qwen-fixes-2026-10/cluster-new-p10-302.md` (this report)

## Self-check

- **Regression fails without the fix, passes with it** (ask 1): RED `ctest` exit 8, `Actual 0`, `Expected 8` (`test-1-red.log`
  and `test-4-4b-nofix.log`). GREEN exit 0 (`test-2-green.log`, `test-5-4b-fix.log`). Procedure 4b commands and exit codes
  are in the table above.
- **Deterministic** (ask 2): no clock, sleep or thread. Inputs are literal floats (NaN, ±inf, 3.0e8, `FLT_MAX`, 1.0), and
  the starting state is set in the test (zoom 1.0, `move(40, 0)`).
- **Fix at the cause, callers checked** (ask 3): the only conversion in `zoomChange` is the one fixed. Both callers
  (`SongEditor.cpp:138`, `PianoRoll.cpp:5083`) are finite and bounded, see Decision. The `move()` after the resize is
  unchanged.
- **No collateral damage** (ask 4): finite zooms give the same width as before. The clamp is above any width a caller
  produces. Nothing is saved, loaded, or exposed as a command. No new control command was added. The upstream manifest
  entry states the change.
- **Verdict evidence** (ask 5): the verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so no "already fixed" evidence
  is needed. The "not reachable today" point is backed by `SongEditor.cpp:81-82,751,138` and `PianoRoll.cpp:155-156,5083`.
- **feature_check.py did not run** (gates): it fails from this sandbox with `PermissionError` on the parent directory.
  The same six simple gates and the two ratchets were run directly from the worktree (table above). Both ratchets exit 1.
  Their output lists regressions in other files. In changed files, the complexity log names nothing from `PositionLine`
  or `ArrangerTimelineTest`.
- **File-length item in a changed file**: `tests/src/gui/ArrangerTimelineTest.cpp` is listed as "new file over 500 lines"
  (974 by the gate's count). It is not a new file: it has been in the tree since `b9a463ad42` (1208 physical lines at HEAD,
  before my edit). My edit adds 36 lines and creates no file. I could not check the gate's baseline entry for it, so the
  parent should judge whether this is a real violation for this change.
- **Build workaround**: ccache's temp directory refused writes (`ccache: error: failed to create temporary file ...
  Permission denied`), so the builds that matter ran with `CCACHE_DISABLE=1`. This changes only the compiler launcher, not
  the code.
- **Not done**: no new test target (see Regression test). The `tests/CMakeLists.txt` change was reverted and no file was
  added for a new test, so the CMake reconfigure stays with the parent.
