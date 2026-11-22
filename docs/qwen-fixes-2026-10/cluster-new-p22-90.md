FIXED

# Cluster new-p22-90: chain.receive level clamp propagates NaN

## Verdict

REAL. The finding is confirmed on the unfixed branch and is fixed here. The reported line 217 is still the clamp line on this branch. Scope: in-process callers only (see "Reachability").

## Evidence (file:line, this branch, base 639e1d23e)

- `src/core/ControlCommandsW3Engine.cpp:217` (before the fix): `receive.level = std::clamp(args.value("level").toDouble(1.0), 0.0, 2.0);`
  `std::clamp` returns its first argument unless it compares below 0 or above 2. A NaN makes both comparisons false, so NaN is returned unchanged.
- The schema check for a `number` argument is only `value.isDouble()` (`src/core/ControlSchema.cpp:49`), with no finiteness or range test. A NaN `QJsonValue` therefore reaches the handler.
- `chainReceive` then calls `makeReceive` (`src/core/ControlCommandsW3Engine.cpp:127-155`), which creates the sidechain send and the Receiver device and passes `receive.level` to the device's `Level` parameter (`:149-151`). I did not verify what `Level`'s setter does with a NaN; the fix keeps NaN from reaching it.
- Sibling commands already refuse non-finite numbers: `src/core/ControlCommandsNoteCurves.cpp:90-92` and `src/core/ControlCommandsNoteExpression.cpp:213`.

## Fix

- `src/core/ControlCommandsW3Engine.cpp:218-224`: a non-finite level is refused with `InvalidArgs` ("'level' is not a finite number") before any send or device is made. The check is in `chainReceive` after the existing tap check, and before the first mutation (`makeReceive`). Out-of-range finite levels are still clamped to 0..2 as before. `<cmath>` added at `:33`.
- An infinity is refused the same way. Before, `+inf` was silently clamped to 2 and `-inf` to 0. The finding asks for a finiteness check, and this is what that check does. Nothing that legitimately passes a finite level changes.

## Reachability

- JSON cannot carry NaN. I checked Qt's parser with a scratch program: `QJsonDocument::fromJson("{\"level\":1e400}")` gives a value with `isDouble()==false`, so the wire path does not deliver a non-finite level. Scratch program: deleted (see Self-check, one empty folder left behind).
- `QJsonValue(std::nan(""))` stays a Double holding NaN (checked with the same program), so an in-process caller can deliver one. The regression test does this.

## Regression test

- `tests/src/core/W3EngineTest.cpp`: new `aReceiveRefusesANonFiniteLevel()`, after `aChannelReceivesAnotherInsideItsChain`. It runs `chain.receive` with NaN, +inf and -inf and requires `InvalidArgs`, no sidechain send and an unchanged device count on channel 3. If a case unexpectedly succeeds, it undoes that step so the RED run leaves no state behind.
- The test is deterministic: no timing, no sleeps, no threads. It is in an existing registered test (`tests/fork-sources.txt:2614`, `tests/CMakeLists.txt:972`), so no registration change is needed.
- Test size: the file-length gate counts code lines. The first version of the test pushed `W3EngineTest.cpp` to 503 counted lines (`REGRESSION: new file over 500 lines`). I shortened the test (single QVERIFY for the send and device checks, separate `!ok` check dropped since `ControlErrorKind::None` on success makes the QCOMPARE fail on success). It now passes the gate.
- The NotFound skip that the sibling test has is not repeated. `tests/CMakeLists.txt:2381-2382` states that W3EngineTest builds the receiver module as a dependency, so the module is always present for this target.

## Commands and exit codes

