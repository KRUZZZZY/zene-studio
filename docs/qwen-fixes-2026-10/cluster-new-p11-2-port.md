# Cluster new-p11-2-port: LAN invite loopback fallback

Verdict: **FIXED**

## Finding and evidence

When LAN discovery returned a null address, `lanInviteAddress()` returned `127.0.0.1`. Both `scope: "lan"` and the LAN fallback for `scope: "auto"` use this path. The helper now returns an empty address with a typed refusal explaining that no LAN address was detected. A detected LAN address follows the existing success path unchanged.

- [CollabSession.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-2-port/src/core/CollabSession.cpp): null address produces a `Refused` result with a reason.
- [CollabSessionAddress.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-2-port/src/core/CollabSessionAddress.h): deterministic address selection helper for tests.
- [CollabPolicyTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-2-port/tests/src/core/CollabPolicyTest.cpp): deterministic null-address regression checks the empty address and typed refusal.

## Regression and build evidence

Builds ran from the worktree root. CTest ran from `build/tests`. Each `ctest -R CollabPolicyTest` run matched one registered test.

| Phase | Exact command | Exit | Result |
|---|---|---:|---|
| RED build | `cmake --build build --target zene CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/build-red.log 2>&1` | 0 | Built `zene` and the test with the original fallback behavior (detail helper returned `127.0.0.1`). |
| RED test | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/test-red.log 2>&1` | 8 | 1/1 failed at `address.isEmpty()`. |
| GREEN build | `cmake --build build --target zene CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/build-green.log 2>&1` | 0 | Rebuilt after the fix. |
| GREEN test | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/test-green.log 2>&1` | 0 | 1/1 passed. |
| Revert proof | `git diff -- src/core/CollabSession.cpp src/core/CollabSessionAddress.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/fix.patch` | 0 | Reversed the product change (helper symbol removed). |
| Reverted build | `cmake --build build --target CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/build-revert.log 2>&1` | 2 | Test target fails to link without the helper definition. |
| Functional RED proof | detail helper temporarily restored to return `127.0.0.1` (build-red/test-red) | 8 | 1/1 failed at `address.isEmpty()`. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/fix.patch` | 0 | Restored the refusal behavior. |
| Final build | `cmake --build build --target zene CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/build-final.log 2>&1` | 0 | Rebuilt the targets including the new helper header. |
| Final test | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/test-final.log 2>&1` | 0 | 1/1 passed. |

## Required gates

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/gate-complexity.log 2>&1` | 1 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-2-port.r1/logs/gate-filelen.log 2>&1` | 1 |

The gate failures are pre-existing on this branch (identical REGRESSION sets on a clean tree) and are not introduced by this change; no new regression entries appear in either log.

## Files in this commit

- `src/core/CollabSession.cpp`
- `src/core/CollabSessionAddress.h`
- `tests/fork-sources.txt`
- `tests/src/core/CollabPolicyTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-2-port.md`
