FIXED

# Cluster gui-1030-session-loop-a-range: the arranger bar ruler has no usable layout height

## Finding

Luna's patrol (050/m0-train-24): "The arranger bar ruler has no usable layout height". The Song Editor's ruler is
inserted into the arranger's vertical layout with no fixed or minimum height, so the layout gives it none and the
numbered ruler, where the loop range is set, cannot be seen or clicked.

## Verdict: FIXED

The cause is real on this branch and is fixed. The ruler's laid-out height was 0 px in the offscreen test window
before the fix (RED), and is at least 32 px after it (GREEN).

## Integration onto 050/m0-train-26 (2026-11-09, run r2)

Accepted patch `bugs/qwen-gui-1030-session-loop-a-range @ d3a8b868e7` (base `2a7fd9f9c7`) merged three-way onto
`050/m0-train-26 @ af010bc3cc` in branch `integration/gui-1030-session-loop-a-range-af010bc3cc`.

- **Conflict:** only `tests/upstream-modifications.txt` (both sides appended lines at the end). Resolved by keeping
  every line of the train's file unchanged and appending the accepted patch's one `src/gui/editors/SongEditor.cpp`
  line after them (file still ends with a newline).
- **`src/gui/editors/SongEditor.cpp`** merged cleanly: the staged hunk is byte-identical to the accepted one (same
  three lines). On the train it is at `SongEditor.cpp:164-166`, immediately before `insertWidget( 0, m_timeLine )` at
  line 167. The train's own changes to this file (the `AddTrackMenu`/`NeutralTrackView` includes and the
  `SongEditorWindow` toolbar actions near line 959) do not touch the ruler or its layout. `TimeLineWidget.cpp` still
  sets no height of its own (`rg -n 'setMinimumHeight|setFixedHeight' src/gui/editors/TimeLineWidget.cpp` finds
  nothing), so the cause and the fix are the same on the train.
- **`tests/src/gui/ArrangerTimelineTest.cpp`** merged cleanly; the regression `theRulerHasAUsableLayoutHeight` is
  unchanged.
- **No semantic difference from the accepted patch.** No new design was needed.

Failure paths and platform hazards checked: the ruler's 0 px layout height (the bug; RED/GREEN below); the other
tests that use the ruler's height (`ArrangerTakePunchTest`, `ArrangerOverlaysVideoLaneTest`, `VideoClipsGuiTest`, all
pass); HiDPI (the minimum goes through `scaledPixels`, the same as the assertion); the piano roll and other editors
that own a `TimeLineWidget` are not affected (the minimum is set only in `SongEditor`). Nothing is on the audio
thread, no undo step and no control command changes, and no saved data changes.

Integration evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1030-session-loop-a-range.r2/logs/`).
The build ran with `CCACHE_DISABLE=1` because ccache's temp directory
(`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not writable in this lane (`01-build-green.log`: ccache
"Permission denied", EXIT=2). Without ccache:

