FIXED

# Cluster new-p16-294 - NaN automation point values pass the clamp

## Verdict

**FIXED.** The finding is real. `automationedit::movePoint` clamped its value with `std::clamp`, which returns NaN unchanged, and the in-place path stored it as the point's value. The same two-comparison range check (`value < min || value > max`) that the `automation.move_point` command uses also lets NaN through, and so do three sibling point writers: `automation.add_point`, `automation.write_to_end` and `automation.draw_shape` (its bounds). All four now refuse NaN before any write.

## Evidence (file:line, current tree)

Base `639e1d23e`, before the fix:

- `src/gui/editors/AutomationPointEdit.cpp:103` (base): `toValue = std::clamp(toValue, min, max);`. `std::clamp` returns `v` when every comparison is false, so NaN comes back as NaN. `movePoint` then hands it to `moveInPlace`, which stores it at `:78` (base) through `clip->putValue(TimePos(toTick), toValue, false)`.
- `src/core/AutomationClip.cpp:423`: `putValue` assigns `m_timeMap[newTime] = AutomationNode(this, value, newTime);` with no finiteness check, so NaN is stored.
- `src/core/ControlCommandsAutomationTools.cpp:179` (base, `movePoint`): `if (value < min || value > max)`. NaN passes both comparisons.
- `src/core/ControlCommandsAutomationEdit.cpp:76` (base, `automationAddPoint`), `src/core/ControlCommandsAutomationTools.cpp:233` (base, `writeToEnd`), `src/core/ControlCommandsAutomationLanes.cpp:135` (base, `shapeBoundsFit`): the same two-comparison check.
- Qt keeps a NaN as a `Double` (probe run: `QJsonValue(nan).isNull()=0 isDouble=1`), so an in-process caller's NaN (a script's 0/0, say) reaches these handlers.

After the fix:

- `src/gui/editors/AutomationPointEdit.cpp:104-108`: `if (!std::isfinite(toValue)) { *error = tr("the value is not a finite number"); return false; }`, placed before the clamp. Both editor callers go through it: `src/gui/editors/ItemListAutomationSource.cpp:101` and `src/gui/editors/AutomationEditorKeys.cpp:160`. Both the command path (`moveByCommand`) and the in-place path (`moveInPlace`) sit behind it.
- The four command checks are written as "inside the range" rather than "outside it", so NaN (which fails both comparisons) is refused with the existing range refusal:
  - `src/core/ControlCommandsAutomationTools.cpp:180` (`automation.move_point`, `movePoint`), refusal before `clip->addJournalCheckPoint()` at `:189`, so a refused move creates no undo step.
  - `src/core/ControlCommandsAutomationTools.cpp:234` (`automation.write_to_end`, `writeToEnd`).
  - `src/core/ControlCommandsAutomationEdit.cpp:77` (`automation.add_point`, `automationAddPoint`), refusal before `automationClipForEdit`, so a refused add creates no track or clip.
  - `src/core/ControlCommandsAutomationLanes.cpp:136` (`automation.draw_shape`, `shapeBoundsFit`), checked before `editableClip`, so a refused draw creates no track or clip.
- Written this way, each check keeps the base branch count (`!(a && b)` = `a || b` in CCN), so the complexity gate stays green.
- Regression tests: `tests/src/core/AutomationLaneChildrenTest.cpp:399` (`movePointRefusesANonFiniteValueOnAnInPlaceClip`), `:415` (`movePointCommandRefusesANonFiniteValue`), `:435` (`addPointCommandRefusesANonFiniteValue`), `:450` (`writeToEndCommandRefusesANonFiniteValue`), `:468` (`drawShapeCommandRefusesANonFiniteBound`).

## Changed files

Product (all fork-owned: `tests/fork-sources.txt` lists `src/gui/editors/AutomationPointEdit.cpp`, `src/core/ControlCommandsAutomationTools.cpp`, and the Edit and Lanes files):

