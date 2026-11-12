IMPLEMENTED

# Q050-063 - Author warp markers with on-clip preview

## Round 6 - integration onto `050/m0-train-25` (`081630835b`)

The integration was done on branch `integration/Q050-063-081630835b` (commit `a5b9059de2`). Logs are in `runs/Q050-063.r6/logs`.

- **What I applied.** I discarded the driver's working state: it had 101 conflicted files, because it applied the feature branch's whole history, including the train-24 merge (`driver-conflict-state.txt`). I then three-way applied only the accepted delta: `git diff 2a7fd9f9c7 a627615a3f`, which is train-24 (feat's merge base) to the accepted tip, 21 files. Four conflicts remained, each additive on both sides, and I resolved each by keeping both sides:
  - `include/Clip.h`: train-25's `TransientRepeat.h` include sits next to the patch's `WarpPublication.h`.
  - `tests/fork-sources.txt` and `tests/upstream-modifications.txt`: union of both sides.
  - `tests/all-sources.txt`: re-derived with the recipe in its header; `tests/all-sources-reproduce.sh` prints REPRODUCES, exit 0.
- **No semantic difference from the accepted patch.** The sorted `+`/`-` lines of `git diff 081630835b HEAD` are identical to those of the accepted diff: `diff` exit 0, empty output in `semantic-diff.txt`. Nothing was redesigned.
- **Hazards from train-25.** Q050-162 (Repeat/Gate) adds `m_transientRate` beside the warp state, plus a play-handle read of the map in `SamplePlayHandle::snapshotTransientRepeat` (`src/core/TransientRepeat.cpp:316`). That function reads the handle's own `m_warp`, which `src/core/SamplePlayHandle.cpp:161` copies inside `SampleTrack::play`'s `WarpPassScope`, so it uses the same generation as the pass. A grep for `m_warp` finds no new writer that bypasses `Clip::publishWarp`. The other audio-path reader is `ClipPreFades.cpp:107` `sourceFrameAt`, which already existed and is inside the scope.
- **Build and tests.** I built `zene`, the nine suites below and `tripleoscillator` (`build-3.log`, EXIT=0). The nine focused suites passed, 9/9, EXIT=0 (`test-1.log`): SampleClipWarpEditTest, WarpMarkersTest, ControlWarpCommandsTest, SampleClipWarpTest, ClipEditsMenuTest, ClipFadesRenderTest, SampleClipWindowTest, ArrangingCatchUpTest and ClipWarpPersistenceTest. Train-25's own warp tests also passed, 5/5, EXIT=0 (`build-4.log`, `test-2.log`): LoopImportWarpTest, LoopDropWarpTest, SampleClipWarpModeMenuTest, SampleClipTransientRepeatTest and SampleClipTransientRepeatSafetyTest.
- **Negative control on the integrated tree** (`mutation.diff`). I made two product changes: `passWarp` always returns `m_warp`, and the first-marker lead is disabled. After a rebuild, `SampleClipWarpEditTest` exited 8. `theRenderFollowsAMovedFirstMarker` and `aPassKeepsOneMapWhileACommitLands` failed; the other 10 passed (`test-3-mutated.log`). After restoring both files and rebuilding (`build-6-restored.log`, 0), the test exited 0 (`test-4-restored.log`).
- **Gates.** `feature_check.py` cannot create its sibling `-feature-check` log folder from this sandbox (PermissionError, `feature-1.log`). I made a `/tmp` copy that differs only in its log folder and in `ROOT` pointing at the real script directory (`feature-check-copy.diff`). It reported RESULT: PASS with exit 0 for all 8 gates: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length (`feature-2.log`, `feature-check-logs/`). Because of the same folder error, `submit_check.py` reports "gates FAIL" (`submit-1.log`); that line is not a gate result.
- **Commands, undo and projects.** No command or metadata changed, so the snapshot, canary and A16 histogram need no regeneration. `doc-figures` passes. The `<warp>` format is unchanged; `ClipWarpPersistenceTest` and `SampleClipWarpTest` pass.

Branch `feat/050-Q050-063-oc`. Feature record: `docs/050-features/Q050-063.md` (design, per-criterion table, known limits).
Round 2 fix: a moved FIRST marker now moves the audio too. The play handle chains rates from the first source frame of
the pass and the pass used to start at the clip's start whatever the first marker said; `SampleClip::leadOutputFramesAt`
(`src/core/SampleClip.cpp`, already added to the handle's offset by `SampleTrack::play`) now also owes a warped clip the
silence until its map reaches the window start. A first marker dragged earlier already worked (the pass starts at
`sourceFrameAt(at)`). Clips with no markers, or whose first marker is at its natural place, render as before.

Rounds 4-5 (the race the reviews found). The map reaches the render thread as immutable generations (`include/WarpPublication.h`):
every write goes through `Clip::publishWarp`, which never takes the engine's change lock, and each clip's pass in
`SampleTrack::play` holds one generation for its window, lead and play handle. `docs/050-features/Q050-063.md` has the design.

**Train merge.** The lane guard refused `git merge` in round 4 and I did not go around it; I moved my lines so the merge had no conflicts,
and the loop's `freshen` then made the merge (`284d2266d9`). This round's build and tests ran on that merged tree, which contains `050/m0-train-24`.

## Acceptance

All commands from `build/tests`, `QT_QPA_PLATFORM=offscreen`, `ctest -R '^SampleClipWarpEditTest$' --output-on-failure`.

| Criterion | Test | Exit |
|---|---|---|
| Double-click adds a marker, drag with grid or transient snap, each marker is a line through the waveform | `aDoubleClickAddsAMarkerAndOneOnAMarkerRemovesIt`, `aDragSnapsToTheGridOrToATransient`, `aDragStaysBetweenItsNeighbours`, `anAgentEditShowsAsTheSameMapWithALineThroughTheWaveform` (image column check) | 0 |
| Preview updates during the drag; final render matches the committed position | `aDragPreviewsTheWaveformThenCommitsOneUndoableMove`, `theRenderFollowsTheCommittedMarker` (real `ProjectRenderer` WAV) | 0 |
| `warp.add` / `warp.move` drive the UI; UI and agent read the same map | `anAgentEditShowsAsTheSameMapWithALineThroughTheWaveform` (`warp.list` vs `SampleClipView::shownWarpGuides`) | 0 |
| Related suites | `ControlWarpCommandsTest`, `SampleClipWarpTest`, `ClipEditsMenuTest`, `ClipFadesRenderTest`, `SampleClipWindowTest`, `ArrangingCatchUpTest` (needs `tripleoscillator` built) | 0 (all pass; `runs/Q050-063.r3/logs/test-3.log`, `test-4.log`) |

| One map per pass; a commit does not take the render lock; refused maps do not wait | `aPassKeepsOneMapWhileACommitLands`, `aCommitDoesNotTakeTheLockARenderPeriodHolds`, `aRefusedMapChangesNothingAndDoesNotWaitForAPass`, `WarpMarkersTest::aPublishedGenerationIsWholeAndFreedOnlyWhenUnheld` | 0 |

Round 5, final tree: the eight review suites plus `WarpMarkersTest`, 9/9, exit 0 (`runs/Q050-063.r5/logs/test-6.log`).

## Self-check

- **Round 5 negative controls** (`runs/Q050-063.r5/logs`). Each was a product mutation, rebuilt and run, then restored. Lock put back in `publishWarp`: EXIT=8,
  `aCommitDoesNotTakeTheLockARenderPeriodHolds` fails (`test-3-lock.log`). `passWarp` always returning `m_warp`: EXIT=8, `aPassKeepsOneMapWhileACommitLands` fails (`test-4-nogen.log`).
  First-marker lead removed: EXIT=8, `theRenderFollowsAMovedFirstMarker` and `aPassKeepsOneMapWhileACommitLands` fail (`test-5-nolead.log`). Restored: EXIT=0.
  Not separately mutated: `aRefusedMapChangesNothingAndDoesNotWaitForAPass` (round 4: lock taken before validation, EXIT=8) and `WarpMarkersTest::aPublishedGenerationIsWholeAndFreedOnlyWhenUnheld`.
- **Gates, round 5.** `feature_check.py` cannot create its log folder from this sandbox (PermissionError on the sibling `-feature-check` directory), so `submit_check.py`
  reports "gates FAIL" for that reason. A `/tmp` copy of the script differing only in its log folder ran every gate: RESULT PASS, exit 0 (`runs/Q050-063.r5/logs/feature-1.log`).
- **Undo, projects, portability, round 5.** No command changed. `aDragPreviewsTheWaveformThenCommitsOneUndoableMove` asserts the drag's release is one undo step, and `control.undo` restores through `loadWarp`.
  An old project's clip has an empty map, so `publishWarp` returns without publishing; `ClipWarpPersistenceTest` passes. The new test code uses `QThread::create` (Qt 5.10+) and `std::atomic`. Hosted Qt 5 / MSVC / macOS builds were not run here.
- **Audio thread, round 5 (replaces the older lines below).** Added on the audio path: `WarpSnapshot` constructor and destructor (one atomic operation each plus a pointer load), `WarpPassScope` (thread-local writes) and `passWarp` (a thread-local read). None allocates, frees, locks, waits, does I/O or emits a signal. Audited audio readers of the map: `SampleTrack::play`, `prefades`, `audioloop` and the `SamplePlayHandle` clip constructor, all inside the scope. An audio-thread read of the map outside a scope would read `m_warp`; the audit found none, nothing enforces it. Reasoned, not TSan-run.
- **Each new test fails with the product change reverted, passes restored.** Round 1: the view's hunks mutated out,
  EXIT=8, 6 of 8 slots FAIL, restored EXIT=0. Round 2: `git apply -R` of the `SampleClip.cpp` hunk, rebuilt, run:
  EXIT=8, `theRenderFollowsAMovedFirstMarker` FAILS ("marker at tick 96 (frame 37800); the bursts start at frames
  22051"), the other 8 pass (`test-2-reverted.log`); hunk re-applied, rebuilt, 9/9 EXIT=0 (`test-3.log`).
- **Every acceptance criterion is proven by a test I ran** - see the table, all exit 0.
- **One undo step per edit; reversibility.** No command was added or changed: the GUI sends the registered `warp.add`,
  `warp.move` and `warp.remove`, which keep their `true_inverse` rows. A drag sends ONE `warp.move` on release (nothing
  is written while the pointer moves); `aDragPreviewsTheWaveformThenCommitsOneUndoableMove` asserts `control.undo_depth`
  rises by exactly 1 and one `control.undo` restores the map. Add/remove are one undo each in
  `aDoubleClickAddsAMarkerAndOneOnAMarkerRemovesIt`. Because no command changed, the command snapshot, canary and A16
  histogram are unchanged (nothing to regenerate); the pre-check's WARN "no command file changed" is this: the agent
  surface for the action already exists and the GUI goes through it.
- **Projects.** No format change; old projects: a clip without markers returns the same lead as before, and a warped
  clip whose first marker is at its natural place (`timelineOffsetAt(sourceIn) <= 0`) returns 0, so its render is
  unchanged (`SampleClipWarpTest` passes). Only a clip whose first marker was authored later than its audio's natural
  place now starts late, as its map always said: the `<warp>` element is written and read by the unchanged `Clip::saveWarp`/`loadWarp`;
  a clip without markers (and a reversed one) draws through the old path. Marker save/load round-trip is covered by the
  existing `SampleClipWarpTest` (passes, 0).
- **Portability.** No Qt-6-only API: `QActionGroup`, `QMenu::addMenu`, `QMouseEvent(type, localPos, screenPos, button,
  buttons, modifiers)` exist in Qt 5.15 and 6; no `std::filesystem`, no POSIX calls, none of the forbidden identifiers
  (`near`, `far`, `small`, `interface`), `std::min`/`std::max` as everywhere else in the tree. I could not run the hosted
  Qt 5 / MSVC / macOS builds here.
- **Gates.** The pre-check's earlier gate failure (complexity of the test helper `triggerInSubmenu`, CCN 12) was fixed
  by splitting it into `openMenu`/`actionNamed`; test rerun EXIT=0. `feature_check.py` cannot run from this sandbox (it
  writes its logs to `zene-feat-q050-063-feature-check/`, outside the write jail: PermissionError), so the later
  "gates FAIL" line of `submit_check.py` is that, not a gate result. I ran the gates by hand from the worktree
  (round 2 rerun: `runs/Q050-063.r3/logs/g1..g8.log`): fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0,
  check-namespace 0, no-tautology 0, doc-figures 0. complexity (1) and file-length (1) list 6 and 15 REGRESSION lines,
  none in a file I touched (`grep SampleClipView|SampleClipWarpEdit` finds nothing); they are other trains' items the
  full script filters against its cached baseline.
