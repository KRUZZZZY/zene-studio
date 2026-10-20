FIXED

# cluster new-p14-343-port: Ctrl-drag track copies bypass the undo journal

Port of the accepted fix bugs/qwen-new-p14-343 (commits 03d50ad81, 4e07e666, cf906b013) onto the newest line
(base b61e5f965, merge-base with FIX-BASE 050/m0-train-9). The conflict with the newest line was in
`src/gui/editors/TrackContainerView.cpp`; that file was re-applied by hand against the current text. The
commit 00695e33 is a docs-only self-check answer and was not ported.

## Finding and verdict

Real, not already fixed on this branch. The `track_` branch of `TrackContainerView::dropEvent` called
`Track::create(..., m_tc)` and recorded nothing: `src/gui/editors/TrackContainerView.cpp:622` (base `b61e5f965`, pre-fix) created the
copy with no undo step, so Edit > Undo left it in the song.

The same bug has a sibling in `PatternEditor::dropEvent`, which creates the copy in the PatternStore and
never journals it (`src/gui/editors/PatternEditor.cpp:157` pre-fix). The accepted cluster fixed both, so this port
does too.

## Change

- `src/gui/editors/TrackContainerView.cpp:623-627`: the copy is recorded with `control::recordCreatedTrack( copy )`
  when `m_tc == Engine::getSong()`. The guard is the accepted one: the song's undo step removes the track from the
  song, and a drop into any other container is not journalled here.
- `src/gui/editors/PatternEditor.cpp:179-180`: the copy is recorded in the PatternStore with
  `control::recordCreatedTrack( t, model() )`.
- `include/ControlTrackCreation.h:44-45`: new container overload `recordCreatedTrack(Track*, TrackContainer*)`.
  The song form (`:41`) delegates to it.
- `src/core/ControlTrackCreation.cpp:95-123`: the undo step looks the track up in its container
  (`trackWithIdIn`), removes it through `removeFromContainer` (view first for the Pattern Editor), and the redo
  builds it in the same container, then refreshes the Pattern Editor's step count (`updatePatternEditor`). For
  the song container the removal is `removeTrack` and the redo is unchanged, so the song path behaves as before.

## Regression tests (tests/src/gui/ArrangerFileDropTest.cpp)

- `aTrackCopiedByADropBelowTheTracksIsOneUndoStep`: a Ctrl-drag onto the empty space below the tracks adds one
  track, one undo step, and undo removes the copy.
- `aTrackCopiedIntoThePatternEditorIsOneUndoStep`: the same drag into the Pattern Editor adds one store track and
  one undo step; undo removes it from the store and restores the pattern editor's pixels-per-bar; redo rebuilds
  it in the store, never in the song.

No timing, sleeps or races. Both are deterministic event sends on the offscreen platform.

## Evidence (commands run from the worktree; logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-343-port.r1/logs/`)

ccache could not write to `/run/user/1000/ccache-tmp`, so every build used `CCACHE_TEMPDIR=/tmp/qwen-p14-343-ccache-tmp`.
The first build without that override failed with EXIT=2 (`build-1-red.log`), before any code was compiled.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| RED build (tests only, product unfixed) | `CCACHE_TEMPDIR=... cmake --build build --target ArrangerFileDropTest -j4` | 0 | `build-2-red.log` |
| RED run | `cd build/tests && ctest -R '^ArrangerFileDropTest$' --output-on-failure` | 8 | `test-1-red.log`: `FAIL! aTrackCopiedByADropBelowTheTracksIsOneUndoStep` at `ArrangerFileDropTest.cpp:351` (undo depth, expected depth+1); `FAIL! aTrackCopiedIntoThePatternEditorIsOneUndoStep` at `:388` (same); then SIGSEGV at teardown (see Open items) |
| Fix build | `cmake --build build --target zene ArrangerFileDropTest -j4` | 0 | `build-3-green.log` |
| GREEN run | `ctest -R '^ArrangerFileDropTest$' -V` | 0 | `test-3-green-verbose.log`: `Totals: 11 passed, 0 failed` |
| Reverse proof: patch saved | `git diff -- include/ControlTrackCreation.h src/core/ControlTrackCreation.cpp src/gui/editors/PatternEditor.cpp src/gui/editors/TrackContainerView.cpp > logs/fix.patch` | 0 | |
| Reverse proof: revert | `git apply -R logs/fix.patch` | 0 | |
| Reverse build | `cmake --build build --target zene ArrangerFileDropTest ControlEditCommandsTest -j4` | 0 | `build-5-reverted.log` |
| Reverse RED run | `ctest -R '^ArrangerFileDropTest$' -V` | 8 | `test-5-reverted.log`: both new tests `FAIL!` (compare, undo depth); no `Totals` line (SIGSEGV at teardown) |
| Reverse check, baseline of an unrelated test | `ctest -R '^ControlEditCommandsTest$' -V` | 8 | `test-6-reverted-controledit.log`: same single failure as with the fix (see Open items), so it predates this change |
| Re-apply | `git apply logs/fix.patch` | 0 | |
| Re-apply build | `cmake --build build --target zene ArrangerFileDropTest ControlEditCommandsTest -j4` | 0 | `build-6-reapplied.log` |
| GREEN after re-apply | `ctest -R '^ArrangerFileDropTest$' -V` | 0 | `test-7-green-reapplied.log`: `Totals: 11 passed, 0 failed`, no signal |

