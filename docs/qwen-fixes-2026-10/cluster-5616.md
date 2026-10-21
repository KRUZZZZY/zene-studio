FIXED

Evidence:
- src/core/ControlCommandsChainEdit.cpp:233-249: chain.rename recorded addUndoStep even when toPath == from (no-op rename). Fix returns a no-change success result without recording an undo step or __transaction.
- Regression test: tests/src/core/ControlChainPresetTest.cpp::renamingToTheCurrentNameRecordsNoUndoStep.

Commands:
- RED: `QT_QPA_PLATFORM=offscreen ZENE_CONTROL_CHECK_RESULTS=set:1 ./ControlChainPresetTest renamingToTheCurrentNameRecordsNoUndoStep` -> exit 1, FAIL `storeNames().contains(kPreset)`.
- GREEN: same command -> exit 0, 3 passed, 0 failed.
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5616` -> RESULT: PASS.

## Self-check

- Regression test FAILS with product change reverted and PASSES with it: RED run `.../ControlChainPresetTest renamingToTheCurrentNameRecordsNoUndoStep` exit 1 (FAIL storeNames().contains(kPreset)); GREEN run exit 0 (3 passed). Recorded in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5616.r1/logs/test-red4.log and test-green... (exit 0).
- Deterministic: no sleeps/timing/races; uses control.set_undo_coalescing with window_ms 0 as the seam for strict one-command-per-step granularity.
- Fix at cause and complete: only chain.rename had the no-op undo recording; checked siblings chain.apply/remove/save and mixer.rename_channel (which already no-ops without a step). Other chain.* commands are not renames.
- No collateral damage: no data format change; no-op path returns the same result fields minus undo transaction, matching mixer.rename_channel. No new limits.
- Siblings consistent: chain.apply/remove record real steps; chain.rename now no-ops when paths match. chain.bypass_all/clean/copy/get_state/list/receive/save are not rename and unaffected.
