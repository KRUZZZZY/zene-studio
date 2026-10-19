# Cluster new-p12-46-port

Verdict: **FIXED**

## Finding and change

The finding was real. In `registerAudioDeviceSet` (`src/core/ControlCommandsSettings.cpp`), the transaction's advertised inverse was chosen by whether the config key was *present* (`present ? audio.device_set : control.undo`). When the prior value was present but not a valid backend (for example an empty string written with `settings.set`), the inverse named `audio.device_set` with that invalid value, and `audio.device_set` refuses invalid backend names, so the advertised inverse could not be replayed even though the recorded undo step restores the prior config state.

The fix advertises `control.undo` whenever the previous value is not accepted by `audio.device_set` (`AudioEngine::isAudioDevNameValid(previous)`), and supplies an empty args object in that case. This is the same correction accepted on `bugs/qwen-new-p12-46`, re-applied by hand against the newer line, which had reverted the condition to `present ?` while refactoring config writes through `writeSetting`.

Evidence: the condition and inverse construction are at `src/core/ControlCommandsSettings.cpp:358-370`. The recorded undo step still carries the original `present` flag (`writeSetting(..., previous, present)`), so absent keys are still deleted on undo.

## Verification evidence

The affected CTest family is `ControlNoAudioDevice` (plus `ControlHeadlessNoAudioDevice` in the same `NoAudio` family): **2 CTest tests executed (2/2)**, both passed after the fix.

| Stage | Exact command and working directory | Exit |
| --- | --- | ---: |
| RED before fix (cwd: `build/tests`) | `QT_QPA_PLATFORM=offscreen ctest -R ControlNoAudioDevice --output-on-failure > .../test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| GREEN after fix (cwd: `build/tests`) | `QT_QPA_PLATFORM=offscreen ctest -R ControlNoAudioDevice --output-on-failure > .../test-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Family after fix (cwd: `build/tests`) | `QT_QPA_PLATFORM=offscreen ctest -R "NoAudio" --output-on-failure > .../test-family-2.log 2>&1; echo EXIT=$?` | 0 |
| Revert product patch (cwd: worktree) | `git apply -R .../fix.patch` | 0 |
| Revert build (cwd: worktree) | `cmake --build build --target zene > .../build-revert.log 2>&1; echo EXIT=$?` | 0 |
| RED with patch reversed (cwd: `build/tests`) | `QT_QPA_PLATFORM=offscreen ctest -R ControlNoAudioDevice --output-on-failure > .../test-reverted.log 2>&1; echo EXIT=$?` | 8 |
| Restore product patch (cwd: worktree) | `git apply .../fix.patch` | 0 |
| Restore build (cwd: worktree) | `cmake --build build --target zene > .../build-reapply.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after restore (cwd: `build/tests`) | `QT_QPA_PLATFORM=offscreen ctest -R ControlNoAudioDevice --output-on-failure > .../test-reapplied.log 2>&1; echo EXIT=$?` | 0 |
| Complexity ratchet (cwd: worktree) | `bash tests/complexity-gate.sh --check > .../gate-complexity.log 2>&1; echo EXIT=$?` | 1 |
| File-length ratchet (cwd: worktree) | `bash tests/file-length-gate.sh --check > .../gate-filelength.log 2>&1; echo EXIT=$?` | 1 |

The RED runs report `an empty previous audio backend must advertise control.undo, got {'args': {'device': ''}, 'op': 'audio.device_set'}`; the GREEN runs report `100% tests passed`. The reversal proof failed with the fix removed and passed after restoring the same patch.

The ratchet gates exit 1 because of pre-existing regressions on this branch that are unrelated to this change (new over-target functions and new files over 500 lines elsewhere in the tree); this change introduces no new complexity or file-length regression (`registerAudioDeviceSet` stays at CCN 4 and `ControlCommandsSettings.cpp` remains under the 500-line limit).

## Files in the commit

- `src/core/ControlCommandsSettings.cpp`
- `tests/control-no-audio-device.py`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-46-port.md`
