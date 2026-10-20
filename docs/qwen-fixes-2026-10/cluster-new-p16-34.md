# Cluster new-p16-34: setting the existing script memory budget still adds an undo step

**Verdict: FIXED**

BUG 12 is real. `script.set_memory_budget` replied `changed:false` for a value it already had, yet still wrote a reversible transaction, so one `control.undo` after a no-op undid nothing visible while the real change stayed in place. The fix skips the setter and the transaction when the requested value equals the current cap, and the registry no longer records a snapshot-class call that replies `changed:false` with no transaction and no journal step.

## Evidence (file:line, current tree)

- Finding was at `src/core/ControlCommandsScript.cpp:396` (`engine->setMemoryBudget`). Before the fix the handler always attached `__transaction`, so the registry always recorded a step. The RED run shows the no-op record's `before` is the 2 MiB cap set by the first call, not the 64 MiB default (see RED below).
- `src/core/ControlCommandsScript.cpp:396` computes `changed = previous != bytes`.
- `src/core/ControlCommandsScript.cpp:400` sets the cap only when `changed`.
- `src/core/ControlCommandsScript.cpp:410` returns before the `__transaction` payload (line 411) when `!changed`.
- `src/core/ControlRegistry.cpp:300-310` `changedNothing`: a `Snapshot` row whose reply has `changed` as a bool `false`, with no `__transaction` and no journal step, counts as changing nothing. The `TrueInverse` branch keeps its earlier condition exactly (`journalled && cls == TrueInverse`).
- `src/core/ControlRegistry.cpp:460` the caller already gates recording on `!changedNothing(...)`, so no new call path was added.
- Regression: `tests/src/core/ScriptMemoryBudgetTest.cpp:220` `settingTheBudgetItAlreadyHasRecordsNoUndoStep()`. It is a private slot, so QTest picks it up without registration. No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged.
- Reversibility class of `script.set_memory_budget` stays `Snapshot` (`src/core/ControlReversibilityTableSnapshot.cpp:185`). A real change still records one honest reversible step.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-34.r1/logs`

| Step | Command (from `build/` tree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| RED build (first try, ccache temp dir unwritable) | `cmake --build build --target ScriptMemoryBudgetTest -j4` | 2 | `build-1-red.log` (ccache `/run/user/1000` permission error, not a code error) |
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16-34 cmake --build build --target ScriptMemoryBudgetTest -j4` | 0 | `build-2-red.log` |
| RED run, fix not applied | `ctest -R ScriptMemoryBudget --output-on-failure` (from `build/tests`) | 8 | `test-1-red.log` |
| GREEN build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16-34 cmake --build build --target zene ScriptMemoryBudgetTest -j4` | 0 | `build-3-green.log` |
| GREEN run | `ctest -R ScriptMemoryBudget --output-on-failure` (from `build/tests`) | 8 | `test-2-green.log` |
| 4b: save product diff | `git diff -- src/core/ControlCommandsScript.cpp src/core/ControlRegistry.cpp > .../logs/fix.patch` | 0 | `fix.patch` (2 hunks) |
| 4b: revert product | `git apply -R .../logs/fix.patch` | 0 | `revert-4b.log` |
| 4b: rebuild reverted | `cmake --build build --target zene ScriptMemoryBudgetTest -j4` | 0 | `build-4-reverted.log` |
| 4b: run reverted | `ctest -R ScriptMemoryBudget --output-on-failure` | 8 | `test-3-reverted-RED.log` |
| 4b: re-apply fix | `git apply .../logs/fix.patch` | 0 | `apply-4b.log` |
| 4b: rebuild restored | `cmake --build build --target zene ScriptMemoryBudgetTest -j4` | 0 | `build-5-restored.log` |
| 4b: run restored | `ctest -R ScriptMemoryBudget --output-on-failure` | 8 | `test-4-restored-GREEN.log` |
| Family build (fixed) | `cmake --build build --target ControlRegistryTest ControlRegistryNestingTest ReversibilityUndoTest UndoBoundsTest UndoHonestyTest UndoHonestyStateTest ScriptBudgetActionTest ScriptCommandBridgeTest ScriptEngineTest ControlAutomationScriptTest -j4` | 0 | `build-7-family.log` |
| Family run (fixed, 13 tests) | `ctest -R "^(ControlRegistryTest\|ControlRegistryNestingTest\|ReversibilityUndoTest\|UndoBoundsTest\|UndoHonestyTest\|UndoHonestyStateTest\|ScriptBudgetActionTest\|ScriptCommandBridgeTest\|ScriptEngineTest\|ControlAutomationScriptTest\|ScriptMemoryBudgetTest\|ScriptBindingsTest\|ScriptStabilisationTest)$" -j4 --output-on-failure` | 8 | `test-7-fixed-family.log` |
| Family baseline build (fix reverted) | `cmake --build build --target zene <same 13 targets> -j4` | 0 | `build-8-baseline.log` |
| Family run (baseline, same 13 tests) | same ctest | 8 | `test-6-baseline-family.log` |
| Family rebuild (fix restored) | `cmake --build build --target zene <same 13 targets> -j4` | 0 | `build-9-fixed-family.log` |

**RED (fix reverted):** `ScriptMemoryBudgetTest::settingTheBudgetItAlreadyHasRecordsNoUndoStep()` FAILS, `test-3-reverted-RED.log`:
`Actual (last->before.value("memory_budget")): 2097152`, `Expected: 67108864`, `Loc: ScriptMemoryBudgetTest.cpp(243)`. The last record is the no-op's, not the real change's.

**GREEN (fix applied):** the same test PASSES, `test-4-restored-GREEN.log`: `PASS : ScriptMemoryBudgetTest::settingTheBudgetItAlreadyHasRecordsNoUndoStep()`, `Totals: 6 passed, 1 failed`. The one failure is the pre-existing case below. ctest's EXIT 8 comes from that pre-existing failure only.

**Affected family (13 tests executed in each run; no "Not Run" in the fixed run):** the pass/fail set is identical name-for-name between the original code (`test-6-baseline-family.log`) and the fix (`test-7-fixed-family.log`). The regression is the only test whose outcome depends on the fix. `ControlRegistryTest`, `ControlRegistryNestingTest`, `ReversibilityUndoTest`, `UndoBoundsTest`, `UndoHonestyStateTest`, `ScriptBudgetActionTest`, `ScriptCommandBridgeTest`, `ScriptBindingsTest`, and `ScriptStabilisationTest` pass in both runs.

### Pre-existing failures (fail identically on the original code, unrelated to this change)

- `ScriptMemoryBudgetTest::aSystemAllocationFailureIsNotReportedAsBudgetExhaustion()`: `ScriptMemoryBudgetTest.cpp:265`, `allocate(...) == nullptr returned FALSE`. Fails with the fix reverted and with it applied. `docs/bunny-fixes-2026-10-05/core/batch-06.md:47` records it green at `652e17d66`. I did not investigate its cause; it is not on the budget-record path this fix touches.
- `ControlAutomationScriptTest` (4 tests): "no track with parameters in the fresh song" (environment).
- `ScriptEngineTest::testInstrumentParameterReadWrite`: `TripleOscillator` loads as `dummy` (environment).
- `UndoHonestyTest::armingSeveralTracksIsOneStep`: take file under `/home/kruzzzzy/Documents/Zene Studio` cannot be opened for writing (environment).

## Gates

**The `feature_check.py` run cannot complete in this sandbox.** It writes its logs to `<worktree>/../<name>-feature-check`, i.e. `zene-studio-program/zene-qwen-new-p16-34-feature-check`. That path is outside the write-jail, so `mkdir` fails with `PermissionError: [Errno 13]` even though the Unix permissions on the parent would allow it (`gates-1.log`, `gates-2-recheck.log`). I did not edit the shared script. As a result the `gates (feature_check.py)` row in `submit_check.py` reads FAIL for this environmental reason, not a gate result.

I ran the same gate commands the script lists, in the same worktree, with logs in the LOG FOLDER (`gates/`):

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets are red on the train, as the task says. Their `REGRESSION:` lines are in files I did not change (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`, and the new-file-over-500 items). None are in `ControlCommandsScript.cpp`, `ControlRegistry.cpp`, or `ScriptMemoryBudgetTest.cpp`, and the new `changedNothing` function is not listed. By the script's own rule (fail only on a new item in a changed file), both ratchet rows PASS for this change.