- `src/gui/editors/AutomationPointEdit.cpp`
- `src/core/ControlCommandsAutomationTools.cpp`
- `src/core/ControlCommandsAutomationEdit.cpp`
- `src/core/ControlCommandsAutomationLanes.cpp`

Tests and docs:

- `tests/src/core/AutomationLaneChildrenTest.cpp` (existing registered test; no CMake change)
- `docs/qwen-fixes-2026-10/cluster-new-p16-294.md` (this report)

No new file, no new control command, no `ControlCommandsSnapshot` change, no CMake change, no `upstream-modifications.txt` change.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-294.r1/logs`. Commands ran from the worktree root or `build/tests`.

Part A: the move path (first commit, `a2b0a8576`).

| # | Command | EXIT | Log / result |
|---|---|---|---|
| 1 | `cmake --build build --target AutomationLanesTest -j2` | 2 | `build-1-red.log`: ccache temp dir outside the writable set |
| 2 | same with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-p16` | 2 | `build-2-red.log`: test used `automationedit::` without `gui::` |
| 3 | same after qualifying | 0 | `build-3-red.log` |
| 4 | `ctest -R AutomationLanesTest --output-on-failure` | 8 | `test-1-red.log`: environment (`tripleoscillator` not built, so fixtures had no parameters) |
| 5 | `cmake --build build --target tripleoscillator -j2` | 0 | `build-4-plugin.log` |
| 6 | `ctest -R AutomationLanesTest` (RED, fix not applied) | 8 | `test-2-red.log`: `21 passed, 2 failed`; the two failures are the new tests |
| 7 | fix applied, `cmake --build build --target zene AutomationLanesTest -j2` | 0 | `build-5-green.log` |
| 8 | `ctest -V -R AutomationLanesTest` (GREEN) | 0 | `test-4-green-verbose.log`: `23 passed, 0 failed` |
| 9 | `ctest -R 'Automation\|ControlAutomation\|DelayLfoAutomation' -j2` (18 tests, first placement) | 0 | `test-5-family-green.log`: `100% tests passed out of 18` |
| 10 | 4b proof, first placement: `git diff -- <2 product files> > logs/fix.patch`; `git apply -R logs/fix.patch` | 0, 0 | |
| 11 | rebuild, `ctest -R AutomationLanesTest -V` (reverted, RED) | 0 / 8 | `test-6-mutant-red.log`: `21 passed, 2 failed` |
| 12 | `git apply logs/fix.patch`, rebuild, `ctest -V` (GREEN) | 0, 0 | `test-7-fixed-green.log`: `23 passed, 0 failed` |
| 13 | Relocation: `AutomationLanesTest.cpp` was ~491 code lines at base and went over the file-length limit with my tests (525 when measured), so the tests moved to `AutomationLaneChildrenTest.cpp` (~273 code lines). A new test file was tried first; CMake re-configure is blocked for this lane (`bunny-guard`, `configure-1.log`, exit 97), so I removed it. | 97 | `configure-1.log` |
| 14 | 4b proof, final placement, reverted: `ctest -R AutomationLaneChildrenTest -V` | 8 | `test-9-mutant-red.log`: `9 passed, 2 failed` |
| 15 | reapplied, rebuilt, GREEN | 0, 0 | `test-10-fixed-green.log`: `11 passed, 0 failed` |
| 16 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `gates-1.log`: `PermissionError` on `<worktree>-feature-check` (outside the write-jail); no gate ran |
| 17 | `python3 -I gate_driver.py <worktree> logs/feature-check` (see Deviations) | 1 | `gates-2.log`: all PASS but `file-length` (525 code lines), fixed by step 13 |
| 18 | same, after relocation | 0 | `gates-3.log`: 8/8 PASS |
| 19 | family, final tree (move fix only) | 0 | `test-11-family-final.log`: 18/18 |

Part B: the sibling commands (second commit).

