FIXED

new-p16-9: FIXED

Evidence: `pluginAb` always added a slot undo step after `storeSlot`, even when slot XML, parameter snapshots, and selected slot were unchanged (`src/core/ControlCommandsDeviceCopy.cpp:411-423`). `DeviceAbUndoTest::anUnchangedStoreDoesNotShieldThePreviousStoreUndo` stores A, edits the parameter, stores the changed A, repeats the same store, then undoes and recalls the original A.

- RED: `cmake --build build --target zene DeviceAbUndoTest` — exit 0 (`logs/004-build-red-noop-with-plugin.log`); `ctest -R DeviceAbUndoTest --output-on-failure` from `build/tests` — exit 8, 5 passed / 1 failed at the expected parameter comparison (`logs/005-test-red-noop-with-plugin.log`).
- GREEN: `cmake --build build --target zene DeviceAbUndoTest` — exit 0 (`logs/006-build-green-noop.log`); `ctest -R DeviceAbUndoTest --output-on-failure` from `build/tests` — exit 0 (`logs/007-test-green-noop.log`). The full binary later reported 7 passed, 0 failed, 0 skipped (`logs/034-test-final-full-device-ab.log`).
- Reverse-patch proof: `git diff -- src/core/ControlCommandsDeviceCopy.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-9.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-9.r1/logs/fix.patch` — exit 0 (`logs/008-proof-noop-reverse.log`); rebuild — exit 0 (`logs/009-build-noop-without-fix.log`); same ctest command — exit 8, same regression assertion (`logs/010-test-noop-without-fix.log`). `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-9.r1/logs/fix.patch` — exit 0 (`logs/011-proof-noop-reapply.log`); rebuild — exit 0 (`logs/012-build-noop-with-fix.log`); same ctest command — exit 0 (`logs/013-test-noop-with-fix.log`).
- The comparison includes A/B XML, parameter values, and `onB`, so selecting a different slot remains a recorded change.

review-223cd64ed: ALREADY-FIXED

Evidence: commit `0dcbe31b0` already changed store undo/redo to capture `AbRecorded` and call `restoreSlots`; `findRecorded` resolves the live effect by chain, slot, and plugin, and `claimAbSlots` rebinds its owner (`src/core/ControlCommandsDeviceCopy.cpp:313-350, 427-428`). `DeviceAbUndoTest::aStoredSlotSurvivesTheDeviceBeingRecreated` stores, unloads, undoes the unload, asserts the rebuilt effect ID differs, then undoes/redoes the store and recalls its state (`tests/src/core/DeviceAbUndoTest.cpp:122-153`).

- Red proof: temporarily used the prior numeric-ID store closures. The first build attempt failed because the now-unused `restoreSlots` triggered `-Werror` (exit 2, `logs/028-build-review-without-fix.log`); after marking that helper unused for the temporary reproduction, `cmake --build build --target zene DeviceAbUndoTest` exited 0 (`logs/030-build-review-without-fix.log`). `./DeviceAbUndoTest aStoredSlotSurvivesTheDeviceBeingRecreated` from `build/tests` exited 1 at the expected `has_a` assertion (`logs/031-test-review-without-fix.log`). The current `0dcbe31b0` implementation was restored.
- GREEN: `cmake --build build --target zene DeviceAbUndoTest ReversibilityUndoDeviceTest ScriptDawRemovalUndoTest ScriptDawBindingTest` — exit 0 (`logs/032-build-final-after-review-proof.log`); `ctest -R 'DeviceAbUndoTest|ReversibilityUndoDeviceTest|ScriptDawRemovalUndoTest|ScriptDawBindingTest' --output-on-failure` from `build/tests` — exit 0, 4/4 tests (`logs/033-test-final-after-review-proof.log`).

new-p14-22: FIXED

Evidence: `forgetDestroyedAbSlots` erases the live record when an effect is destroyed (`src/core/ControlCommandsDeviceCopy.cpp:275-295`), while `journalRemovalDocument` rebuilt the effect from device XML without carrying session A/B state. It now captures the owned A/B state for a recordable removal, rebinds it after successful attachment, and includes the snapshot in structural undo byte accounting (`src/core/ControlCommandsDeviceCopy.cpp:492-524`, `src/core/ControlStructuralSupport.cpp:713-753`). The snapshot restore runs after the audio change guard is released.

