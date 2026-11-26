FIXED

# 2994 — A dropped stop can leave the recorder permanently unlandable

Verdict: **FIXED** (the bug is real and the fix is in the pass that refused).

## The finding, checked against this tree

The pass refuses — consuming nothing — while a recorded start has no stop:
`src/core/ControlCommandsSessionRecordLand.cpp:109-120` builds the refusal, `validateLandRequest`
(`:275`) applies it before `consumeLandEvents` (`:483`) touches the ring. The recorder's ring is
bounded (`include/SessionArrangementRecorder.h:114`, `Capacity = 2 * 64`) and a push into a full ring
is dropped and counted (`:188`); neither a disarm nor a reset clears the ring
(`recorderDisarmKeepsThePerformance`, `tests/src/core/SessionArrangementRecordTest.cpp`). So a
performance whose ring overflows while a cell is playing loses the stop that would close that start,
and every later pass refuses on a start that can never be closed — the full ring is never drained
and the recording is lost.

Two earlier fixes narrowed that window and are present here: the ring is sized for one whole
worst-case performance (`include/SessionArrangementRecorder.h:114`,
`docs/bunny-fixes-2026-10-05/core/batch-06.md:53`, guarded by
`SessionArrangementRecordTest::recorderCapacityCoversAWorstCasePerformance:173`) and a retrigger
records the stop that closes the previous span before its next start
(`src/core/SessionFollow.cpp:229`, `docs/qwen-fixes-2026-10/cluster-2495.md:129`). Neither closes it:
a performance with more than one ring of traffic still strands its last starts. That is what the
regression test drives, and what the first RED run reproduced verbatim:
`2 recorded launch(es) have not stopped yet: (track 0, scene 0) from tick 1000, (track 1, scene 0)
from tick 1000 ... nothing was consumed.`

## The fix

The refusal now names a launch that is still **playing**: its stop is on its way and the ring has
room for it. A start in a **full** ring is not that — the stop that would close it has no room, the
ring never grows, only a pass drains it, and no later pass would see that stop either.

- `src/core/ControlCommandsSessionRecordLand.cpp:476-479` — the snapshot is bounded by the ring's own
  capacity (`MaxLandEvents`, `src/core/ControlCommandsSessionRecordInternal.h:71`), so a full snapshot
  *is* a full ring. The handler computes that and hands it to the validation gate.
- `:263-276` — the gate refuses only while `openCount > 0 && !ringFull`; the pair/content
  preflight (`validateLandPairs`) is unchanged and still runs before anything is consumed.
- `:350-358` — a stranded start is reported with `landed: false`, `reason: "stop_dropped"` and its
  recorded `start_tick`; no end is invented and no clip is made for it.
- `:508-516` — those entries are appended to the reply's `landed` array after the pairs and counted
  in `collapsed`, so every not-landed entry still falls in exactly one summary bucket. The pass then
  consumes the whole ring as before; the clips it makes are unchanged and stay under the one
  composite ProjectJournal checkpoint, so undo is still one step.

Nothing on the audio thread changed: `push()`/`pop()`/`snapshot()` and the producer in
`SessionFollow.cpp` are untouched, and the changed functions (`validateLandRequest`,
`stopDroppedEntry`, the handler) run on the model thread inside a command handler.

No command was added, no command id, description or schema changed, so `ControlCommandsSnapshot` and
its canary are untouched. `landed` is declared as a bare array in the committed snapshot
(`tools/mcp-zene-control/zene_control/commands_snapshot.json:29630`), so the new entry shape needs no
schema change. `session.arrangement_record_status`, the other caller of `landPairEvents`
(`src/core/ControlCommandsSessionRecord.cpp:133-141`), only reads the ring and is unchanged.

### Why the fix is not a producer-side reservation

The first attempt reserved ring capacity in `push()` (a start admitted only when a slot is free for
its stop). It breaks a pre-existing contract test that fills the ring with `Capacity` starts —
`tests/src/core/SessionArrangementRecordTest.cpp:157` — because the ring admits `Capacity` events of
any kind and any per-start reservation must refuse the last starts. That run is logged and red:
`test-2-green.log` (`EXIT=8`, `recorderRequiresArmingAndDropsWhenFull`). The capacity test is not mine to
weaken, so the fix moved to the consumer, where the ring's bounded contract is unchanged.

## Regression test

`tests/src/core/ControlSessionRecordLandTest.cpp:617` — `aRingThatFillsWhileCellsPlayStillLands`:
63 closed launches (126 events, no start open) leave two slots; two cells start and both stops are
dropped because the ring is now full (`QCOMPARE(recorder().dropped(), 2)`); one land pass must
succeed, land the 63 pairs as 63 clips, report the two stranded starts as `stop_dropped` (counted in
`collapsed`, no clip, appended after the pairs), drain the ring, and a second pass must answer
"nothing has been recorded".

