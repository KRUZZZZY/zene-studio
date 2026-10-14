# Cluster new-p10-121

**Verdict: FALSE-POSITIVE**

The finding points to `include/endian_handling.h:50-53`. This worktree builds as C++20 (`build/tests/CMakeFiles/EndianHandlingTest.dir/flags.make:9` recorded `-std=gnu++20`). C++20 [expr.shift](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/n4860.pdf) defines a left shift as the unique value congruent to `E1 × 2^E2` modulo `2^N`; undefined behavior applies to a negative shift count or one at least the width of the promoted left operand. In the reported `0x00000080` case, `(i & 0x000000ff)` is nonnegative and the shift count is 24, less than 32. The resulting `0x80000000` bit pattern is defined by the C++20 rule. The other shifts in the expression also have in-range counts. No production change is warranted.

GigPlayer calls this helper for its 24-bit sample conversion at `tests/reference/GigPlayer/GigPlayer.cpp:580-583` and `595-598`. A temporary probe forced `isLittleEndian()` to false, asserted that condition, and called `swap32IfBE(0x00000080)` with UBSan enabled. It passed, confirming the reported branch executes without an undefined-shift diagnostic. The temporary test and its build registration were removed because they did not fail on the original code and therefore were not a valid regression.

## Evidence

| Check | Exact command | Result |
|---|---|---|
| RED regression | Not applicable: the claim is false for this C++20 project. The forced-big-endian probe command was `cd build/tests && ctest -R '^EndianHandlingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-121.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; 1/1 passed on the unchanged header, so it was not a RED regression. |
| GREEN regression | Not applicable: no production fix was made. | No GREEN run claimed. |
| Probe target build | `cmake --build build --target EndianHandlingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-121.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Probe family count | `cd build/tests && ctest -R '^EndianHandlingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-121.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; 1/1 passed. |
| `zene` rebuild | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-121.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-121.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-121.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |

The first target build attempt, `cmake --build build --target EndianHandlingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-121.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc`, exited 2 because the generated Makefiles did not yet contain the new temporary target. After the build system refreshed, the probe target built successfully. One intervening `zene` build call exceeded the tool wait and returned no captured exit code; the identical command was rerun as `build-3.log` and returned `EXIT=0`.

**Files committed:** `docs/qwen-fixes-2026-10/cluster-new-p10-121.md` only.
