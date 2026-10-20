# Cluster new-p15-245: hiding every ruler lane removes its context menu

**Verdict: FIXED**

Finding (Luna patrol, historical line 182): turning off the last visible arranger ruler lane hid the whole ruler widget. Its context menu is the only in-UI way to show lanes again, so after the menu closed the user could not reach it.

## Evidence

- **The cause:** `src/gui/ArrangerRulerLanes.cpp:177-187` (`relayout`, after the fix). Before the fix it called `setVisible(height > 0)` (line 182 of the unfixed file), so when no lane was drawn the widget disappeared and took its right-click target with it. "No lane drawn" happens when all four lanes are off by preference, or when only the chord lane is on and the chord track is empty (`laneDrawn`, `src/gui/ArrangerRulerLanes.cpp:154-157`).
- **No other in-UI route:** a search of `src/`, `include/`, `plugins/`, `tests/` and `data/` for `ruler_lanes`, `rulerlanes` and `ArrangerRulerLanes` finds: the menu (`contextMenuEvent`, `src/gui/ArrangerRulerLanes.cpp:412-428`, the only caller of the toggle outside the handler); the control command, registered at `src/gui/ControlCommandsArrangerView.cpp:444` with its reversibility entry at `src/core/ControlReversibilityTableArranger.cpp:107` (control surface, not in the UI); and class references in `SongEditor.cpp`, `ArrangerOverlays.cpp` (construction, line 271), `ArrangerRulerTempo.cpp` and `VideoLane.cpp`, which toggle nothing. The context menu is the only in-UI way to show a lane again.
- **The fix:** `src/gui/ArrangerRulerLanes.cpp:65` adds `ClosedHeight = 8`. `relayout()` (line 183) now does `setFixedHeight(height > 0 ? height : ClosedHeight)` and no longer hides the widget. The ruler therefore keeps an 8 px strip under the bar ruler with no lane drawn, and right-click still reaches `contextMenuEvent`. With any lane drawn, the height is unchanged.
- **Header doc:** `include/ArrangerRulerLanes.h:88-89` notes the strip on `relayout()`. This is a comment-only change in a header, rebuilt with every target that includes it.
- **Not changed:** the preference (`ui/rulerlanes`), the command (`arrangement.ruler_lanes`, its schema and description), the undo model, the project format, the audio thread. No new control command, so `ControlCommandsSnapshot` and the canary figures are not affected.

## Regression test

`tests/src/gui/ArrangerTimelineTest.cpp:221-247`, `aRulerWithEveryLaneHiddenKeepsItsContextMenuReachable`, in the existing family.

It hides all four lanes with `arrangement.ruler_lanes`, then asserts that the ruler is not hidden and has height > 0 (line 238-239). It shows one lane and checks it draws, then restores all four lanes. A local guard (`LanesShownAgain`, line 229) restores the lane preference on every exit path. The first RED run, before the guard, showed that an aborted test left the preference all-hidden and broke two later tests (`aCrowdedLaneNamesWhatItLeftOutInItsTooltip`, `aDoubleClickZoomsToASectionAndFitSelectionFitsTheRange`). That log is kept as `test-1-red.log`. The guard fixed it, and the RED run in `test-2-red.log` shows only the new test failing.

