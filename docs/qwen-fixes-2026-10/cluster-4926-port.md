# Cluster 4926-port: Back-to-Arrangement and the Arrangement Record tap's stops

**Verdict: ALREADY-FIXED**

The port of cluster 4926 is not needed on this line: the newest line already
carries the mechanism that keeps the finding's failure mode from happening,
and the branch's own regression - ported here verbatim - is GREEN against the
unmodified tree and RED the moment that mechanism is removed.

## The finding, and where it pointed

> BUG 2: Back-to-arrangement disables recording before reset stops are captured.
> The scheduler reset is queued after the recorder is disarmed. When the audio
> thread later processes the reset, its stop events may not be recorded. Arm
> recording, launch a cell, then call `session.back_to_arrangement`. The start
> remains open in the ring, so landing refuses for lack of a stop.

The code the finding names is still exactly as filed on this line:

- `src/core/ControlCommandsSessionRecord.cpp:187` - `recorder.setArmed(false);`
- `src/core/ControlCommandsSessionRecord.cpp:188` -
  `Engine::getSong()->sessionScheduler().reset();`
- the reset is CARRIED OUT on the audio thread, later, in
  `SessionScheduler::consumeResetRequest()` (`src/core/SessionFollow.cpp:437`),
  and THAT is where the performance's stops are taped:
  `m_recorder.recordStop( slot.track, slot.scene, m_positionTicks )`
  (`src/core/SessionFollow.cpp:458`).
- the land pass refuses a performance with an open launch:
  `src/core/ControlCommandsSessionRecordLand.cpp:249` ->
  `validateOpenStarts()` (`src/core/ControlCommandsSessionRecordLand.cpp:105`,
  message at `:114`).

So the ORDER the finding describes is still the order in the code. What changed
is that the disarmed tap no longer drops the closing stop.

## Why the finding was REAL where it was filed, and is NOT real here

`bugs/qwen-4926` was based on train-9 (`b61e5f965`). The newest line this port
branch is cut from carries a LATER fix for the same class of defect:

    1c0554dc6  fix(session): tape the stop that closes a recorded start (cluster new-p16-212)

`git merge-base --is-ancestor 1c0554dc6 HEAD` -> **YES** (it is on this line),
and `git merge-base --is-ancestor 1c0554dc6 b61e5f965` -> **NO** (the old
branch base did not have it). That is also why the port was needed at all: the
old branch had to fix the defect ITSELF, in `include/SessionArrangementRecorder.h`
- the very file whose newer content made the merge conflict.

The mechanism, on this line, in `include/SessionArrangementRecorder.h`:

- `:261-268` - `shouldRecord()`: a START is taped only while the tap is armed;
  a STOP is taped while the tap is armed **and also while it is DISARMED when
  it closes a span the ring already carries**.
- `:276-290` - `closesRecordedStart()`: the ring's own bounded pass that
  decides whether a stop closes a start the ring already holds.
- `:170-175` - `push()` gates on `shouldRecord()` (not on `armed()`).

So the disarm at `ControlCommandsSessionRecord.cpp:187` no longer loses the
reset's stops: the stop of a launch that the ring already carries a start for
IS taped, even though the tap was switched off before the reset was carried
out. The launch is therefore a PAIR again, and the land pass has nothing open
to refuse. The finding's exact symptom - "the start remains open in the ring,
so landing refuses for lack of a stop" - cannot occur on this line.

This is the same conclusion the newest line reached by a different route, and it
is WIDER than the old branch's fix: it also covers a tap that is switched off
by any other route (a manual disarm, `session.arrangement_record_arm` with
`armed=false`), not only the one the Back-to-Arrangement switch asks for.

## The regression test (ported, and it is the branch's own)

`tests/src/core/ControlSessionRecordLandTest.cpp` -
`backToArrangementLeavesThePerformanceLandable()`: the branch's final version
(the PAIR-asserting one from `80dc660b2`), ported as-is, with the deterministic
fixture correction made in QA (below).

It drives the real command and the real scheduler, with no wall-clock timing:
`initTestCase()` stops the `AudioDummy` render thread that `Engine::init(true)`
starts, the clock is injected (`ctx.positionTicks`), the test drives each
period itself (`scheduler.processAudio(ctx, 16)`), and the ring is read through
the recorder's own snapshot.

