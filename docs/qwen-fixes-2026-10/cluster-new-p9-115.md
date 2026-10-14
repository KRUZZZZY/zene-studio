# Cluster new-p9-115

**Verdict: FIXED**

The regression injects a deterministic failure into the first `malloc` used by `sp_dcblock_create()`, then checks the existing allocator API returns `SP_NOT_OK` and leaves the output pointer null ([test:34](../../tests/src/plugins/ReverbScAllocationTest.cpp#L34), [test:56](../../tests/src/plugins/ReverbScAllocationTest.cpp#L56)). It does not call `sp_dcblock_create_and_init_pair()`, an API added during the fix. The test is registered as `ReverbScAllocationTest` ([tests/CMakeLists.txt:1156](../../tests/CMakeLists.txt#L1156)).

The product fix checks the `malloc` result and returns `SP_NOT_OK` on failure ([dcblock.c:18](../../plugins/ReverbSC/dcblock.c#L18)). ReverbSC's pair setup handles allocation failure at construction and sample-rate reinitialization ([ReverbSC.cpp:64](../../plugins/ReverbSC/ReverbSC.cpp#L64), [ReverbSC.cpp:141](../../plugins/ReverbSC/ReverbSC.cpp#L141)); processing bypasses safely when setup left the blockers null ([ReverbSC.cpp:80](../../plugins/ReverbSC/ReverbSC.cpp#L80)).

## Regression evidence

Each RED and final GREEN CTest command matched and executed **1 affected-family test**. With the product hunks reversed to the pre-fix tree, the test built and failed at line 60: actual status `1` (`SP_OK`), expected `0` (`SP_NOT_OK`). Restoring the product patch produced a passing test.

| Check | Cwd | Exact command | Exit | Result |
|---|---|---|---:|---|
| Reverse product patch | worktree | `git diff b03660f4b^ HEAD -- plugins/ReverbSC/dcblock.c plugins/ReverbSC/dcblock.h plugins/ReverbSC/ReverbSC.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r3/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r3/logs/fix.patch'; code=$?; echo EXIT=$code; exit $code` | 0 | Reversed product files only; revised test stayed unchanged. |
| RED rebuild | worktree | `cmake --build build --target zene reverbsc ReverbScAllocationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r3/logs/build-3-red.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | Pre-fix product and regression compiled. |
| RED CTest | `build/tests` | `ctest -R '^ReverbScAllocationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r3/logs/test-2-red.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 8 | 1 test ran and failed on the expected status assertion. |
| Restore product patch | worktree | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r3/logs/fix.patch'; code=$?; echo EXIT=$code; exit $code` | 0 | Restored the product files. |
| GREEN rebuild | worktree | `cmake --build build --target zene reverbsc ReverbScAllocationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r3/logs/build-4-green.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | Fixed product and regression compiled. |
| GREEN CTest | `build/tests` | `ctest -R '^ReverbScAllocationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r3/logs/test-3-green.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | 1/1 test passed. |

## Gates

| Exact command | Exit | Result |
|---|---:|---|
| `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r3/logs/gate-1-complexity.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | PASS. |
| `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r3/logs/gate-2-file-length.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | PASS. |

## Files committed in this review correction

- `tests/src/plugins/ReverbScAllocationTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-115.md`

The product fix is already committed earlier on this branch.
