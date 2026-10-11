# Cluster new-p9-115

**Verdict: FIXED**

`sp_dcblock_create()` returns `SP_NOT_OK` on allocation failure at [dcblock.c:18](../../plugins/ReverbSC/dcblock.c#L18). The shared pair setup checks each create and init result, destroys any partial state, and leaves both output pointers null on failure ([dcblock.c:45](../../plugins/ReverbSC/dcblock.c#L45)); destroy also clears the pointer ([dcblock.c:28](../../plugins/ReverbSC/dcblock.c#L28)). The constructor and sample-rate reinitializer check the pair result ([ReverbSC.cpp:64](../../plugins/ReverbSC/ReverbSC.cpp#L64), [ReverbSC.cpp:141](../../plugins/ReverbSC/ReverbSC.cpp#L141)). If setup fails, `processImpl` returns without changing the input buffer, so the effect is bypassed ([ReverbSC.cpp:80](../../plugins/ReverbSC/ReverbSC.cpp#L80)).

The deterministic regression forces failure on allocation one and allocation two in separate child processes, then checks the caller setup returns normally with an error and null outputs ([ReverbScAllocationTest.cpp:39](../../tests/src/plugins/ReverbScAllocationTest.cpp#L39), [ReverbScAllocationTest.cpp:81](../../tests/src/plugins/ReverbScAllocationTest.cpp#L81)). The CTest family is `ReverbScAllocationTest`, registered in `tests/CMakeLists.txt:1156`; the source is registered in `tests/fork-sources.txt:2067`.

## Regression evidence

Each CTest run matched and executed **1 affected-family test**. The RED run below attempted both injected allocation failure points and failed on the `handledBothFailures` assertion because the child caller crashed.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED, safety patch removed (`test-5.log`; cwd `build/tests`) | `ctest -R '^ReverbScAllocationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r2/logs/test-5.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 8 | Expected failure: caller child crashed for the forced allocation failures. |
| GREEN, safety patch restored (`test-6.log`; cwd `build/tests`) | `ctest -R '^ReverbScAllocationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r2/logs/test-6.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | 1/1 test passed. |

## Rebuild and negative control

| Check | Exact command | Exit |
|---|---|---:|
| Remove the production safety patch | `git diff -- plugins/ReverbSC/ReverbSC.cpp plugins/ReverbSC/dcblock.c > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r2/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r2/logs/fix.patch'; code=$?; echo EXIT=$code; exit $code` | 0 |
| Rebuild without the safety patch (`build-6.log`) | `cmake --build build --target reverbsc ReverbScAllocationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r2/logs/build-6.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 |
| Restore the production safety patch | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r2/logs/fix.patch'; code=$?; echo EXIT=$code; exit $code` | 0 |
| Rebuild with the safety patch (`build-7.log`) | `cmake --build build --target reverbsc ReverbScAllocationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r2/logs/build-7.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 |

## Gates

| Exact command | Exit | Result |
|---|---:|---|
| `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r2/logs/gate-3.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | PASS. |
| `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r2/logs/gate-4.log' 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | PASS. |

## Files committed

- `plugins/ReverbSC/ReverbSC.cpp`
- `plugins/ReverbSC/dcblock.c`
- `plugins/ReverbSC/dcblock.h`
- `tests/src/plugins/ReverbScAllocationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-115.md`