Deterministic: events are pushed through the recorder's public API with explicit ticks, the engine's
dummy audio thread is stopped in `initTestCase` (`:104-110`), and the command is invoked through the
registry. No sleep, timer, thread or wall-clock value is involved.

## Commands and exit codes (this session)

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/2994.r1/logs/`.

Initial RED, with the test added and no product change:

```
cmake --build build --target zene ControlSessionRecordLandTest -j4      # build-1-red.log      EXIT=0
cd build/tests && ctest -R '^ControlSessionRecordLandTest$' --output-on-failure   # test-1-red.log  EXIT=8
    FAIL!  : aRingThatFillsWhileCellsPlayStillLands() 'landed.ok' returned FALSE.
             (2 recorded launch(es) have not stopped yet ... nothing was consumed.)
```

Producer-side attempt (rejected, kept as evidence):

```
cmake --build build --target zene ControlSessionRecordLandTest SessionArrangementRecordTest ...  # build-2-green.log EXIT=2
    (make: *** No rule to make target 'ControlSessionRecordTest'. Stop. - a target name I guessed wrong)
cmake --build build --target zene ControlSessionRecordLandTest SessionArrangementRecordTest \
        ControlSessionRecordLandUndoTest -j4                                # build-3-green.log EXIT=0
ctest -R '^(ControlSessionRecordLandTest|ControlSessionRecordLandUndoTest|SessionArrangementRecordTest)$'
                                                                           # test-2-green.log   EXIT=8
    FAIL!  : SessionArrangementRecordTest::recorderRequiresArmingAndDropsWhenFull()
             'recorder.recordStart(0, 0, static_cast<tick_t>(i))' returned FALSE. (the ring refused a slot)
```

Final fix:

```
cmake --build build --target zene ControlSessionRecordLandTest SessionArrangementRecordTest \
        ControlSessionRecordLandUndoTest -j4                                # build-4-green.log EXIT=0
ctest -R '^(ControlSessionRecordLandTest|ControlSessionRecordLandUndoTest|SessionArrangementRecordTest)$'
                                                                           # test-3-green.log   EXIT=0 (3/3)
```

4b — the test must catch the bug (`fn=logs/step-4b.log`, `logs/step-4b-green.log`):

```
git diff -- src/core/ControlCommandsSessionRecordLand.cpp > logs/fix.patch  # EXIT=0
git apply -R logs/fix.patch                                                # EXIT=0
cmake --build build --target zene ControlSessionRecordLandTest -j4         # build-5-red-reverted.log  EXIT=0
cd build/tests && ctest -R '^ControlSessionRecordLandTest$' --output-on-failure
                                                                          # test-4-red-reverted.log  EXIT=8
    FAIL!  : aRingThatFillsWhileCellsPlayStillLands() 'landed.ok' returned FALSE.
             (2 recorded launch(es) have not stopped yet ... nothing was consumed.)
git apply logs/fix.patch                                                   # EXIT=0 (re-applied at the worktree root)
cmake --build build --target zene ControlSessionRecordLandTest -j4         # build-6-green-restored.log EXIT=0
cd build/tests && ctest -R '^ControlSessionRecordLandTest$' --output-on-failure
                                                                          # test-5-green-restored.log EXIT=0
```

Honest note on the 4b script: its first form did `cd build/tests` and then `cd ..`, which lands in
`build`, so the re-apply and the second build ran from the wrong directory (`build-green EXIT=1`,
`SCRIPT-EXIT=99`). The worktree was verified with `git status` (product file still reverted), the
patch was re-applied from the worktree root (`EXIT=0`), and the green half was re-run as
`logs/step-4b-green.log` (`product-diff-present EXIT=0`, `build-green EXIT=0`, `test-green EXIT=0`).
No result in this report comes from the failed run.

Gates, final diff:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # gate-2-final.log EXIT=0
    PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace
    PASS no-tautology / doc-figures / complexity (no new item) / file-length (no new item)
```

## Test matrix