The test does not open the context menu itself. `QMenu::exec` blocks, and no seam exists for it. It proves the target the menu needs (visible, with room to right-click). The menu code path is unchanged.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-245.r1/logs/`. All builds ran from the worktree root, with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p15-245` set on each build (see the environment note below). All ctest runs ran from `build/tests`.

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| build attempt, default ccache temp dir | `cmake --build build --target ArrangerTimelineTest -j4 > build-1-red.log 2>&1` | build-1-red.log | 2 (ccache could not create a temp file under `/run/user/1000`, not a code error) |
| RED build (first attempt, cascade) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p15-245 cmake --build build --target ArrangerTimelineTest -j4 > build-2-red.log 2>&1` | build-2-red.log | 0 |
| RED test (first attempt, cascade) | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > test-1-red.log 2>&1` | test-1-red.log | 8 (3 failed: the new test plus 2 cascade failures, see above) |
| RED build after the guard | `CCACHE_TEMPDIR=… cmake --build build --target ArrangerTimelineTest -j4 > build-3-red.log 2>&1` | build-3-red.log | 0 |
| **RED test** | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > test-2-red.log 2>&1` | test-2-red.log | **8**: `FAIL! ... aRulerWithEveryLaneHiddenKeepsItsContextMenuReachable() '!lanes->isHidden()' returned FALSE`, at `ArrangerTimelineTest.cpp(238)`; Totals 27 passed, **1 failed** |
| GREEN build | `CCACHE_TEMPDIR=… cmake --build build -j4 --target zene ArrangerTimelineTest ArrangerOverlaysVideoLaneTest > build-4-green.log 2>&1` | build-4-green.log | 0 |
| **GREEN test** | `ctest -R '^ArrangerTimelineTest$' -V > test-4-green-verbose.log 2>&1` | test-4-green-verbose.log | **0**: `PASS aRulerWithEveryLaneHiddenKeepsItsContextMenuReachable()`; Totals 28 passed, 0 failed |
| Family test | `ctest -R '^ArrangerOverlaysVideoLaneTest$' -V > test-5-green-overlays.log 2>&1` | test-5-green-overlays.log | 0 (Totals 10 passed, 0 failed) |
| Reversed-fix proof: save patch | `git diff -- include/ArrangerRulerLanes.h src/gui/ArrangerRulerLanes.cpp > fix.patch` | fix.patch | 0 |
| Reversed-fix proof: revert | `git apply -R fix.patch` | — | 0 |
| Reversed-fix proof: rebuild | `CCACHE_TEMPDIR=… cmake --build build -j4 --target zene ArrangerTimelineTest > build-5-unfixed.log 2>&1` | build-5-unfixed.log | 0 |
| **Reversed-fix proof: test must fail** | `ctest -R '^ArrangerTimelineTest$' -V > test-6-proof-unfixed.log 2>&1` | test-6-proof-unfixed.log | **8**: the same assertion at `ArrangerTimelineTest.cpp(238)`; Totals 27 passed, 1 failed |
| Restored-fix proof: apply | `git apply fix.patch` | — | 0 |
| Restored-fix proof: rebuild | `CCACHE_TEMPDIR=… cmake --build build -j4 --target zene ArrangerTimelineTest > build-6-refixed.log 2>&1` | build-6-refixed.log | 0 |
| **Restored-fix proof: test must pass** | `ctest -R '^ArrangerTimelineTest$' -V > test-7-proof-fixed.log 2>&1` | test-7-proof-fixed.log | **0**: Totals 28 passed, 0 failed |

Gates (run as the same commands `feature_check.py` runs, from the worktree, because the script itself could not run; see below):

| Gate | Command | Log | EXIT |
| --- | --- | --- | --- |
| feature_check.py | `python3 …/feature_check.py <worktree>` | gate-1-feature-check.log | 1: `PermissionError` creating `zene-qwen-new-p15-245-feature-check/` next to the worktree, outside the writable area |
| fork-sources | `bash tests/fork-sources-gate.sh` | gate-2-fork-sources.log | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | gate-3-all-sources.log | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | gate-4-no-upstream.log | 0 |
| check-namespace | `tests/scripted/check-namespace` | gate-5-check-namespace.log | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | gate-6-no-tautology.log | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | gate-7-doc-figures.log | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | gate-8-complexity.log | 1 (6 REGRESSION lines, none in my changed files) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | gate-9-file-length.log | 1 (15 REGRESSION lines; one names my file, see below) |

Ratchet comparison against the base. The base is HEAD `2da2ed356` (the fix base's merge-base). I exported it read-only with `git archive HEAD | tar -x -C /tmp/p15-245-base`, and ran the same two ratchets there (`base-8-complexity.log`, EXIT=1; `base-9-file-length.log`, EXIT=1). The base has the same 6 complexity and 15 file-length REGRESSION lines as the worktree. The only file-length item in my files is `tests/src/gui/ArrangerTimelineTest.cpp`. It is over 500 lines at base (689 in the base export; 714 in the worktree), so its key `new file over 500 lines: tests/src/gui/ArrangerTimelineTest.cpp` is already in the base list and is not new. The complexity list names no function in my changed files.

Under `feature_check.py`'s own rule (ratchet items fail only when new and in a changed file), both ratchet rows would pass. I did not run the script itself, so this is my reading of its rule, not its output.

## Environment notes

- `CCACHE_TEMPDIR` was pointed at `/tmp/ccache-tmp-p15-245` for every build. The default ccache temp dir under `/run/user/1000` was not writable, and the first build attempt failed on that (`build-1-red.log`, EXIT=2). Nothing in the repo changed.
- `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check/`, which is outside the writable area. It failed with `PermissionError` (`gate-1-feature-check.log`, EXIT=1). I ran its gates directly from the worktree instead, as listed above.
- The base export lives at `/tmp/p15-245-base` (created by me; not removed).

## Self-check

Answers to the completion check's questions, with the evidence:

1. **Each new test FAILS with the product change reverted and PASSES restored.** Yes. RED: `test-2-red.log`, exit 8, `aRulerWithEveryLaneHiddenKeepsItsContextMenuReachable` fails at `ArrangerTimelineTest.cpp(238)`. Reverted proof: `test-6-proof-unfixed.log`, exit 8, same assertion. Restored: `test-7-proof-fixed.log`, exit 0, 28 passed.
2. **Every acceptance criterion is proven by a test.** The criterion is "a hidden ruler still offers the lanes' context menu". The test proves the right-click target exists: the ruler is not hidden and has height > 0. It does not open the menu (`QMenu::exec` blocks; there is no seam), so the menu's own code is untested. The gap is stated in the report.
3. **Each edit is one undo step; each command has an honest reversibility class.** No command changed, and the lanes preference is not undoable project state (`ConfigManager` `ui/rulerlanes`). The change adds no undo entry.
4. **Nothing on the audio thread allocates, locks, waits, logs or emits.** The change is GUI-only (`ArrangerRulerLanes` widget layout). No audio-path function is touched.
5. **Old projects still load and render as before.** The project format is unchanged. The only persisted state is the existing lane preference, which keeps its meaning. With any lane drawn, the layout is unchanged.
6. **Qt 5.15 / MSVC / macOS portability.** The change uses only `setFixedHeight` and `isHidden`, which exist on every target. The test adds a local struct with a destructor. No new platform API.
7. **The fix is at the cause, and siblings are consistent.** The cause is the single `setVisible(height > 0)` in `relayout()`. `relayout()` is the only place that changes lane geometry, and the chord-lane poll (`followChordTrack`) calls it. The chord-only case (no lanes but an empty chord track) takes the same path, so it is covered too.
8. **Collateral.** The existing family tests pass: `ArrangerTimelineTest` 28/28 and `ArrangerOverlaysVideoLaneTest` 10/10. Test-suite-wide, the new test restores the lane preference on every exit path, so the cases after it see the same state as before.
9. **Gates.** Six gates exit 0; complexity and file-length exit 1 on the train, with no new item in my files (see the ratchet comparison). `feature_check.py` itself could not run here (outside the write-jail). Its rows are reproduced above, not reported as its output.
10. **Manual GUI check.** Not done. Only offscreen tests ran (`QT_QPA_PLATFORM=offscreen`). The menu itself was not exercised.
