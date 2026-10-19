# Cluster new-p11-36-port

Verdict: **FIXED**

## What this job was

A port job. The fix for this finding was accepted on 2026-10 as branch `bugs/qwen-new-p11-36`
(commit `e4f5d5089`, "Fix output bus channel creation failures (cluster new-p11-36)"), but it could
not be merged into the newest line (`050/m0-train-5`): it breaks `ControlPluginBusesTest` there
(ControlCommandsPluginBuses change; git bisect). This tree is a fresh branch off the newest line
(`ded355440`), so the fix is re-applied by hand - not merged, not cherry-picked - with the
train-5 break removed.

## The finding

`plugin.output_buses_create_channels` hid an activation failure.

- Before the fix, `src/core/ControlCommandsPluginBuses.cpp:294` was
  `if (route.channelId < 0) { continue; }`: `createChannelFor` (line 246) creates the mixer
  channel, routes the bus to it, and - when the plug-in refuses to activate the bus - deletes the
  channel again and returns `-1`. The loop then skipped that bus and the command still answered
  success, so the caller got a half-done setup and no reason for it.
- The same line is in place on this branch, so the bug is **REAL here**, not already fixed.
- The sibling command for one bus, `plugin.output_bus_route`, already refuses the same condition
  with the same wording (`src/core/ControlCommandsPluginBuses.cpp:183-187`: "the plug-in did not
  activate output bus %1"), so the family was inconsistent: one bus refused, many buses hid it.

## The port

The product change is the accepted fix, written by hand against the current code
(`src/core/ControlCommandsPluginBuses.cpp:293-303`):

```cpp
		route.channelId = createChannelFor(buses, bus, route.name);
		if (route.channelId < 0)
		{
			for (auto it = created.rbegin(); it != created.rend(); ++it)
			{
				buses.route(it->bus, -1);
				deleteChannelById(it->channelId);
			}
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the plug-in did not activate output bus %1").arg(bus));
		}
```

A refused bus now refuses the whole command, the channels and routes made for the buses before it
are taken away again, and the failing bus index is in the error message. The refusal happens
*before* `recordCreation`, so the refused command records no undo step and claims no transaction.

### The train-5 break, and how the port removes it

The accepted fix alone breaks the newest line's coverage test - reproduced here before anything
was changed (`logs/test-2.log`, EXIT=8):

```
FAIL!  : ControlPluginBusesTest::oneStepMakesAChannelForEveryUnroutedBusAndOneUndoRemovesThem() 'created.ok' returned FALSE. (the plug-in did not activate output bus 4)
Totals: 7 passed, 1 failed, 0 skipped
```

The reason is the newest line's own fixture, `tests/src/plugins/BusesTestInstrument.cpp`: an
instrument with five buses - Main, Kick, Snare, Aux (off until it is routed) and **Locked, a bus
the plug-in never activates**. The coverage case
`ControlPluginBusesTest::oneStepMakesAChannelForEveryUnroutedBusAndOneUndoRemovesThem` measured
the old best-effort contract on that instrument ("Locked cannot be routed, so its channel is
deleted again at once"), which the atomic setup cannot satisfy: with the setup atomic, the command
on that instrument always refuses.

The accepted fix's branch base did not have that fixture or that case
(`git merge-base --is-ancestor 26c101048 e4f5d5089^` -> false: `tests/src/core/ControlPluginBusesTest.cpp`
does not exist there), which is why the conflict only surfaced at merge time.

The port keeps every assertion of that case and changes only what it measures: the success path is
now measured on the buses the plug-in *does* activate (the case saves the instrument's own layout,
puts a layout whose buses are all active in its place, and puts the original back afterwards), and
the refusal the locked bus causes gets its own case. No assertion of the newest line was weakened
or removed, and no fixture was changed.

## Regression test

`tests/src/core/ControlPluginBusesTest.cpp:234-253` -
`aBusThePluginWillNotActivateRefusesTheWholeSetup()`: it runs
`plugin.output_buses_create_channels` on the fixture instrument, and asserts that the answer is a
typed refusal naming the bus that refused (`bus 4`), that **no** mixer channel was left behind
(`mixer->numChannels()` is unchanged), that **no** bus is routed (`routeOf(1) == -1`,
`routedCount() == 0`), and that the refused command recorded **no** undo step
(`control.transactions` is unchanged). It is the accepted fix's regression test, ported to the
newest line's fixture: the accepted one drove the CLAP fixture
(`tests/src/core/InstrumentLayersCommandsTest.cpp`, a fixture this build does not have - the case
skips here with "this build has no clapinstrument module"), this one drives the in-tree
`BusesTestInstrument`, which is always built.

The test is deterministic: no wall-clock timing, no sleeps, no races. The bus that refuses is the
fixture's own `Locked` bus, whose enable hook always returns false
(`tests/src/plugins/BusesTestInstrument.cpp:110-116`).

## Evidence

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-36-port.r1/logs`.
Commands were run from the worktree root unless a working directory is stated.

| Item | Command | Exit / result |
| --- | --- | ---: |
| Baseline, untouched tree | `ctest -R '^ControlPluginBusesTest$'` (in `build/tests`) | **0** - 1/1 (`test-1.log`) |
| The train-5 break, accepted fix alone | `ctest -R '^ControlPluginBusesTest$'` | **8** - 7 passed, 1 failed: `oneStepMakesAChannelForEveryUnroutedBusAndOneUndoRemovesThem()` ('created.ok' returned FALSE) (`test-2.log`) |
| RED, new regression test, product change reverted | `ctest -R '^ControlPluginBusesTest$'` | **8** - 8 passed, 1 failed: `aBusThePluginWillNotActivateRefusesTheWholeSetup()` ('!refused.ok' returned FALSE) (`test-3.log`) |
| GREEN, product change in place | `ctest -R '^ControlPluginBusesTest$'` | **0** - 1/1 (`test-4.log`) |
| RED proof (product patch reversed, final source) | `ctest -R '^ControlPluginBusesTest$'` | **8** - the same new case fails (`test-17.log`) |
| GREEN proof (product patch restored, final source) | `ctest -R '^(ControlPluginBusesTest\|InstrumentLayersCommandsTest)$'` | **8** - `ControlPluginBusesTest` passes; `InstrumentLayersCommandsTest` fails on the pre-existing case below (`test-18.log`) |
| Affected family sweep (7 tests) | `ctest -R '^(ControlPluginBusesTest\|InstrumentLayersCommandsTest\|ReversibilityContractTest\|ReversibilityUndoTest\|InstrumentRoutingTest\|InstrumentSetupUiTest\|InstrumentLatencyPdcTest)$'` | **8** - 6 pass, 1 pre-existing failure (`test-8.log`) |
| The same sweep with the product patch reverted | same command | **8** - the same pre-existing failure, plus the new case RED (`test-10.log`) |
| The pre-existing failure on the pristine tree | `ctest -R '^InstrumentLayersCommandsTest$'` (product change reverted **and** the test file restored) | **8** - `perVoiceModulatorReportsItsMode()` ('Attack' does not resolve to a parameter) (`test-11.log`) |
| Final GREEN, final source | `ctest -R '^ControlPluginBusesTest$'` | **0** - 1/1 (`test-16.log`); the per-case list: 9 passed (`direct-test-3.log`) |
| The command surface is unchanged | `ctest -R '^ControlCommandsSnapshot$'` | **0** - 1/1 (`test-14.log`) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** - `RESULT: PASS`, all 8 rows PASS (`gate-feature-check.log`, and again after the last edit `gate-feature-check-2.log`) |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | **1** - pre-existing train items only, none in a file this change touches (`gate-complexity.log`) |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | **1** - pre-existing train items only, none in a file this change touches (`gate-file-length.log`) |

The one failure in the family sweep is **pre-existing and unrelated**:
`InstrumentLayersCommandsTest::perVoiceModulatorReportsItsMode` fails with "'Attack' does not
resolve to a parameter: the instrument has no parameter named 'Attack'". It fails identically on
the pristine tree (both the product change reverted and the test file restored - `test-11.log`),
it is a modulator case with nothing to do with output buses, and the same sweep with the product
patch reverted shows it again (`test-10.log`).

Affected family: 1 CTest entry (`ControlPluginBusesTest`, 9 QtTest cases) plus the 7-test sweep
above (6 pass, 1 pre-existing environment failure).

### Commands and exit codes

| Log | Command | Exit |
| --- | --- | ---: |
| `build-1.log` | `cmake --build build --target ControlPluginBusesTest` (pristine tree) | 0 |
| `build-2.log` | `cmake --build build --target ControlPluginBusesTest` (accepted fix alone) | 0 |
| `build-3.log` | `cmake --build build --target ControlPluginBusesTest` (product change reverted) | 0 |
| `build-4.log` | `cmake --build build --target ControlPluginBusesTest` (product change in place) | 0 |
| `build-5.log` | `cmake --build build --target InstrumentLayersCommandsTest` | 0 |
| `build-6.log` | `cmake --build build --target ControlPluginBusesTest` (product patch reversed) | 0 |
| `build-7.log` | `cmake --build build --target ControlPluginBusesTest` (product patch restored) | 0 |
| `build-8.log` | `cmake --build build --target ReversibilityContractTest ReversibilityUndoTest InstrumentRoutingTest InstrumentSetupUiTest InstrumentLatencyPdcTest` | 0 |
| `build-9.log` | `cmake --build build --target ControlPluginBusesTest InstrumentLayersCommandsTest InstrumentRoutingTest InstrumentSetupUiTest InstrumentLatencyPdcTest ReversibilityContractTest ReversibilityUndoTest` (product patch reversed) | 0 |
| `build-10.log` | `cmake --build build --target InstrumentLayersCommandsTest` (pristine tree) | 0 |
| `build-11.log` | `cmake --build build --target zene ControlPluginBusesTest InstrumentLayersCommandsTest` | 0 |
| `build-12.log` | `cmake --build build --target ControlPluginBusesTest` (final source) | 0 |
| `build-13.log` | `cmake --build build --target ControlPluginBusesTest` (final source, after the layout save/restore) | 0 |
| `build-14.log` | `cmake --build build --target ControlPluginBusesTest` (final source, product patch reversed) | 0 |
| `build-15.log` | `cmake --build build --target ControlPluginBusesTest zene` (final source, product patch restored) | 0 |
| `test-1.log` | `ctest -R '^ControlPluginBusesTest$' --output-on-failure` | 0 |
| `test-2.log` | `ctest -R '^ControlPluginBusesTest$' --output-on-failure` | 8 |
| `test-3.log` | `ctest -R '^ControlPluginBusesTest$' --output-on-failure` | 8 (RED) |
| `test-4.log` | `ctest -R '^ControlPluginBusesTest$' --output-on-failure` | 0 (GREEN) |
| `test-5.log` | `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure` | 0 |
| `test-6.log` | `ctest -R '^ControlPluginBusesTest$' --output-on-failure` | 8 (RED proof) |
| `test-7.log` | `ctest -R '^(ControlPluginBusesTest\|InstrumentLayersCommandsTest)$' --output-on-failure` | 0 (GREEN proof) |
| `test-8.log` | the 7-test sweep | 8 |
| `test-9.log` | the 7-test sweep, repeated | 8 |
| `test-10.log` | the 7-test sweep, product patch reversed | 8 |
| `test-11.log` | `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure` (pristine tree) | 8 |
| `test-12.log` | `ctest -R '^(ControlPluginBusesTest\|InstrumentLayersCommandsTest)$' --output-on-failure` | 8 |
| `test-13.log` | `ctest -R '^ControlPluginBusesTest$' --output-on-failure` | 0 |
| `test-14.log` | `ctest -R '^ControlCommandsSnapshot$' --output-on-failure` | 0 |
| `test-15.log` | `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure`, three times | 8, 8, 8 (the pre-existing case, every run) |
| `test-16.log` | `ctest -R '^ControlPluginBusesTest$' --output-on-failure` (final source) | 0 (final GREEN) |
| `test-17.log` | `ctest -R '^ControlPluginBusesTest$' --output-on-failure` (final source, product patch reversed) | 8 (RED proof) |
| `test-18.log` | `ctest -R '^(ControlPluginBusesTest\|InstrumentLayersCommandsTest)$' --output-on-failure` (final source, product patch restored) | 8 (the pre-existing case) |
| `solo-1.log` .. `solo-6.log` | the same command, six earlier runs | the same pre-existing failure in every run |
| `direct-test-1.log` | `./InstrumentLayersCommandsTest` (per-case list) | 0 |
| `direct-test-2.log` | `./ControlPluginBusesTest` (per-case list: 9 passed) | 0 |
| `direct-test-3.log` | `./ControlPluginBusesTest` (per-case list, final source: 9 passed) | 0 |
| `gate-feature-check.log` | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 |
| `gate-feature-check-2.log` | the same command, after the last edit | 0 |
| `gate-complexity.log` | `bash tests/complexity-gate.sh --check` | 1 |
| `gate-file-length.log` | `bash tests/file-length-gate.sh --check` | 1 |

Every ctest command was run from `build/tests`; every build command from the worktree root.

### Regression proof (step 4b)

```
git diff -- src/core/ControlCommandsPluginBuses.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-36-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-36-port.r1/logs/fix.patch
```

exit **0** (the product file only - the test file stays in place), then
`cmake --build build --target ControlPluginBusesTest` (`build-14.log`, exit 0) and
`ctest -R '^ControlPluginBusesTest$' --output-on-failure` (`test-17.log`): exit **8**, the new case
fails again with "'!refused.ok' returned FALSE". (The same proof on the earlier source: `build-6.log`
/ `test-6.log`, exit **8**.)

```
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-36-port.r1/logs/fix.patch
```

exit **0**, then `cmake --build build --target ControlPluginBusesTest zene` (`build-15.log`, exit 0)
and `ctest -R '^(ControlPluginBusesTest|InstrumentLayersCommandsTest)$' --output-on-failure`
(`test-18.log`): `ControlPluginBusesTest` passes; `InstrumentLayersCommandsTest` fails only on the
pre-existing modulator case below (on the earlier source the same run was 2/2, `test-7.log`).

## Files in the commit

- `src/core/ControlCommandsPluginBuses.cpp` - the ported fix (fork-NEW file, already registered in
  `tests/fork-sources.txt:1437`).
- `tests/src/core/ControlPluginBusesTest.cpp` - the regression case and the adapted coverage case
  (fork-NEW file, already registered in `tests/fork-sources.txt:2248`).
- `docs/qwen-fixes-2026-10/cluster-new-p11-36-port.md` - this report.

No new file was added, so `tests/fork-sources.txt` is unchanged; both changed files are
fork-authored, so no `tests/upstream-modifications.txt` entry is owed.

## Self-check

1. **The regression test FAILS without the fix and PASSES with it.** RED: `ctest -R
   '^ControlPluginBusesTest$' --output-on-failure` on the tree with the product change reverted,
   exit **8** (`test-3.log`, and again in the step-4b proof `test-17.log`). GREEN: the same
   command, exit **0** (`test-4.log`, `test-16.log`; the two-test family run `test-7.log` and
   `test-18.log`, where only the pre-existing case below fails). The failing assertion is
   `QVERIFY2(!refused.ok, "the command hid the bus the plug-in would not activate")`.
2. **The test is deterministic.** No wall-clock timing, no sleeps, no races. The refusing bus is
   the fixture's own `Locked` bus, whose enable hook always refuses
   (`tests/src/plugins/BusesTestInstrument.cpp:110-116`); the channel count, the routes and the
   transaction count are read from the model right after the call.
3. **The fix is at the cause and complete.** The cause is the silent `continue` on a failed
   `createChannelFor` (`src/core/ControlCommandsPluginBuses.cpp:294` before the change). Callers
   and siblings checked: `createChannelFor` is called from `createChannels` (the command) and
   from the redo lambda of `recordCreation` (line 280) - the redo is the *other path into the same
   function*, and it is left as it was on purpose: a journal redo action is a `void` lambda
   (`include/ControlReversibility.h`, `ProjectJournal::addJournalAction`), so a redo has no failure
   channel to report through, and the accepted fix does not touch it either. The sibling command
   `plugin.output_bus_route` (line 170) already refuses the same condition with the same wording,
   so the family now answers alike. The undo step is recorded only after the loop succeeded
   (`recordCreation`, line 308), so a refused command records no step and claims no transaction -
   asserted by the new case.
4. **No collateral damage.** Only the failure path of `plugin.output_buses_create_channels`
   changed: a command that could route every bus behaves exactly as before (the same channels,
   the same names, the same one undo step). Nothing is written to a project file, no save format
   changes, and no limit is added. The newest line's own assertions are all still in place; the
   coverage case keeps every one of them and only measures the success path on the buses the
   plug-in activates.
5. **Realtime, undo and commands.** The change is on the control thread (the command handler);
   nothing on the audio thread allocates, frees, locks, waits or logs. The undo stays one honest
   step: a refused command records none. No control command was added, and the command surface is
   unchanged (`ControlCommandsSnapshot` passes, `test-14.log`).
6. **Gates.** `feature_check.py` exits **0** with `RESULT: PASS` (all eight rows). The two
   ratchets are red on the train by design and list only pre-existing items; neither lists a file
   this change touches (`gate-complexity.log`, `gate-file-length.log`).
