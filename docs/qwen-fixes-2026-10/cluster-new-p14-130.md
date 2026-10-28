BLOCKED - the new regression test has never been compiled or run (build tree needs reconfiguring); the fix is written but its RED/GREEN proof is not recorded.

# Cluster new-p14-130: LADSPA browser clears its row to -1

## Finding
`plugins/LadspaBrowser/LadspaDescription.cpp` `rowChanged()` indexed `m_pluginKeys[_pluginIndex]` without a range check. `QListWidget::currentRowChanged` reports -1 when its current item is cleared (for example by `QListWidget::clear()`), so the slot read `m_pluginKeys[-1]`.

## Evidence in the tree (current branch, pre-fix line numbers)
- `plugins/LadspaBrowser/LadspaDescription.cpp:166-170` (original): `rowChanged` had no guard before `m_pluginKeys[_pluginIndex]`.
- `plugins/LadspaBrowser/LadspaDescription.cpp:86-87`: `currentRowChanged(int)` is connected to `rowChanged(int)`.
- `plugins/LadspaBrowser/LadspaDescription.cpp:74-81`: `m_pluginKeys` and `pluginNames` are pushed together, so they stay the same length.
- `plugins/LadspaBrowser/LadspaDescription.cpp:104-108`: the constructor's `m_pluginKeys[0]` is guarded by `pluginList->count() > 0`.
- `include/LadspaManager.h:47`: `ladspa_key_t` is `QPair<QString, QString>`, so an out-of-range read copies garbage QStrings (the failure can be a crash, not only a wrong value).

Verdict basis: the finding is REAL in the sense that the slot indexes with an unguarded signed value that Qt can emit. Whether the bad read is reachable by a user click was not established. Single-selection clicks do not clear the current item, so the trigger is the list being cleared programmatically. I have not proven a user-visible path.

## Fix (written, not verified by build)
`plugins/LadspaBrowser/LadspaDescription.cpp` `rowChanged()`: return when `_pluginIndex < 0 || _pluginIndex >= m_pluginKeys.size()`, so the last valid selection is kept. The `m_pluginKeys[0]` read in the constructor and the double-click path are unchanged and were checked for consistency.

## Regression test (written, never compiled)
`tests/src/gui/LadspaDescriptionTest.cpp`, registered in `tests/CMakeLists.txt` (source list, a `LadspaDescriptionTest` source/fixture block mirroring `LadspaPortDialogTest`, and the offscreen environment) and in `tests/all-sources.txt`. Cases:
1. `selectedRowIsDescribedAndDoubleClickEmitsIt` - normal path; the fixture plugin is row 0 and double-click emits its key. Expected to pass before and after the fix.
2. `clearingTheRowKeepsTheLastSelection` - `list->clear()` then checks the description widget pointer is unchanged and double-click still emits the fixture key. Expected to FAIL before the fix (`update()` replaces the widget, or the UB read crashes).
3. `rowsOutsideTheKeyListAreIgnored` - `invokeMethod` of `rowChanged(-1)` and `rowChanged(1)`. Expected to FAIL before the fix.
No sleeps, timing or races. The only seam is the real `QListWidget` signal; the fixture is the existing `SampleRateDependentLadspa` plugin.

`tests/upstream-modifications.txt` has a new line for `plugins/LadspaBrowser/LadspaDescription.cpp` (inherited file changed).

## Commands run and exit codes
| Step | Command | EXIT | Log |
|---|---|---|---|
| RED build attempt 1 | `cd build && cmake --build . --target LadspaDescriptionTest -j4` | 2 (`make: *** No rule to make target 'LadspaDescriptionTest'`) | `logs/build-1-red.log` |
| Reconfigure attempt | `cd build && cmake .` | 97 (bunny-guard: "configuring is the parent's job") | `logs/configure-1.log` |
| RED build attempt 2 | `cd build && cmake --build . --target LadspaDescriptionTest -j4` | 2 (same error) | `logs/build-2-red.log` |
| Gates | `python3 .../qwen/feature_check.py <worktree>` | 1 (`PermissionError` creating `zene-qwen-new-p14-130-feature-check`, a sibling directory outside the write-jail; no check ran) | `logs/gates-1.log` |
| Completion check | `python3 .../qwen/submit_check.py <worktree> new-p14-130` | 1 (FAIL: clean tree, report, self-check, tests added, gates) | `logs/submit-check-1.log` |

No RED run, no GREEN run, no mutation-revert proof (step 4b), no ctest run. These are not claimed.

## Why blocked
1. The lane build tree was configured before `tests/CMakeLists.txt` gained `LadspaDescriptionTest`. Make does not know the target, and the bunny shim refuses reconfiguring from the lane. The missing capability is a parent-side `cmake .` in `zene-qwen-new-p14-130/build` (or an `lmms_build` run by the parent). I did not run `mcp__lmms-lab__lmms_build`: it configures the worktree, which the lane is not allowed to do.
2. `feature_check.py` and `submit_check.py` write their logs to `<worktree>-feature-check`, a sibling of the worktree outside the write-jail. The gate cannot run from inside the lane.

## Next steps for the parent
- Reconfigure the build tree, then run `cmake --build . --target LadspaDescriptionTest -j4` and `ctest -R LadspaDescriptionTest --output-on-failure` from `build/tests`.
- Confirm RED: revert `plugins/LadspaBrowser/LadspaDescription.cpp` with `git diff -- plugins/LadspaBrowser/LadspaDescription.cpp > fix.patch && git apply -R fix.patch`, rebuild, expect failures in cases 2 and 3; re-apply and expect all three to pass.
- Run the gates from a place that can write its logs.
- Until those run, the test file is unverified and may not compile.

## Self-check
- Regression test FAILS without the fix / PASSES with it: NOT RUN (build blocked). Exit codes: none recorded.
- Deterministic (no timing/sleep/race): yes by inspection. Uses the real `QListWidget` signal and the fixture plugin; the pointer comparison is against a widget kept alive by `update()`.
- Fix at the cause and complete: the cause is the unguarded `m_pluginKeys` index in `rowChanged`. Checked the constructor's `m_pluginKeys[0]` (guarded by `count() > 0`, same length as `pluginNames`) and the double-click path (`LadspaBrowser.cpp:175` connects `doubleClicked`). Not exercised by a build.
- No collateral damage: the change only returns early for indices that previously caused undefined behaviour; valid rows, saved projects and the browser's plugin list are unchanged.
- Verdict is BLOCKED, not ALREADY-FIXED or FALSE-POSITIVE, so no code-citation proof of "already fixed" is needed.
