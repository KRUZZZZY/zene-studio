FIXED

## Decision

REAL. At the pre-fix base `2da2ed356`, `PmpMapper::unmap()` returned when no mapping result existed yet, leaving the pending request and retry timer alive. A late PCP/NAT-PMP answer could then report success or start UPnP after `PortMapping::stop()`.

The existing product fix is retained: [PortMapping.cpp](../../src/core/PortMapping.cpp#L151) marks the orchestrator stopped before cancelling either mapper, and its completion handlers return for stopped attempts. [PortMappingPmp.cpp](../../src/core/PortMappingPmp.cpp#L110) stops retries and expires only a protocol stage that may have opened a port. [PortMappingUpnp.cpp](../../src/core/PortMappingUpnp.cpp#L251) invalidates prior callbacks and stops the search timer.

## Regression coverage

- `PortMappingCodecTest::pmpIgnoresLatePcpReplyAfterUnmap` waits until the fake gateway receives cleanup, then checks PCP MAP (`02 01`), zero lifetime, and that all received requests are PCP (no NAT-PMP deletion). The late reply is synchronized through the mapper socket's `readyRead` signal.
- `UpnpMapperFakeRouterTest::staleAddAfterStopAndRestartKeepsTheNewerMapping` releases the stale Add reply while the newer Add reply remains held. A queued `QNetworkAccessManager::finished` hook synchronizes on stale reply handling; when that handling creates a `GetExternalIPAddress` follow-up, the test waits for that reply too. It asserts no completion before releasing the newer Add. With product changes reversed, this assertion fails at line 536.
- The port-mapping tests no longer use fixed-count `processEvents()` loops or `wait(10)` calls. Renewal is driven by direct `renew()` calls; the test checks the armed 3600-second timer but never waits for its timeout.

## Verification evidence

The focused family is 3 CTest targets (46 QtTest cases): `PortMappingCodecTest`, `PortMappingOrchestratorTest`, and `UpnpMapperFakeRouterTest`.

| Step | Exact command | Exit | Result / log |
| --- | --- | ---: | --- |
| Product patch | `git diff 3cff36c59^ HEAD -- include/PortMapping.h src/core/PortMapping.cpp src/core/PortMappingPmp.cpp src/core/PortMappingUpnp.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-179-port-port.r7/logs/fix.patch; code=$?; echo EXIT=$code` | 0 | Product files only; `logs/fix.patch` |
| Reverse product hunks | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-179-port-port.r7/logs/fix.patch; code=$?; echo EXIT=$code` | 0 | Tests unchanged |
| RED build | `cmake --build build --target zene PortMappingCodecTest PortMappingOrchestratorTest UpnpMapperFakeRouterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-179-port-port.r7/logs/build-5-reversed-final.log 2>&1; code=$?; echo EXIT=$code` | 0 | Reversed product compiled |
| RED run | `ctest -R '^(PortMappingCodecTest|PortMappingOrchestratorTest|UpnpMapperFakeRouterTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-179-port-port.r7/logs/test-5-reversed-final.log 2>&1; code=$?; echo EXIT=$code` | 8 | 3/3 targets fail; stale UPnP completion fails before the newer reply is released (`UpnpMapperFakeRouterTest.cpp:536`) |
| Restore product hunks | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-179-port-port.r7/logs/fix.patch; code=$?; echo EXIT=$code` | 0 | Product fix restored |
| GREEN build | `cmake --build build --target zene PortMappingCodecTest PortMappingOrchestratorTest UpnpMapperFakeRouterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-179-port-port.r7/logs/build-6-restored-final.log 2>&1; code=$?; echo EXIT=$code` | 0 | Final test code compiled |
| GREEN run | `ctest -R '^(PortMappingCodecTest|PortMappingOrchestratorTest|UpnpMapperFakeRouterTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-179-port-port.r7/logs/test-6-restored-final.log 2>&1; code=$?; echo EXIT=$code` | 0 | 3/3 targets passed |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-179-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-179-port-port.r7/logs/feature-check-3.log 2>&1; code=$?; echo EXIT=$code` | 0 | All 8 rows PASS; complexity and file-length report no new items |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-179-port-port new-p8-179-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-179-port-port.r7/logs/submit-check-1.log 2>&1; code=$?; echo EXIT=$code` | 0 | PASS - submit; all five Self-check asks are answered |

The feature gate table was: fork-sources PASS; all-sources-reproduce PASS; no-upstream-regression PASS; check-namespace PASS; no-tautology PASS; doc-figures PASS; complexity PASS; file-length PASS. The standalone `bash tests/file-length-gate.sh --check` diagnostic exited 1 because it reports 20 train-wide over-limit entries; the changed fake-router test is below the limit after tightening its helper, and the baseline-filtered feature gate passes. The first feature-check trial found that test at 507 code lines; the helper was tightened and the final feature-check passed. Logs were searched with `rg` because no fff grep tool was exposed in this session.

## Files committed

- `tests/src/core/PortMappingCodecTest.cpp`
- `tests/src/core/UpnpMapperFakeRouterTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-179-port-port.md`

## Self-check

- **Does the regression fail with product hunks reversed and pass restored?** Yes. The product-only reversed build exited 0; the 3-target CTest run exited 8, including `staleAddAfterStopAndRestartKeepsTheNewerMapping` failing at the pre-release assertion. Restoring the product patch and rebuilding exited 0; the same 3-target CTest run exited 0.
- **Is the test deterministic?** Yes. Fake gateway/router receipt and Qt reply signals are the synchronization points. The 5-second `QTRY_*` values are failure bounds, not timing inputs. The renewal test invokes `renew()` directly and does not wait for a timer.
- **Were callers and sibling paths checked?** Yes. `PortMapping::start()` calls `stop()` and clears the stopped state for the new attempt; the destructor also calls `stop()`. Both mapper completion handlers are connected in the constructor. The PCP/NAT-PMP mapper and UPnP mapper each invalidate their own pending work, and tests cover their cancellation paths plus the orchestrator stop path.
- **Is there collateral behavior?** No saved-data, serialization, command, or project-format behavior changes. The edits in this round only improve tests; the existing product fix changes only completion and cleanup for canceled mappings.
- **Does this affect realtime, undo, or commands?** No audio-thread code is touched. Port mapping runs on the control/UI side. No command or undo behavior was added or changed.
- **Are tests registered and gates passing?** Yes. Both existing test files are registered in `tests/fork-sources.txt`; no new test file was added. The final feature-check passed all 8 rows.
