# cluster new-p11-83

Verdict: **FIXED**

The Delay LFO control stores a period in seconds; `DelayEffect::processImpl` reciprocates the value to get the LFO rate in Hz. The model now calls it “LFO period,” and the dialog calls the knob “TIME” and displays “LFO period” with seconds.

## Evidence

- `plugins/Delay/DelayEffect.cpp:93` reciprocates the model value.
- `plugins/Delay/DelayControls.cpp:38` names the control “LFO period.”
- `plugins/Delay/DelayControlsDialog.cpp:59` labels the knob “TIME”; line 62 labels the hint “LFO period:” and appends seconds.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:225` loads the Delay module, checks the live model display name, and checks the dialog’s label and hint strings.
- `tests/upstream-modifications.txt:625-626` records both inherited production file changes.

The configured build has no `DelayControls` CTest: `ctest -N -R '^DelayControls$'` from `build/tests` exited 0 and reported `Total Tests: 0`. The regression runs in the existing `PluginPortsMigrationTest` family instead. The final run executed 1 test.

The migration harness initializes a render-only engine, so constructing the full dialog crashes in `EqFader` when it looks up the GUI main window. The final regression checks the actual loaded model at runtime and asserts the dialog’s source labels and unit directly.

## Regression and verification

All CTest commands below ran from `build/tests`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs`.

| Purpose | Exact command | Exit | Evidence |
|---|---|---:|---|
| RED build | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/build-10.log 2>&1; status=$?; echo EXIT=$status; exit 0` | 0 | `build-10.log` |
| RED regression | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/test-10.log 2>&1; status=$?; echo EXIT=$status; exit 0` | 8 | Actual model name was “LFO frequency”; 1 test failed. |
| GREEN build | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/build-11.log 2>&1; status=$?; echo EXIT=$status; exit 0` | 0 | `build-11.log` |
| GREEN regression | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/test-11.log 2>&1; status=$?; echo EXIT=$status; exit 0` | 0 | 1/1 tests passed. |
| Reverse fix patch | `git diff -- plugins/Delay/DelayControls.cpp plugins/Delay/DelayControlsDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/fix.patch; status=$?; echo EXIT=$status; exit 0` | 0 | `fix.patch` contains only the two production files. |
| Reversal build | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/build-10.log 2>&1; status=$?; echo EXIT=$status; exit 0` | 0 | Rebuilt before rerunning the test. |
| Reversal regression | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/test-10.log 2>&1; status=$?; echo EXIT=$status; exit 0` | 8 | The original “LFO frequency” assertion failed again. |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/fix.patch; status=$?; echo EXIT=$status; exit 0` | 0 | Restored both production edits. |
| Reapply build and regression | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/build-11.log 2>&1; status=$?; echo EXIT=$status; exit 0` then `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/test-11.log 2>&1; status=$?; echo EXIT=$status; exit 0` | 0 / 0 | Reapplied regression passed, 1/1. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/gate-3.log 2>&1; status=$?; echo EXIT=$status; exit 0` | 0 | PASS |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-83.r1/logs/gate-4.log 2>&1; status=$?; echo EXIT=$status; exit 0` | 0 | PASS |

## Files committed

- `plugins/Delay/DelayControls.cpp`
- `plugins/Delay/DelayControlsDialog.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-83.md`
