# Cluster new-p12-181-port

Verdict: **FIXED**

Port job: the fix accepted on `bugs/qwen-new-p12-181` (commit `748695c9a`, "Fix UPnP requested lease
after discovery") could not be merged into the newest line (`050/m0-train-5`) because
`tests/src/core/PortMappingCodecTest.cpp` conflicted. The port was written by hand against the current
code; the newest line had already replaced the `#define private public` seam of the old fix with a
`UpnpMapperTestAccess` friend struct and had grown a dedicated fake-router harness
(`tests/src/core/UpnpMapperFakeRouterTest.cpp`), so the regression now lives there and exercises the
public test seam instead of poking private members.

The bug itself was still present on this branch: `UpnpMapper::onDescription()` asked the router for a
fixed 3600-second lease after the device description had been fetched, discarding the lease the caller
passed to `map()`. The mapping therefore lasted 3600 seconds whatever the caller asked for, and the
reported lifetime was that 3600.

## Evidence

- [include/PortMapping.h:152](../../include/PortMapping.h#L152) - `UpnpMapper` now keeps the requested lease
  in `m_requestedLease`.
- [src/core/PortMappingUpnp.cpp:91](../../src/core/PortMappingUpnp.cpp#L91) - `map()` stores the lease it was
  given; [src/core/PortMappingUpnp.cpp:154](../../src/core/PortMappingUpnp.cpp#L154) - `onDescription()` hands
  that lease to `addMapping()` instead of the hard-coded 3600. Renewals (`map()` on a mapper that already
  knows the router) keep using the lease of their own call, unchanged.
- [tests/src/core/UpnpMapperFakeRouterTest.cpp:373](../../tests/src/core/UpnpMapperFakeRouterTest.cpp#L373) -
  `theFirstMappingAfterDiscoveryAsksForTheRequestedLease()` maps with a 7200-second and then a 600-second
  lease through the fake router, and checks both the `<NewLeaseDuration>` in the AddPortMapping SOAP body
  and the lifetime in the reported result.
- The pre-fix test failed because the AddPortMapping body carried `<NewLeaseDuration>3600</NewLeaseDuration>`.
  Reversing only the product patch and rebuilding reproduced exactly that failure; restoring the patch made
  the family pass again.

## Test and gate results

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline RED | `ctest -R UpnpMapperFakeRouterTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | `theFirstMappingAfterDiscoveryAsksForTheRequestedLease` failed at `UpnpMapperFakeRouterTest.cpp(379)`: the SOAP body did not carry the requested 7200. 11 other test functions in the target passed. |
| GREEN | `ctest -R "UpnpMapperFakeRouterTest\|PortMapping" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | 3 of 3 matching CTest targets passed (PortMappingCodecTest, PortMappingOrchestratorTest, UpnpMapperFakeRouterTest). |
| Reversal RED | `ctest -R "UpnpMapperFakeRouterTest\|PortMapping" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | With the product patch reversed and rebuilt, the same assertion failed at `UpnpMapperFakeRouterTest.cpp(379)`; 2 of 3 targets still passed. |
| Restored GREEN | `ctest -R "UpnpMapperFakeRouterTest\|PortMapping" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | 3 of 3 matching CTest targets passed. |
| Wider family GREEN | `ctest -R "UpnpMapperFakeRouterTest\|PortMapping\|Collab" --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 0 | 7 of 7 matching CTest targets passed (CollabPolicyTest, CollabTransportTest, PortMappingCodecTest, PortMappingOrchestratorTest, UpnpMapperFakeRouterTest, CollabPanelTest, ControlCollab). |
| Complexity gate (this change) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 1 | Pre-existing failure of this branch, not caused by this change: the same 6 REGRESSION entries appear on the pristine tree (`gate-0-baseline.log`, exit 1) and none of them is in a file this fix touches. |
| File-length gate (this change) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 1 | Pre-existing failure of this branch, not caused by this change: the same 14 REGRESSION entries appear on the pristine tree (`gate-0-filelen-baseline.log`, exit 1); none of them is a file this fix touches. |
| Complexity gate (pristine tree) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/gate-0-baseline.log 2>&1; echo EXIT=$?` | 1 | The branch as checked out fails the same way before this fix. |
| File-length gate (pristine tree) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/gate-0-filelen-baseline.log 2>&1; echo EXIT=$?` | 1 | The branch as checked out fails the same way before this fix. |
| Fork-sources gate | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 | Every tracked source is registered, 0 stale entries: this report is registered in `tests/fork-sources.txt`, and the three files the fix touches were already registered there as fork-NEW. |

Both ratchets already fail on the newest line (`050/m0-train-5`) this branch was cut from: the complexity
gate reports over-CCN functions in `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`,
`src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`
and `tests/src/gui/XyPadPairTest.cpp`; the file-length gate reports fork files over 500 code lines such as
`src/core/TakeLane.cpp` and `tests/src/gui/ArrangerTimelineTest.cpp`. Neither list mentions
`include/PortMapping.h`, `src/core/PortMappingUpnp.cpp` or `tests/src/core/UpnpMapperFakeRouterTest.cpp`
(the test file is 421 code lines after this change, 405 before it, against a limit of 500). The lists before
and after this change are byte-identical, so this fix moves neither ratchet. No limit or baseline was
touched, and the unrelated regressions were left alone: they are not this bug's.

All six builds used
`cmake --build build --target zene UpnpMapperFakeRouterTest PortMappingCodecTest PortMappingOrchestratorTest`
(the last one also `CollabPolicyTest CollabTransportTest`, plus a separate `CollabPanelTest` build),
with output logged to `build-1.log` through `build-6.log`; each exited 0. `zene` and every test target that
includes the changed `include/PortMapping.h` were rebuilt after each reversal or restoration.

The product patch was captured with
`git diff -- src/core/PortMappingUpnp.cpp include/PortMapping.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/fix.patch`
and reversed with `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/fix.patch`
(exit 0), then restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-181-port.r1/logs/fix.patch`
(exit 0).

The product change is off the audio thread (a network discovery callback), so it adds no allocation, no mutex
and no logging on a realtime path.

## Files committed

- `include/PortMapping.h`
- `src/core/PortMappingUpnp.cpp`
- `tests/src/core/UpnpMapperFakeRouterTest.cpp`
- `tests/fork-sources.txt` (this report is a fork-NEW file and is registered there)
- `docs/qwen-fixes-2026-10/cluster-new-p12-181-port.md`