1. launch a cell (`requestLaunch`) and run one period: the launch is taped and
   the cell is still playing;
2. run a second period at tick 96, so the performance is a span;
3. run `session.back_to_arrangement` through the registry;
4. drive one more period: the scheduler carries out the reset, and the stop of
   the launch it ends is taped with it;
5. the tap must be over (`recorder().armed()` is false);
6. the ring must hold the PAIR - one start, one stop - and the land pass must
   succeed and write one clip.

The tick is deliberately NOT asserted, because the finding concerns the
MISSING stop, not its exact tick; what the defect broke was the PAIR.

### The QA correction: one producer on the scheduler

QA found a real defect in the first submission of this regression: it drove
`SessionScheduler::processAudio()` BY HAND while `Engine::init(true)` had
started the `AudioDummy` render thread, and that thread reaches the SAME
scheduler (`src/core/Engine.cpp:90` starts processing, `include/AudioDummy.h:102-114`
renders the periods, `src/core/Song.cpp:474` calls the scheduler). Two producers
on one scheduler means the events land on whichever thread got there first -
nondeterministic.

The fixture now stops the dummy device before any test drives the scheduler:

```cpp
Engine::init(true);
Engine::audioEngine()->audioDev()->stopProcessing(); // This test drives scheduler periods explicitly.
```

That is the repo's own idiom for a test that drives the periods itself (see
`tests/src/core/ArrangeToolsTest.cpp:216`,
`tests/src/core/AudioBusPlayHandleQueueTest.cpp:158`,
`tests/src/core/AudioClipLoopTest.cpp:128`). With the dummy thread stopped,
the test's periods are the ONLY ones the scheduler sees, so the regression is
serial and deterministic - and the tick it does not assert no longer matters.

### RED: the defect, re-created on this line

The proof of "this test catches the bug" on a line where the bug is already
fixed is to remove the newest line's mechanism and watch the test fail. The
mechanism removed is the closing-stop rule only - the gate in `push()` goes
back to the pre-`1c0554dc6` form (`if( !armed() )` instead of
`if( !shouldRecord( event ) )`), which is EXACTLY the state the old branch base
was in:

```text
cmake --build build --target zene ControlSessionRecordLandTest -j$(nproc) > logs/build-r2-red.log 2>&1; echo BUILD_EXIT=$?
   -> BUILD_EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R "ControlSessionRecordLandTest" --output-on-failure > logs/test-r2-red.log 2>&1; echo TEST_EXIT=$?
   -> TEST_EXIT=8
```

with exactly the reported symptom in the log:

```text
FAIL!  : ControlSessionRecordLandTest::backToArrangementLeavesThePerformanceLandable() Compared values are not the same
   Actual   (count)         : 1
   Expected (std::size_t{2}): 2
   Loc: [../../tests/src/core/ControlSessionRecordLandTest.cpp(448)]
FAIL!  : ControlSessionRecordLandTest::aDisarmedTapStillLandsThePerformanceItRecorded() 'recorder().recordStop( 0, 0, 192 )' returned FALSE. ()
```

The ring held the START ALONE - the finding's "the start remains open in the
ring". The second failure is the newest line's OWN pre-existing test for the
same rule, which fails with the rule removed: the two are the same defect.

### GREEN: the tree as it stands

The mechanism was restored (`cp /tmp/rec_hdr_r2_backup.h include/SessionArrangementRecorder.h`,
`git diff` clean for the header) and rebuilt:

```text
cmake --build build --target zene ControlSessionRecordLandTest -j$(nproc) > logs/build-r2-green.log 2>&1; echo BUILD_EXIT=$?
   -> BUILD_EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R "ControlSessionRecordLandTest|SessionSchedulerTest|SessionFollowTest|SessionArrangementRecordTest" --output-on-failure > logs/test-r2-green.log 2>&1; echo TEST_EXIT=$?
   -> TEST_EXIT=0   (4/4 passed)
```

### Determinism

After the QA fixture correction (the dummy render thread stopped), the ported
case was run 15 times in a row, on its own:

```text
for i in $(seq 1 15); do QT_QPA_PLATFORM=offscreen ./ControlSessionRecordLandTest \
    backToArrangementLeavesThePerformanceLandable; done
   -> LOOP_FAILS=0 of 15
```