| Step | Command | EXIT |
| --- | --- | --- |
| Build (GREEN) | `CCACHE_DISABLE=1 cmake --build build --target zene ArrangerTimelineTest -j4` (`02-build-green.log`) | 0 |
| GREEN | `ctest -R '^ArrangerTimelineTest$' --output-on-failure` from `build/tests` (`03-test-green.log`), 1/1 passed; direct binary run `04-test-green-direct.log`: `PASS : ArrangerTimelineTest::theRulerHasAUsableLayoutHeight()`, `Totals: 45 passed, 0 failed` | 0 |
| Revert product fix | `git diff --cached HEAD -- src/gui/editors/SongEditor.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| Rebuild without fix | same build command (`05-build-red.log`) | 0 |
| RED | `ctest -R '^ArrangerTimelineTest$' --output-on-failure` (`06-test-red.log`): `FAIL! : ArrangerTimelineTest::theRulerHasAUsableLayoutHeight() 'ruler->height() >= scaledPixels(32)' returned FALSE. (the ruler has 0 px of layout height)`, `Totals: 44 passed, 1 failed` | 8 |
| Reapply fix | `git apply logs/fix.patch` | 0 |
| Rebuild with fix | same build command (`07-build-green2.log`) | 0 |
| Family | `ctest -R '^ArrangerTimelineTest$\|^VisibilitySetMenuTest$\|^HybridTrackGuiTest$\|^TrackViewDeferredLifetimeTest$'` after building the three other targets (`09-build-family.log` EXIT=0; `10-test-family.log`): 4/4 passed | 0 |
| Neighbours | `ctest -R '^VideoClipsGuiTest$\|^ArrangerTakePunchTest$\|^ArrangerOverlaysVideoLaneTest$'` (`11-build-siblings.log` EXIT=0; `12-test-siblings.log`): 3/3 passed | 0 |
| Gates | `feature_check.py` (`14-feature-check.log`): RESULT PASS, 8/8 rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length "no new item in your files") | 0 |

`feature_check.py` itself could not run as installed: it creates its log directory next to the worktree
(`<worktree>-feature-check`), which this lane may not write (`13-feature-check.log`: `PermissionError`, EXIT=1).
It was run from a copy (`logs/fc-tool/feature_check_logs_redirected.py`) that differs from the original in two lines
only: `ROOT` points at the original script's directory (so `FIX-BASE` and the cached base listings are the same
ones), and the log directory is `logs/feature-check/`. The gates, the base and the comparison are unchanged.

Files committed in the integration: `src/gui/editors/SongEditor.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`,
`tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-gui-1030-session-loop-a-range.md`.

The sections below are from the accepted patch's own run (line numbers there are the old base's).

## Evidence (file:line, this branch)

- `src/gui/editors/SongEditor.cpp:161-163` (after the fix): the arranger's ruler gets `setMinimumHeight(scaledPixels(32))`
  before it is inserted at index 0 of the column (`insertWidget( 0, m_timeLine )`, line 164).
- Before the fix, nothing sized the ruler: `src/gui/editors/TimeLineWidget.cpp` has no `setFixedHeight`,
  `setMinimumHeight`, `sizeHint` or `minimumSizeHint` (checked with `rg`). A plain `QWidget` has no minimum, so
  `QVBoxLayout` gave it 0 px next to the Expanding track scroll area.
- The bug was visible to the existing tests: `tests/src/gui/ArrangerTakePunchTest.cpp:181-184` resizes the ruler to
  32 px with the comment "The offscreen test window does not always give the ruler its laid-out height; a real one is
  32 pixels". The fix gives the ruler that height in the layout itself; that test's resize is left as it was.
- Why the minimum sits in `SongEditor.cpp` and not in `TimeLineWidget.cpp`: `PianoRoll.cpp:291`,
  `AutomationEditor.cpp:133` construct the same widget with `yoff` 0, are not in a layout, and only set its width
  (`PianoRoll.cpp:4110`, `AutomationEditor.cpp:1560`). A minimum in the shared constructor would resize those editors'
  rulers too. `PatternEditor.cpp:62-67` also inserts its ruler into a layout, without a height; it is a sibling with the
  same pattern and is NOT changed here (outside this finding; not verified).
- The loop-range gesture itself is not changed. The finding's "dragging the adjacent lanes creates no loop range"
  concerns the sections/markers lanes (`ArrangerRulerLanes`), which were not changed. This fix only restores the
  ruler's height, so the existing ruler gestures (`TimeLineWidget::mousePressEvent`, `src/gui/editors/TimeLineWidget.cpp:382`)
  have room to be hit. The end-to-end "set a loop, play past its end" behaviour was NOT re-driven in this run.

## Regression test

`tests/src/gui/ArrangerTimelineTest.cpp:407-416` `theRulerHasAUsableLayoutHeight()`: after the song editor is shown,
asserts the ruler is visible and `ruler->height() >= 32`, with no test-side resize. Family: `ArrangerTimelineTest`
(the file that already measures the ruler's lanes in `theRulerHasALaneEachUnderTheBarRuler`). It is not one of the
three listed files, because the arranger ruler's geometry is already tested in this file.

## Commands and exit codes

Environment note: the first build failed with `ccache: error: failed to create temporary file ... Permission denied`
(`build-1-red.log`, EXIT=2). The compile itself did not fail (the `.o` steps reached the ccache error). All later
builds were run with `CCACHE_DISABLE=1` (ccache off for that invocation only; no config changed).

| # | Command (run from the worktree unless noted) | Log | EXIT |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target zene ArrangerTimelineTest -j4` | build-1-red.log | 2 (ccache permission error, no compile error) |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target zene ArrangerTimelineTest -j4` | build-2-red.log | 0 |
| 3 | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` | test-1-red.log | **8 (RED)** |

