ALREADY-FIXED

# Cluster 5173: liveness tokens do not protect modulation targets from concurrent destruction

Verdict: ALREADY-FIXED on branch `bugs/qwen-5173`. Two earlier commits replaced the check-then-use
window the finding describes with a pin-and-wait lifetime scheme. Both are ancestors of HEAD (round 1:
`git merge-base --is-ancestor <sha> HEAD` EXIT 0 for each):

- `73ecb52ba3 fix(core): pin modulation targets during audio writes (cluster 5173)`
- `97789a3fd0 fix(core): scope modulation teardown gates to target generations (cluster 5173)`

Round 2 (this round) changes no lifetime behaviour. Review round 1 found that the regression test did
not prove the fix: it passed with the writer wait deleted. Its interim repair (39982f8481) observed
the destroyer for a 250 ms wall-clock window, and the definition of done forbids wall-clock timing in
tests. This round replaces that window with a deterministic test seam, a production-null hook inside
`retireTarget`'s wait loop. It proves RED (wait loop deleted) and GREEN.

## Evidence (file:line on this branch)

- `include/ModulationLayer.h:418-435` has `beginTargetWrite(slot, token)`. On the audio thread it
  CAS-increments a writer count in `m_targetWriters[slot]`. The increment happens only while that
  atomic word still carries this token's generation and the closing bit is clear. It then re-checks
  `targetAlive`. It does not allocate, lock or wait. `:437-443` has `endTargetWrite`, which releases
  the pin.
- `src/core/ModulationLayerPublisher.cpp:309-327` is `applyModulationBlock`. It takes the pin at `:311`
  and holds it through an RAII guard, `TargetWritePin`. The model write at `:325`
  (`writeModulatedEntry(entry.model, ...)`) and the per-voice `entry.instrument->modulateNote` (through
  `applyVoiceEntry`) both run while the pin is held. `drivingSource` reads only the view's bytes and the
  token. It never dereferences the model.
- `src/core/AutomatableModel.cpp:184` emits `aboutToBeDestroyed()` at the entry of
  `~AutomatableModel`. `src/core/Instrument.cpp:78` and `src/tracks/InstrumentTrack.cpp:255` do the
  same for an instrument target. `publishView` connects these signals to `retireTarget`
  (`ModulationLayerPublisher.cpp:236-245`).
- `src/core/ModulationLayerPublisher.cpp:137-168` is `retireTarget`. It sets the closing bit with a CAS
  on the same atomic word, then waits (`:155-161`) until this generation's writer count reaches 0, and
  only then clears `m_alive`. Destruction continues only after it returns.
- The writer's increment and the destroyer's closing bit are read-modify-writes on one atomic word, so
  they are totally ordered. If the increment comes first, the destroyer waits for the write. If the
  closing bit comes first, `beginTargetWrite` fails and the entry is skipped. This is design reasoning:
  local TSan does not run on this box.

## Round 2 change: the deterministic regression

Product, test seam only. Production behaviour does not change:
- `include/ModulationLayer.h:500-505` (private, visible only to the friend `ModulationAudioViewTest`)
  declares `RetireWaitHook`, `s_retireWaitHook` and `announceRetireWait()`.
- `src/core/ModulationLayerPublisher.cpp:130-135` defines them. The hook is `nullptr` unless a test
  sets it.
- `:154,158`: inside `retireTarget`'s wait loop, `announceRetireWait()` is called once, on the
  destroying thread, when that thread starts waiting for a pinned write. It costs one atomic load and
  never runs on the audio thread.

Test: `tests/src/core/ModulationAudioViewTest.cpp:233-303`, `targetDestructionWaitsForAnInFlightAudioWrite`:
1. The audio write parks inside the pinned region (a `dataChanged` hook blocks on `resumeWrite`).
2. A destroyer thread deletes the model.
3. A second `aboutToBeDestroyed` slot is connected after the publisher's, so it runs only once
   `retireTarget` has returned. It records whether the parked write had already resumed.
4. The main thread waits on one semaphore. Either the wait hook releases it (a build that waits), or
   the post-retire slot does (a build that retired while the write was parked).
5. Assertions: the hook fired, retire had not returned while the write was parked, and retire
   returned only after the write resumed.

There are no sleeps and no clocks. `try_acquire_for(5s)` only bounds a hang: on a correct build the
hook fires because the write is pinned and parked. The 250 ms window from 39982f8481 is removed. The
assertions are stronger than the old `teardownEntered` check: the hook can only fire after the
closing bit is set, and `!targetAlive` is still asserted at the end.

## Commands (round 2; logs in `/home/kruzzzzy/.config/kads/zene/state/runs/5173.r2/logs`)

These are the runs whose output I read in this session (build-8 onward, test-13 onward, gates-2). The
red patch `logs/red-waitloop.patch` deletes the writer-wait loop in `retireTarget`
(`ModulationLayerPublisher.cpp:154-161`), which is the central hunk of the earlier fix.

