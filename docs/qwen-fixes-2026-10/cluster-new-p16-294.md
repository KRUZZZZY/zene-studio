FIXED

# Cluster new-p16-294 - NaN automation point values pass the clamp

## Verdict

**FIXED.** The finding is real. `automationedit::movePoint` clamped its value with `std::clamp`, which returns NaN unchanged, and the in-place path stored it as the point's value. The `automation.move_point` command's range check (two comparisons) also let NaN through. Both now refuse a non-finite value before any write.

## Evidence (file:line, current tree)

Before the fix (HEAD `639e1d23e`):

- `src/gui/editors/AutomationPointEdit.cpp:103` (HEAD): `toValue = std::clamp(toValue, min, max);`. `std::clamp` returns `v` when every comparison is false, so NaN comes back as NaN.
- `src/gui/editors/AutomationPointEdit.cpp:109` (HEAD): `movePoint` hands the unchecked value to `moveInPlace`, which stores it at `:78` (HEAD) through `clip->putValue(TimePos(toTick), toValue, false)`.
- `src/core/AutomationClip.cpp:423`: `putValue` assigns `m_timeMap[newTime] = AutomationNode(this, value, newTime);` with no finiteness check, so NaN is stored.
- `src/core/ControlCommandsAutomationTools.cpp:185` (HEAD `:179`): `if (value < min || value > max)`. NaN passes both comparisons. Qt keeps a NaN as a `Double` (probe: `QJsonValue(nan).isNull()=0 isDouble=1`, so an in-process caller's NaN reaches the handler).

After the fix:

- `src/gui/editors/AutomationPointEdit.cpp:103-108`: `if (!std::isfinite(toValue)) { *error = tr("the value is not a finite number"); return false; }`, placed before the clamp and before both the command path and the in-place path. Both editor callers go through it: `src/gui/editors/ItemListAutomationSource.cpp:101` and `src/gui/editors/AutomationEditorKeys.cpp:160`.
- `src/core/ControlCommandsAutomationTools.cpp:181-184`: `if (!std::isfinite(value)) { return ControlResult::failure(ControlErrorKind::Refused, "new_value is not a finite number"); }`, placed before the range check at `:185`. Refusal happens before `clip->addJournalCheckPoint()` (`:194`), so a refused move creates no undo step.
- Regression tests: `tests/src/core/AutomationLaneChildrenTest.cpp:399` (`movePointRefusesANonFiniteValueOnAnInPlaceClip`) and `:415` (`movePointCommandRefusesANonFiniteValue`).

## Changed files (committed with this report)

- `src/gui/editors/AutomationPointEdit.cpp` (fork-owned, `tests/fork-sources.txt:2098`)
- `src/core/ControlCommandsAutomationTools.cpp` (fork-owned, `tests/fork-sources.txt:1397`)
- `tests/src/core/AutomationLaneChildrenTest.cpp` (existing registered test; no CMake change)
- `docs/qwen-fixes-2026-10/cluster-new-p16-294.md` (this report)

No new file, no new control command, no `ControlCommandsSnapshot` change, no CMake change, no `upstream-modifications.txt` change (both product files are fork-owned).

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-294.r1/logs`. Every command ran from the worktree root or `build/tests`, as the brief requires.

| # | Command | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target AutomationLanesTest -j2` | 2 | `build-1-red.log` (ccache temp dir not writable) |
| 2 | same, with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-p16` | 2 | `build-2-red.log` (my test used `automationedit::` without the `gui::` qualifier) |
| 3 | same, after qualifying `gui::automationedit` | 0 | `build-3-red.log` |
| 4 | `cd build/tests && ctest -R AutomationLanesTest --output-on-failure` | 8 | `test-1-red.log` (environment: `tripleoscillator` not built, so fixtures had no parameters) |
| 5 | `cmake --build build --target tripleoscillator -j2` | 0 | `build-4-plugin.log` |
| 6 | `ctest -R AutomationLanesTest --output-on-failure` (RED, fix not applied) | 8 | `test-2-red.log`: `Totals: 21 passed, 2 failed`; the two failures are the new tests |
| 7 | fix applied; `cmake --build build --target zene AutomationLanesTest -j2` | 0 | `build-5-green.log` |
| 8 | `ctest -V -R AutomationLanesTest` (GREEN) | 0 | `test-4-green-verbose.log`: `Totals: 23 passed, 0 failed` |
| 9 | `ctest -R 'Automation\|ControlAutomation\|DelayLfoAutomation' -j2` (18 tests, before relocation) | 0 | `test-5-family-green.log`: `100% tests passed out of 18` |
| 10 | `git diff -- src/gui/editors/AutomationPointEdit.cpp src/core/ControlCommandsAutomationTools.cpp > logs/fix.patch` | 0 | `logs/fix.patch` |
| 11 | `git apply -R logs/fix.patch` (4b, first placement) | 0 | |
| 12 | `cmake --build build --target zene AutomationLanesTest -j2` (reverted) | 0 | `build-7-mutant-red.log` |
| 13 | `ctest -R AutomationLanesTest -V` (reverted, RED) | 8 | `test-6-mutant-red.log`: `Totals: 21 passed, 2 failed` |
| 14 | `git apply logs/fix.patch` | 0 | |
| 15 | rebuild + `ctest -R AutomationLanesTest -V` (GREEN) | 0 / 0 | `build-8-fixed-green.log`, `test-7-fixed-green.log`: `23 passed, 0 failed` |
| 16 | Relocation: `AutomationLanesTest.cpp` (code lines ~491 at HEAD, file-length gate red at 525 with my tests) moved to `AutomationLaneChildrenTest.cpp`; a new test file was tried first, but the lane build tree cannot be re-configured (`bunny-guard`, `configure-1.log`, exit 97) | 97 | `configure-1.log` |
| 17 | `git apply -R logs/fix.patch` (4b, final placement) | 0 | |
| 18 | `cmake --build build --target zene AutomationLaneChildrenTest -j2` (reverted) | 0 | `build-10-mutant-red.log` |
| 19 | `ctest -R AutomationLaneChildrenTest -V` (reverted, RED) | 8 | `test-9-mutant-red.log`: `Totals: 9 passed, 2 failed`; failures at `AutomationLaneChildrenTest.cpp:399`-`:415` |
| 20 | `git apply logs/fix.patch` | 0 | |
| 21 | `cmake --build build --target zene AutomationLaneChildrenTest -j2` (GREEN) | 0 | `build-11-fixed-green.log` |
| 22 | `ctest -R AutomationLaneChildrenTest -V` (GREEN) | 0 | `test-10-fixed-green.log`: `Totals: 11 passed, 0 failed` |
| 23 | `ctest -R 'Automation\|ControlAutomation\|DelayLfoAutomation' -j2 --output-on-failure` (final tree) | 0 | `test-11-family-final.log`: `100% tests passed out of 18` |
| 24 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `gates-1.log`: `PermissionError` creating `<worktree>-feature-check` (outside the write-jail); no gate ran |
| 25 | `python3 -I logs/../gate_driver.py <worktree> logs/feature-check` (same SIMPLE/RATCHET lists and regression comparison as `feature_check.py`, logs in the run folder) | 1 | `gates-2.log`: all gates PASS except `file-length` (the test file at 525 code lines; fixed by relocation in step 16) |
| 26 | same driver, final tree | 0 | `gates-3.log`: `RESULT: PASS`, 8/8 rows PASS |

Gate table (step 26, `gates-3.log`):

| Gate | Result |
|---|---|
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |

Affected-family test count: `ctest -R 'Automation|ControlAutomation|DelayLfoAutomation'` ran 18 tests, all passed (steps 9 and 23). The regression binary `AutomationLaneChildrenTest` ran 11 QTest methods, 2 of them new, all passed (step 22).

## Deviations from the brief (all reported, none hidden)

1. **ccache temp dir.** ccache's configured temp dir is outside this lane's writable set (`dsh/home/ccache-tmp-r6`, permission denied). I built with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-p16`. No source or config changed.
2. **`tripleoscillator` plugin.** The fixtures need it; it was not built in this tree. I built the `tripleoscillator` target (step 5). Without it, `parameter.isEmpty()` fails before any product code runs, including on untouched tests.
3. **Test location.** The brief asked for a new test next to the closest tests. I first put them in a new file, `AutomationPointEditTest.cpp`. CMake would have needed re-configuring, which this lane's build guard blocks, and I removed that file. I then put the tests in `AutomationLaneChildrenTest.cpp` (automation-lane tests, already registered with the plugin condition, about 273 code lines). `AutomationLanesTest.cpp` was the closest area but sits at about 491 code lines at HEAD, so adding the tests there pushes it over the file-length gate's 500-line limit.
4. **`feature_check.py` log folder.** The script writes its logs to `<worktree>/../<name>-feature-check`, outside the write-jail, so it fails with `PermissionError` (step 24). I ran the same six `SIMPLE` and two `RATCHET` commands and the same regression comparison from `logs/gate_driver.py` (it imports the lists and helpers from `feature_check.py`), with logs in `logs/feature-check/`. The gate logic is unchanged; only the log location differs.

## Self-check

1. **The regression test fails with the product change reverted and passes with it.** Steps 17-22: with `git apply -R logs/fix.patch` (EXIT 0), the two new tests fail (EXIT 8, `9 passed, 2 failed`, `test-9-mutant-red.log`). With `git apply logs/fix.patch` (EXIT 0), they pass (EXIT 0, `11 passed, 0 failed`, `test-10-fixed-green.log`). The same proof held in the first placement (steps 11-15, `21 passed, 2 failed` then `23 passed, 0 failed`).

2. **Deterministic.** No clock, sleep, thread or race. The NaN is `std::numeric_limits<float>::quiet_NaN()` / `<double>`. The clip is built at fixed ticks (10, 20) on a fixed-length automation track. The in-place test calls `gui::automationedit::movePoint` directly. The command test goes through `ControlRegistry::invoke`, which is synchronous.

3. **Fix at the cause, callers and siblings checked.**
   - Both callers of `movePoint` (`ItemListAutomationSource.cpp:101`, `AutomationEditorKeys.cpp:160`) pass through the new guard. Both in-place (`moveInPlace`) and command (`moveByCommand`) paths sit behind it.
   - The `automation.move_point` handler is the other place a NaN is accepted (`ControlCommandsAutomationTools.cpp:181`). I checked whether Qt keeps NaN as a `Double` (probe run: `isNull=0 isDouble=1`), which it does, so the handler is reachable from in-process callers.
   - **Not changed (follow-up, outside this finding):** other commands use the same two-comparison range check, which also passes NaN: `ControlCommandsAutomationTools.cpp:233`, `ControlCommandsAutomationLanes.cpp:135`, `ControlCommandsAutomationEdit.cpp:76`, `ControlCommandsPluginParams.cpp:254`. They should be checked the same way in their own cluster. This cluster only changes the move path the finding names.

4. **No collateral damage.**
   - Finite values: unchanged. The clamp still applies to every finite value, and the command's range refusal still applies.
   - Infinities in the editor path: `std::isfinite` also refuses ±∞, where previously `std::clamp` silently mapped +∞ to the range maximum. This is the behaviour the finding asks for ("reject non-finite values before clamping"). No caller in the tree produces an infinite value: the item list maps an integer 0..100 into the range, and the keys add a finite step. In the command, ±∞ was already refused (it is outside any finite range), so nothing changes there.
   - Saved projects: no load or save path changed. A NaN already stored in an old file still loads as before. A key move from such a point is now refused (it used to write NaN again); the item-list edit writes a finite value and still works.
   - Undo: a refused move returns before `addJournalCheckPoint`, so no undo step is created and the undo stack is unchanged.
   - Realtime: no audio-thread code touched (`movePoint` is an editor/command-thread path).

5. **Verdict FIXED** (not ALREADY-FIXED or FALSE-POSITIVE), so no "already fixed" claim needed proof. The original defect is reproduced by step 6, which fails on the unfixed tree.
