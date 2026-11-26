FIXED

# Cluster 4736: track reordering detached an active recording route from its track

## Verdict and reach

REAL, and fixed at the cause. `track.set_arm` addresses a RECORD ROUTE by the track's POSITION in
the song, which is the contract the command documents; what was missing is that once a track has
STARTED a capture, that capture belongs to the track, not to the row the track stands on. Removing,
inserting or reordering tracks above it moved the row, and `track.set_arm` (and the GUI's own arm
query) then resolved a route the track never armed:

- disarm stopped a route that was not armed and left the real capture writing to its take file;
- the track now standing on the armed row could stop (and showed as owning) another track's capture.

Fix: a route remembers the stable `Track::id()` that armed it (new binding, cleared with the
capture), a track resolves to *its* route while that capture runs, and a row that merely moved onto
an armed route can neither stop it nor claim it. The position mapping stays exactly as documented
for everything that is not an armed capture.

## Evidence (file:line on this branch)

- `src/core/ControlCommandsArrangementState.cpp:144` `trackRouteOf()` returns the track's index in
  `Song::tracks()`; the command's contract block above `track.set_arm` documents that mapping.
- `src/core/ControlCommandsArrangementState.cpp:343` is the route the handler used to act on before
  this fix: `trackRouteOf()` and nothing else. `src/core/ControlCommandsArrangementState.cpp:519`
  (`control::trackRecordArmed`, the GUI arm button's query at
  `src/gui/tracks/TrackOperationsWidget.cpp:144,152,241`) resolved the same way.
- `include/TrackRecorder.h:156-157,215` — the binding: `setArmedTrackId()` / `armedTrackId()` over
  `std::atomic<int> m_armedTrackId{-1}`. Written and read by the control thread only; the audio
  thread's atomics are untouched.
- `src/core/audio/TrackRecorder.cpp:168` clears it in `arm()` (a route armed by number binds no
  track) and `src/core/audio/TrackRecorder.cpp:303` clears it in `disarm()` (the capture is over).
- `src/core/ControlCommandsArrangementState.cpp:160` `routeArmedByTrack()` — the route a track's own
  capture runs on, `-1` when it has none; `:173` `holdsAnotherTracksCapture()`; `:185`
  `resolveArmRoute()` — own route first, else the position, and a DISARM of a route bound to another
  track is refused typed (`:210`, naming the owning `trk-<n>`). `:313` binds the arming track after
  a successful `recorder.armTrack()`; `:343` is the handler's one call into that resolution.
- Tests: `tests/src/core/ControlArmReversibilityTest.cpp:134` and `:182` (helper `:63`).

## What deliberately did not change

- Arming still maps by position, and the task-batch verb `track.set_arm_many` still goes through
  `setArm` (`ControlCommandsArrangementState.cpp:425,456`), so both resolve identically.
