new-p21-14: FIXED

Decision: REAL. `clip.process` previously accepted a non-empty SampleClip with `sampleRate() == 0`, then passed that rate into both `prepared()`'s resampling divisor and the later window scale (`src/core/ControlCommandsClipProcess.cpp:64, 120-139`). The handler now refuses a nonpositive source rate before chain resolution or allocation. `tests/src/core/DeviceToolsChainTest.cpp:275` installs a non-empty zero-rate source, invokes the command with an empty chain, and asserts `InvalidArgs` plus unchanged source data and rate. The valid print/undo path remains in `aClipIsPrintedThroughAChain`.

Test matrix: normal valid print/undo; invalid zero-rate non-empty source with source preservation; differing source/engine rates exercised by the tail test added with cluster 5602; all work stays in the offline control command; existing C++20/Qt6 test target, no platform-specific API.

| Evidence | Command / result |
| --- | --- |
| Test dependency | `cmake --build build --target amplifier` → EXIT=0, because the original targeted test build did not yet build this plugin module. The DeviceToolsChainTest CMake target now depends on `amplifier` when available. |
| Initial RED | `ctest -R '^DeviceToolsChainTest$' --output-on-failure` from `build/tests` → EXIT=8; with the amplifier target built, 8 Qt cases passed and `aClipWithNoSampleRateIsRefusedBeforeProcessing` failed (`actual Refused`, expected `InvalidArgs`). |
| GREEN | `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1 CTest target). |
| RED with fix removed (4b) | `git diff -- src/core/ControlCommandsClipProcess.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=8 with the same assertion. |
| GREEN after restoring patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1). |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-14` → EXIT=0, all gates PASS (final run at log 059). |
| Completion gate | `submit_check.py` will be run after all three fixes and the final report entries are present. |
| Commit files | `src/core/ControlCommandsClipProcess.cpp`, `tests/src/core/DeviceToolsChainTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p21-14.md`. |

5602: FIXED

Decision: REAL. `prepared()` appended `tailFrames` to the processed source, but `clip.process` kept `sourceOut` at the old window's scaled end, so the rendered tail could not be played. In addition, `replaceSample()` only updates length when auto-resize is enabled, which is false for manually trimmed windows. The handler now extends the kept window through the rendered buffer whenever at least one tail frame was produced and extends clip length to cover that window if necessary (`src/core/ControlCommandsClipProcess.cpp:131-149`). This preserves the old window for zero-frame tails and does not shorten a clip whose authored length is already longer. The regression at `tests/src/core/DeviceToolsChainTest.cpp:307` manually trims the start of a 2,000-frame source, resamples from half the engine rate, requests 0.25 seconds, checks the extended window and timeline length cover the rendered tail, then verifies one undo restores the original source, window and length.

Test matrix: normal zero-tail print remains covered by the existing command test; positive tail coverage uses an amplifier and a 0.25-second deterministic render; a manually trimmed source with auto-resize disabled and a half-rate source validate resampling, tail placement and timeline extension; undo is checked in the same command test; all allocations and length edits remain in the offline control command; existing C++20/Qt6 test target, no platform-specific API.

| Evidence | Command / result |
| --- | --- |
| Initial RED | `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; `ctest -R '^DeviceToolsChainTest$' --output-on-failure` from `build/tests` → EXIT=8; 9 Qt cases passed and `aRenderedTailExtendsThePlayableWindowAndClipLength` failed (`sourceOut` 4,000, expected 15,025). |
| GREEN | `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1 CTest target; all Qt cases passed). |
| RED with fix removed (4b, final code) | `git diff -- src/core/ControlCommandsClipProcess.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=8 with the same assertion (`sourceOut` 4,000, expected 15,025; 9 passed, 1 failed). |
| GREEN after restoring patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1). |
| Targeted plugin build | `DeviceToolsChainTest` now adds a dependency on `amplifier` in `tests/CMakeLists.txt`, so a targeted build loads the real plugin instead of skipping the command cases. `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; ctest from `build/tests` → EXIT=0 (1/1). |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-14` → EXIT=0, all gates PASS (final run at log 059). |
| Commit files | `src/core/ControlCommandsClipProcess.cpp`, `tests/src/core/DeviceToolsChainTest.cpp`, `tests/CMakeLists.txt`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p21-14.md`. |

new-p22-5: FIXED

Decision: REAL. The invalid-clip branch fires when the clip is not a SampleClip, has no data, has no frames, or has a nonpositive rate, but its old error said `"'clip' is an audio clip with audio"` (`src/core/ControlCommandsClipProcess.cpp:120-122`). It now says the clip “must be an audio clip with audio.” `tests/src/core/DeviceToolsChainTest.cpp:292` passes an empty SampleClip and checks both `InvalidArgs` and that wording.

Test matrix: existing valid print/undo exercises the normal path; an empty audio clip exercises the audio-less failure path; the zero-rate regression exercises invalid sample-rate refusal; the string-only change uses no audio-thread or platform-specific API.

| Evidence | Command / result |
| --- | --- |
| RED | `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; `ctest -R '^DeviceToolsChainTest$' --output-on-failure` from `build/tests` → EXIT=8; 10 Qt cases passed and `anEmptyClipGetsAnActionableProcessError` failed because the message lacked “must be.” |
| GREEN | `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1 CTest target). |
| RED with fix removed (4b) | `git diff -- src/core/ControlCommandsClipProcess.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; `ctest -R '^DeviceToolsChainTest$' --output-on-failure` from `build/tests` → EXIT=8 with the same message assertion (10 passed, 1 failed). |
| GREEN after restoring patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-14.r1/logs/fix.patch` → EXIT=0; `cmake --build build --target zene DeviceToolsChainTest` → EXIT=0; same ctest command from `build/tests` → EXIT=0 (1/1). |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-14` → EXIT=0, all gates PASS (final run at log 059). |
| Completion gate | Run `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-14 new-p21-14` after this commit; its final result will be recorded below. |
| Commit files | `src/core/ControlCommandsClipProcess.cpp`, `tests/src/core/DeviceToolsChainTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p21-14.md`. |

## Self-check

- Decisions: all three findings are REAL; their former failure conditions and the corrected paths are documented above.
- Regression proof: each new assertion failed with only its own product fix removed and passed after restoration: zero rate at logs 010/013, tail at logs 045/048, and error text at logs 055/058. Each affected-family run executed the `DeviceToolsChainTest` CTest target (1/1); its target builds the amplifier module so command cases do not skip.
- Determinism: the tests inject the sample rates, source windows and tail duration; they use no sleeps, wall-clock timing or races.
- Completeness and collateral: `prepared()` has one caller, in `clip.process`; the command refuses invalid source rates before calling it. Zero-tail window behavior is unchanged, positive tails extend the clip through the rendered buffer, and undo restores the prior source/window/length. The message-only fix touches no serialization or saved-data format. All three conditions are validated in the same command handler; no sibling command calls `prepared()`.
- Realtime and undo: no audio-thread path was changed; the tail source and duration changes share the existing clip checkpoint, and the regression verifies one undo restores the prior source, window and length.
- Feature gate: final `feature_check.py` command above → EXIT=0; all gates PASS.
- Completion gate: run and answer its ASK items after commit, then record the result here.
- Commit files: listed separately for each bug above; commit IDs will be added after the final commit.