| Step | Command | EXIT | Log / evidence |
|---|---|---|---|
| GREEN build | `cmake --build build --target ModulationAudioViewTest -j4` | 0 | build-8.log |
| GREEN | `cd build/tests && ctest -R ModulationAudioView --output-on-failure` | 0 | test-13.log, `1/1 Test #186: ModulationAudioViewTest ... Passed` |
| RED: remove the wait | `git apply logs/red-waitloop.patch` | 0 | |
| RED build | `cmake --build build --target ModulationAudioViewTest -j4` | 0 | build-9.log |
| RED | `cd build/tests && ctest -R ModulationAudioView --output-on-failure` | 8 | test-14.log: `FAIL!  : ModulationAudioViewTest::targetDestructionWaitsForAnInFlightAudioWrite() 'waitedForWriter && !retireReturnedWhileParked' returned FALSE. (the target was retired while the audio thread still held its raw pointer)` at `ModulationAudioViewTest.cpp(297)` |
| restore the wait | `git apply -R logs/red-waitloop.patch` | 0 | |
| GREEN again | `cmake --build build --target ModulationAudioViewTest -j4`; `ctest -R ModulationAudioView --output-on-failure` | 0; 0 | build-10.log, test-15.log `Passed` |
| 4b revert | `git diff -- include/ModulationLayer.h src/core/ModulationLayerPublisher.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 | |
| 4b RED | `cmake --build build --target ModulationAudioViewTest -j4` | 2 | build-12.log: `ModulationAudioViewTest.cpp:248:43: error: 's_retireWaitHook' is not a member of 'lmms::ModulationLayerPublisher'`. The test needs the seam, so reverting the seam stops it compiling. The meaningful RED is the wait-loop removal above. |
| 4b GREEN | `git apply logs/fix.patch`; `cmake --build build --target ModulationAudioViewTest -j4`; `ctest -R ModulationAudioView --output-on-failure` | 0; 0; 0 | build-13.log, test-17.log `100% tests passed out of 1` |
| header consumers | `cmake --build build --target zene ModulationLayerTest ModulationLayerValueTest ModulationLayerProjectRoundTripTest ModulationRateTest ModulatorToolsTest ControlModulatorCommandsTest -j4` | 0 | build-14.log. A full `cmake --build build -j4` is refused by the lane's build guard (EXIT 97, build-11.log: "build a named target"), so the targets are named. |
| family | `cmake --build build --target ModulatorPanelTest -j4`; `cd build/tests && ctest -R "Modulation\|Modulator" --output-on-failure` | 0; 0 | build-16.log; test-19.log `100% tests passed out of 8` (ModulationLayerValue, ModulationLayer, ModulationAudioView, ControlModulatorCommands, ModulationLayerProjectRoundTrip, ModulationRate, ModulatorTools, ModulatorPanel) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | gates-2.log, table below |

### Gates (gates-2.log, verbatim)

```
feature-check kads-5173 (base 3436a808b, 5 changed files; logs in /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5173/build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** With the fix's
  wait loop deleted it fails, ctest EXIT 8 (test-14.log). With the loop restored it passes, EXIT 0
  (test-13.log, test-15.log). Reverting this round's seam hunk (procedure 4b) stops the test
  compiling (build EXIT 2, build-12.log). Re-applying it builds and passes (test-17.log, EXIT 0).
- **The test is deterministic.** The seam is `ModulationLayerPublisher::s_retireWaitHook`, called from
  `retireTarget`'s wait loop. The synchronisation points are a `Model::dataChanged` park and an
  `aboutToBeDestroyed` slot that runs after retire returns. No sleeps, no `steady_clock`. The 5 s and
  2 s `try_acquire_for` bounds only keep a hang finite and never decide a pass.
- **The fix is at the cause and complete; callers and siblings checked.** Every raw-pointer write on
  the audio path is in `applyModulationBlock` and runs under the pin taken at
  `ModulationLayerPublisher.cpp:311`. Both teardown sources reach `retireTarget`:
  - the model, through `AutomatableModel.cpp:184` → `:236`;
  - the instrument, through `Instrument.cpp:78` and `InstrumentTrack.cpp:255` → `:242`.

  A rebind waits for writers in `publishView`'s own loop. That loop is a sibling with its own tests
  (`anEditWaitingForAnActiveWriter...`), and this round leaves it unchanged. The seam is the only
  product change. `ModulationLayer.h` gained a private static member, so `zene` and every
  Modulation/Modulator test target were rebuilt by name (build-14.log, build-16.log). The lane
  guard refuses a full build.
- **No collateral damage.** No saved data, format, limit, command or undo path is touched. In
  production the hook is null, so `announceRetireWait()` is one acquire load per retire wait, off the
  audio thread. Both files are fork sources (`tests/fork-sources.txt:1270`, `:2147`), so the
  registration lists need no change.
- **ALREADY-FIXED: the code that shows it.** `include/ModulationLayer.h:418-443`,
  `src/core/ModulationLayerPublisher.cpp:137-168` (with the wait at `:155-161`), `:236-245` and
  `:311-325`, and `src/core/AutomatableModel.cpp:184`.

## Other findings

- Round 1 could not produce the gate table: the baseline checkout in `qwen/gate-baselines/tree` was
  refused. In round 2 `feature_check.py` ran to completion (gates-2.log, EXIT 0).
- `retireTarget` busy-waits (yield) on the destroying thread. It relies on the `Q_ASSERT_X` at
  `src/core/AutomatableModel.cpp:180` that no target is destroyed on an audio worker, and that check
  compiles out in release builds. The earlier fix already had this property; it is not changed here.

Delegation: an earlier round-2 session wrote the seam and the test rewrite, and left them uncommitted
(captured as `5173.r2/pre-leftover.patch`). This session restored the patch, and I reviewed the
diff. Haiku (three starts) ran every build and ctest run: GREEN, RED with the wait loop deleted, the
4b revert/re-apply, the named header-consumer rebuild with the Modulation family, and feature_check.
I checked the FAIL/PASS lines in test-13/14/15 myself. Sonnet and DeepSeek were not needed: no new
code had to be written this session.
