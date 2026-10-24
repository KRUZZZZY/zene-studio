FIXED

## Decision and code evidence

**REAL.** The constructor called `fillChannels()` once, and that function omitted channels with zero effects. The three Bind pickers now refresh their state immediately before opening: `RefreshingComboBox::showPopup()` at `src/gui/ModulatorPanel.cpp:55`, with callbacks wired at `src/gui/ModulatorPanel.cpp:198`. The refreshes retain the selected channel, stable effect ID, and parameter name when they remain available (`src/gui/ModulatorPanel.cpp:313`, `src/gui/ModulatorPanel.cpp:329`, `src/gui/ModulatorPanel.cpp:350`).

## Regression matrix

| Path | Coverage | Result |
|---|---|---|
| Normal populated path | `theEffectPickerShowsEffectsAddedWhileThePanelIsOpen` starts with a channel, an effect, and its `Gain` parameter already present. | PASS |
| Empty channel becomes selectable | `theChannelPickerShowsAChannelAfterItGainsAnEffect` opens the panel while its new channel has no effects, confirms it is absent, appends an effect to the live chain, opens the picker, selects the channel, and sees `Gain`. | PASS |
| Effect list changes while open | The effect picker sees a second appended effect and its `Mix` parameter. Reordering that selected effect updates its index while retaining its stable ID; removing its sibling refreshes the remaining entry. | PASS |
| Empty chain after removal | Removing the only effect makes that channel disappear from the picker again and clears the effect list. | PASS |
| Exhaustion | Not applicable: mixer channels are an unbounded vector and this fix adds no limit. | N/A |
| Realtime | Refresh is called by GUI combo popup handling and reads `dsp.get_state`; no process/render or audio-thread code changed. | Reviewed |
| Platform/build | Built on the configured Linux C++20 / Qt 6 tree. The implementation uses `QComboBox::showPopup`, `Qt::UserRole`, and standard C++ callbacks. Other platform builds were not run. | Linux build PASS |

The regression uses a small `PickerTestEffect` fixture appended synchronously to the real `EffectChain`; it does not depend on external plugin modules. The preexisting plugin-backed binding case skipped on this build because it loads no effect module.

## Verification evidence

The CTest family contains one registered test (`ModulatorPanelTest`, CTest #447). Its final verbose run reports 8 passed, 0 failed, and 1 skipped; both new regression slots passed. The skip is the preexisting plugin-backed case described above.

| Stage | Command (output redirected to the named log) | Exit | Evidence |
|---|---|---:|---|
| Reverse only the product diff | `git diff -- src/gui/ModulatorPanel.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-109.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-109.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 | `fix.patch` |
| RED rebuild | `cmake --build build --target zene ModulatorPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-109.r1/logs/build-13.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Rebuilt with product diff reversed |
| RED test (run from `build/tests`) | `ctest -R '^ModulatorPanelTest$' --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-109.r1/logs/test-15.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 | Both new picker tests fail without the fix |
| Restore product diff | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-109.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Product fix restored |
| GREEN rebuild | `cmake --build build --target zene ModulatorPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-109.r1/logs/build-14.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Rebuilt with fix |
| GREEN test (run from `build/tests`) | `ctest -R '^ModulatorPanelTest$' --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-109.r1/logs/test-16.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | 1/1 CTest target passed; 8 passed, 0 failed, 1 skipped |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-109 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-109.r1/logs/gates-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | All eight gate rows PASS |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-109 new-p17-109 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-109.r1/logs/submit-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | PASS; all ASK items are answered in Self-check |

The first feature-gate run returned exit 1 because the checked-in `tests/all-sources.txt` contained 1,148 vendored sfizz/Abseil source entries that its documented recipe excludes. I regenerated its entry list from that recipe; `fork-sources` and `all-sources-reproduce` then passed. No gate thresholds or baselines changed.

## Files in the commit

- `src/gui/ModulatorPanel.cpp`
- `tests/src/gui/ModulatorPanelTest.cpp`
- `tests/all-sources.txt` (regenerated from its documented recipe to restore the source-manifest gate)
- `docs/qwen-fixes-2026-10/cluster-new-p17-109.md`

## Self-check

- **Regression proof:** with only the production diff reversed, the rebuilt test failed with exit 8; after reapplying the same patch, the rebuilt test passed with exit 0. See `build-13.log`, `test-15.log`, `build-14.log`, and `test-16.log` above.
- **Determinism:** no sleeps, timestamps, or races. The test appends fixture effects directly to the live chain and opens the combo popup synchronously.
- **Cause and siblings:** `fillChannels`, `fillEffects`, and `fillParameters` are now refreshed at their corresponding picker popup. Existing constructor initialization and channel/effect selection connections remain in place. Stable IDs preserve a selected effect across reorder; a removed item falls back to an available entry.
- **Collateral and saved data:** the change is confined to the GUI picker widgets and their list refreshes. It adds no command, serialized field, limit, or project save/load behavior; old project data continues through the unchanged loader and renderer paths.
- **Realtime and undo:** no audio-thread code or undo path changed; the refresh runs from GUI interaction, and no control command was added.
- **Gate exit codes:** final `feature_check.py` exit 0 and `submit_check.py` exit 0.
