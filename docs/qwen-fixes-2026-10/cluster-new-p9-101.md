# Cluster new-p9-101

Verdict: **FIXED**

`src/core/DrumSynth.cpp:506-507` now clamps the parsed distortion rate to the supported 0–7 range before calculating `DStep`. This keeps the downsampling increment positive and at most 20, while preserving every supported preset rate. The loop advances by `DStep` at `src/core/DrumSynth.cpp:872`.

The regression is `SampleBufferResampleTest::negativeDrumSynthDistortionRateDoesNotHang` at `tests/src/core/SampleBufferResampleTest.cpp:79`. It renders a `.ds` file with distortion enabled and `Rate=-1` in a child process with a bounded wait. The same assertion failed both on the original code and with the production patch removed, and passed after the fix was restored.

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-101.r1/logs/`.

Commands below ran from the worktree root unless marked `build/tests`. Each command's output was redirected to the named log, and the recorded shell exit is appended to that log.

| Evidence | Command | Exit | Result |
|---|---|---:|---|
| Pre-fix build | `cmake --build build --target zene SampleBufferResampleTest` (log `build-1.log`) | 0 | Rebuilt `zene` and the affected test target. |
| RED | `ctest -R SampleBufferResampleTest --output-on-failure` (cwd `build/tests`, log `test-1.log`) | 8 | `completed` returned FALSE: renderer stalled on `Rate=-1`. |
| Fixed build | `cmake --build build --target zene SampleBufferResampleTest` (log `build-2.log`) | 0 | Rebuilt both targets with the fix. |
| GREEN | `ctest -R SampleBufferResampleTest --output-on-failure` (cwd `build/tests`, log `test-2.log`) | 0 | Passed. |
| Remove production patch | `git diff -- src/core/DrumSynth.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-101.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-101.r1/logs/fix.patch` | 0 | Restored original production behavior for the sensitivity check. |
| Sensitivity build | `cmake --build build --target zene SampleBufferResampleTest` (log `build-3.log`) | 0 | Rebuilt after removing the patch. |
| Sensitivity RED | `ctest -R SampleBufferResampleTest --output-on-failure` (cwd `build/tests`, log `test-3.log`) | 8 | The identical regression failed on the original behavior. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-101.r1/logs/fix.patch` | 0 | Restored the fix. |
| Sensitivity GREEN build | `cmake --build build --target zene SampleBufferResampleTest` (log `build-4.log`) | 0 | Rebuilt with the restored fix. |
| Sensitivity GREEN | `ctest -R SampleBufferResampleTest --output-on-failure` (cwd `build/tests`, log `test-4.log`) | 0 | The identical regression passed. |
| Affected-family test count | `ctest -R SampleBufferResampleTest --output-on-failure` (cwd `build/tests`, log `test-4.log`) | 0 | 1/1 registered CTest target executed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (log `gate-3.log`) | 0 | PASS; no regressions. |
| File-length gate | `bash tests/file-length-gate.sh --check` (log `gate-4.log`) | 0 | PASS; no regressions. |

Files committed:

- `src/core/DrumSynth.cpp`
- `tests/src/core/SampleBufferResampleTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-101.md`
