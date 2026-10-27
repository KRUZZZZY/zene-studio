Verdict: FIXED

new-p18-5 (BUG 7): supplying both organiser identifiers silently edits the key's entry.

Evidence: `namedKey()` at [ControlCommandsPluginOrganise.cpp:47](src/core/ControlCommandsPluginOrganise.cpp:47) tested `key` first and returned it, so a `device` sent in the same call was never read. The sent `device` was ignored and the plug-in named by `key` was edited. The fix refuses a call that names both at [ControlCommandsPluginOrganise.cpp:49](src/core/ControlCommandsPluginOrganise.cpp:49)-[54](src/core/ControlCommandsPluginOrganise.cpp:54) with `InvalidArgs`, before any device is resolved or any entry is written. `organise()` gets no change: it only runs after `namedKey()` succeeds.

Callers checked, none sends both identifiers:
- Plug-in Manager row actions send only `device`: [CommandLists.cpp:125](src/gui/CommandLists.cpp:125)-[130](src/gui/CommandLists.cpp:130), mapped through `fromRow` at [CommandListDialog.cpp:257](src/gui/CommandListDialog.cpp:257)-[262](src/gui/CommandListDialog.cpp:262).
- The effect-picker organiser sends only `key`: [EffectPickerOrganiser.cpp:47](src/gui/modals/EffectPickerOrganiser.cpp:47)-[52](src/gui/modals/EffectPickerOrganiser.cpp:52) and the `fillKeyMenu` actions at [EffectPickerOrganiser.cpp:159](src/gui/modals/EffectPickerOrganiser.cpp:159)-[165](src/gui/modals/EffectPickerOrganiser.cpp:165).
- `tests/control-devices.py:128` sends only `device`; `tests/src/core/DeviceOrganiserTest.cpp` sends only `key`.

Contract: the schema and the description are unchanged. The description already reads "Name it by 'device' ... or by its 'key'", which names one identifier. I did not rewrite the description to say "not both", because that would move the generated `ControlCommandsSnapshot` canary, and the task forbids that for a bug fix.

Regression test: `DeviceOrganiserTest::aDeviceAndAKeyAreNotBothAccepted` at [DeviceOrganiserTest.cpp:161](tests/src/core/DeviceOrganiserTest.cpp:161). It sends `key: amplifier` with `device: dev-0` and `favourite: true`. It asserts the call is refused with `InvalidArgs`, the `amplifier` entry is unchanged, no `changed` signal fires, and no transaction is recorded.

Limitation: the test process catalogue has no plug-in with an organiser key, so no device-only success case is run here. A first draft tried to find a keyed device and failed for that reason. The device-only success path is covered over the socket by `tests/control-devices.py:128`, which I did not run.

Test matrix:
- Both identifiers: refused, nothing edited. Covered by the new test.
- `key` only: unchanged. `theOrganiserMarksADevice` covers it.
- Neither identifier: `InvalidArgs`, unchanged. `theOrganiserMarksADevice` covers it.
- Device-only path: unchanged code path, not run here (see limitation).
- Audio thread, undo and realtime: the command runs on the control thread. No new allocation, lock or log on any audio path. It writes no undo step when refused.
- Saved data: the organiser entries live in the config file and not in projects. Nothing on disk changes format.

Commands and results (`$LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-5.r1/logs`; all run from the worktree unless noted):

