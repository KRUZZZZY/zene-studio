# cluster new-p12-160

Verdict: **BLOCKED**

The race is real and the behavior fix is implemented. Graph callbacks now publish tap gains and low-pass cutoffs through lock-free atomic pending values. `MultitapEchoEffect::applyPendingControls()` copies those values into the active DSP state and retunes filters at the start of audio processing. Sample-rate changes publish an atomic ratio and are also applied on the audio thread. The active gain, cutoff, and filter state are no longer written from the UI callbacks.

File evidence:

- [MultitapEchoControls.cpp](../../plugins/MultitapEcho/MultitapEchoControls.cpp:128): graph slots publish pending values; `sampleRateChanged()` publishes the ratio at line 173.
- [MultitapEcho.h](../../plugins/MultitapEcho/MultitapEcho.h:67): pending lock-free values and audio-owned active arrays; the sample ratio is atomic at line 77.
- [MultitapEcho.cpp](../../plugins/MultitapEcho/MultitapEcho.cpp:91): pending controls and filter coefficients are applied on the render thread; `processImpl()` calls this at line 139.
- [PluginPortsMigrationTest.cpp](../../tests/src/plugins/PluginPortsMigrationTest.cpp:2486): regression verifies edits do not mutate active values until audio processing applies them.

## Regression and build evidence

The focused regression was run with the production patch reversed, then with it restored. The red assertion observed active gain `0.5011872` where it expected the pre-edit value `1.0`; the identical focused test passed with the patch restored.

| Check | Exact command | Exit |
|---|---|---:|
| Remove production fix | `git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/fix.patch'` | 0 |
| Rebuild without fix | `cmake --build build --target zene PluginPortsMigrationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/build-5.log' 2>&1; status=$?; printf 'EXIT=%d\n' "$status" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/build-5.log'; exit "$status"` | 0 |
| RED, fix reversed | `./PluginPortsMigrationTest multitapEchoGraphEditsAreDeferredToAudioThread > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/test-5.log' 2>&1; status=$?; printf 'EXIT=%d\n' "$status" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/test-5.log'; exit "$status"` (from `build/tests`) | 1 |
| Restore production fix | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/fix.patch'` | 0 |
| Rebuild with fix | `cmake --build build --target zene PluginPortsMigrationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/build-6.log' 2>&1; status=$?; printf 'EXIT=%d\n' "$status" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/build-6.log'; exit "$status"` | 0 |
| GREEN, fix restored | `./PluginPortsMigrationTest multitapEchoGraphEditsAreDeferredToAudioThread > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/test-6.log' 2>&1; status=$?; printf 'EXIT=%d\n' "$status" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/test-6.log'; exit "$status"` (from `build/tests`) | 0 |
| Full affected-family CTest target | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/test-7.log' 2>&1; status=$?; printf 'EXIT=%d\n' "$status" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/test-7.log'; exit "$status"` (from `build/tests`) | 8 |

The affected-family run executed one CTest target: 45 Qt cases passed, one failed, and two skipped. The failure is the pre-existing `nonRealtimeLadspaEffectsAreInvalid` fixture lookup (`SWH Decimator test fixture was not loaded`); it also failed in the pre-fix run. The new graph regression and `multitapEchoRetunesAllTapsAfterSampleRateChange` passed after the fix.

## Required gates

| Gate | Exact command | Exit | Evidence |
|---|---|---:|---|
| Complexity | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/gate-1.log' 2>&1; status=$?; printf 'EXIT=%d\n' "$status" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/gate-1.log'; exit "$status"` | 1 | Six over-target functions are reported in unrelated files; no Multitap Echo function is listed. |
| File length | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/gate-2.log' 2>&1; status=$?; printf 'EXIT=%d\n' "$status" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-160.r1/logs/gate-2.log'; exit "$status"` | 1 | Fourteen unrelated files over 500 code lines are reported; no Multitap Echo file is listed. |

Both gates fail on unrelated existing tree violations. Their limits and baselines were not changed. The bug fix and regression are complete, but the required gates did not pass, so this report is marked BLOCKED.
