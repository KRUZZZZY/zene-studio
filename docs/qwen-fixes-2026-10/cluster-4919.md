FIXED

# Cluster 4919: chain.receive clamped an out-of-range level instead of refusing it

## Decision

REAL. `chain.receive` accepted any number as `level` and silently clamped it to 0..2, so a caller asking for 4.0 got a send at 2.0, and -0.5 got 0.0. The command's contract says the level is 0..2, so an out-of-range value should be refused.

Evidence in the base code (before the fix):

- `src/core/ControlCommandsW3Engine.cpp:218`: `const double level = args.value("level").toDouble(1.0);` (the level read; an omitted level defaults to 1.0).
- `src/core/ControlCommandsW3Engine.cpp:219-223`: a non-finite level (NaN, +inf, -inf) is refused with `InvalidArgs`. The comment at :219 explains that `std::clamp` passes NaN through.
- `src/core/ControlCommandsW3Engine.cpp:224`: `receive.level = std::clamp(level, 0.0, 2.0);`. This is the silent clamp. It runs before any refusal for a finite out-of-range value.
- Contract: `src/core/ControlCommandsW3Engine.cpp:295` says `'level' (0..2, 1 when left out)`. The command definition is at :291-299.
- Schema: `src/core/ControlVocabulary.cpp:80-83` (`numberProperty()`) returns `{"type": "number"}` with no bounds, so the schema accepts any number and the range check has to be in the handler.
- Sibling refusal: `src/core/ControlCommandsMixerRoutes.cpp:86-88` (`validSendAmount`: 0..2) and its use at :139-142, which refuses with `InvalidArgs` and the message "a send is 0..2". Sends refuse out-of-range values; the receive did not.

## Fix

`src/core/ControlCommandsW3Engine.cpp:224-228` now refuses a finite level outside 0..2 with `ControlErrorKind::InvalidArgs` and the message "level N is out of range: a receive level is 0..2", before any mutation. Then `receive.level = level;` (:228) stores the validated value without a clamp.

The full diff is in `fix.patch` (log dir of run 4919.r1). It is 6 lines of product code (`+5 -1`, net `+4`).

## Why the fix is complete

- The only reader of the level is `receive.level` at `ControlCommandsW3Engine.cpp:228`, and the only consumers of `receive` are `makeReceive` (:229) and its undo/redo lambda (:231). All three now see the validated value. The `makeReceive` level write at :150-152 is the one that reaches the device parameter.
- An omitted level still defaults to 1.0 (`toDouble(1.0)` at :218), so the no-level call is unchanged.
- A refusal returns at :224-227, before `makeReceive` (:229) and before `addUndoStep` (:231). So a refused call creates no send, no device, and no undo step.
- No header, API, or schema changed.

## Test matrix

Test file: `tests/src/core/W3EngineTest.cpp`.

| Case | Test and line | Expected |
|---|---|---|
| Normal level 2.0 (boundary) accepted, send created, device added, then undone | `aChannelReceivesAnotherInsideItsChain`, :510-527 | ok, send exists, undo removes both |
| Refused: 4.0, 2.5, -0.5, NaN, +inf, -inf | `aReceiveRefusesALevelOutsideItsRange`, :529-541 | `InvalidArgs`; no sidechain send; device count unchanged |
| Omitted level | Unchanged path, default 1.0 at :218 | Not separately tested in this cluster |
| Existing send is kept as it is | Code comment at `ControlCommandsW3Engine.cpp:216-217`; not separately tested in this cluster | Unchanged by this fix |
| Control thread only | `chainReceive` is a control-command handler | No audio-path code touched |

The refusal test contains an undo branch (:537) that runs only if a call wrongly succeeds (a RED run). It keeps the device count check so a leftover receive still fails the test.

## RED / GREEN evidence

Run directory: `/home/kruzzzzy/.config/kads/zene/state/runs/4919.r1/logs/`.

The run logs do not record the full command line, so the commands below are the build target and ctest filter the logs show (`Test #398: W3EngineTest`, `Test #196: PhaseFChannelScaleTest`). The quiet-run wrapper prints the exit code; logs that show no `EXIT=` line did not record one.