RED / GREEN, in order (logs in the LOG folder `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-90.r1/logs`):

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| build (failed: ccache temp dir not writable under the sandboxed `dsh/home` path) | `cmake --build build --target W3EngineTest -j4` | 2 | build-1-red.log |
| build (with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p22-90`) | `cmake --build build --target W3EngineTest -j4` | 0 | build-2-red.log |
| RED (unfixed, first version of the test) | `cd build/tests && ctest -R W3EngineTest --output-on-failure` | 8 | test-1-red.log: `FAIL! W3EngineTest::aReceiveRefusesANonFiniteLevel() '!refused.ok' returned FALSE` at `W3EngineTest.cpp:540`; "13 passed, 1 failed" |
| GREEN (fixed, first version) | build then `ctest -R W3EngineTest` | 0 / 0 | build-3-green.log, test-2-green.log; direct run `./W3EngineTest`: EXIT 0, "14 passed, 0 failed, 0 skipped" (test-3-green-direct.log) |
| Proof, step 4b: reverse fix (`git apply -R fix.patch`) | `git apply -R …/fix.patch` | 0 | |
| RED after reverse | build (build-4-reverted.log, 0) then ctest | 8 | test-4-reverted.log: `FAIL!` at `W3EngineTest.cpp:540` |
| Re-apply fix (`git apply fix.patch`) | `git apply …/fix.patch` | 0 | build-5-regreen.log: 0; test-5-regreen.log: 0 |
| RED, final test version (fix reverted) | reverse-apply, build (build-7-final-red.log, 0), ctest | 8 | test-7-final-red.log: `FAIL! W3EngineTest::aReceiveRefusesANonFiniteLevel() Compared values are not the same` at `W3EngineTest.cpp:538` (the errorKind QCOMPARE) |
| GREEN, final test version (fix re-applied) | apply, build (build-8-final-green.log, 0), ctest | 0 | test-8-final-green.log: "100% tests passed out of 1" |
| Related family, final | `cmake --build … W3EngineTest PhaseFChannelScaleTest ReceiverEffectTest` (build-9-final-family.log, 0); `ctest -R "W3EngineTest\|PhaseFChannelScaleTest\|ReceiverEffectTest"` | 0 | test-9-final-family.log: 3 of 3 passed (`PhaseFChannelScaleTest`, `ReceiverEffectTest`, `W3EngineTest`) |

Affected-family test count: 3 ctest tests executed, 3 passed (the new test function is one of the 14 functions in `W3EngineTest`, 14 passed, 0 skipped).

## Gates

- `python3 …/qwen/feature_check.py <worktree>` could not run as written: it creates its log folder at `<worktree>/../zene-qwen-new-p22-90-feature-check`, which is in the projects directory, outside this run's write-jail. The run stopped at that `mkdir`. Log: `gates-1-feature-check.log`, exit 1 (`PermissionError`).
- I ran the same checks through the script's own functions (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, and the `complexity` and `file-length` ratchets against the cached base listing). The driver is `logs/gate_driver.py`; its logs are in `logs/gates-final/`. Result, final tree: exit 0, `RESULT: PASS`, every row PASS (`gates-3-final-summary.log`).
- The `file-length` ratchet run by itself (`bash tests/file-length-gate.sh --check`, `gate-fl-check.log`) exits 1 on `tests/src/gui/ArrangerTimelineTest.cpp (948)`. That file is not in this branch's change set, and it is red on the train on purpose (per the brief). Nothing this branch changes is listed.

## Not changed (siblings in other commands)

The same `toDouble` then clamp without a finiteness check appears in other commands. Each is its own finding and is out of scope for this fix. Not verified further: `src/core/ControlCommandsModulatorVoices.cpp:190` (depth), `src/core/ControlCommandsDeviceParams.cpp:303` (amount), `src/core/ControlCommandsClipProcess.cpp:123` (tail), `src/core/ControlBounceSupport.cpp:64`, `src/core/ControlCommandsMixerMeters.cpp:143,188,190`.

## Commit

Files: `src/core/ControlCommandsW3Engine.cpp`, `tests/src/core/W3EngineTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p22-90.md`.

## Self-check

- **Test fails with the fix reverted, passes with it.** Reverse-apply of the product diff, rebuild, run: `test-7-final-red.log`, ctest EXIT 8, `FAIL! … aReceiveRefusesANonFiniteLevel() Compared values are not the same` at `W3EngineTest.cpp:538`. Re-apply, rebuild, run: `test-8-final-green.log`, EXIT 0, "100% tests passed out of 1". Full sequence in the table above.
- **Deterministic.** The test uses three fixed literal levels (`std::nan("")`, `HUGE_VAL`, `-HUGE_VAL`) and calls `ControlRegistry::invoke` synchronously on the test thread. No timing, sleeps, threads or seams. It reads the mixer state (`channelSidechainSend(1, 3)`, channel 3's device count) right after the call.
- **Fix at the cause, complete.** The only place a `chain.receive` level is read is `chainReceive` (`ControlCommandsW3Engine.cpp:218-224`). `makeReceive` is called only from `chainReceive` (`:225`) and from the undo/redo lambda (`:227`), and both use the `receive.level` that `chainReceive` set, which is now finite. `rg -n makeReceive src include tests` shows only `:128` (definition), `:225` and `:227`. The non-finite refusal sits before any mutation: `receiveEnds` only resolves channels, and the first mutation is `makeReceive` at `:225`.
  - Sibling reads in the same file: `clip.pre_fade` reads `fade_in`/`fade_out` as integers (`:260-261`). The integer schema rule (`ControlSchema.cpp:52`) rejects NaN (`floor(NaN) == NaN` is false), so they cannot be NaN. `automation.latency_compensation` (`:277`) reads a boolean. `mixer.post_inserts` (`:283`) reads a string. The other `automation.*` ids on the checker's list are defined in other files, not in this file.
  - Other `toDouble` clamps without a finiteness check exist in other commands. They are separate findings, listed under "Not changed" above, and were not touched.
- **No collateral damage.** `makeReceive` is not called from any project load or save path (see the `rg` above), so old projects and saved data are unaffected. Finite levels go through the same clamp to 0..2 as before. The only behaviour change is for non-finite input: NaN is now refused (it was stored), and ±inf is now refused (it was clamped to 2 or 0). Wire callers cannot send either (the JSON parser does not produce a non-finite number; see "Reachability"), so the change reaches in-process callers only. The new check is not a limit on legitimate values, so it cannot reject a caller that is in range today.
- **Verdict FIXED.** ALREADY-FIXED and FALSE-POSITIVE do not apply.
- **Gate (feature_check.py) — not run as written.** The script writes its logs to `<worktree>/../<name>-feature-check` (projects directory, outside this run's write-jail): `gates-1-feature-check.log`, EXIT 1, `PermissionError`. The same checks, through the script's own functions with the logs redirected to the LOG folder (`logs/gate_driver.py`), give `gates-3-final-summary.log`: EXIT 0, `RESULT: PASS`, all eight rows PASS. The script itself is unchanged. Re-run `feature_check.py` from a shell with write access to the projects directory to get its own table.
