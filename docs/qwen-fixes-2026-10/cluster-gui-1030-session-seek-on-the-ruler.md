Verdict: FIXED

gui-1030-session-seek-on-the-ruler: FIXED

Finding: "The arranger bar ruler has no usable layout height" (Luna, 050/m0-train-24, `src/gui/editors/SongEditor.cpp:161`; the line is now `:165`). Clicking the numbered ruler near bar 3 did not seek, because that ruler was 0 px tall.

Evidence (file:line, current tree at base `2a7fd9f9c`):

- The numbered bar ruler is `TimeLineWidget`. Its constructor sets no size (`src/gui/editors/TimeLineWidget.cpp:61-73`), and it sits in the song editor's `QVBoxLayout` with no size hint of its own. Before the fix the layout gave it nothing: the RED run printed `the bar ruler is 0 px tall, under 24` (`test-2-red.log`, `test-5-unfixed-red.log`).
- The fix is a floor on that ruler, in the song editor only: `BAR_RULER_MIN_HEIGHT = 24` at `src/gui/editors/SongEditor.cpp:86`, and `m_timeLine->setMinimumHeight( BAR_RULER_MIN_HEIGHT )` at `src/gui/editors/SongEditor.cpp:164`, before the ruler joins the layout at `:165`.
- The seek is correct once the ruler has a hit area: a press starts `Action::MovePositionMarker` (`TimeLineWidget.cpp:420`), and a move sets `m_timeline->setTicks` (`TimeLineWidget.cpp:448-449`).
- The lanes Luna clicked are not the numbered ruler. `src/gui/ArrangerRulerLanes.cpp:183` gives the lanes a fixed height, and `LaneHeights{16, 16, 20, 18}` is at `src/gui/ArrangerRulerLanes.cpp:64`. The lanes have a height; the numbered ruler does not.
- The numbered ruler appears once in the song editor's layout (the new test's `entries == 1` check passes). The two `insertWidget` calls (`SongEditor.cpp:132` and `:165`) do not duplicate it at runtime, so no change was made there.

Regression test: `tests/src/gui/ArrangerTimelineTest.cpp:411`, `theBarRulerHasAUsableHeightAndSeeksWhereItIsDragged`. The three suites named in the brief do not cover the ruler; ArrangerTimelineTest is the suite that already drives the arranger ruler lanes and real mouse input, so the regression goes there. It adds `#include <QVBoxLayout>` at `:53`. The test:

1. checks the ruler is visible and listed once in the song editor's `QVBoxLayout`;
2. checks `ruler->height() >= 24`;
3. clicks at bar 3 (`markerX(2 * Bar)`) and expects the play position within `Bar / 8` of `2 * Bar`;
4. presses at bar 3, moves to bar 5 with a synthetic `QMouseEvent` delivered by `sendEvent`, releases, and expects the play position near `4 * Bar`.

Registration: `src/gui/editors/SongEditor.cpp` is an inherited file, so it is declared in `tests/upstream-modifications.txt` (last line) with a reason. No new source files, so `tests/fork-sources.txt` and `tests/all-sources.txt` are unchanged.