- `record.arm_track` still arms BY NUMBER and binds no track; `record.disarm_track` /
  `record.disarm_all` still address routes directly. A route armed by number answers exactly as
  before (`trackRecordArmed` treats `owner < 0` as the row's own, `:519-540`).
- No reply field, schema or command description changed — the command-surface snapshot is untouched.
- Nothing is serialised: a project file gains no attribute, so old projects load byte-identically.

## The surface's arm query had the same defect (second path)

`src/core/McuSurface.cpp:75` is the surface's arm query. It used to keep its own copy of the
mapping — "the route `@a` track maps to (its place in the song, as `track.set_arm` maps it)" — and
served the REC button (`:235`) and the REC LED (`:288`). After a move it read the wrong row: REC on
the armed track sent `armed: true` (a `Busy` refusal, so the capture could not be stopped from the
surface) and REC on the track that moved onto the row sent `armed: false`. It now returns
`control::trackRecordArmed(track)` — the query the window's arm button uses — so surface and window
cannot disagree. Covered by `McuSurfaceTest::recFollowsTheTrackWhenAnotherTrackMovesOntoItsRow`.

## Test matrix

| path | test | evidence |
| --- | --- | --- |
| normal | arm a track by id, read its arm state, disarm it | `ControlArmReversibilityTest::armingOneTrackRefusesUndoThatWouldLeaveItsTakeBehind`, `ControlRecordingRoutesTest` (both green) |
| failure (the bug) | remove the track above an armed one, then disarm it: its OWN route must stop and its job run | `ControlArmReversibilityTest::disarmingATrackAfterAnotherTracksRemovalStopsItsOwnCapture` |
| failure (the bug) | a track that MOVES onto the armed row cannot stop/claim/arm over the capture, and `track.set_arm_many` stops the owner's route | `ControlArmReversibilityTest::aTrackThatMovedOntoAnArmedRouteCannotStopAnotherTracksCapture` |
| failure (the bug, second path) | after the same move, the hardware surface's REC on the moved track cannot stop the capture, and REC on the armed track does stop its own | `McuSurfaceTest::recFollowsTheTrackWhenAnotherTrackMovesOntoItsRow` |
| exhaustion | a track past `MultiTrackRecorder::MaxRoutes` is still refused with the old message | `ControlRecordingRoutesTest` family, `MultiTrackRecorderTest` (green) |
| reconfiguration | input-channel capacity shrink disarms routes; route source re-pointing | `RecordingInputPathTest`, `RecordClipTest`, `TwoTrackRecordingHarness` (green) |
| no song / no engine | `trackRecordArmed` and `track.set_arm` with `Engine::destroy()` | `ControlTrackRouteNoSongTest` (green) |
| audio thread | no code added on the audio path: the binding is touched only by `arm()`/`disarm()` and the two control-thread commands; `processInput*` unchanged | `MultiTrackRecorderTest`, `RecordingPassTest`, `TwoTrackRecordingHarness` (green) |
| platforms | no Qt-6-only API, no new POSIX code; one `std::atomic<int>` member (C++11) | gates below |

## Commands and exit codes (copied from this session's logs)

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/4736.r1/logs/`.

RED (test first, before the fix) — `ControlRecordingRoutesTest`, where the tests were written first:

```sh
cmake --build build --target ControlRecordingRoutesTest -j4   # build-2-red.log EXIT=0
cd build/tests && ctest -R ControlRecordingRoutesTest --output-on-failure   # test-3-red.log EXIT=8
#  FAIL! disarmingATrackAfterAnotherTracksRemovalStopsItsOwnCapture '!recorder.track(route).isArmed()'
#  FAIL! aTrackThatMovedOntoAnArmedRouteCannotStopAnotherTracksCapture 'trackRecordArmed(owner)'
```

GREEN with the fix (same file, later moved — see "file-length" below):

```sh
cmake --build build --target zene ControlRecordingRoutesTest -j4   # build-3-green.log EXIT=0
cd build/tests && ctest -R ControlRecordingRoutesTest --output-on-failure   # test-4-green.log EXIT=0
```

Step 4b, reverse only the product files and run the test that is now shipped. The fix is committed,
so the patch is taken against its parent commit `3436a808b6` (a plain `git diff -- <files>` against
the worktree is empty once the fix is in the commit):

```sh
# RED, at the branch head (log 4b-rerun)
git diff 3436a808b6 HEAD -- include/TrackRecorder.h src/core/audio/TrackRecorder.cpp \
  src/core/ControlCommandsArrangementState.cpp > LOGS/fix-2.patch   # DIFF_EXIT=0, 3 files changed, 106 insertions(+), 18 deletions(-)
git apply -R LOGS/fix-2.patch                                       # tree back to the pre-fix product code
cmake --build build --target zene ControlArmReversibilityTest -j4    # build-11-red-reversed.log, "Built target ControlArmReversibilityTest"
cd build/tests && ctest -R '^ControlArmReversibilityTest$' --output-on-failure   # test-16-red-reversed.log TEST_EXIT=8
#  Totals: 3 passed, 2 failed:
#  FAIL! disarmingATrackAfterAnotherTracksRemovalStopsItsOwnCapture
#        '!recorder.track(route).isArmed()' at ControlArmReversibilityTest.cpp(171)
#  FAIL! aTrackThatMovedOntoAnArmedRouteCannotStopAnotherTracksCapture
#        'control::trackRecordArmed(control::resolveTrack(owner, &error))' at ...(218)
# GREEN, restored
git apply LOGS/fix-2.patch                                          # REAPPLY_EXIT=0, `git status` empty
cmake --build build --target zene ControlArmReversibilityTest -j4    # build-12-green-restored.log BUILD_EXIT=0
cd build/tests && ctest -R '^ControlArmReversibilityTest$' --output-on-failure   # test-17-green-restored.log TEST_EXIT=0, 100% passed
```

Re-run at the final commit (`bfc4285599`), same two commands, so the exit codes below belong to the
tree the proof manifest names:

```sh
cd <worktree> && git apply -R LOGS/fix-2.patch             # A_REVERSE_EXIT=0
cmake --build build --target zene ControlArmReversibilityTest -j4   # build-23-red.log A_BUILD_RED_EXIT=0
cd build/tests && ctest -R '^ControlArmReversibilityTest$' --output-on-failure   # test-31-red.log A_TEST_RED_EXIT=8
cd <worktree> && git apply LOGS/fix-2.patch                # A_RESTORE_EXIT=0
cmake --build build --target zene ControlArmReversibilityTest -j4   # build-24-green.log A_BUILD_GREEN_EXIT=0
cd build/tests && ctest -R '^ControlArmReversibilityTest$' --output-on-failure   # test-32-green.log A_TEST_GREEN_EXIT=0
```

The first, test-first RED (`build-2-red.log`, `test-3-red.log` EXIT=8, `build-3-green.log`,
`test-4-green.log` EXIT=0) is in the same log folder: the slots were written and run before any
product change, then again with it.

The surface's own query (`src/core/McuSurface.cpp`), same proof on its own patch:

```sh
git diff -- src/core/McuSurface.cpp > LOGS/mcu-fix.patch             # MCU_DIFF_EXIT=0
cmake --build build --target zene McuSurfaceTest -j4                  # build-20-mcu-fix.log BUILD_GREEN_EXIT=0
cd build/tests && ctest -R '^McuSurfaceTest$' --output-on-failure     # test-27-mcu-green.log MCU_GREEN_EXIT=0
git apply -R LOGS/mcu-fix.patch                                       # MCU_REVERSE_EXIT=0
cmake --build build --target zene McuSurfaceTest -j4                  # build-21-mcu-red.log BUILD_RED_EXIT=0
cd build/tests && ctest -R '^McuSurfaceTest$' --output-on-failure     # test-28-mcu-red.log MCU_RED_EXIT=8
#  FAIL! McuSurfaceTest::recFollowsTheTrackWhenAnotherTrackMovesOntoItsRow()
#        '!recorder.track(route).isArmed()' - "the surface read the owner's new row and never
#        stopped its capture"
git apply LOGS/mcu-fix.patch                                          # MCU_RESTORE_EXIT=0
cmake --build build --target zene McuSurfaceTest -j4                  # build-22-mcu-restored.log BUILD_RESTORED_EXIT=0
cd build/tests && ctest -R '^McuSurfaceTest$' --output-on-failure     # test-29-mcu-restored.log MCU_RESTORED_EXIT=0
```

(The first attempt at this block, `build-17..19`, did not compile the new slot — `TrackList` is a
`std::vector` and has no `indexOf`; the slot was changed to `std::find` and the runs above are the
ones that count. The stale binary made `test-24..26` pass, which is why they are not cited.)

Re-run at the final commit (`bfc4285599`) for the surface hunk:

```sh
cd <worktree> && git apply -R LOGS/mcu-fix.patch            # B_REVERSE_EXIT=0
cmake --build build --target zene McuSurfaceTest -j4        # build-25-red.log B_BUILD_RED_EXIT=0
cd build/tests && ctest -R '^McuSurfaceTest$' --output-on-failure   # test-33-red.log B_TEST_RED_EXIT=8
cd <worktree> && git apply LOGS/mcu-fix.patch               # B_RESTORE_EXIT=0
cmake --build build --target zene McuSurfaceTest -j4        # build-26-green.log B_BUILD_GREEN_EXIT=0
cd build/tests && ctest -R '^McuSurfaceTest$' --output-on-failure   # test-34-green.log B_TEST_GREEN_EXIT=0
```

Family (every test that touches the recorder, the arm verbs, the journal or undo):

```sh
cd build/tests && ctest -R '^(ControlRecordingRoutesTest|ControlArmReversibilityTest|ControlTrackRouteNoSongTest|MultiTrackRecorderTest|RecordingInputPathTest|UndoHonestyTest|RecordToolsNoEngineTest|RecordClipTest|RecordingCrashLossTest|TwoTrackRecordingHarness|RecordingPassTest)$' --output-on-failure
# test-14-family-rerun.log EXIT=0 — 100% tests passed, 0 tests failed out of 11
# test-18-family.log FAMILY_EXIT=0 — same 11 tests at the branch head, 100% passed (15.33 s)
# test-30-family-final.log FAMILY_EXIT=0 — the same family plus McuSurfaceTest: 12/12, 100% (17.00 s)
```

Gates:

```sh
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # gates-2.log EXIT=0
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # gates-4.log GATES_EXIT=0 at the branch head
#   PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace
#   PASS no-tautology / doc-figures / complexity / file-length — RESULT: PASS
```

An earlier gate run (`gates-1.log`, EXIT=1) failed `file-length` because the two tests first lived
in `tests/src/core/ControlRecordingRoutesTest.cpp` and pushed its CODE lines to 528. They were moved
to `ControlArmReversibilityTest.cpp` (the existing `track.set_arm` test, ~230 code lines) and the
restored `ControlRecordingRoutesTest.cpp` is unchanged in this branch; gates-2.log is that re-check.

## Self-check

- **The regression fails with the product change reverted, passes restored.** Re-verified at the
  branch head: `git diff 3436a808b6 HEAD -- <the three product files> > fix-2.patch` (DIFF_EXIT=0),
  `git apply -R fix-2.patch`, rebuild (`build-11-red-reversed.log`), `ctest -R
  '^ControlArmReversibilityTest$'` **TEST_EXIT=8** with both new slots failing on the bug's own
  symptom (`test-16-red-reversed.log`, assertions at `ControlArmReversibilityTest.cpp(171)` and
  `(218)`); then `git apply fix-2.patch` (REAPPLY_EXIT=0, tree clean), rebuild
  (`build-12-green-restored.log` BUILD_EXIT=0) and the same test **TEST_EXIT=0, 100% passed**
  (`test-17-green-restored.log`). On the final commit `bfc4285599` the same two patches were re-run:
  `A_TEST_RED_EXIT=8` (`test-31-red.log`), `A_TEST_GREEN_EXIT=0` (`test-32-green.log`),
  `B_TEST_RED_EXIT=8` (`test-33-red.log`, the surface slot) and `B_TEST_GREEN_EXIT=0`
  (`test-34-green.log`), all with build exits 0. The same log folder holds the test-first RED
  (`test-3-red.log`, EXIT=8, run before any product change). Pre-submit: `submit-4.log` RESULT:
  PASS, SUBMIT_EXIT=0 (re-run after the surface commit is `submit-5.log`).
- **Deterministic.** No sleeps, wall clock, threads or races in the tests: every state is driven
  through `ControlRegistry` commands (`track.add`, `track.set_arm`, `track.set_arm_many`,
  `track.move`, `track.remove`) and read back through the recorder's own arm flag and the take
  journal's existence. No timestamps are injected because none are needed: the test observes state,
  not time.
- **At the cause and complete — every caller and sibling checked.** `track.set_arm` (`setArm`),
  its batch sibling `track.set_arm_many` (delegates to `setArm`; the batch assertion in
  `aTrackThatMovedOntoAnArmedRouteCannotStopAnotherTracksCapture` proves it resolves the binding),
  the GUI query `control::trackRecordArmed`, `record.arm_track` (binds no track, unchanged),
  `record.disarm_track` and `record.disarm_all` (both go through `TrackRecorder::disarm()`, which
  clears the binding), `record.get_state` / `armReply` (report route state, not ownership —
  unchanged). The other `track.*` commands (`add`, `remove`, `move`, `sort`, `set_name`, …) do not
  touch the recorder at all: they are the operations that USED to detach the capture, which is why
  the binding, not a ban on moving tracks, is the fix. The hardware surface's own resolve
  (`McuSurface.cpp:75`, a copy of the mapping) now delegates to `control::trackRecordArmed`; a
  `grep isArmed` over the worktree shows no other reader of a record route's armed flag outside the
  recorder itself, `trackRecordArmed` and tests.
- **No collateral damage.** Nothing new is persisted (no XML attribute, no journal field), so old
  projects load and save exactly as before. A route armed by number keeps the old by-position
  behaviour. The only new refusal fires when a DISARM addresses an armed route bound to a *different*
  track — the exact case that used to stop another track's capture. Arming onto an occupied route
  keeps its existing `Busy` refusal. The one test this change touches that is red in this environment
  (`McuSurfaceFeedbackTest::recArmsTheTracksRecordRoute`, no writable working directory for its take
  file) is red with the fix reverted too — same slot, same assertion, BUILD exit 0 both times
  (`test-21-mcu-reverted.log`, `test-22-mcu-restored.log`).
- **Realtime/undo.** The binding is an `std::atomic<int>` written by `arm()`/`disarm()` and the two
  control commands; the audio thread never reads it, and `processInput*` is untouched. `track.set_arm`
  recorded no inverse before and records none now (`ControlArmReversibilityTest` still proves the
  typed `Irreversible` refusal), so undo stays one honest step and no control command was added.
- **Sibling commands in the changed command file** (`arrangement.get_state`, and the whole `track.*`
  group): consistent — `arrangement.get_state`/`trackEditState` report rows and ids only, never arm
  state, so they needed no change; within `track.*` only `set_arm`/`set_arm_many` address record
  routes, and both share one resolution now.
- **The other siblings the pre-submit check names** — `arrangement.fit_tracks`, `get_state`, `modes`,
  `modifier_tools`, `navigator`, `next_transient`, `nudge`, `ruler_lanes`, `scroll`, `select_clip`,
  `set_tool`, `zoom`, `zoom_back`, `zoom_forward`, `zoom_toggle`: all view, navigation or selection
  verbs. None reads or writes a record route, an arm flag or a track's recorder index (the only
  recorder reader in this file is `control::trackRecordArmed`, changed with `setArm`), so none is
  affected and none was touched.
- **Not verified here:** TSan does not run on this box, so thread safety is argued from the design
  (single-writer control-thread atomic, no audio-thread reader) rather than measured. The
  `ControlCommandsSnapshot` canary was not regenerated because no command id, schema or description
  changed.
- **The verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE:** the defect reproduces on this branch
  with the product change reverted (`test-10-red-reversed.log`, EXIT=8), so the buggy behaviour was
  live in this tree and the report's evidence is the fix, not a pre-existing guard.

## Other findings (not fixed here)

- `RecordToolsTest::theRemainingRecordTime` fails in this environment: `cannot measure the volume
  holding '/home/kruzzzzy/Documents/Zene Studio'` — that directory does not exist on this box
  (`ls` this session). The same pre-existing environment failure is recorded at
  `docs/qwen-fixes-2026-10/cluster-ci-0b372147c313d00a-port.md:136`. Not related to routes.
- `RecordingCrashLossTest::sigkillAtControlledPointsLosesLessThanTheBound` failed once while other
  builds ran (`test-12-family-final.log`, EXIT=8) and passed alone (`test-13`, EXIT=0) and in the
  family rerun (`test-14`, EXIT=0). It is a SIGKILL/grace-window test, load-sensitive by design.
- `McuSurfaceFeedbackTest::recArmsTheTracksRecordRoute` fails in this environment, and did so with
  the product change reverted too: the same slot, the same assertion
  (`'recorder.track(route).isArmed()' returned FALSE`) in `test-21-mcu-reverted.log` (fix reverted)
  and `test-22-mcu-restored.log` (fix restored), BUILD exits 0 both times. Its setup arms a route
  without a writable working directory (no temp dir, unlike `ControlArmReversibilityTest`), so the
  take file cannot be opened — the same environment failure as `RecordToolsTest` above. Not caused
  by this fix and not touched.
- Removing a track that is ARMED leaves its route writing (the binding now names a deleted
  `trk-<n>`; `record.disarm_track {route}` still stops it). Out of this finding's scope, but the
  honest state is visible: `record.routes_get_state` reports the route armed.