and the four related session tests together:

```text
QT_QPA_PLATFORM=offscreen ctest -R "ControlSessionRecordLandTest|SessionSchedulerTest|SessionFollowTest|SessionArrangementRecordTest" --output-on-failure > logs/test-r2-2.log 2>&1
   -> TEST_EXIT=0, 4/4 passed
```

## What was NOT ported, and why

The old branch's fix (a9cdedfa6 + ccf8e491c) added a DEFERRED-END PROTOCOL to
the recorder: an ARM EPOCH, `endTapWhenResetIsProcessed()` (model thread),
`endTapAfterReset()` (audio thread), an atomic state word that moves the arm
flag and the epoch together, and a test seam
(`setDeferredEndBoundaryHookForTest`). Porting it would mean:

- changing the tap's lifecycle (the tap would stay ARMED after
  `session.back_to_arrangement` until the reset is processed - an observable
  change to what `session.arrangement_record_arm` and the `armed` state report),
- replacing the newest line's hand-off (`setArmed` + the consumer's
  `waitForProducer()` + the closing-stop rule, cluster 2495 and new-p16-212)
  with a different one,
- for a defect that no longer reproduces on this line.

Porting it would change behaviour OUTSIDE the bug on a line that already
behaves correctly, so this port ships the REGRESSION (the guard) and the
verdict, not the mechanism. `grep -rn "endTapWhenResetIsProcessed\|endTapAfterReset\|armEpoch" src include tests`
-> **no hits**: the old protocol is not in this tree and nothing needs it.

## Evidence table

