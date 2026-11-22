FIXED

# Cluster 4919: chain.receive clamped an out-of-range level instead of refusing it

## Decision

REAL. `chain.receive` accepted any number as `level` and silently clamped it to 0..2, so a caller asking for 4.0 got a send at 2.0, and -0.5 got 0.0. The command's contract says the level is 0..2, so an out-of-range value should be refused.

Evidence in the base code (before the fix):

- `src/core/ControlCommandsW3Engine.cpp:218`: `const double level = args.value("level").toDouble(1.0);` (the level read; an omitted level defaults to 1.0).
- `src/core/ControlCommandsW3Engine.cpp:219-223`: a non-finite level (NaN, +inf, -inf) is refused with `InvalidArgs`. The comment at :219 explains that `std::clamp` passes NaN through.
- `src/core/ControlCommandsW3Engine.cpp:224`: `receive.level = std::clamp(level, 0.0, 2.0);`. This is the silent clamp. It runs before any refusal for a finite out-of-range value.
- Contract: `src/core/ControlCommandsW3Engine.cpp:295` says `'level' (0..2, 1 when left out)`. The command definition is at :292-300.
- Schema: `src/core/ControlVocabulary.cpp:80-83` (`numberProperty()`) returns `{"type": "number"}` with no bounds, so the schema accepts any number and the range check has to be in the handler.
- Sibling refusal: `src/core/ControlCommandsMixerRoutes.cpp:86-88` (`validSendAmount`: 0..2) and its use at :139-142, which refuses with `InvalidArgs` and the message "a send is 0..2". Sends refuse out-of-range values; the receive did not.

## Fix

`src/core/ControlCommandsW3Engine.cpp:224-228` now refuses a finite level outside 0..2 with `ControlErrorKind::InvalidArgs` and the message "level N is out of range: a receive level is 0..2", before any mutation. Then `receive.level = level;` (:228) stores the validated value without a clamp.

The full diff is in `fix.patch` (log dir of run 4919.r1). It is 6 lines of product code (`+5 -1`, net `+4`). The commit that carries the fix is `08159e0b72`; `fix-final.patch` in the same log dir is the diff of that commit.

## Why the fix is complete