| # | Command | EXIT | Log / result |
|---|---|---|---|
| 20 | new sibling tests added (`add_point`, `write_to_end`, `draw_shape`); RED on the tree with the move fix and no sibling fix | 8 | `test-12-sibling-red.log`: `11 passed, 3 failed` at lines 444, 460, 476 |
| 21 | sibling guards added; GREEN (write_to_end test corrected to compare point counts, since a fresh clip holds its own tick-0 point, `AutomationClip.cpp:211`) | 0 | `test-14-sibling-green.log`: `14 passed, 0 failed` |
| 22 | 4b proof, sibling diff reverted: `git apply -R logs/fix2.patch`; `ctest -R AutomationLaneChildrenTest -V` | 8 | `test-15-sibling-mutant.log`: `11 passed, 3 failed` |
| 23 | reapplied; `ctest -V` | 0 | `test-16-sibling-fixed.log`: `14 passed, 0 failed` |
| 24 | gates: complexity FAIL, CCN 11 for the new branches in `movePoint` and `writeToEnd` (`gates-4.log`) | 1 | fixed by writing the four checks as "inside the range" (same CCN as base) |
| 25 | 4b proof vs base, all product changes: `git diff 639e1d23e -- <4 product files> > logs/fix-full.patch`; `git apply -R logs/fix-full.patch`; `ctest -R AutomationLaneChildrenTest -V` | 8 | `test-19-full-mutant.log`: `9 passed, 5 failed` (all five new tests fail) |
| 26 | `git apply logs/fix-full.patch`; rebuild; `ctest -V` | 0, 0 | `test-20-full-fixed.log`: `14 passed, 0 failed` |
| 27 | family, final tree: rebuild 18 targets + `zene` (`build-21-family-final2.log`), `ctest -R 'Automation\|ControlAutomation\|DelayLfoAutomation' -j2` | 0, 0 | `test-21-family-final2.log`: `100% tests passed out of 18` |
| 28 | gate driver, final tree | 0 | `gates-5.log`: `RESULT: PASS`, 8/8 rows PASS |
| 29 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`, final tree | 1 | `gates-4-direct.log`: same `PermissionError` as step 16 |

Gate table (step 28, `gates-5.log`):

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

Affected-family test count: `ctest -R 'Automation|ControlAutomation|DelayLfoAutomation'` ran 18 tests, all passed (steps 9, 19, 27). The regression binary `AutomationLaneChildrenTest` ran 14 QTest methods, 5 of them new, all passed (step 26).

## Deviations from the brief (all reported)

1. **ccache temp dir.** ccache's configured temp dir is outside this lane's writable set, so I built with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-p16`. No source or config changed.
2. **`tripleoscillator` plugin.** The fixtures need it and it was not built in this tree. I built the `tripleoscillator` target (step 5). Without it, `parameter.isEmpty()` fails before any product code runs, including on untouched tests.
3. **Test location.** The brief asked for a new test next to the closest tests. A new test file needs CMake re-configuration, which this lane's build guard blocks (step 13). The tests therefore sit in `AutomationLaneChildrenTest.cpp`. `AutomationLanesTest.cpp` is the closest area but sits at about 491 code lines, so adding the tests there breaks the file-length gate's 500-line limit.
4. **`feature_check.py` log folder.** The script writes its logs to `<worktree>/../<name>-feature-check`, outside the write-jail, so it fails with `PermissionError` (steps 16 and 29). `submit_check.py` runs that script, so its `gates (feature_check.py)` item will show FAIL for this reason alone. I ran the same six `SIMPLE` and two `RATCHET` commands and the same regression comparison from `logs/gate_driver.py` (it imports the lists and helpers from `feature_check.py`), with logs in `logs/feature-check/`. Gate logic unchanged; only the log location differs. All eight rows pass.
5. **Sibling scope (added).** The brief asks to check siblings. Three sibling point writers (`add_point`, `write_to_end`, `draw_shape` bounds) had the same NaN gap, so I fixed them in the same way rather than leave an inconsistent set. This is a judgement call, and the change is listed here so it can be reviewed.
6. **Refusal message.** For NaN, the command refusals now use the existing range message (`value nan is outside the range ...`), since NaN is not inside any range. The editor path (`AutomationPointEdit.cpp`) has its own message, `the value is not a finite number`.