| Step | Log | What it shows | Exit |
|---|---|---|---|
| Build, test change against old code (RED) | `build-2-red.log` | Builds target `W3EngineTest` | not recorded in log (build completed) |
| Run RED | `test-2-red.log` | `W3EngineTest` Failed; FAIL at `W3EngineTest.cpp(538)`: `refused.errorKind` is 0, expected `InvalidArgs` (3) | not recorded in log (ctest reports failure) |
| Apply fix | `fix.patch` | 6-line product diff to `ControlCommandsW3Engine.cpp` | `fix.patch.exit`: EXIT=0 |
| Build with fix (GREEN) | `build-3-green.log` | Builds target `W3EngineTest` | not recorded in log |
| Run GREEN | `test-3-green.log` | `W3EngineTest` Passed (100% of 1) | not recorded in log |
| Family run | `test-4-family.log` | `PhaseFChannelScaleTest` and two others: 100% of 3 passed | not recorded in log |
| Build with fix reversed (reversal check) | `build-4-reversed.log` | Builds target `W3EngineTest` with the product hunks reversed | EXIT=0 |
| Run reversed (must fail) | `test-5-reversed-red.log` | `W3EngineTest` Failed; FAIL: `aReceiveRefusesALevelOutsideItsRange()` at `W3EngineTest.cpp(538)` | EXIT=8 |
| Rebuild with fix restored | `build-5-regreen.log` | Builds target `W3EngineTest` | EXIT=0 |
| Run restored (GREEN) | `test-6-regreen.log` | `W3EngineTest` Passed (100% of 1) | EXIT=0 |

The RED run failed at the new test, not at build. The reversal run failed at the same assertion, so the test follows the product change.

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

**Regression test fails with the product change reverted (exit codes).** Yes. With the product hunks reversed, `build-4-reversed.log` gives EXIT=0 and `test-5-reversed-red.log` gives EXIT=8 with FAIL at `W3EngineTest.cpp(538)` in `aReceiveRefusesALevelOutsideItsRange()`. With the fix restored, `build-5-regreen.log` gives EXIT=0 and `test-6-regreen.log` gives EXIT=0. The RED log `test-2-red.log` also fails at the same line, but its exit code was not recorded.

**Every acceptance criterion is proven by a test run.**
- Out-of-range refused (4.0, 2.5, -0.5, NaN, +inf, -inf): `aReceiveRefusesALevelOutsideItsRange`, passes with the fix (`test-6-regreen.log`, EXIT=0; the run covers the whole W3EngineTest binary).
- Normal level 2.0 accepted and undone: `aChannelReceivesAnotherInsideItsChain`, same binary run.
- Note: the logs show the whole `W3EngineTest` executable, not each test case separately. Per-case exit codes were not recorded.

**Undo and realtime.** `chainReceive` is a control-thread command handler. The change adds a range check and a store. Nothing is added to the audio path. The undo step is still `addUndoStep` at `ControlCommandsW3Engine.cpp:231`, and a refusal returns at :224-227, before that call, so a refusal creates no undo step.

**Deterministic.** The test uses fixed literal values (4.0, 2.5, -0.5, `std::nan("")`, `HUGE_VAL`, `-HUGE_VAL`). It has no sleeps and no threads. (The `std::thread` at `W3EngineTest.cpp:282` belongs to a different test and is not used here.)

**Cause and siblings.** The cause is the `std::clamp` at the old :224, which replaced an out-of-range level instead of refusing it. Sibling checks:
- Only `chain.receive` reads a level in the chain command group. Other `chain.*` commands take no level.
- `automation.*` and `mixer.post_inserts` take booleans and strings, not a level number.
- `clip.pre_fade` takes schema-bounded integers.
- `mixer.send_to` and `mixer.send_pan` already refuse out-of-range amounts (packet statement). The shared helper is `validSendAmount` (`ControlCommandsMixerRoutes.cpp:86-88`), called at :139 and :198; I did not re-read which command id maps to each call site.
- The `automation.*` and `clip.pre_fade` siblings are stated from the packet and were not each re-read in this session.

**Collateral.** This command is not a project-load path, so saved projects are not read through it. Receiver levels 0..2 load as before because they are unchanged. An omitted level still defaults to 1.0.

**Sibling commands listed by submit_check (`submit-check-1.log`).** The check lists 15 ids, sorted and capped: app.about, app.ask, app.help, app.online_help, app.task_cancel, app.tasks, app.tour, app.tours, app.update_check_get, app.update_check_run, app.update_check_set, app.version, app.whats_new, arrangement.fit_tracks, arrangement.get_state. The check builds this list from every command file changed on the branch (210 files against base fa4a5f2e7), not from this commit alone, so it is capped and includes ids this cluster does not touch. Within this commit's file, `ControlCommandsW3Engine.cpp`, the groups it names are automation (23 ids), chain (10 ids) and clip and mixer (larger groups). None of the listed ids is `chain.receive`, and none of the listed ids is in `ControlCommandsW3Engine.cpp`. Only `chainReceive` reads a level there (`:218`). I did not open the handlers of the 15 listed ids, so their consistency with this fix is not verified beyond that: this fix does not change them.

**Verdict.** The defect is real (see Decision), so the ALREADY-FIXED / FALSE-POSITIVE item does not apply. The fix is at the cause: the clamp that replaced the value is gone, and the refusal happens before any mutation.
