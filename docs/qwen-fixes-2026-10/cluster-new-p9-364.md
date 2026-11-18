# Cluster new-p9-364

**Verdict: FIXED**

`UpnpMapper::onAdded()` now clears the cached IGD service after a failed HTTP request that has no SOAP fault code. A valid SOAP fault keeps the service cached because it confirms that the endpoint responded. A transport failure during `GetExternalIPAddress` also invalidates the cache for the next attempt. The behavior is covered by a local HTTP 500 regression: after the first request fails, the next `map()` must enter SSDP discovery.

Evidence is in [PortMappingUpnp.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-364/src/core/PortMappingUpnp.cpp:170), [PortMappingCodecTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-364/tests/src/core/PortMappingCodecTest.cpp:262), and the test accessor declaration in [PortMapping.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-364/include/PortMapping.h:124).

## Regression evidence

Affected family: `PortMappingCodecTest`; **1 CTest test executed** (1/1). The original behavior failed at `failedCachedEndpointTriggersDiscovery()` because `m_searching` remained false. The fixed behavior passed.

| Stage | Exact command (working directory) | Exit | Result |
|---|---|---:|---|
| Initial RED build | `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/build-1-red.log 2>&1` (worktree) | 0 | Rebuilt `zene` and the affected test target before running the regression. |
| Initial RED test | `ctest -R PortMappingCodecTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/test-1-red.log 2>&1` (`build/tests`) | 8 | 0% passed; expected assertion failed. |
| Initial GREEN build | `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/build-2-green.log 2>&1` (worktree) | 0 | Rebuilt after the fix. |
| Initial GREEN test | `ctest -R PortMappingCodecTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/test-2-green.log 2>&1` (`build/tests`) | 0 | 100% passed; 1/1 CTest test. |
| Reverse patch creation | `git diff -- src/core/PortMappingUpnp.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/fix.patch` (worktree) | 0 | Patch contains only the production file. |
| Remove fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/fix.patch` (worktree) | 0 | Removed the production fix for sensitivity proof. |
| Proof RED build | `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/build-3-proof-red.log 2>&1` (worktree) | 0 | Rebuilt without the fix. |
| Proof RED test | `ctest -R PortMappingCodecTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/test-3-proof-red.log 2>&1` (`build/tests`) | 8 | Same regression assertion failed without the fix. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/fix.patch` (worktree) | 0 | Restored the production fix. |
| Proof GREEN build | `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/build-4-proof-green.log 2>&1` (worktree) | 0 | Rebuilt with the fix restored. |
| Proof GREEN test | `ctest -R PortMappingCodecTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/test-4-proof-green.log 2>&1` (`build/tests`) | 0 | 100% passed; 1/1 CTest test. |

## Gates

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/gate-1-complexity.log 2>&1` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-364.r1/logs/gate-2-file-length.log 2>&1` | 0 |

## Files committed

- `include/PortMapping.h`
- `src/core/PortMappingUpnp.cpp`
- `tests/src/core/PortMappingCodecTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-364.md`
