FIXED

# cluster new-p14-343-port: Ctrl-drag track copies bypass the undo journal

Port of the accepted fix bugs/qwen-new-p14-343 (commits 03d50ad81, 4e07e666, cf906b013) onto the newest line
(base b61e5f965, merge-base with FIX-BASE 050/m0-train-9). The conflict with the newest line was in
`src/gui/editors/TrackContainerView.cpp`; that file was re-applied by hand against the current text. The
commit 00695e33 is a docs-only self-check answer and was not ported.

Round 2 (review REWORK, Luna): a Ctrl-drag copy into the Pattern Editor when the Song had no pattern track also
made the Song's first pattern track, and the undo step covered only the store copy. Fixed in this round; see
"Round 2" below.

## Finding and verdict

Real, not already fixed on this branch. The `track_` branch of `TrackContainerView::dropEvent` called
`Track::create(..., m_tc)` and recorded nothing: `src/gui/editors/TrackContainerView.cpp:622` (base `b61e5f965`,
pre-fix) created the copy with no undo step, so Edit > Undo left it in the song.

The same bug has a sibling in `PatternEditor::dropEvent`, which creates the copy in the PatternStore and never
journals it (`src/gui/editors/PatternEditor.cpp:157` at base, pre-fix). A third case came up in review: when the
Song has no pattern track, the store copy's creation also makes one (`PatternStore::updateAfterTrackAdd`,
`src/core/PatternStore.cpp:74-79`), and that track has to be in the same undo step.

## Change

- `src/gui/editors/TrackContainerView.cpp:623-627`: the song copy is recorded with `control::recordCreatedTrack( copy )`
  when `m_tc == Engine::getSong()`. The guard is the accepted one: a drop into any other container is not journalled
  here.
- `src/gui/editors/PatternEditor.cpp`:
  - `firstPatternTrack(store)` (anonymous namespace, before `dropEvent`): when the store has no pattern, it calls
    `Song::addPatternTrack()` and captures the new Song track (`song->tracks().back()`) with
    `control::captureCreatedTrack`. This happens before the copy exists.
  - `dropEvent`: takes `firstPatternTrack(m_ps)` before `Track::create`, captures the copy after the clip fix, and
    records both with `control::recordCreatedTracks`. Undo removes the copy and then the Song track; redo rebuilds the
    Song track first, then the copy.
- `include/ControlTrackCreation.h`:
  - `struct CreatedTrack { Track* track; TrackContainer* container; QString saved; }`.
  - `captureCreatedTrack(track, container)`: saves the track as it stands.
  - `recordCreatedTracks(const QVector<CreatedTrack>&)`: one undo step for the group. Undo runs last-made first,
    redo first-made first.
  - `recordCreatedTrack(track)` and `recordCreatedTrack(track, container)` are kept and delegate to the group form.
- `src/core/ControlTrackCreation.cpp`: the group recorder keeps the id-not-pointer discipline and the per-container
  lookup (`trackWithIdIn`, `removeFromContainer`, `updatePatternEditor`) from round 1.

Why the Song track is captured before the copy exists: a Song pattern track saves the PatternStore it owns when it is
pattern 0 (`src/tracks/PatternTrack.cpp:163-166`), and restores that store on load (`:204-206`). Captured after the
copy was in the store, its state carried the copy, so redo rebuilt the copy twice. Round-2 debugging showed exactly
that: after redo the store held 2 copies (`logs/test-6-debug.log`, store size 0 -> 1 while the Song track was rebuilt,
then a second copy). Capturing the Song track while the store is empty fixes it.

## Regression tests (tests/src/gui/ArrangerFileDropTest.cpp)

- `aTrackCopiedByADropBelowTheTracksIsOneUndoStep`: a Ctrl-drag onto the empty space below the tracks adds one
  track, one undo step, and undo removes the copy.
- `aTrackCopiedIntoThePatternEditorIsOneUndoStep`: the same drag into the Pattern Editor, with a Song pattern track
  already present, adds one store track and one undo step; undo removes it; redo rebuilds it in the store, never in the
  song.
