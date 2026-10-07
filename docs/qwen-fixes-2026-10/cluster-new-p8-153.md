# Cluster new-p8-153: feedback takeover survives reload

Verdict: **FIXED**.

`MidiController::saveSettings()` persisted the feedback flag and feedback-modified output channel but not the port settings feedback had borrowed. After reload, `loadSettings()` therefore had no takeover flags or baseline to use when feedback was disabled. The controller now saves the borrowed mode and output-channel baseline while it owns them, validates and restores them on load, and removes stale attributes when the settings are no longer borrowed. In this code path, `MidiPort` reloads Input mode from its readable/writable models; the RED assertion is the persisted feedback channel surviving disable.

Evidence:

- [MidiController.cpp](../../src/core/midi/MidiController.cpp):174 restores borrowed settings when feedback is disabled; lines 261-273 persist/remove the baselines; lines 293-315 validate and restore the takeover state.
- [MidiControllerMenuTest.cpp](../../tests/src/gui/MidiControllerMenuTest.cpp):116 adds an Input-mode, channel-7/output-channel-3 save/reload test. After feedback borrows channel 7, disabling it after reload must restore channel 3.
- [upstream-modifications.txt](../../tests/upstream-modifications.txt):731 records the inherited production-file change and its regression test.

## Verification evidence

The logged build commands below were run from the worktree root; ctest commands were run from `build/tests`. Every build/test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-153.r1/logs/`.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Original-code build (valid regression) | `cmake --build build --target zene MidiControllerMenuTest` (`build-2.log`) | 0 | Built before the RED run. |
| RED | `ctest -R '^MidiControllerMenuTest$' --output-on-failure` (`test-2.log`) | 8 | Failed as required: actual output channel 7, expected original channel 3. |
| Fixed-code build | `cmake --build build --target zene MidiControllerMenuTest` (`build-3.log`) | 0 | Rebuilt `zene` and the affected test target. |
| GREEN | `ctest -R '^MidiControllerMenuTest$' --output-on-failure` (`test-3.log`) | 0 | Passed, 1/1 registered CTest tests. |
| Reverse patch | `git diff -- src/core/midi/MidiController.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-153.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-153.r1/logs/fix.patch` | 0 | Temporarily removed only the production fix. |
| Reverse-proof build | `cmake --build build --target zene MidiControllerMenuTest` (`build-4.log`) | 0 | Rebuilt after reversing the fix. |
| Reverse-proof RED | `ctest -R '^MidiControllerMenuTest$' --output-on-failure` (`test-4.log`) | 8 | Reproduced the same channel-7-versus-3 failure. |
| Reapply patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-153.r1/logs/fix.patch` | 0 | Restored the production fix. |
| Reapplied build | `cmake --build build --target zene MidiControllerMenuTest` (`build-5.log`) | 0 | Rebuilt `zene` and the affected test target. |
| Reapplied GREEN | `ctest -R '^MidiControllerMenuTest$' --output-on-failure` (`test-5.log`) | 0 | Passed, 1/1 registered CTest tests. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (`gate-1.log`) | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` (`gate-2.log`) | 0 | Passed. |

An initial draft run is also retained: `cmake --build build --target zene MidiControllerMenuTest` (`build-1.log`) exited 0, and `ctest -R '^MidiControllerMenuTest$' --output-on-failure` (`test-1.log`) exited 8 because that draft incorrectly expected the loaded port mode to be Duplex. I corrected the regression to assert the required post-disable baseline; `test-2.log` is the RED evidence used above.

The `fff grep` tool was not exposed in this session. I searched the saved logs with `rg -n` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/midi/MidiController.cpp`
- `tests/src/gui/MidiControllerMenuTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-153.md`