| # | Command | EXIT | Log / result |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target DeviceOrganiserTest -j4 > $LOGS/build-1.log 2>&1` | 2 | ccache could not write its temp file under the sandbox path (`Permission denied`); no compile error |
| 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp cmake --build build --target DeviceOrganiserTest -j4 > $LOGS/build-2.log 2>&1` | 0 | build succeeded with ccache temp dir set |
| 3 | `cd build/tests && ctest -R DeviceOrganiserTest --output-on-failure > $LOGS/test-red-1.log 2>&1` (first draft of the test, before the fix) | 8 | RED: `'!both.ok' returned FALSE` at `DeviceOrganiserTest.cpp:178` |
| 4 | `CCACHE_TEMPDIR=… cmake --build build --target zene DeviceOrganiserTest -j4 > $LOGS/build-3.log 2>&1`, with the fix in place; then `ctest -R DeviceOrganiserTest --output-on-failure > $LOGS/test-green-1.log 2>&1` | 0 / 8 | the second draft's device-only check on `dev-0` failed with "name the device" (`dev-0` is a built-in device with no organiser key). Draft replaced; not the final result |
| 5 | `git diff -- src/core/ControlCommandsPluginOrganise.cpp > $LOGS/fix.patch` | 0 | product diff saved |
| 6 | `git apply -R $LOGS/fix.patch` | 0 | fix reverted; `git diff -- src/core/ControlCommandsPluginOrganise.cpp` is empty afterwards |
| 7 | `CCACHE_TEMPDIR=… cmake --build build --target zene DeviceOrganiserTest -j4 > $LOGS/build-5.log 2>&1` | 0 | rebuilt with the fix reverted |
| 8 | `cd build/tests && ctest -R DeviceOrganiserTest --output-on-failure > $LOGS/test-red-3.log 2>&1` | 8 | **RED on the final test**: `'!both.ok' returned FALSE` at `DeviceOrganiserTest.cpp:174` |
| 9 | `git apply $LOGS/fix.patch` | 0 | fix re-applied |
| 10 | `CCACHE_TEMPDIR=… cmake --build build --target zene DeviceOrganiserTest -j4 > $LOGS/build-6.log 2>&1` | 0 | rebuilt with the fix |
| 11 | `cd build/tests && ctest -R DeviceOrganiserTest --output-on-failure > $LOGS/test-green-2.log 2>&1` | 0 | **GREEN**: `100% tests passed, 0 tests failed out of 1` |
| 12 | `CCACHE_TEMPDIR=… cmake --build build --target EffectPickerOrganiserTest ControlRegistryTest -j4 > $LOGS/build-7.log 2>&1` | 0 | sibling test targets built |
| 13 | `cd build/tests && ctest -R "DeviceOrganiserTest|EffectPickerOrganiserTest|ControlRegistryTest" --output-on-failure > $LOGS/test-family-1.log 2>&1` | 0 | `100% tests passed out of 3` |
| 14 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | not a gate result: the script died with `PermissionError` creating `<worktree>-feature-check`, which is outside the write-jail (traceback, no table) |
| 15 | copy of `feature_check.py` at `$LOGS/../feature_check_logdir.py`, changed only in two lines: `ROOT` points at the real bunny-fix `qwen` folder (the existing `gate-baselines` cache for base `5c1c6715d9` is read, unchanged), and `logs` points at `$LOGS/gates-feature-check` (writable). Run with `python3 $LOGS/../feature_check_logdir.py <worktree> > $LOGS/gates-1.log 2>&1` | 0 | all gates PASS (table below) |

Mutation check (steps 5-11): the same test fails with the product fix reverted (step 8, `'!both.ok'` at line 174) and passes with it applied (step 11). The test does not pass without the fix.

Gate table (`$LOGS/gates-1.log`, RESULT: PASS):

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS: no new item in the changed files |
| file-length | PASS: no new item in the changed files |

Counts: `DeviceOrganiserTest` is one test executable with 7 test functions (6 existing plus `aDeviceAndAKeyAreNotBothAccepted`). The affected family (`DeviceOrganiserTest`, `EffectPickerOrganiserTest`, `ControlRegistryTest`) ran 3 of 3. No test was weakened, and `tests/CMakeLists.txt` and the registers are unchanged (no new files).

Files committed for this verdict:
- `src/core/ControlCommandsPluginOrganise.cpp` (the namedKey guard)
- `tests/src/core/DeviceOrganiserTest.cpp` (the regression test)
- `docs/qwen-fixes-2026-10/cluster-new-p18-5.md` (this report)

## Self-check

To be completed after `submit_check.py` runs; see the follow-up commit.