- RED: `cmake --build build --target zene DeviceAbUndoTest` — exit 0 (`logs/015-build-red-unload-slots.log`); `ctest -R DeviceAbUndoTest --output-on-failure` from `build/tests` — exit 8, 6 passed / 1 failed because restored slot A was absent (`logs/016-test-red-unload-slots.log`).
- GREEN: `cmake --build build --target zene DeviceAbUndoTest` — exit 0 (`logs/017-build-green-unload-slots.log`); same ctest command — exit 0 (`logs/018-test-green-unload-slots.log`).
- Reverse-patch proof: `git diff -- src/core/ControlCommandsDeviceCopy.cpp src/core/ControlDeviceTools.h src/core/ControlStructuralSupport.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-9.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-9.r1/logs/fix.patch` — exit 0 (`logs/022-proof-unload-reverse.log`); `cmake --build build --target zene DeviceAbUndoTest` — exit 0 (`logs/023-build-unload-without-fix.log`); `./DeviceAbUndoTest storedSlotsSurviveUnloadUndo` from `build/tests` — exit 1 at the expected missing-slot assertion (`logs/024-test-unload-without-fix.log`). Reapply — exit 0 (`logs/025-proof-unload-reapply.log`); rebuild and the four-test ctest family both exited 0 (`logs/026-build-final-device-family.log`, `logs/027-test-final-device-family.log`).
- Failure and capacity paths are unchanged: removal capture refusal returns before an A/B snapshot or undo step is recorded. `ReversibilityUndoDeviceTest`, `ScriptDawRemovalUndoTest`, and `ScriptDawBindingTest` passed after the change.

## Final test and gate results

| Check | Result |
|---|---|
| `DeviceAbUndoTest` full binary | 7 passed, 0 failed, 0 skipped; exit 0 (`logs/034-test-final-full-device-ab.log`) |
| `DeviceCopyTest` full binary | 16 passed, 0 failed, 0 skipped; exit 0 (`logs/043-test-devicecopy-detail-final.log`) |
| A/B, device copy, and structural removal ctest family | 5/5 passed; exit 0 (`logs/049-test-after-complexity-helper.log`) |
| `feature_check.py` final report run | all eight gates PASS; exit 0 (`logs/054-feature-check-final-report.log`) |
| `submit_check.py` after the complexity fix | PASS; exit 0 (`logs/053-submit-check-after-complexity-fix.log`) |

The first feature gate run failed because the train snapshot had reintroduced 1,148 vendored `plugins/SfzPlayer/sfizz/` paths excluded by `tests/all-sources.txt`'s published recipe. I re-derived that manifest with the recipe; the final gate run passed `fork-sources` and `all-sources-reproduce`. The regeneration output is `logs/036-all-sources-derived.txt`.

The first post-commit submit check found `pluginAb` at CCN 11. I extracted the slot comparison into `sameAbSlots`; the subsequent feature and submit checks passed.

The `DeviceAbUndoTest` target now depends on the amplifier module so its A/B regressions execute instead of skipping when the target is built. The initial no-module attempt was a skip and is not counted as regression evidence (`logs/002-test-red-noop.log`, `logs/003-test-noop-detail.log`).

## Self-check

- RED/GREEN per bug: new-p16-9 failed at its undo/value assertion without the store comparison and passed after restore; review-223cd64ed failed at the slot assertion with the old numeric-ID closures and passed with the branch's existing resolver; new-p14-22 failed at `has_a` without the removal snapshot and passed after it was restored. Exact commands and exit codes are listed above.
- Determinism: tests use loaded plugin parameters and explicit command order. No sleeps, wall-clock values, or races are used; `sendPostedEvents(...DeferredDelete)` deterministically completes the destruction before undo.
- Undo and sibling coverage: unchanged stores now take no step, changed stores still take one; unload retains one existing structural undo step. `plugin.ab recall` / `toggle` use their existing state-restore path. `journalRemovalDocument` callers checked were `plugin.unload` (`src/core/ControlCommandsPlugin.cpp:357`), script effect removal (`src/core/ScriptDawEdit.cpp:234`), and track preset replacement (`src/core/ControlCommandsTrackPresets.cpp:118`); the shared removal recorder handles them consistently. `DeviceCopyTest` passed all 16 cases.
- Sibling command audit from the checker: `chain.apply` (ControlCommandsChainEdit.cpp) restores a chain XML checkpoint; `chain.bypass_all` / `chain.clean` (ControlCommandsMixerTools.cpp) change bypass state or use `removeDevicesRecorded`; `chain.copy` uses the independent copy/replacement path; `chain.get_state`, `chain.list`, `chain.save`, `chain.remove`, and `chain.rename` operate on preset files; `chain.receive` is routing setup in ControlCommandsW3Engine.cpp. None calls the `pluginAb` store branch; the removal snapshot change is confined to callers of `journalRemovalDocument`. No command schema or registration changed. The existing `DeviceCopyTest` exercised `chain.copy` and `chain.clean` with 16 passed, 0 skipped.
- Realtime and saved data: the new capture/restore helpers run on the command/undo path; restoration occurs outside `requestChangesGuard`. No render code, project serialization format, or saved project data changed. A/B slots remain session state.
- Portability: the configured Linux C++20 / Qt build passed. The change uses standard `shared_ptr`, `QString`, `QChar`, and `std::vector` APIs and adds no platform-specific code; MSVC and macOS builds were not run in this worktree.
- Gate follow-through: the first submit check reported the complexity item above; after extracting `sameAbSlots`, `feature_check.py` passed (`logs/052-feature-check-pre-followup-commit.log`) and `submit_check.py` passed (`logs/053-submit-check-after-complexity-fix.log`).

## Files committed

- `src/core/ControlCommandsDeviceCopy.cpp`
- `src/core/ControlDeviceTools.h`
- `src/core/ControlStructuralSupport.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/src/core/DeviceAbUndoTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p16-9.md`