| Check | Exact command | Exit / result |
| --- | --- | --- |
| Baseline (unmodified tree) | `cmake --build build --target ControlSessionRecordLandTest -j$(nproc) > logs/build-1.log 2>&1` | `BUILD_EXIT=0` |
| Baseline test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "ControlSessionRecordLandTest" --output-on-failure > logs/test-1.log 2>&1` | `TEST_EXIT=0`, 1/1 passed (the ported test is GREEN with NO product change) |
| Round 2 build (fixture corrected) | `cmake --build build --target zene ControlSessionRecordLandTest -j$(nproc) > logs/build-r2-1.log 2>&1` | `BUILD_EXIT=0` |
| Round 2 focused test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "ControlSessionRecordLandTest" --output-on-failure > logs/test-r2-1.log 2>&1` | `TEST_EXIT=0`, 1/1 passed |
| Round 2 related tests | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "ControlSessionRecordLandTest\|SessionSchedulerTest\|SessionFollowTest\|SessionArrangementRecordTest" --output-on-failure > logs/test-r2-2.log 2>&1` | `TEST_EXIT=0`, **4/4 passed** |
| RED (the newest line's closing-stop rule removed) | `cmake --build build --target zene ControlSessionRecordLandTest -j$(nproc) > logs/build-r2-red.log 2>&1` | `BUILD_EXIT=0` |
| RED test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "ControlSessionRecordLandTest" --output-on-failure > logs/test-r2-red.log 2>&1` | `TEST_EXIT=8`, 2 failures, ring holds 1 event (the start alone) |
| GREEN (mechanism restored) | `cmake --build build --target zene ControlSessionRecordLandTest -j$(nproc) > logs/build-r2-green.log 2>&1` | `BUILD_EXIT=0` |
| GREEN test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "ControlSessionRecordLandTest\|SessionSchedulerTest\|SessionFollowTest\|SessionArrangementRecordTest" --output-on-failure > logs/test-r2-green.log 2>&1` | `TEST_EXIT=0`, 4/4 passed |
| Determinism (after the fixture correction) | 15 x `QT_QPA_PLATFORM=offscreen ./ControlSessionRecordLandTest backToArrangementLeavesThePerformanceLandable` | 0 failures of 15 |
| Affected family | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "Session\|Arrangement\|ControlSession" --output-on-failure > logs/test-r2-family.log 2>&1` | `TEST_EXIT=8`; **31 of 32 ctest targets passed**; the one failure is `AbletonLinkSessionTest` (SEGFAULT in a multicast-network test) and is PRE-EXISTING: with the ported test removed from the tree the same target still crashes (`logs/test-ableton-pristine-1.log`, `PRISTINE_EXIT=8`), and that binary does not contain the ported file |
| Gates, before the manifest re-derivation | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gate-1.log 2>&1` | `GATE_EXIT=1`: `fork-sources` and `all-sources-reproduce` FAIL, the other SIX rows PASS (`no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length`) |
| Gates, baseline (PRISTINE tree, 0 changed files) | same command, `> logs/gate-pristine-1.log 2>&1` | `GATE_PRISTINE_EXIT=1`: the SAME two rows FAIL - the drift is the train's own `plugins/SfzPlayer/sfizz/external/**` manifest entries, untouched by this port |
| Gates, FINAL | same command, `> logs/gate-2.log 2>&1` | `GATE_EXIT=0`, **RESULT: PASS** - all eight rows PASS (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length`) |

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4926-port.r1/logs/`.

### The two red gate rows, and what this port did about them

They were red on the TRAIN, not because of this port: with the tree PRISTINE
(0 changed files) the same two rows fail (`logs/gate-pristine-1.log`). Both had
ONE cause - `tests/all-sources.txt` did not match its own published recipe:

- the train commit `07d6d02bd` ("all-sources re-derived (automatic merge
  train)") regenerated the manifest with a broader command than the recipe in
  the manifest's own header, so the entry list carried **1148 VENDORED paths**
  (`plugins/SfzPlayer/sfizz/external/**`) that the recipe excludes ON PURPOSE
  ("Vendored third-party trees are excluded on purpose - they are not this
  repo's code");
- the recipe derives NOTHING that the manifest is missing (0 paths), so the
  drift is exactly those 1148 extra entries - and `bash
  tests/all-sources-reproduce.sh` -> `EXIT=1` ("does not match the recipe"),
  `bash tests/fork-sources-gate.sh` -> `EXIT=1` (same cause).

The gate's own message gives the repair ("Fix by re-deriving the list: run the
'Regenerate with' command from the manifest's header and write its output over
the entry list (never hand-edit, never delete a path the recipe derives)"),
and the repo has a precedent for it: `a71fcd6f1` ("chore(tests): re-derive
the whole-tree scope manifest from its own recipe (cluster new-p14-199-port)").
This port did exactly that, in its own commit `1a3e91e36`: the header is
untouched, the entry list is the recipe's byte-exact output, and the two rows
are green. NO gate limit, baseline or threshold was changed, and
`tests/fork-sources.txt` was NOT touched (it reproduces on its own - the
fork-sources gate failed only because it also runs the all-sources check).

## Files changed by this port

- `tests/src/core/ControlSessionRecordLandTest.cpp` - the ported regression
  `backToArrangementLeavesThePerformanceLandable()` and the QA fixture
  correction (the `AudioDummy` render thread stopped in `initTestCase()`, so
  the test is the scheduler's only producer) (a test file, already
  registered in `tests/fork-sources.txt:2363`; no new file, no registration
  change, no product change).
- `tests/all-sources.txt` - the whole-tree scope manifest re-derived from its
  OWN recipe, because the train's regeneration had left it carrying 1148
  vendored paths the recipe excludes (the gate's own repair instruction, with
  the repo's precedent `a71fcd6f1`). Header untouched, entry list only; no
  gate limit or baseline changed.
- `docs/qwen-fixes-2026-10/cluster-4926-port.md` - this report.

No product file is changed: `include/SessionArrangementRecorder.h`,
`src/core/ControlCommandsSessionRecord.cpp` and `src/core/SessionFollow.cpp`
are byte-identical to HEAD.

## Self-check

**The regression test FAILS with your product change reverted and PASSES with
it (exit codes).**
There is no product change to revert: the newest line already carries the fix
(`1c0554dc6`). The equivalent proof was run in the only meaningful direction -
the newest line's mechanism was removed (`push()`'s gate changed from
`shouldRecord()` back to `armed()`), the tree rebuilt
(`BUILD_EXIT=0`) and the test run: `TEST_EXIT=8`, the ring held 1 event instead
of 2 (the start alone), i.e. the finding's exact symptom. The mechanism was
then restored and the tree rebuilt (`BUILD_EXIT=0`): `TEST_EXIT=0`, 1/1 passed.
Both logs are quoted above.

**The test is deterministic: no wall-clock timing, sleeps or races (name the
seam or injected values it uses).**
The clock is INJECTED (`ctx.positionTicks`, `ctx.ticksPerBar`,
`ctx.framesPerTick`, `ctx.transportRunning`). The ONE concurrency hazard is
removed in the fixture: `initTestCase()` stops the `AudioDummy` render thread
that `Engine::init(true)` starts, because that thread reaches the SAME
scheduler (`src/core/Engine.cpp:90`, `include/AudioDummy.h:102-114`,
`src/core/Song.cpp:474`) and would make the test's own periods a SECOND
producer - that was the one blocking finding of the round-1 QA review. With
the dummy thread stopped, the test's periods are the only ones the scheduler
sees, so the reset is carried out serially at the injected clock. The ring is
read through its own `snapshot()`; there is no sleep, no timer and no
concurrent scheduler producer. The test counts the pair, which is the failure
mode; the tick it deliberately does not assert no longer matters. After the
fixture correction: 15 consecutive runs, 0 failures.

**The fix is at the cause and complete: every caller and sibling of what you
changed was checked and stays consistent.**
Nothing product-side changed. The readers of the recorder's state were checked
anyway: `session.arrangement_record_arm` (the sibling of the switch, and the
only other writer of the arm flag - `src/core/ControlCommandsSessionRecord.cpp:96`),
`session.stop_all` (which ends the session WITHOUT ending the tap, and is
unaffected), the land pass (`src/core/ControlCommandsSessionRecordLand.cpp:249`
-> `:105`), and the reset path (`src/core/SessionFollow.cpp:437` -> `:458`).
The closing-stop rule (`include/SessionArrangementRecorder.h:261-290`) is
consistent with all of them: a disarmed tap still closes what it opened, and
records nothing new.

**No collateral damage: old projects and saved data still load as before, and
any limit or check you added does not hit other callers that legitimately
exceed it.**
No product file changed, so no saved data, project format, limit or check
changed. The ported test adds no limit and no check to production code. The
only non-test change outside the report is `tests/all-sources.txt`, whose
entry list was re-derived from its OWN published recipe: the paths REMOVED are
the 1148 vendored third-party files the recipe excludes on purpose, and no
path the recipe derives was dropped (the diff is 1148 deletions and 0
additions) - the whole-tree scope measures exactly what its own header says it
measures. No gate limit, baseline or threshold was changed.

**If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
the intended contract that shows it.**
- `include/SessionArrangementRecorder.h:261-268` (`shouldRecord`) and
  `:276-290` (`closesRecordedStart`) - the rule that tapes the closing stop of
  a disarmed tap, introduced by `1c0554dc6` (cluster new-p16-212), which IS an
  ancestor of HEAD (`git merge-base --is-ancestor 1c0554dc6 HEAD` -> exit 0).
- `include/SessionArrangementRecorder.h:170-175` - `push()` gates on that
  rule.
- `src/core/SessionFollow.cpp:437` / `:458` - the reset path, on the audio
  thread, tapes the stop that ends each playing slot.
- `src/core/ControlCommandsSessionRecordLand.cpp:249` -> `:105` - the land
  pass refuses only an OPEN launch; with the stop taped, nothing is open.
- The contract of the command itself: `session.back_to_arrangement`'s own
  description says the session stops "and the performance can still be
  landed" (`src/core/ControlCommandsSessionRecord.cpp:160-166`), and the ported
  regression shows the land pass SUCCEEDING with one clip.

**Sibling commands in the command files you changed.**
No command file changed (`src/core/ControlCommandsSessionRecord.cpp` is
untouched), so the sibling list does not apply. For the record, the siblings of
the `session.*` group were reviewed against this verdict:
`session.arrangement_record_arm` (the only other writer of the arm flag -
it still arms and disarms the tap directly, and the newest line's rule makes
even a manual disarm landable), `session.stop_all` (ends the session without
ending the tap - the tap is the caller's, as the arm command's description
says), `session.arrangement_record_land` (the consumer of the ring) and
`session.arrangement_record_status`. None of them reproduces the finding's
symptom on this line.

**Realtime, undo and commands.**
No product code was added or changed: nothing new runs on the audio thread, no
allocation, no lock, no wait, no logging, no signal. The ported test changes
no undo behaviour and adds no control command (the `session.*` id set is
unchanged; `tools/mcp-zene-control/zene_control/commands_snapshot.json` is
untouched).