Affected family (`build/tests`, all with the fix, `ctest -R '^(AutomationClipDisconnectTest|TrackViewDeferredLifetimeTest|FolderSummaryLaneTest|AudioAnalysisTest|AudioEditingTest|ControlEditCommandsTest|ArrangerTimelineTest|ArrangerFileDropTest)$' -V`, EXIT 8, `test-4-family.log`):

| Test | Passed | Failed | Skipped |
| --- | --- | --- | --- |
| AutomationClipDisconnectTest | 4 | 0 | 0 |
| TrackViewDeferredLifetimeTest | 8 | 0 | 0 |
| FolderSummaryLaneTest | 7 | 0 | 0 |
| AudioAnalysisTest | 21 | 0 | 0 |
| AudioEditingTest | 10 | 0 | 3 (`slicert` plug-in not in this build) |
| ArrangerTimelineTest | 32 | 0 | 0 |
| ArrangerFileDropTest | 11 | 0 | 0 |
| ControlEditCommandsTest | 18 | 1 | 0 (`trackSetArmManyReportsOnlyTheTracksItChanged`, see Open items) |

Eight tests executed; seven fully green, one with a pre-existing environment failure.

Gates, before commit:

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Gate script as specified | `python3 .../feature_check.py <worktree>` | 1 | `gates-1-feature-check.log`: `PermissionError` creating `<worktree>-feature-check` beside the worktree. That directory is outside the write-jail, so the script could not run. It also writes a shared gate cache and a shared `tree` worktree (`qwen/gate-baselines/`) when the base's baseline is missing; that was not run. |
| Same checks, logs under /tmp | `python3 -I /tmp/qwen-p14-343-gates/driver.py <worktree> 050/m0-train-9` (copy: `logs/gate-driver-copy.py`) | 0 | `gates-2-feature-check-tmp-logs.log`: `RESULT: PASS`. fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures: exit 0 each. complexity and file-length: no new item in the changed files. |

The driver imports `feature_check.py`'s own gate lists and helpers (`SIMPLE`, `RATCHET`, `run`, `key`, `item_file`,
`REG`) and repeats its `main()` comparison. The base listing comes from a `git archive` export of `b61e5f965` under
`/tmp`, not from the shared cache. Only the log and cache location differ from the script.

## Open items (not addressed here)

- **Teardown crash on a red run.** When an assertion fails, `aTrackCopiedIntoThePatternEditorIsOneUndoStep` returns
  before `delete pattern`, and the run ends in SIGSEGV inside `InstrumentTrackView::~InstrumentTrackView`
  (`test-1-red.log` line 58, `test-5-reverted.log`). The RED assertions do fail; the crash is at teardown. A
  green run is clean (`test-3-green-verbose.log`, `test-7-green-reapplied.log`: no signal). Not investigated;
  fixing it would mean a scope guard around `pattern` and a check on the leftover store copy.
- **`ControlEditCommandsTest::trackSetArmManyReportsOnlyTheTracksItChanged`** fails with `armed.ok` false because
  the take file `/home/kruzzzzy/Documents/Zene Studio/zene-take-trk40.wav` cannot be opened for writing. The write
  is outside the worktree and the write-jail refuses it. The same failure occurs with the fix reverted
  (`test-6-reverted-controledit.log`), so it is environmental and unrelated to this change.
- **Redo of the song drop** is not asserted in the song test (undo only). The redo path is the one
  `recordCreatedTrack` already uses for every other track command.
- **Open items from the accepted cluster report** (not reproduced or investigated here): the TrackView
  constructor's deferred-view crash, and a song pattern track the drop adds when the song has none.

