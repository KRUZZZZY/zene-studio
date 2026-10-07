# Cluster new-p8-180 — FIXED

## Evidence

- `src/core/PortMappingPmp.cpp:66-97`: a pending restart stops the old timer, binds a replacement `QUdpSocket` while the old socket is still bound, rejects bind failure or reuse of the old local port, then replaces the old socket. Bind failures reset the result and emit a failed mapping result.
- `tests/src/core/PortMappingCodecTest.cpp:233-275`: `restartMapIgnoresStaleReply` restarts with identical parameters, records and compares the stale and fresh reply destinations, releases the stale map reply and verifies it does not finish the new attempt before its own reply arrives. The test also verifies the old socket is replaced, making the regression deterministic when ephemeral port allocation happens to choose different ports.

The destination and stale-reply assertions alone initially passed against the old close-then-bind code because this host chose different ephemeral ports. The socket-replacement assertion made the production-only reversal fail deterministically at `originalSocketDestroyed`; the restored implementation passes the full stale-reply scenario.

## Verification

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-180.r2/logs`.

| Check | Command | Exit | Result |
|---|---|---:|---|
| Initial stale-reply-only run against old implementation | `ctest -R PortMappingCodecTest --output-on-failure` from `build/tests` | 0 | The host chose different ephemeral ports, so this version alone did not expose the close-then-bind defect (`logs/002-test-red-ctest.log`). |
| Reverse production patch | `git diff -- src/core/PortMappingPmp.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-180.r2/fix.patch` | 0 | Captured production-only hunk (`logs/019-reverse-apply-final.log`) |
| Apply reverse patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-180.r2/fix.patch` | 0 | Old close-then-bind implementation restored |
| Preliminary reverse test | `ctest -R PortMappingCodecTest --output-on-failure` from `build/tests` | 0 | The port allocator again selected different ports, so I added the socket-replacement assertion (`logs/007-reversed-ctest.log`). |
| RED build | `cmake --build build --target zene PortMappingCodecTest` | 0 | `logs/020-final-reversed-build.log` |
| RED test | `ctest -R PortMappingCodecTest --output-on-failure` from `build/tests` | 8 | 1/1 test ran; `restartMapIgnoresStaleReply` failed because the original socket was not replaced (`logs/021-final-reversed-ctest.log`) |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-180.r2/fix.patch` | 0 | Restored the fix (`logs/022-restore-apply-final.log`) |
| GREEN build | `cmake --build build --target zene PortMappingCodecTest` | 0 | `logs/023-final-restored-build.log` |
| GREEN test | `ctest -R PortMappingCodecTest --output-on-failure` from `build/tests` | 0 | 1/1 test passed (`logs/024-final-restored-ctest.log`) |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `logs/025-complexity-gate-final.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `logs/026-file-length-gate-final.log` |

The `fff grep` tool was not available in this session, so I searched the saved logs with individual `rg` searches for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/PortMappingPmp.cpp`
- `tests/src/core/PortMappingCodecTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-180.md`