- `aTrackCopiedIntoAnEmptyPatternEditorIsOneUndoStep` (round 2): the same drag into a Song with zero pattern
  tracks. It asserts the precondition (0 Song pattern tracks, `numOfPatterns()==0`), then one undo step. After the
  drop there is 1 Song pattern track and 1 store copy; undo removes both; redo restores both; the redone copy has
  1 pattern clip.

No timing, sleeps or races. Each test is a deterministic event send on the offscreen platform. `init()` calls
`clearProject()`, so the empty-Song precondition holds at the start of the test.

## Evidence (commands run from the worktree; logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-343-port.r1/logs/` for round 1 and
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-343-port.r2/logs/` for round 2)

ccache could not write to `/run/user/1000/ccache-tmp`, so every build used `CCACHE_TEMPDIR=/tmp/qwen-p14-343-ccache-tmp`.
The first build without that override failed with EXIT=2 (r1 `build-1-red.log`), before any code was compiled.

### Round 1 (r1 logs)

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| RED build (tests only, product unfixed) | `CCACHE_TEMPDIR=... cmake --build build --target ArrangerFileDropTest -j4` | 0 | `build-2-red.log` |
| RED run | `cd build/tests && ctest -R '^ArrangerFileDropTest$' --output-on-failure` | 8 | `test-1-red.log`: `FAIL! aTrackCopiedByADropBelowTheTracksIsOneUndoStep` at `ArrangerFileDropTest.cpp:351`; `FAIL! aTrackCopiedIntoThePatternEditorIsOneUndoStep` at `:388`; SIGSEGV at teardown (see Open items) |
| Fix build | `cmake --build build --target zene ArrangerFileDropTest -j4` | 0 | `build-3-green.log` |
| GREEN run | `ctest -R '^ArrangerFileDropTest$' -V` | 0 | `test-3-green-verbose.log`: `Totals: 11 passed, 0 failed` |
| Reverse proof: revert | `git apply -R logs/fix.patch` | 0 | |
| Reverse RED run | `ctest -R '^ArrangerFileDropTest$' -V` | 8 | `test-5-reverted.log`: both new tests `FAIL!` (undo depth) |
| Reverse baseline | `ctest -R '^ControlEditCommandsTest$' -V` | 8 | `test-6-reverted-controledit.log`: the same single environment failure as with the fix |
| Re-apply and GREEN | `git apply logs/fix.patch`; build; ctest | 0 | `test-7-green-reapplied.log`: `Totals: 11 passed, 0 failed` |

### Round 2 (r2 logs)

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Debug attempt 1 (Song track recorded after the copy) | build + `ctest -R '^ArrangerFileDropTest$' -V` | 8 | `test-6-debug.log`: the new test fails after redo with 2 store copies (`store=0`, then `store=1` during the Song-track rebuild); debug lines removed since |
| Round-2 RED: round-2 product diff reversed | `git diff -- include/ControlTrackCreation.h src/core/ControlTrackCreation.cpp src/gui/editors/PatternEditor.cpp > logs/fix-r2b.patch`; `git apply -R logs/fix-r2b.patch`; `cmake --build build --target zene ArrangerFileDropTest -j4` | 0 / 0 / 0 | `build-8-red.log` |
| RED run | `ctest -R '^ArrangerFileDropTest$' -V` | 8 | `test-8-red.log`: `FAIL! aTrackCopiedIntoAnEmptyPatternEditorIsOneUndoStep` at `ArrangerFileDropTest.cpp:438`, `Actual (songPatternTracks()): 1`, `Expected (0): 0`; `Totals: 11 passed, 1 failed`; no signal |
| Round-2 GREEN: re-apply | `git apply logs/fix-r2b.patch`; `cmake --build build --target zene ArrangerFileDropTest -j4` | 0 / 0 | `build-9-green.log` |
| GREEN run | `ctest -R '^ArrangerFileDropTest$' -V` | 0 | `test-9-green.log`: `Totals: 12 passed, 0 failed`; the three `aTrackCopiedInto…`/`aTrackCopiedByADrop…` tests pass; no signal |

Affected family (`build/tests`, `ctest -R '^(AutomationClipDisconnectTest|TrackViewDeferredLifetimeTest|FolderSummaryLaneTest|AudioAnalysisTest|AudioEditingTest|ControlEditCommandsTest|ArrangerTimelineTest|ArrangerFileDropTest|ControlSessionRecordLandTest|RecordingPassTest)$' -V`, EXIT 8, `test-11-family.log`, final code):

| Test | Passed | Failed | Skipped |
| --- | --- | --- | --- |
| ControlSessionRecordLandTest | 13 | 0 | 0 |
| RecordingPassTest | 13 | 0 | 0 |
| AudioAnalysisTest | 21 | 0 | 0 |
| AudioEditingTest | 10 | 0 | 3 (`slicert` plug-in not in this build) |
| AutomationClipDisconnectTest | 4 | 0 | 0 |
| TrackViewDeferredLifetimeTest | 8 | 0 | 0 |
| ArrangerTimelineTest | 32 | 0 | 0 |
| ArrangerFileDropTest | 12 | 0 | 0 |
| FolderSummaryLaneTest | 7 | 0 | 0 |
| ControlEditCommandsTest | 18 | 1 | 0 (`trackSetArmManyReportsOnlyTheTracksItChanged`, see Open items) |

Ten tests executed; nine fully green, one with a pre-existing environment failure.

### Gates (before commit)

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Gate script as specified | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `gates-3-feature-check.log`: `PermissionError` writing `…-feature-check/fork-sources.log`. The log folder beside the worktree exists, but this sandbox refuses writes outside the worktree and the run folder. |
| Same checks, logs under /tmp | `python3 -I /tmp/qwen-p14-343-gates/driver.py <worktree> 050/m0-train-9` (copy: `logs/gate-driver-copy.py`) | 0 | `gates-4-driver.log`: `RESULT: PASS`. fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures: exit 0 each. complexity and file-length: no new item in the changed files. |

The review reports a direct `feature_check.py` pass (EXIT 0) in its environment; in this sandbox the script cannot
write its log folder, so this report relies on the driver result above. The driver imports the script's own gate
lists and helpers (`SIMPLE`, `RATCHET`, `run`, `key`, `item_file`, `REG`) and repeats its `main()` comparison. The
base listing comes from a `git archive` export of `b61e5f965` under `/tmp`, not from the shared cache. Only the
log and cache location differ from the script. No gate or baseline was changed.

## Open items (not addressed here)

- **Teardown crash in the round-1 red state.** In round 1, a failing `aTrackCopiedIntoThePatternEditorIsOneUndoStep`
  returned before `delete pattern`, and the run ended in SIGSEGV inside `InstrumentTrackView::~InstrumentTrackView`
  (`r1/test-1-red.log`, `r1/test-5-reverted.log`). Not re-run in round 2: the round-2 red run ends cleanly
  (`r2/test-8-red.log`). The round-1 test still has the early return; not investigated further.
- **`ControlEditCommandsTest::trackSetArmManyReportsOnlyTheTracksItChanged`** fails with `armed.ok` false because
  the take file `/home/kruzzzzy/Documents/Zene Studio/zene-take-trk40.wav` cannot be opened for writing. The write is
  outside the worktree and the write-jail refuses it. The same failure occurs with the fix reverted, so it is
  environmental and unrelated to this change.
- **Song-only Ctrl-drag redo** is not asserted (undo only). The empty-Song test covers redo for the Song track and the
  store copy together.
- **Open items from the accepted cluster report** (not reproduced or investigated here): the TrackView constructor's
  deferred-view crash.

## Files committed

Round 1 (commits 26bc70650, e26ed5e93, fff96414d):
- `include/ControlTrackCreation.h`, `src/core/ControlTrackCreation.cpp`
- `src/gui/editors/PatternEditor.cpp`, `src/gui/editors/TrackContainerView.cpp`
- `tests/src/gui/ArrangerFileDropTest.cpp`
- `tests/upstream-modifications.txt` (`TrackContainerView.cpp`, `PatternEditor.cpp`; the two `ControlTrackCreation`
  files are already in `tests/fork-sources.txt`)
- `docs/qwen-fixes-2026-10/cluster-new-p14-343-port.md`

Round 2 (this commit): `include/ControlTrackCreation.h`, `src/core/ControlTrackCreation.cpp`,
`src/gui/editors/PatternEditor.cpp`, `tests/src/gui/ArrangerFileDropTest.cpp`, `tests/upstream-modifications.txt`
(reason line for `PatternEditor.cpp` updated), and this report.

## Self-check

1. **Test fails with the product change reverted, passes with it.** Yes, for the round-2 case:
   the round-2 product diff was reversed (`git apply -R logs/fix-r2b.patch`), and
   `ctest -R '^ArrangerFileDropTest$' -V` gave EXIT 8 with `FAIL!` at `ArrangerFileDropTest.cpp:438`
   (`r2/test-8-red.log`). Re-applied, EXIT 0, `Totals: 12 passed, 0 failed` (`r2/test-9-green.log`). The round-1 cases
   are proven in the round-1 table above.
2. **Deterministic.** Yes. No timers, sleeps or threads on these paths. The `track_` branch and the Pattern Editor
   drop call `Track::create` / `Song::addPatternTrack` synchronously. The tests send fixed `QDragEnterEvent` and
   `QDropEvent` at fixed points on the offscreen platform, with the track's own saved state as the drag payload
   (`trackCopyDrag`). `init()` clears the project, so the empty-Song precondition is exact. The `processEvents()` calls
   only flush events already posted.
3. **At the cause; callers and siblings.** Callers of `recordCreatedTrack(Track*)`: the five existing command callers
   (`ControlCommandsAudioAnalysis.cpp:118`, `ControlCommandsAudioSlice.cpp:259`,
   `ControlCommandsBrowserMedia.cpp:323`, `ControlCommandsClipImportFile.cpp:294`, `ControlCommandsRecorder.cpp:112`,
   `ControlCommandsVideo.cpp:176`) still call the one-argument form, which delegates to the group recorder with the
   song as container. Their record is taken at the same point as before, right after creation, and the undo removes
   the track through `removeFromContainer`, which for the song calls `removeTrack` as before. The song undo lookup is
   the same scan `resolveTrack` does (`src/core/ControlEditSupport.cpp:136-158`); a negative id matches nothing in both.
   Siblings: the song drop (`TrackContainerView.cpp:623-627`) and the Pattern Editor drop (`PatternEditor.cpp`, the
   `track_` branch) are both covered; a drop into the Pattern Editor with a Song pattern track already present is
   unchanged (`aTrackCopiedIntoThePatternEditorIsOneUndoStep` passes). The other `Track::create` drops in
   `TrackContainerView::dropEvent` (`instrument`, `samplefile`, `presetfile`) make a new instrument from a browser or
   file drag, not a copy of an existing track, and are outside this finding. File drops below the tracks are journalled
   by ArrangerFileDrop (`aFileDroppedBelowTheTracksGetsANewTrackAndOneUndoRemovesBoth`, passing in `test-3-green-verbose.log`).
4. **No collateral damage.** Yes. No format, save or load change; no new control command (the canary is untouched); no
   limit or check added. The drop's order changed only in the empty-Song case: the Song's pattern track is made before the
   copy instead of during it, and the copy's clips are still checked and fixed by the same clip code. The copy ends with
   one pattern clip (asserted). The affected family is green except the environmental test in Open items
   (`r2/test-11-family.log`). `ControlEditCommandsTest` fails the same way with the fix reverted
   (`r1/test-6-reverted-controledit.log`).
5. **Verdict.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so the code-citation item does not apply. The pre-fix
   evidence is `TrackContainerView.cpp:622` and `PatternEditor.cpp:157` at base `b61e5f965`.

**Gates item: FAIL in `submit_check.py`, environmental.** `submit_check.py` runs `feature_check.py`, which writes its
logs beside the worktree. This sandbox refuses that write, so the script exits 1 with `PermissionError`
(`r2/gates-3-feature-check.log`). The same checks with logs under `/tmp` pass (`r2/gates-4-driver.log`, `RESULT: PASS`).
The gate script itself needs to run where its log folder can be written.
