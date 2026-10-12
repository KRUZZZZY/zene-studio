# Cluster new-p12-54: controller buffers stale after frame counter reset

## Verdict: FIXED

`Controller::resetFrameCounter()` used to mark each controller buffer as updated for period `0`, then reset `s_periods` to `0`. A read at that point therefore reused the buffer from before the reset. It now marks each buffer invalid with `-1`, forcing the next read to refresh it.

Evidence: [src/core/Controller.cpp:174](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-54/src/core/Controller.cpp:174) resets the counter after invalidating the buffer at line 178. The deterministic probe and regression are in [tests/src/core/ControllerSurfaceTest.cpp:82](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-54/tests/src/core/ControllerSurfaceTest.cpp:82) and [tests/src/core/ControllerSurfaceTest.cpp:140](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-54/tests/src/core/ControllerSurfaceTest.cpp:140). The regression expected `0.4` after reset and received stale `0.8` on both RED runs.

## Evidence

| Check | Exact command | Exit |
|---|---|---:|
| Build regression before fix | `cmake --build build --target ControllerSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED on original code | `ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| First fixed build (`zene` and all listed controller test targets) | `cmake --build build --target zene ControllerSurfaceTest MidiLearnThreadTest MidiLearnTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| First GREEN family run | `ctest -R '^(ControllerSurfaceTest|MidiLearnThreadTest|MidiLearnTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Reverse production patch | `git diff -- src/core/Controller.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Reversal build | `cmake --build build --target zene ControllerSurfaceTest MidiLearnThreadTest MidiLearnTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED with production patch reversed | `ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Final rebuild (`zene` and all listed controller test targets) | `cmake --build build --target zene ControllerSurfaceTest MidiLearnThreadTest MidiLearnTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Final GREEN family run | `ctest -R '^(ControllerSurfaceTest|MidiLearnThreadTest|MidiLearnTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-54.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

The final affected-family run executed **3 CTest tests**: `ControllerSurfaceTest`, `MidiLearnTest`, and `MidiLearnThreadTest`; all passed. The two RED runs each executed the `ControllerSurfaceTest` CTest target and failed at the intended assertion.

## Files in this change

- `src/core/Controller.cpp`
- `tests/src/core/ControllerSurfaceTest.cpp`
- `tests/upstream-modifications.txt` (records the inherited production-file change and regression)
- `docs/qwen-fixes-2026-10/cluster-new-p12-54.md`