Commands and exit codes (all logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-session-seek-on-the-ruler.r1/logs/`; a build with `CCACHE_TEMPDIR=$TMPDIR/ccache-gui1030` set in its environment, because ccache could not write its temp files under the default directory from this session):

| Step | Command | Result |
| --- | --- | --- |
| Baseline build, attempt 1 | `cmake --build build --target ArrangerTimelineTest -j4 > build-1-baseline.log 2>&1` | EXIT=2 (ccache: Permission denied on its temp dir; no code error) |
| Baseline build, attempt 2 | `CCACHE_TEMPDIR=$TMPDIR/ccache-gui1030 cmake --build build --target ArrangerTimelineTest -j4 > build-2-baseline.log 2>&1` | EXIT=0 |
| RED build, attempt 1 | `... --target ArrangerTimelineTest -j4 > build-3-red.log 2>&1` | EXIT=2: `int slots` is a Qt macro (`slots`); renamed to `entries` |
| RED build | `... --target ArrangerTimelineTest -j4 > build-4-red.log 2>&1` | EXIT=0 |
| RED test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ArrangerTimelineTest$' -V > test-2-red.log 2>&1` | EXIT=8. `FAIL! : ...theBarRulerHasAUsableHeightAndSeeksWhereItIsDragged() 'ruler->height() >= MinBarRulerHeight' returned FALSE. (the bar ruler is 0 px tall, under 24)`; Totals 39 passed, 1 failed |
| GREEN build | `cmake --build build --target zene ArrangerTimelineTest -j4 > build-5-green.log 2>&1` | EXIT=0 |
| GREEN test | `ctest -R '^ArrangerTimelineTest$' -V > test-4-green-verbose.log 2>&1` | EXIT=0. `PASS: ...theBarRulerHasAUsableHeightAndSeeksWhereItIsDragged()`; Totals 40 passed, 0 failed |
| Step 4b: patch | `git diff -- src/gui/editors/SongEditor.cpp > fix.patch` | EXIT=0 |
| Step 4b: revert | `git apply -R fix.patch` | EXIT=0 |
| Step 4b: rebuild unfixed | `cmake --build build --target zene ArrangerTimelineTest -j4 > build-6-unfixed.log 2>&1` | EXIT=0 |
| Step 4b: RED with fix reverted | `ctest -R '^ArrangerTimelineTest$' -V > test-5-unfixed-red.log 2>&1` | EXIT=8. `FAIL!` at `ruler->height() >= MinBarRulerHeight` (0 px tall); Totals 39 passed, 1 failed |
| Step 4b: re-apply | `git apply fix.patch` | EXIT=0 |
| Step 4b: rebuild fixed | `cmake --build build --target zene ArrangerTimelineTest -j4 > build-7-refixed.log 2>&1` | EXIT=0 |
| Step 4b: GREEN with fix | `ctest -R '^ArrangerTimelineTest$' -V > test-6-refixed-green.log 2>&1` | EXIT=0; Totals 40 passed, 0 failed |
| Family build | `cmake --build build --target VisibilitySetMenuTest TrackViewDeferredLifetimeTest TrackFolderMenuTest ArrangerTakePunchTest ArrangerOverlaysVideoLaneTest ArrangerTimelineTest -j4 > build-8-family.log 2>&1` | EXIT=0 |
| Family tests | `ctest -R '^(VisibilitySetMenuTest\|TrackViewDeferredLifetimeTest\|TrackFolderMenuTest\|ArrangerTakePunchTest\|ArrangerOverlaysVideoLaneTest\|ArrangerTimelineTest)$' -V > test-7-family.log 2>&1` | EXIT=0; 6/6 tests passed (#440, #450, #451, #456, #480, #491), 0 FAIL lines |
| Upstream registration gate | `bash tests/no-upstream-regression-gate.sh > upstream-gate-1.log 2>&1` | EXIT=0 |
| Feature gates, as written | `python3 .../qwen/feature_check.py <worktree>` | EXIT=1, before any check ran: `PermissionError` creating `<worktree>-feature-check` in the worktree's parent directory, which is outside the write jail. Its output was overwritten by the wrapper run below |
| Feature gates, same script via wrapper | `python3 -I <run folder>/gate_wrapper.py <worktree>` (imports the unmodified `feature_check.py`, runs its own `SIMPLE` and `RATCHET` lists against the same base, and only the log folder is changed to the run folder `logs/gates/`) | EXIT=0; fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all PASS (exit 0); complexity and file-length PASS ("no new item in your files"); RESULT: PASS (`gates-1.log`) |
| Completion check, before commit | `python3 .../qwen/submit_check.py <worktree> gui-1030-session-seek-on-the-ruler > submit-check-0-precommit.log 2>&1` | EXIT=1, expected before the commit and report exist (clean tree, commit, report, self-check). Its "gates" row also failed, for the write-jail reason above |

Files committed for this verdict: `src/gui/editors/SongEditor.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`, `tests/upstream-modifications.txt`, and this report. Commit message names cluster gui-1030-session-seek-on-the-ruler.

## Self-check

- **Working tree clean / commit exists / report present / self-check present:** satisfied by the commit that adds this file. The completion check is rerun after the commit; its result is in the final message.
- **Gates (feature_check.py) row:** the check's own `feature_check.py` call fails in this sandbox because the script writes its logs to `<worktree>/../<name>-feature-check`, outside the write jail (`PermissionError`, exit 1). The same gates, run by the wrapper, all pass (exit 0). This is an environment limit, not a code failure. The gate limits and baselines were not changed.
- **Ask 1: the regression FAILS without the fix and PASSES with it.** RED with the fix reverted: `test-5-unfixed-red.log`, EXIT=8, `0 px tall, under 24`. GREEN with the fix: `test-6-refixed-green.log`, EXIT=0, 40/40. The original RED on the pre-fix tree (`test-2-red.log`) matches.
- **Ask 2: deterministic.** No sleeps, no timers and no `QTRY_` waits in the new test. Input is delivered with `QTest::mouseClick` / `QTest::mousePress` and one synthetic `QMouseEvent` through `sendEvent`. The coordinates come from `markerX`, and the expected ticks are exact arithmetic on `Bar` (192 ticks). The tolerance `Bar / 8` covers the pixel-to-tick rounding at 40 px per bar (`TimeLineWidget.cpp:310-315`). The test was run three times on the fixed tree (`test-3-green.log`, `test-4-green-verbose.log`, `test-6-refixed-green.log`), and it failed the same way twice on the unfixed tree (`test-2-red.log`, `test-5-unfixed-red.log`).
- **Ask 3: fix at the cause, callers and siblings.** The cause is the zero-height numbered ruler in the song editor's layout. Every other `TimeLineWidget` host was checked by grep: `PatternEditor.cpp:62-67` (the pattern editor also inserts its ruler into a `QVBoxLayout` with no minimum, so it may show the same 0 px symptom; NOT built or run, so unverified), `PianoRoll.cpp:291` and `AutomationEditor.cpp:133` (their vertical geometry was not checked; they only set the ruler's width, `PianoRoll.cpp:4110` and `AutomationEditor.cpp:1560`). I left them unchanged because this finding is scoped to the arranger and the brief forbids behaviour changes outside the bug.
  - The position line is placed at `m_timeLine->height()` (`SongEditor.cpp:740`) and sized from it (`:748`). It was 0 and is now 24. The code already assumes the ruler's height, so the line now starts under the bar ruler. No test covers the position line; it was not checked visually.
  - The 24 px constant is not scaled for HiDPI. The ruler lanes' `LaneHeights` (`ArrangerRulerLanes.cpp:64`) are unscaled too, so this matches them. HiDPI was not checked.
- **Ask 4: no collateral damage.** The change is a layout floor on one widget. No project format, saved data or load path changes, and no new control command. The floor only raises the minimum, so a project that already loads keeps its layout otherwise. The six family suites (VisibilitySetMenuTest, TrackViewDeferredLifetimeTest, TrackFolderMenuTest, ArrangerTakePunchTest, ArrangerOverlaysVideoLaneTest, ArrangerTimelineTest) pass with it (`test-7-family.log`, 6/6). Nothing on the audio thread, undo stack or command path is touched.
- **Ask 5 (ALREADY-FIXED / FALSE-POSITIVE evidence):** not applicable; the verdict is FIXED.
- **Sibling commands (submit_check):** the diff touches no command files, so no sibling command list applies.
- **Not checked:** the full ctest suite (only the six-suite family and the gates), the pattern editor's ruler, HiDPI, and a visual check of the position line and the 24 px ruler.