| path | what drives it | result |
| --- | --- | --- |
| normal | 1 pair lands at its ticks, notes and audio copied | `ControlSessionRecordLandTest` 18 cases, EXIT=0 |
| failure (the bug) | ring full with two open starts, both stops dropped | `aRingThatFillsWhileCellsPlayStillLands`, EXIT=0 with the fix, EXIT=8 without |
| refusal that must stay | start still playing, ring NOT full, nothing consumed | `aStillOpenLaunchRefusesAndConsumesNothing`, EXIT=0 |
| exhaustion | `Capacity` events of any kind, then a dropped 129th | `recorderRequiresArmingAndDropsWhenFull`, `recorderCapacityCoversAWorstCasePerformance`, EXIT=0 |
| reconfiguration | disarm keeps the ring; a disarmed tap still tapes the closing stop; reset records stops | `recorderDisarmKeepsThePerformance`, `recorderTapesTheStopThatClosesARecordedStart`, `aDisarmedTapStillClosesTheStartItRecorded` (not built in this tree — `SessionArrangementRecordTest` carries the first two), EXIT=0 |
| audio thread | the diff adds nothing to `push`/`pop`/`snapshot` or the producer; changed code is model-thread command handling | by inspection of `git diff` |
| platform | no new Qt API, no allocation, no platform-specific code; C++20 only | builds and runs as logged above |

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted:
  `test-4-red-reverted.log` EXIT=8 (`aRingThatFillsWhileCellsPlayStillLands`, refusal message
  quoted above). Restored: `test-5-green-restored.log` EXIT=0, and the family
  `test-3-green.log` EXIT=0 (3/3).
- **Deterministic.** No wall-clock timing, sleeps or races: the ring is filled through
  `SessionArrangementRecorder::recordStart/recordStop` with explicit ticks, the dummy audio device
  is stopped in `initTestCase`, and the pass is invoked through `ControlRegistry::invoke`.
- **Cause and completeness.** The refusal is the cause: it treated "no stop in the ring" as "still
  playing" for ever. It now refuses only while that reading is true (the ring has room for the
  stop). `validateLandRequest` has exactly one caller (`:478`), `stopDroppedEntry` reads the same
  `OpenStart` the refusal names, and the other `landPairEvents` caller
  (`session.arrangement_record_status`, `src/core/ControlCommandsSessionRecord.cpp:133-141`) is
  read-only and untouched. The producer (`include/SessionArrangementRecorder.h`) is byte-identical
  to its base version, so the ring's bounded contract is unchanged.
- **No collateral damage.** Old projects: the ring is in-memory only and nothing new is persisted,
  so save/load is untouched. The ring still admits `Capacity` events of any kind
  (`recorderRequiresArmingAndDropsWhenFull` EXIT=0). The reply only gains entries in an
  unconstrained array plus the `collapsed` count those entries belong to; no key, schema or command
  description changed, so `ControlCommandsSnapshot` is not moved.
- **Undo.** One pass still takes one composite `Track` checkpoint before the first clip
  (`checkpointLandTracks`), and a stranded start creates no journal entry, so one `control.undo`
  still reverses the pass exactly (`ControlSessionRecordLandUndoTest` EXIT=0).
- **Realtime.** Nothing added on the audio path: the diff touches only
  `src/core/ControlCommandsSessionRecordLand.cpp`, none of whose functions runs in `process()`.
  `push()`, `pop()`, `snapshot()` and `SessionFollow`'s producer are unchanged.
- **Sibling commands** (`session.*`, printed because this lane changed a `ControlCommands*` file):
  `arrangement_record_arm` and `back_to_arrangement` are untouched; `arrangement_record_status`
  still reports `open_starts`/`dropped`/`landable_pairs` from the same pairing and is read-only;
  `stop_all`, `set_grid`, `set_slot` are untouched. Only `arrangement_record_land`'s internal gate
  and its reply entries changed.
- **ALREADY-FIXED / FALSE-POSITIVE evidence: not applicable.** The verdict is FIXED; the finding
  reproduced on this tree (`test-1-red.log` EXIT=8, `test-4-red-reverted.log` EXIT=8), so there is
  no claim to support with a code reference.
- **Known limitation, stated rather than hidden.** The command's `description` still says
  "REFUSED ... while any recorded start is still open". That text lives in
  `commands_snapshot.json:29607`, so changing it would move the ControlCommandsSnapshot canary for a
  word-only edit; I left it and recorded the precise rule in the design doc
  (`docs/SESSION-ARRANGEMENT-RECORD.md:131-139`) and in the file and gate comments. A reviewer who
  wants the description re-worded should regenerate the snapshot with it.
- **Not built in this tree:** `SessionTransportBarTest` and `SessionSchedulerTest` have no binary
  here, so I could not run them. The transport bar's assertion is the refusal message for a
  non-full ring, which this change does not touch.

## Other findings

- `docs/SESSION-ARRANGEMENT-RECORD.md` still says "64 events between two command calls" and "a full
  32-cell performance fits"; the ring has held `2 * MaxActiveSlots = 128` since
  `docs/bunny-fixes-2026-10-05/core/batch-06.md`. Stale doc figure, outside this bug.
- `build-2-green.log` `EXIT=2` was my own wrong target name (`ControlSessionRecordTest` does not
  exist); recorded here so the log set is not left with an unexplained failure.
