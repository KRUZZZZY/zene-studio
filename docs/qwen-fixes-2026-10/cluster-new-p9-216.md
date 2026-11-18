# Cluster new-p9-216: LADSPA channel counts depend on port names

**Verdict: FIXED**

`LadspaManager` now counts every audio input and audio output by LADSPA port
direction, without inspecting its display name. Control ports remain excluded.
The existing `Ladspa2LMMS` classification sends transfer layouts with unequal
counts or unsupported channel counts to its invalid-effects list.

## Evidence

| File and lines | Evidence |
| --- | --- |
| `src/core/LadspaManager.cpp:196-202`, `217-223` | Counts are incremented for audio ports in the matching input/output direction. |
| `src/core/Ladspa2LMMS.cpp:48-63` | Equal 1/2/4-channel transfers are valid effects; mismatched or unsupported transfers are invalid effects. |
| `tests/src/plugins/LadspaChannelCountsTest.cpp:22-38` | Loads a LADSPA module with `Left`/`Right` port names, a misleadingly named control input, and a 1-in/2-out layout; checks channel counts and transfer classification. |
| `tests/CMakeLists.txt:41-42`, `4261-4274` | Registers the test and builds its isolated LADSPA fixture module. |

The original implementation failed the new test at the stereo input count:
actual `0`, expected `2`. Reversing the product patch made the same test fail
again; reapplying it made the test pass.

## Build and test commands

All commands below ran in the worktree unless the working directory is stated.
The initial target lookup used stale generated Makefiles; a full build attempt
was blocked by the workspace guard. Building the named `zene` target refreshed
the configured tree, after which the new test target built normally.

| Command | Exit |
| --- | ---: |
| `cmake --build build --target LadspaChannelCountsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 2 |
| `cmake --build build > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 97 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target LadspaChannelCountsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `ctest -R LadspaChannelCountsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` (from `build/tests`, original code: RED) | 8 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target LadspaChannelCountsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `ctest -R LadspaChannelCountsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` (from `build/tests`, GREEN) | 0 |
| `git diff -- src/core/LadspaManager.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc` (patch reversed) | 0 |
| `cmake --build build --target LadspaChannelCountsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/build-8.log 2>&1; rc=$?; echo EXIT=$rc` (patch reversed) | 0 |
| `ctest -R LadspaChannelCountsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` (from `build/tests`, patch reversed: RED) | 8 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/build-9.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target LadspaChannelCountsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/build-10.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `ctest -R LadspaChannelCountsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` (from `build/tests`, re-applied patch: GREEN) | 0 |

Affected-family count: **1/1 CTest test passed** on GREEN runs.

## Ratchet gates

| Command | Exit |
| --- | ---: |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-216.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Files committed

- `src/core/LadspaManager.cpp`
- `tests/CMakeLists.txt`
- `tests/upstream-modifications.txt`
- `tests/src/plugins/LadspaChannelCountFixture.cpp`
- `tests/src/plugins/LadspaChannelCountsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-216.md`