## Self-check

1. **The regression tests fail with the product change reverted and pass with it.**
   - Move path: steps 11 and 14 (RED, exit 8: `21/2` and `9/2`); steps 12 and 15 (GREEN, exit 0).
   - Sibling path and full set: step 22 (RED, exit 8, `11/3`) and step 25 (RED vs base, exit 8, `9/5`); steps 23 and 26 (GREEN, exit 0, `14/0`).

2. **Deterministic.** No clock, sleep, thread or race. The NaN is `std::numeric_limits<float>::quiet_NaN()` or `<double>`. Clips are built at fixed ticks (10, 20, 288) on fixed-length automation tracks. The in-place test calls `gui::automationedit::movePoint` directly. The command tests go through `ControlRegistry::invoke`, which is synchronous. Counts are compared before and after (`write_to_end`), not read from a clock.

3. **Fix at the cause, callers and siblings checked.**
   - Callers of `movePoint`: `ItemListAutomationSource.cpp:101` and `AutomationEditorKeys.cpp:160`. Both go through the guard at `AutomationPointEdit.cpp:104`, which sits before both the command and the in-place path.
   - Siblings: every `automation.*` command in the registry snapshot (22 commands) was checked against the change.
     - Changed: `automation.move_point` (`:180`), `automation.add_point` (`ControlCommandsAutomationEdit.cpp:77`), `automation.write_to_end` (`:234`), `automation.draw_shape` (`ControlCommandsAutomationLanes.cpp:136`, bounds), each with a regression test.
     - No range check at all in the command files, so no NaN path from this change: `automation.add_trim`, `automation.clear`, `automation.get_state`, `automation.lane_add`, `automation.lanes_set_collapsed`, `automation.last_touched`, `automation.latency_compensation`, `automation.mode_set`, `automation.ramp_get`, `automation.ramp_set`, `automation.record_mode_set`, `automation.remove_point`, `automation.set_curve`, `automation.switches`, `automation.thin`, `automation.transform`, `automation.trim_get`, `automation.trim_set`.
     - Not verified in this lane: whether the commands in the last group take a NaN through some other path (for example, `automation.ramp_set`, `automation.transform`, `automation.trim_set`). That is a follow-up for their own cluster.
   - Outside the `automation.*` group, `src/core/ControlCommandsPluginParams.cpp:254` has the same two-comparison check. It is not changed here and is a follow-up.

4. **No collateral damage.**
   - Finite values: unchanged. The clamp still applies to every finite editor value; command range refusals still apply to out-of-range finite values with the same message.
   - Infinities in the editor path: `AutomationPointEdit.cpp:104` refuses ±∞, where `std::clamp` used to map +∞ to the range maximum. This is the behaviour the finding asks for. No caller produces an infinity: the item list maps an integer 0..100 into the range, and the keys add a finite step. In the commands, ±∞ was already refused (outside any finite range), so nothing changes there.
   - Saved projects: no load or save path changed. A NaN already stored in an old file still loads as before. A key move from such a point is now refused (it used to write NaN again); an item-list edit writes a finite value and still works.
   - Undo: refused moves and adds return before any journal checkpoint or clip creation, so no undo step appears and no track or clip is created.
   - Realtime: no audio-thread code touched. All four paths are editor or command-thread paths.

5. **Verdict FIXED** (not ALREADY-FIXED or FALSE-POSITIVE), so no "already fixed" claim needed proof. The defect is reproduced by step 6 (move path) and step 20 (sibling paths), both failing on the unfixed tree.
