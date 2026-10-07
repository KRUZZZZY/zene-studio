# Cluster new-p8-13

**Verdict: FALSE-POSITIVE**

The handler omits its private `__transaction` payload when no device IDs were copied, but that does not omit the transaction record when replacement changed the destination. `chainCopy` removes the destination's existing devices first (`src/core/ControlCommandsDeviceCopy.cpp:220-229`). `removeDevicesRecorded` adds an undo step for each removed device (`src/core/ControlDeviceMoves.cpp:131-152`). The registry detects a step produced during the command (`src/core/ControlRegistry.cpp:338-359`) and, when the payload is absent, records an honest transaction as reversible if the command is a true inverse and left a journal step (`src/core/ControlRegistry.cpp:385-402`). `chain.copy` is classified as a true inverse (`src/core/ControlReversibilityTableDevices.cpp:43-44`). The early return at `src/core/ControlCommandsDeviceCopy.cpp:150-154` therefore leaves the registry's journal-step fallback in effect. If replacement removes nothing and no copy is made, there is no change to record.

## Evidence

| Check | Command and exit | Result |
|---|---|---|
| RED regression | No valid RED command: the finding is not a defect, so no regression was retained. A temporary diagnostic run used `ctest -R DeviceCopyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-13.r1/logs/test-1.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` from `build/tests`; **EXIT=8**. | It failed while loading an effect in the fixture, before exercising the suspected transaction path. |
| GREEN regression | N/A; no production fix was made. | No red-green claim. |
| Build | `cmake --build build --target zene DeviceCopyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-13.r1/logs/build-3.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status` — **EXIT=0**. | Rebuilt `zene` and the affected test target. |
| Affected test family | `ctest -R DeviceCopyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-13.r1/logs/test-2.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` from `build/tests` — **EXIT=8**. | 1 CTest target ran; QtTest reported 8 passed, 1 failed, 8 skipped. The failure was `aChainIsBypassedAndCleaned` because this build has no loadable effect modules; copy-specific cases skipped for the same reason. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-13.r1/logs/gate-complexity-2.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit $gate_status` — **EXIT=0**. | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-13.r1/logs/gate-file-length-2.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit $gate_status` — **EXIT=0**. | Passed. |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p8-13.md`

No production or test files were changed.
