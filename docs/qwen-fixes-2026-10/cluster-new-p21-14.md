new-p21-14: FIXED

Decision: REAL. `clip.process` previously accepted a non-empty SampleClip with `sampleRate() == 0`, then passed that rate into both `prepared()`'s resampling divisor and the later window scale (`src/core/ControlCommandsClipProcess.cpp:64, 113-134`). The handler now refuses a nonpositive source rate before chain resolution or allocation. `tests/src/core/DeviceToolsChainTest.cpp:274` installs a non-empty zero-rate source, invokes the command with an empty chain, and asserts `InvalidArgs` plus unchanged source data and rate. The valid print/undo path remains in `aClipIsPrintedThroughAChain`.

Test matrix: normal valid print/undo; invalid zero-rate non-empty source with source preservation; differing source/engine rates exercised by the tail test added with cluster 5602; all work stays in the offline control command; existing C++20/Qt6 test target, no platform-specific API.

| Evidence | Command / result |
| --- | --- |
| Initial RED | `ctest -R '^DeviceToolsChainTest$' --output-on-failure` from `build/tests` → EXIT=8; with the amplifier target built, 8 Qt cases passed and `aClipWithNoSampleRateIsRefusedBeforeProcessing` failed (`actual Refused`, expected `InvalidArgs`). |
| GREEN | `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1 CTest target). |
| RED with fix removed (4b) | `git diff -- src/core/ControlCommandsClipProcess.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=8 with the same assertion. |
| GREEN after restoring patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1). |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-14` → EXIT=0, all gates PASS. |
| Completion gate | `submit_check.py` will be run after all three fixes and the final report entries are present. |
| Commit files | `src/core/ControlCommandsClipProcess.cpp`, `tests/src/core/DeviceToolsChainTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p21-14.md`. |
