# Cluster new-p10-534

**Verdict: FIXED**

`sp_process_raw()` used `sp->len == 0` to select infinite streaming, but then decremented that unsigned sentinel and did not advance the sample position. The streaming branch now leaves `len` at zero and increments `pos` after each output frame. The finite-length branch is unchanged.

## Evidence

| File and line | Evidence |
| --- | --- |
| `plugins/ReverbSC/base.c:82-89` | The zero-length loop now writes each channel and advances `sp->pos`; it no longer decrements `sp->len`. |
| `tests/src/plugins/ReverbSCRawProcessingTest.cpp:72-90` | A callback starts at position 37, captures three frames, and checks positions 37/38/39, final `pos == 39`, and preserved `len == 0`. |
| `tests/CMakeLists.txt:2435-2450` | Registers a focused test that compiles the production `plugins/ReverbSC/base.c` into its target. |

The RED run failed at `callbackPositions[1]`: actual 37, expected 38. The affected CTest family has one test; the green run reported 1/1 passed.

## Commands and exit codes

Commands ran from the worktree root unless marked `build/tests`. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-534.r1/logs`.

| Purpose | Command | Exit |
| --- | --- | ---: |
| Initial test-target lookup before build-system regeneration (`build-1.log`) | `cmake --build build --target ReverbSCRawProcessingTest` | 2 |
| Regenerate through a normal build (`build-2.log`) | `cmake --build build --target zene` | 0 |
| Compile original-code regression (`build-4.log`) | `cmake --build build --target ReverbSCRawProcessingTest` | 0 |
| **RED**, original code (`test-2.log`, run from `build/tests`) | `ctest -R ReverbSCRawProcessingTest --output-on-failure` | 8 |
| Build fixed product, plugin and regression (`build-5.log`) | `cmake --build build --target zene reverbsc ReverbSCRawProcessingTest` | 0 |
| **GREEN** (`test-3.log`, run from `build/tests`) | `ctest -R ReverbSCRawProcessingTest --output-on-failure` | 0 |
| Save the product-only patch (`fix.patch`) | `git diff -- plugins/ReverbSC/base.c > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-534.r1/logs/fix.patch` | 0 |
| Reverse the fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-534.r1/logs/fix.patch` | 0 |
| Rebuild with fix reversed (`build-6.log`) | `cmake --build build --target zene reverbsc ReverbSCRawProcessingTest` | 0 |
| **RED**, negative control (`test-4.log`, run from `build/tests`) | `ctest -R ReverbSCRawProcessingTest --output-on-failure` | 8 |
| Reapply the fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-534.r1/logs/fix.patch` | 0 |
| Rebuild restored fix (`build-7.log`) | `cmake --build build --target zene reverbsc ReverbSCRawProcessingTest` | 0 |
| **GREEN**, restored fix (`test-5.log`, run from `build/tests`) | `ctest -R ReverbSCRawProcessingTest --output-on-failure` | 0 |
| Complexity gate (`gate-3.log`) | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate (`gate-4.log`) | `bash tests/file-length-gate.sh --check` | 0 |
| Whole-tree source manifest check (`manifest-1.log`) | `bash tests/all-sources-reproduce.sh` | 0 |
| Inherited-change declaration check (`manifest-2.log`) | `bash tests/no-upstream-regression-gate.sh` | 0 |

An earlier stdout-redirection version of the regression also exited 8 (`test-1.log`) but did not expose the assertion in CTest output; it is not used as RED evidence. The revised test produced the direct assertion above. The first test-target lookup failed only because the changed CMake test list had not yet been regenerated; the normal `zene` build regenerated it without a manual configure command.

## Files committed

- `plugins/ReverbSC/base.c`
- `tests/src/plugins/ReverbSCRawProcessingTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-534.md`
