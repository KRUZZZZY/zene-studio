QA VERIFIED (2026-10-28): the finding is real; the focused test passes with the guard and fails when only the product hunk is reversed.

# Cluster new-p14-130: LADSPA browser clears its row to -1

## Finding
`plugins/LadspaBrowser/LadspaDescription.cpp` `rowChanged()` indexed `m_pluginKeys[_pluginIndex]` without a range check. `QListWidget::currentRowChanged` reports -1 when its current item is cleared (for example by `QListWidget::clear()`), so the slot read `m_pluginKeys[-1]`.

## Evidence in the tree (current branch, pre-fix line numbers)
- `plugins/LadspaBrowser/LadspaDescription.cpp:166-170` (original): `rowChanged` had no guard before `m_pluginKeys[_pluginIndex]`.
- `plugins/LadspaBrowser/LadspaDescription.cpp:86-87`: `currentRowChanged(int)` is connected to `rowChanged(int)`.
- `plugins/LadspaBrowser/LadspaDescription.cpp:74-81`: `m_pluginKeys` and `pluginNames` are pushed together, so they stay the same length.
- `plugins/LadspaBrowser/LadspaDescription.cpp:104-108`: the constructor's `m_pluginKeys[0]` is guarded by `pluginList->count() > 0`.
- `include/LadspaManager.h:47`: `ladspa_key_t` is `QPair<QString, QString>`, so an out-of-range read copies garbage QStrings (the failure can be a crash, not only a wrong value).

Verdict basis: the finding is REAL. `currentRowChanged(int)` can report -1 when the current row is cleared; the regression test exercises the actual list signal connected to this slot and the unguarded read crashes when the guard is reversed.

## Fix
`plugins/LadspaBrowser/LadspaDescription.cpp` `rowChanged()`: return when `_pluginIndex < 0 || _pluginIndex >= m_pluginKeys.size()`, so the last valid selection is kept. The `m_pluginKeys[0]` read in the constructor and the double-click path are unchanged and were checked for consistency.

## Regression test
`tests/src/gui/LadspaDescriptionTest.cpp`, registered in `tests/CMakeLists.txt` (source list, a `LadspaDescriptionTest` source/fixture block mirroring `LadspaPortDialogTest`, and the offscreen environment) and in `tests/all-sources.txt`. Cases:
1. `selectedRowIsDescribedAndDoubleClickEmitsIt` - normal path; the fixture plugin is row 0 and double-click emits its key. Expected to pass before and after the fix.
2. `clearingTheRowKeepsTheLastSelection` - `list->clear()` then checks the description widget pointer is unchanged and double-click still emits the fixture key. The reversed run confirms this case crashes without the guard.
3. `rowsOutsideTheKeyListAreIgnored` - invokes `rowChanged(-1)` and `rowChanged(1)` and checks the description widget is unchanged.
No sleeps, timing or races. The test uses the real `QListWidget` signal; the fixture is the existing `SampleRateDependentLadspa` plugin.

`tests/upstream-modifications.txt` has a new line for `plugins/LadspaBrowser/LadspaDescription.cpp` (inherited file changed).

## Commands run and exit codes
| Step | Command | EXIT | Log |
|---|---|---|---|
| RED build attempt 1 | `cd build && cmake --build . --target LadspaDescriptionTest -j4` | 2 (`make: *** No rule to make target 'LadspaDescriptionTest'`) | `logs/build-1-red.log` |
| Reconfigure attempt | `cd build && cmake .` | 97 (bunny-guard: "configuring is the parent's job") | `logs/configure-1.log` |
| RED build attempt 2 | `cd build && cmake --build . --target LadspaDescriptionTest -j4` | 2 (same error) | `logs/build-2-red.log` |
| Gates | `python3 .../qwen/feature_check.py <worktree>` | 1 (`PermissionError` creating `zene-qwen-new-p14-130-feature-check`, a sibling directory outside the write-jail; no check ran) | `logs/gates-1.log` |
| Completion check | `python3 .../qwen/submit_check.py <worktree> new-p14-130` | 1 (FAIL: clean tree, report, self-check, tests added, gates) | `logs/submit-check-1.log` |

The lane could not run the test before submission because the generated build tree predated this target. Parent QA completed the build, GREEN, and mutation-revert checks below.

## Why the fixer submitted BLOCKED
1. The lane build tree was configured before `tests/CMakeLists.txt` gained `LadspaDescriptionTest`. Make does not know the target, and the bunny shim refuses reconfiguring from the lane. The missing capability is a parent-side `cmake .` in `zene-qwen-new-p14-130/build` (or an `lmms_build` run by the parent). I did not run `mcp__lmms-lab__lmms_build`: it configures the worktree, which the lane is not allowed to do.
2. `feature_check.py` and `submit_check.py` write their logs to `<worktree>-feature-check`, a sibling of the worktree outside the write-jail. The gate cannot run from inside the lane.

## Parent QA verification (2026-10-28)

| Step | Command | EXIT | Result |
|---|---|---:|---|
| Configure updated test target | `/usr/bin/cmake -S . -B build` | 0 | Configured successfully; the lane shim had blocked configuration. |
| Build fixed code | `cmake --build build --target zene ladspabrowser LadspaDescriptionTest -j12` | 0 | App, shipping LADSPA browser module and regression test built. |
| Fixed regression | `cd build/tests && ctest -R LadspaDescriptionTest --output-on-failure` | 0 | 1/1 passed. |
| Reverse only product hunk | `git diff 37eaf07557842a34c66bd34f6706cb0c31b0c737..11f4f5165affcb0d14d34f09b756ccc304a8573b -- plugins/LadspaBrowser/LadspaDescription.cpp > /tmp/new-p14-130-product.patch && git apply -R /tmp/new-p14-130-product.patch` | 0 | Only the guard in the product file was reversed; tests stayed unchanged. |
| Build reversed code | `cmake --build build --target zene LadspaDescriptionTest -j12` | 0 | Rebuilt successfully. |
| Reversed regression | `cd build/tests && ctest -R LadspaDescriptionTest --output-on-failure` | 8 | Failed with SIGSEGV in `clearingTheRowKeepsTheLastSelection`; the normal-row case passed first. |
| Restore and rebuild | `git checkout -- plugins/LadspaBrowser/LadspaDescription.cpp`; then `cmake --build build --target zene ladspabrowser LadspaDescriptionTest -j12` | 0 | Restored committed guard and rebuilt all touched targets. |
| Restored regression | `cd build/tests && ctest -R LadspaDescriptionTest --output-on-failure` | 0 | 1/1 passed. |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-130` | 0 | PASS; no new complexity or file-length items in touched files. |

## Self-check
- Regression test FAILS without the fix / PASSES with it: verified; reversed ctest EXIT 8 and restored ctest EXIT 0.
- Deterministic (no timing/sleep/race): verified in the focused test; clearing the list emits the connected row change and the reversed code crashes in that case.
- Fix at the cause and complete: the cause is the unguarded `m_pluginKeys` index in `rowChanged`. The constructor's `m_pluginKeys[0]` is guarded by `count() > 0`, and double-click emits the last valid `m_currentSelection` (`LadspaBrowser.cpp:175` connects the signal). All touched targets built.
- No collateral damage: the change only returns early for indices that previously caused undefined behaviour; valid rows, saved projects and the browser's plugin list are unchanged.
- Final review status: finding REAL and fix verified; the original BLOCKED status was resolved by the parent-side build and test run above.