- The only reader of the level is `receive.level` at `ControlCommandsW3Engine.cpp:228`, and the only consumers of `receive` are `makeReceive` (:229) and its undo/redo lambda (:231). All three now see the validated value. The `makeReceive` level write at :150-152 is the one that reaches the device parameter.
- An omitted level still defaults to 1.0 (`toDouble(1.0)` at :218), so the no-level call is unchanged.
- A refusal returns at :224-227, before `makeReceive` (:229) and before `addUndoStep` (:231). So a refused call creates no send, no device, and no undo step.
- No header, API, or schema changed.
- The success reply is `target`, `from`, `plugin`, `tap_point` and `send_created` (`ControlCommandsW3Engine.cpp:233-235`). It still does not include the applied level, and this fix does not add one (outside this cluster's scope). For an accepted value the applied level is the requested one: `receive.level = level` (`:228`) stores it unchanged. The silent clamping came from the old `std::clamp` (`:224`), which changed out-of-range values. The mandated range refusal (`:224-227`) removes that symptom: an in-range value is never coerced, and an out-of-range value is refused, not replaced with 0 or 2.

## Test matrix

Test file: `tests/src/core/W3EngineTest.cpp`.

| Case | Test and line | Expected |
|---|---|---|
| Valid levels 0.0, 0.5, 1.0, 2.0 accepted. Each: `send_created`, sidechain send exists, one Receiver added, one undo, send gone, device count restored | `aChannelReceivesAnotherInsideItsChain`, :510-527 (loop over the four levels, same assertions) | ok for each level; r2 table below |
| Refused: 4.0, 2.5, -0.5, NaN, +inf, -inf | `aReceiveRefusesALevelOutsideItsRange`, :529-541 | `InvalidArgs`; no sidechain send; device count unchanged |
| Omitted level | Unchanged path, default 1.0 at :218 | Not separately tested in this cluster |
| Existing send is kept as it is | Code comment at `ControlCommandsW3Engine.cpp:216-217`; not separately tested in this cluster | Unchanged by this fix |
| Control thread only | `chainReceive` is a control-command handler | No audio-path code touched |

The refusal test contains an undo branch (:537) that runs only if a call wrongly succeeds (a RED run). It keeps the device count check so a leftover receive still fails the test.

## RED / GREEN evidence

Run directory: `/home/kruzzzzy/.config/kads/zene/state/runs/4919.r1/logs/`.

### Round 2 (r2): valid levels 0.0, 0.5, 1.0, 2.0 and RED/GREEN

What changed in r2:

- `tests/src/core/W3EngineTest.cpp`, `aChannelReceivesAnotherInsideItsChain` (:510-527): the successful receive now loops over 0.0, 0.5, 1.0 and 2.0. Each pass keeps the original assertions: `run` ok, `send_created` true, sidechain send present, device count +1, `control.undo` ok, send gone, device count restored. The two wrong-target checks (`ch(1)`, `trk-1`) are a loop with the same `InvalidArgs` assertion.
- `tests/src/core/W3EngineTest.cpp`, comment in `aReceiveRefusesALevelOutsideItsRange` (:531): corrected. The old comment said the clamp "passed NaN through". The new comment says a non-finite level is refused before the clamp, which matches `ControlCommandsW3Engine.cpp:220-223` (before the clamp at `:224`).
- This report.
- No other file changed in r2. Product code is unchanged since `08159e0b72` for the fixed file: `git log 08159e0b72..HEAD -- src/core/ControlCommandsW3Engine.cpp` lists no commit. The branch HEAD also contains merges of other clusters that change six other `src/` files (for example `src/core/Mixer.cpp`). Those changes are not from this cluster and were not made in r2.

Commands and exit codes. Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/4919.r2/logs/`. The commands below are the exact eight commands of the p3 run, in order. Recorded exits: patch 0, reverse/apply 0, red build 0, red ctest 8, re-apply 0, green build 0, green ctest 0, family 0. The build and test commands append `EXIT=` to their logs, so the EXIT value is the last line of each of those logs. The patch, reverse-apply and re-apply commands (steps 1, 2 and 5) are not appended to any log: their `EXIT=0` was printed by the shell during the p3 run. Both builds recompiled `ControlCommandsW3Engine.cpp.o` (`build-3-red.log`, `build-4-green.log`).

```
RUN=/home/kruzzzzy/.config/kads/zene/state/runs/4919.r2/logs
WT=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-4919
cd $WT && git diff 08159e0b72^ 08159e0b72 -- src/core/ControlCommandsW3Engine.cpp > $RUN/fix.patch
cd $WT && git apply --check -R $RUN/fix.patch && git apply -R $RUN/fix.patch
cd $WT && cmake --build build --target zene W3EngineTest -j4 > $RUN/build-3-red.log 2>&1; rc=$?; echo EXIT=$rc >> $RUN/build-3-red.log; echo EXIT=$rc
cd $WT/build/tests && ctest -R W3EngineTest --output-on-failure > $RUN/test-3-red.log 2>&1; rc=$?; echo EXIT=$rc >> $RUN/test-3-red.log; echo EXIT=$rc
cd $WT && git apply $RUN/fix.patch
cd $WT && cmake --build build --target zene W3EngineTest -j4 > $RUN/build-4-green.log 2>&1; rc=$?; echo EXIT=$rc >> $RUN/build-4-green.log; echo EXIT=$rc
cd $WT/build/tests && ctest -R W3EngineTest --output-on-failure > $RUN/test-4-green.log 2>&1; rc=$?; echo EXIT=$rc >> $RUN/test-4-green.log; echo EXIT=$rc
cd $WT/build/tests && ctest -R 'W3EngineTest|ReceiverEffectTest|PhaseFChannelScaleTest' --output-on-failure > $RUN/test-5-family.log 2>&1; rc=$?; echo EXIT=$rc >> $RUN/test-5-family.log; echo EXIT=$rc
```

| Step | Log | Result | EXIT |
|---|---|---|---|
| RED build, fix reverted in the worktree | `build-3-red.log` | `Built target W3EngineTest`; `ControlCommandsW3Engine.cpp.o` recompiled | 0 |
| RED run | `test-3-red.log` | `FAIL!  : W3EngineTest::aReceiveRefusesALevelOutsideItsRange()`; `refused.errorKind` actual 0, expected 3 (`InvalidArgs`); 0% passed, 1 failed | 8 |
| GREEN build, fix restored | `build-4-green.log` | `Built target W3EngineTest`; `ControlCommandsW3Engine.cpp.o` recompiled | 0 |
| GREEN run | `test-4-green.log` | `W3EngineTest Passed`; 100% tests passed out of 1 | 0 |
| Family run | `test-5-family.log` | `PhaseFChannelScaleTest`, `ReceiverEffectTest`, `W3EngineTest` all Passed; 100% out of 3 | 0 |
| Gates | `gates-1.log` | `RESULT: PASS` | 0 |

The revert before `build-3-red` and the re-apply before `build-4-green` are not in any log. The RED output (actual 0 where 3 is expected) shows the pre-fix behaviour, and both builds recompiled `ControlCommandsW3Engine.cpp.o`. The valid-level results come from the loop assertions in `test-4-green.log` and `test-5-family.log`, which are binary-level exits. Per-level exit codes are not recorded.

### Authoritative evidence (final product commit `08159e0b72`)

Variables used in the commands below:

```
RUN=/home/kruzzzzy/.config/kads/zene/state/runs/4919.r1/logs
WT=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-4919
```

Commands, in run order. Each log's last line is the `EXIT=` value the shell recorded for that command:

```
cd $WT && git diff 08159e0b72^ 08159e0b72 -- src/core/ControlCommandsW3Engine.cpp > $RUN/fix-final.patch; echo EXIT=$?
cd $WT && git apply -R $RUN/fix-final.patch; echo EXIT=$?
cd $WT && cmake --build build --target zene W3EngineTest -j4 > $RUN/build-6-final-red.log 2>&1; rc=$?; echo EXIT=$rc >> $RUN/build-6-final-red.log; echo EXIT=$rc
cd $WT/build/tests && ctest -R W3EngineTest --output-on-failure > $RUN/test-7-final-red.log 2>&1; rc=$?; echo EXIT=$rc >> $RUN/test-7-final-red.log; echo EXIT=$rc
cd $WT && git apply $RUN/fix-final.patch; echo EXIT=$?
cd $WT && cmake --build build --target zene W3EngineTest -j4 > $RUN/build-7-final-green.log 2>&1; rc=$?; echo EXIT=$rc >> $RUN/build-7-final-green.log; echo EXIT=$rc
cd $WT/build/tests && ctest -R W3EngineTest --output-on-failure > $RUN/test-8-final-green.log 2>&1; rc=$?; echo EXIT=$rc >> $RUN/test-8-final-green.log; echo EXIT=$rc
cd $WT/build/tests && ctest -R 'W3EngineTest|ReceiverEffectTest|PhaseFChannelScaleTest' --output-on-failure > $RUN/test-9-final-family.log 2>&1; rc=$?; echo EXIT=$rc >> $RUN/test-9-final-family.log; echo EXIT=$rc
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py $WT
```

The feature check line is reconstructed from how `submit_check.py` (line 168) invokes it. The exact command used to write `gates-1.log` was not recorded in that log, so the line above is the form the tool uses, not a verbatim record of that run.

| Step | Log | What it shows | Exit |
|---|---|---|---|
| Generate fix patch from `08159e0b72` | `fix-final.patch` (17 lines) | Diff of the product commit | EXIT=0 (printed by the shell; patch file is not a log) |
| Reverse-apply the fix (RED setup) | none | Product hunks reversed in the worktree | EXIT=0 (printed) |
| Build with fix reversed | `build-6-final-red.log` | Builds `zene` and `W3EngineTest` | EXIT=0 |
| Run with fix reversed (must fail) | `test-7-final-red.log` | `W3EngineTest (Failed)`; `FAIL!  : W3EngineTest::aReceiveRefusesALevelOutsideItsRange()` | EXIT=8 |
| Re-apply the fix; tree check | none | `git status --porcelain` empty after re-apply | EXIT=0 (printed) |
| Build with fix (GREEN) | `build-7-final-green.log` | Builds `zene` and `W3EngineTest` | EXIT=0 |
| Run with fix (GREEN) | `test-8-final-green.log` | `W3EngineTest Passed`; 100% tests passed out of 1 | EXIT=0 |
| Family run | `test-9-final-family.log` | `W3EngineTest`, `ReceiverEffectTest`, `PhaseFChannelScaleTest`: 100% tests passed out of 3 | EXIT=0 |
| Feature check gates | `gates-1.log` | fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length: all PASS; `RESULT: PASS` | EXIT=0 |

The RED run failed at the new test, not at build. The reversal run failed at the same assertion, so the test follows the product change.

### Earlier runs (superseded)

The logs `build-2-red.log`, `test-2-red.log`, `build-3-green.log`, `test-3-green.log` and `test-4-family.log` are exploratory and are superseded by the final runs above. Their exit codes were not recorded, so they are not evidence for this report. They are kept in the log dir only for history.

The earlier reversal and regreen runs also have recorded exits and agree with the final runs: `build-4-reversed.log` EXIT=0, `test-5-reversed-red.log` EXIT=8 (FAIL at `aReceiveRefusesALevelOutsideItsRange()`), `build-5-regreen.log` EXIT=0, `test-6-regreen.log` EXIT=0.

## Gates

From `gates-1.log` (feature-check kads-4919, base fa4a5f2e7):

| Gate | Result |
|---|---|
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in your files) |
| file-length | PASS (no new item in your files) |

`RESULT: PASS`, `EXIT=0`.

## Self-check

**Regression test fails with the product change reverted (exit codes).** Yes. Final run: `test-7-final-red.log` gives EXIT=8 with `FAIL!  : W3EngineTest::aReceiveRefusesALevelOutsideItsRange()`, after `build-6-final-red.log` gives EXIT=0. With the fix restored, `build-7-final-green.log` gives EXIT=0, `test-8-final-green.log` gives EXIT=0, and the family run `test-9-final-family.log` gives EXIT=0. Round 2 repeats the check with the same test: `test-3-red.log` gives EXIT=8 with the fix reverted (`FAIL!  : W3EngineTest::aReceiveRefusesALevelOutsideItsRange()`, `refused.errorKind` actual 0, expected 3), and `test-4-green.log` gives EXIT=0 with the fix restored.

**Every acceptance criterion is proven by a test run.**
- Out-of-range refused (4.0, 2.5, -0.5, NaN, +inf, -inf): `aReceiveRefusesALevelOutsideItsRange`, passes with the fix (`test-8-final-green.log`, EXIT=0; the run covers the whole W3EngineTest binary).
- Valid levels accepted and undone: `aChannelReceivesAnotherInsideItsChain`, same binary run. Round 2 (r2) extends it to 0.0, 0.5, 1.0 and 2.0 with the original assertions; the r2 binary-level runs are `test-4-green.log` (EXIT=0) and `test-5-family.log` (EXIT=0). Per-level exit codes are not recorded.
- Note: the logs show the whole `W3EngineTest` executable, not each test case separately. Per-case exit codes were not recorded.

**Undo and realtime.** `chainReceive` is a control-thread command handler. The change adds a range check and a store. Nothing is added to the audio path. The undo step is still `addUndoStep` at `ControlCommandsW3Engine.cpp:231`, and a refusal returns at :224-227, before that call, so a refusal creates no undo step.

**Deterministic.** The test uses fixed literal values (4.0, 2.5, -0.5, `std::nan("")`, `HUGE_VAL`, `-HUGE_VAL`) for refusals, and 0.0, 0.5, 1.0, 2.0 for the normal-path loop. It has no sleeps and no threads. (The `std::thread` at `W3EngineTest.cpp:282` belongs to a different test and is not used here.)

**Cause and siblings.** The cause is the `std::clamp` at the old :224, which replaced an out-of-range level instead of refusing it. Sibling audit (checked against `ControlCommandsMixerRoutes.cpp` and `ControlCommandsW3Engine.cpp`):
- Only `chain.receive` reads a level in `ControlCommandsW3Engine.cpp`; the read is `chainReceive` at :218.
- `mixer.send_to` uses `handleRouteWrite` and `validSendAmount`, called at `ControlCommandsMixerRoutes.cpp:139`; it refuses out-of-range amounts.
- `mixer.sidechain_to` uses `handleSidechainWrite`, with `validSendAmount` called at :198.
- `mixer.send_many` shares `handleRouteWrite`, so it takes the same refusal path as `mixer.send_to`.
- `mixer.route_to` ignores amount.
- The earlier sibling statements about `automation.*`, `clip.pre_fade`, `mixer.post_inserts` and `mixer.send_pan` were not re-read in this session and are removed from this audit rather than stated as verified.

**Collateral.** This command is not a project-load path, so saved projects are not read through it. Receiver levels 0..2 load as before because they are unchanged. An omitted level still defaults to 1.0.

**Sibling commands listed by submit_check (`submit-check-1.log`).** The check lists 15 ids, sorted and capped: app.about, app.ask, app.help, app.online_help, app.task_cancel, app.tasks, app.tour, app.tours, app.update_check_get, app.update_check_run, app.update_check_set, app.version, app.whats_new, arrangement.fit_tracks, arrangement.get_state. The check builds this list from every command file changed on the branch (210 files against base fa4a5f2e7), not from this commit alone, so it is capped and includes ids this cluster does not touch. Within this commit's file, `ControlCommandsW3Engine.cpp`, the groups it names are automation (23 ids), chain (10 ids) and clip and mixer (larger groups). None of the listed ids is `chain.receive`, and none of the listed ids is in `ControlCommandsW3Engine.cpp`. Only `chainReceive` reads a level there (`:218`). I did not open the handlers of the 15 listed ids, so their consistency with this fix is not verified beyond that: this fix does not change them.

**Verdict.** The defect is real (see Decision), so the ALREADY-FIXED / FALSE-POSITIVE item does not apply. The fix is at the cause: the clamp that replaced the value is gone, and the refusal happens before any mutation. Round 2 keeps this verdict: REAL.