## Self-check

Each ASK item from `submit_check.py`, answered:

- **The regression test FAILS with your product change reverted (reverse the non-test hunks, rebuild, run: exit code?), then PASSES restored.** Yes. Reverted: `build-4-reverted.log` EXIT 0, `test-3-reverted-RED.log` EXIT 8, `settingTheBudgetItAlreadyHasRecordsNoUndoStep` FAIL at `ScriptMemoryBudgetTest.cpp:243`. Restored: `build-5-restored.log` EXIT 0, `test-4-restored-GREEN.log` PASS. The first RED run (`test-1-red.log`, EXIT 8) shows the same failure before any product change.
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).** Yes. No timing, sleeps or threads. It drives the registry with fixed values (2 MiB, then the default cap) and reads `ControlRegistry::lastTransaction()`. The only injected value is the budget itself, passed as `bytes`.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.** The cause is the handler always recording a transaction, plus the registry recording any snapshot-class call without checking whether it changed anything. The `changedNothing` rule is the only registry caller path (`ControlRegistry.cpp:460`). Of the 42 snapshot-class rows, the only other producer of a `changed` flag is `patcher.set_wiring` (`ControlCommandsPatcherEdit.cpp:398`), which always attaches `__transaction` (`:426`), so the new rule cannot fire for it. The other snapshot rows reply with no `changed` key.
- **No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.** I added no limit or check. The budget is process state, not project state, so no saved file changes. The cap and range checks are unchanged. A real change (value different from the cap) behaves exactly as before, including the transaction, mechanism and inverse.
- **Sibling commands in the command files you changed - confirm each is consistent with your fix (or say why it is not affected): script.list, script.run, script.set_memory_budget.** `script.list` is non-mutating and not in the snapshot table, so it is unaffected. `script.run` (`ControlReversibilityTableSnapshot.cpp:100`) is snapshot and always attaches `__transaction` on both paths (`ControlCommandsScript.cpp:338` and `:346`); it replies `changed_project`, not `changed`, so the new rule does not apply. `script.set_memory_budget` is the fixed command.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.** Not applicable: the verdict is FIXED.
- **Realtime, undo and commands.** Nothing on an audio thread changed. The handler and registry run on the control path. A real change still records one honest reversible step. No new control command was added, so the commands snapshot and canary are unchanged.
- **Registration.** No new files. The existing test file gained a slot, so the registration files are unchanged, and `no-upstream-regression` passes.
- **Qt / portability.** No Qt API added. The change uses `QJsonValue::isBool()` and `toBool()`, which exist in Qt 5.15 and Qt 6.
- **Gates row in the submit check.** FAIL for the environmental reason in Gates above. The eight gate commands it runs all exit 0 except the two ratchets, which pass under the script's rule.

## Environment notes

- `CCACHE_TEMPDIR` had to point at `/tmp/ccache-tmp-p16-34` because ccache could not write under `/run/user/1000`. That was an environment issue, not a code error.
- `feature_check.py` needs write access to `zene-studio-program/<worktree>-feature-check`, outside the write-jail. Someone with that access needs to run it, or the script's log location needs to move under the run folder.