## Files committed

- `include/ControlTrackCreation.h`
- `src/core/ControlTrackCreation.cpp`
- `src/gui/editors/PatternEditor.cpp`
- `src/gui/editors/TrackContainerView.cpp`
- `tests/src/gui/ArrangerFileDropTest.cpp`
- `tests/upstream-modifications.txt` (two registration lines: `TrackContainerView.cpp`, `PatternEditor.cpp`; the two
  `ControlTrackCreation` files are already in `tests/fork-sources.txt`)
- `docs/qwen-fixes-2026-10/cluster-new-p14-343-port.md`

## Self-check

1. **Test fails with the product change reverted, passes with it.** Yes. Reverted (`git apply -R`, rebuilt):
   `ctest -R '^ArrangerFileDropTest$' -V` EXIT 8, `test-5-reverted.log`, both new tests `FAIL!` at
   `ArrangerFileDropTest.cpp:351` and `:388` (undo depth). Re-applied (`git apply`, rebuilt):
   EXIT 0, `test-7-green-reapplied.log`, `Totals: 11 passed, 0 failed`. The first RED, before any product
   change, is `test-1-red.log` (EXIT 8, same two assertions).
2. **Deterministic.** Yes. No timers, sleeps or threads on this path: the `track_` branch calls `Track::create`
   synchronously (`TrackContainerView.cpp:623`; the thread in the `instrument` branch is not on this path). The
   test sends `QDragEnterEvent`/`QDropEvent` at fixed points on the offscreen platform, with the track's own saved
   state as the drag payload (`trackCopyDrag`), and the zoom is pinned by `arrangement.zoom` in `init()`.
   `QCoreApplication::processEvents()` in the Pattern Editor test only flushes events already posted.
3. **At the cause; callers and siblings.** Callers of `recordCreatedTrack(Track*)`: the five existing command
   callers (`ControlCommandsAudioAnalysis.cpp:118`, `ControlCommandsAudioSlice.cpp:259`,
   `ControlCommandsBrowserMedia.cpp:323`, `ControlCommandsClipImportFile.cpp:294`,
   `ControlCommandsRecorder.cpp:112`, `ControlCommandsVideo.cpp:176`) still use the one-argument form, which
   delegates to the container form with the song. Their undo removes the track through `removeFromContainer`,
   which for the song calls `removeTrack` as before. Their redo builds the track in the song as before. The one
   change inside the song path is the undo lookup: `trackWithIdIn(song, id)` scans `Engine::getSong()->tracks()`
   for `track->id() == id`, the same scan `resolveTrack` does (`src/core/ControlEditSupport.cpp:136-158`); a
   negative id matches nothing in both. Siblings: the Pattern Editor drop (`PatternEditor.cpp:179-180`) is fixed
   the same way; the song drop is `TrackContainerView.cpp:623-627`. The other `Track::create` drops in
   `TrackContainerView::dropEvent` (`instrument`, `samplefile`, `presetfile`) create a new instrument from a
   browser or file drag, not a copy of an existing track, and are outside this finding. The file drops below the
   tracks are already journalled by ArrangerFileDrop (`aFileDroppedBelowTheTracksGetsANewTrackAndOneUndoRemovesBoth`,
   passing in `test-3-green-verbose.log`).
4. **No collateral damage.** Yes. No format, save or load change; no new control command (the canary is untouched);
   no limit or check added. The affected family is green except the environmental test noted in Open items
   (`test-4-family.log`, table above). `ControlEditCommandsTest` fails the same way with the fix reverted
   (`test-6-reverted-controledit.log`).
5. **Verdict.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so the code-citation item does not apply. The
   pre-fix evidence is `TrackContainerView.cpp:622` and `PatternEditor.cpp:157` in the base commit
   `b61e5f965` (`git show b61e5f965:src/gui/editors/TrackContainerView.cpp`).

**Gates item: FAIL, environmental, not a code failure.** `submit_check.py` runs `feature_check.py`, which
creates `<worktree>-feature-check` beside the worktree; that path is outside the write-jail, so the script exits 1
with `PermissionError` (`gates-1-feature-check.log`). The same checks, with logs under `/tmp`, pass:
`gates-2-feature-check-tmp-logs.log`, `RESULT: PASS` (driver copy: `logs/gate-driver-copy.py`). The
ratchet items (complexity, file-length) report no new item in the changed files. The gate script itself
needs to be run where its log folder can be written.