RED evidence (test-1-red.log:32): `FAIL! : ArrangerTimelineTest::theRulerHasAUsableLayoutHeight() 'ruler->height() >= 32' returned FALSE. (the ruler has 0 px of layout height)`; `Totals: 39 passed, 1 failed`.

| # | Command | Log | EXIT |
| --- | --- | --- | --- |
| 4 | fix applied; `CCACHE_DISABLE=1 cmake --build build --target zene ArrangerTimelineTest -j4` | build-3-green.log | 0 |
| 5 | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` | test-2-green.log | **0 (GREEN)** |
| 6 | `cd build/tests && ./ArrangerTimelineTest theRulerHasAUsableLayoutHeight -v2` | test-3-green-single.log | 0 (`PASS : ...theRulerHasAUsableLayoutHeight()`, `Totals: 3 passed`) |
| 7 | `cd build/tests && ./ArrangerTimelineTest` (full family) | test-7-totals-green.log | 0 (`Totals: 40 passed, 0 failed`) |

### Step 4b: the test catches the bug

| # | Command | Log | EXIT |
| --- | --- | --- | --- |
| 8 | `git diff -- src/gui/editors/SongEditor.cpp > logs/fix.patch` | fix.patch | 0 |
| 9 | `git apply -R logs/fix.patch` (fix removed) | - | 0 |
| 10 | `CCACHE_DISABLE=1 cmake --build build --target zene ArrangerTimelineTest -j4` | build-4-revert.log | 0 |
| 11 | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` | test-4-revert.log | **8 (FAILS without the fix)**, `FAIL! ... (the ruler has 0 px of layout height)` |
| 12 | `git apply logs/fix.patch` (fix restored) | - | 0 |
| 13 | `CCACHE_DISABLE=1 cmake --build build --target zene ArrangerTimelineTest -j4` | build-5-reapply.log | 0 |
| 14 | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` | test-5-reapply.log | **0 (PASSES with the fix)** |

### Affected family and neighbours (with the fix)

| # | Command | Log | EXIT |
| --- | --- | --- | --- |
| 15 | `CCACHE_DISABLE=1 cmake --build build --target VisibilitySetMenuTest TrackViewDeferredLifetimeTest TrackFolderMenuTest ArrangerTakePunchTest -j4` | build-6-neighbours.log | 0 |
| 16 | `cd build/tests && ctest -R '^(ArrangerTimelineTest\|ArrangerTakePunchTest\|VisibilitySetMenuTest\|TrackViewDeferredLifetimeTest\|TrackFolderMenuTest)$' --output-on-failure` | test-6-family.log | 0 (`100% tests passed out of 5`) |

Tests executed: ArrangerTimelineTest 40 (39 existing + 1 new), all passed; neighbours 4 more test binaries, all passed.

### Gates

`feature_check.py` could not run from its normal location: it creates `<worktree>-feature-check/` next to the worktree
(`feature_check.py:70-71`), which is outside the write-jail (`gates-1.log`: `PermissionError`, EXIT=1). The same checks were
run by hand from the worktree, with the same commands as the script's `SIMPLE` and `RATCHET` lists
(`feature_check.py:22-29`), logs in the LOG FOLDER:

| Gate | Command | Log | EXIT |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | gate-fork-sources.log | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | gate-all-sources-reproduce.log | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | gate-no-upstream-regression.log | 0 |
| check-namespace | `tests/scripted/check-namespace` | gate-check-namespace.log | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | gate-no-tautology.log | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | gate-doc-figures.log | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | gate-complexity.log | **1** (see below) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | gate-file-length.log | **1** (see below) |

The two ratchets exit 1 in check mode with `FAIL: ... ratchet regressed (check mode: baseline not written)`. Their base
listing is computed in a checkout outside the write-jail, which I did not do. My analysis of their REGRESSION lines
(not a gate PASS):
- complexity: 7 items, all in files this branch does not touch (`WasmSandbox.cpp`, `XyPadPairTest.cpp`,
  `DawProjectReadTracks.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsMixerTools.cpp`,
  `ControlCommandsSessionRecordLand.cpp`, `VideoAudioExtract.cpp`). None in `SongEditor.cpp` or the new test.
- file-length: 26 items. The only one in a file this branch changed is `tests/src/gui/ArrangerTimelineTest.cpp` (1000
  code lines). It was already 1256 lines at HEAD (`git show HEAD:... | wc -l`), so the item existed before this change
  and is not new. The change added about nine code lines to it.

## Files committed

- `src/gui/editors/SongEditor.cpp` (fix: minimum height for the arranger ruler)
- `tests/src/gui/ArrangerTimelineTest.cpp` (regression: `theRulerHasAUsableLayoutHeight`)
- `tests/upstream-modifications.txt` (registration reason for the inherited `SongEditor.cpp` change)
- `docs/qwen-fixes-2026-10/cluster-gui-1030-session-loop-a-range.md` (this report)

## Choices and limits

- The minimum is 32 px via `scaledPixels(32)`: the ruler height the repo's own punch-band test assumes, and the track row
  height (`DEFAULT_TRACK_HEIGHT` is 32, `include/Track.h:71`). `scaledPixels` matches the other fixed sizes in this file.
- Not changed: `PatternEditor` (same layout pattern, sibling, not verified), `PianoRoll`, `AutomationEditor` (no layout).
- Not verified: the end-to-end loop-then-play behaviour from the finding, and the lanes' loop gestures.

## Self-check

`submit_check.py` (log `submit-check-1.log`, EXIT=1): PASS commits on the branch; PASS working tree clean; PASS report
present; PASS tests added or changed; **FAIL gates (feature_check.py)**. The gates row is blocked by the environment, not
by the code: `feature_check.py` creates `<worktree>-feature-check/` next to the worktree (`feature_check.py:70-71`),
outside the write-jail (`gates-1.log`: `PermissionError`). The same checks were run by hand (table above): six simple
gates EXIT 0; the two ratchets EXIT 1 in check mode with no base listing, analysed above. A real gate PASS needs
`feature_check.py` to run from a location the write-jail allows, which I could not provide.

- **Test fails with the change reverted and passes with it:** step 4b. Reverted: `test-4-revert.log`, EXIT=8, `FAIL!
  ... the ruler has 0 px of layout height`. Reapplied: `test-5-reapply.log`, EXIT=0. The RED run before the fix is
  `test-1-red.log`, EXIT=8.
- **Deterministic:** the test reads one geometry value after `QCoreApplication::processEvents()`, the same pump the
  file's `init()` already uses. It uses no sleep, no `QTRY_*`, no timer and no random input. The offscreen layout is
  synchronous, so no seam was needed. Repeated runs gave the same result: RED with the fix absent (EXIT=8 in
  `test-1-red.log` and `test-4-revert.log`); GREEN with the fix (EXIT=0 in `test-2-green.log`, `test-3-green-single.log`,
  `test-5-reapply.log`, `test-7-totals-green.log`).
- **At the cause, complete:** the cause is that the arranger's ruler (the only `TimeLineWidget` inserted into the
  `SongEditor` column, `SongEditor.cpp:129` and `:164`) has no minimum. The minimum is on the same widget object, so
  both inserts are covered. Siblings checked: `PianoRoll.cpp:291` and `AutomationEditor.cpp:133` have no layout and only
  get `setFixedWidth` (`PianoRoll.cpp:4110`, `AutomationEditor.cpp:1560`), so they are unchanged by design. `PatternEditor.cpp:62-67`
  has the same layout pattern and is NOT changed. That is a known gap, not verified, and it is outside this finding.
- **No collateral:** no saved data, project format, control command or undo path is touched. Visible effect, intended:
  the arranger's ruler now takes 32 px (scaled), so the track area starts 32 px lower. The position line already starts
  at `m_timeLine->height()` (`SongEditor.cpp:736`), so it now starts below the ruler instead of at the top of the column.
  Neighbour tests (ArrangerTakePunchTest, TrackFolderMenuTest, TrackViewDeferredLifetimeTest, VisibilitySetMenuTest)
  pass with the fix (test-6-family.log, 5/5). No limit or check was added.
- **Not verified:** the end-to-end "set a couple of bars on the ruler, enable loop, play past the end" behaviour from the
  finding, and the sections/markers lane gestures. The ruler gestures are only exercised through the existing
  punch-band test, which resizes the ruler itself.
