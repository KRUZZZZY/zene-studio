new-p21-14: FIXED

Decision: REAL. `clip.process` previously accepted a non-empty SampleClip with `sampleRate() == 0`, then passed that rate into both `prepared()`'s resampling divisor and the later window scale (`src/core/ControlCommandsClipProcess.cpp:64, 113-134`). The handler now refuses a nonpositive source rate before chain resolution or allocation. `tests/src/core/DeviceToolsChainTest.cpp:274` installs a non-empty zero-rate source, invokes the command with an empty chain, and asserts `InvalidArgs` plus unchanged source data and rate. The valid print/undo path remains in `aClipIsPrintedThroughAChain`.

Test matrix: normal valid print/undo; invalid zero-rate non-empty source with source preservation; differing source/engine rates exercised by the tail test added with cluster 5602; all work stays in the offline control command; existing C++20/Qt6 test target, no platform-specific API.

| Evidence | Command / result |
| --- | --- |
| Test dependency | `cmake --build build --target amplifier` → EXIT=0, because the original targeted test build did not yet build this plugin module. The DeviceToolsChainTest CMake target now depends on `amplifier` when available. |
| Initial RED | `ctest -R '^DeviceToolsChainTest$' --output-on-failure` from `build/tests` → EXIT=8; with the amplifier target built, 8 Qt cases passed and `aClipWithNoSampleRateIsRefusedBeforeProcessing` failed (`actual Refused`, expected `InvalidArgs`). |
| GREEN | `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1 CTest target). |
| RED with fix removed (4b) | `git diff -- src/core/ControlCommandsClipProcess.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=8 with the same assertion. |
| GREEN after restoring patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1). |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-14` → EXIT=0, all gates PASS. |
| Completion gate | `submit_check.py` will be run after all three fixes and the final report entries are present. |
| Commit files | `src/core/ControlCommandsClipProcess.cpp`, `tests/src/core/DeviceToolsChainTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p21-14.md`. |

5602: FIXED

Decision: REAL. `prepared()` appended `tailFrames` to the processed source, but `clip.process` kept `sourceOut` at the old window's scaled end, so the rendered tail could not be played. In addition, `replaceSample()` only updates length when auto-resize is enabled, which is false for manually trimmed windows. The handler now extends the kept window through the rendered buffer whenever at least one tail frame was produced and extends clip length to cover that window if necessary (`src/core/ControlCommandsClipProcess.cpp:123-145`). This preserves the old window for zero-frame tails and does not shorten a clip whose authored length is already longer. The regression at `tests/src/core/DeviceToolsChainTest.cpp:292` manually trims the start of a 2,000-frame source, resamples from half the engine rate, requests 0.25 seconds, checks the extended window and timeline length cover the rendered tail, then verifies one undo restores the original source, window and length.

Test matrix: normal zero-tail print remains covered by the existing command test; positive tail coverage uses an amplifier and a 0.25-second deterministic render; a manually trimmed source with auto-resize disabled and a half-rate source validate resampling, tail placement and timeline extension; undo is checked in the same command test; all allocations and length edits remain in the offline control command; existing C++20/Qt6 test target, no platform-specific API.

| Evidence | Command / result |
| --- | --- |
| Initial RED | `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; `ctest -R '^DeviceToolsChainTest$' --output-on-failure` from `build/tests` → EXIT=8; 9 Qt cases passed and `aRenderedTailExtendsThePlayableWindowAndClipLength` failed (`sourceOut` 4,000, expected 15,025). |
| GREEN | `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1 CTest target; all Qt cases passed). |
| RED with fix removed (4b, final code) | `git diff -- src/core/ControlCommandsClipProcess.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=8 with the same assertion (`sourceOut` 4,000, expected 15,025; 9 passed, 1 failed). |
| GREEN after restoring patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1). |
| Targeted plugin build | `DeviceToolsChainTest` now adds a dependency on `amplifier` in `tests/CMakeLists.txt`, so a targeted build loads the real plugin instead of skipping the command cases. `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; ctest from `build/tests` → EXIT=0 (1/1). |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-14` → EXIT=0, all gates PASS (final for this commit at log 042). |
| Commit files | `src/core/ControlCommandsClipProcess.cpp`, `tests/src/core/DeviceToolsChainTest.cpp`, `tests/CMakeLists.txt`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p21-14.md`. |
