Verdict: FIXED
new-p14-16: FIXED
new-p14-17: FIXED

Bug new-p14-16 (src/core/ControlCommandsHardwareIo.cpp ioSetup): empty `audio.io_setup` args left the setup unchanged but still recorded an undo step. Fix compares the resulting setup with the current setup and returns without write/undo/__transaction when unchanged.

Bug new-p14-17 (src/core/ControlCommandsHardwareIo.cpp hardwareOutput): setting a hardware output to its current route (both outputSet and output) recorded an undo step. Fix returns the current state without apply/undo/__transaction/setModified when both stored fields already match.

Evidence in tests/src/core/HardwareIoTest.cpp:
- new tests `anEmptyIoSetupAddsNoUndoStep` and `settingHardwareOutputToItsCurrentRouteAddsNoUndoStep` check `Engine::projectJournal()->undoDepth()` unchanged.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `cd build/tests && QT_QPA_PLATFORM=offscreen ./HardwareIoTest` (log test-red-1.log) **EXIT=2**; both new tests FAIL (undo depth changed). |
| GREEN after fix | same (log test-green-1.log) **EXIT=0**; both new tests PASS. |
| Reversed-fix proof | `git apply -R .../fix.patch` **EXIT=0**; rebuild **EXIT=0**; test (log test-reverted-1.log) **EXIT=2**, both new tests FAIL. |
| Restored-fix proof | `git apply .../fix.patch` **EXIT=0**; rebuild **EXIT=0**; test (log test-reapply-1.log) **EXIT=0**, both new tests PASS. |
| Affected family | `ctest -R HardwareIoTest` (log ctest-1.log) **EXIT=0**, 1/1 passed. |
| Complexity gate | feature_check.py **EXIT=0** (complexity: no new item in your files). |
| File-length gate | feature_check.py **EXIT=0** (file-length: no new item in your files). |

## Build and test commands

```text
cmake --build build --target HardwareIoTest > .../logs/build-test-2.log 2>&1; EXIT=0 (RED build)
cd build/tests && QT_QPA_PLATFORM=offscreen ./HardwareIoTest > .../logs/test-red-1.log 2>&1; EXIT=2 (RED)
cmake --build build --target HardwareIoTest > .../logs/build-test-3.log 2>&1; EXIT=0 (GREEN build)
cd build/tests && QT_QPA_PLATFORM=offscreen ./HardwareIoTest > .../logs/test-green-1.log 2>&1; EXIT=0 (GREEN)
git apply -R .../logs/fix.patch; EXIT=0
cmake --build build --target HardwareIoTest > .../logs/build-revert-1.log 2>&1; EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ./HardwareIoTest > .../logs/test-reverted-1.log 2>&1; EXIT=2 (reversed RED)
git apply .../logs/fix.patch; EXIT=0
cmake --build build --target HardwareIoTest > .../logs/build-reapply-1.log 2>&1; EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ./HardwareIoTest > .../logs/test-reapply-1.log 2>&1; EXIT=0 (restored GREEN)
cd build/tests && ctest -R HardwareIoTest --output-on-failure > .../logs/ctest-1.log 2>&1; EXIT=0 (1/1)
python3 feature_check.py ... > .../logs/feature-check-1.log 2>&1; EXIT=0
```

Files committed:
- src/core/ControlCommandsHardwareIo.cpp
- tests/src/core/HardwareIoTest.cpp
- docs/qwen-fixes-2026-10/cluster-new-p14-16.md

## Self-check

- The regression test FAILS with the product change reverted and PASSES with it: reversed-fix run EXIT=2 (both new tests FAIL), restored-fix run EXIT=0 (both PASS); see evidence table.
- The test is deterministic: no wall-clock timing, sleeps or races; it uses `Engine::projectJournal()->undoDepth()` as the seam to observe whether a step was recorded.
- The fix is at the cause and complete: checked siblings `audio.io_list` (read-only, unaffected), `audio.device_list`/`audio.device_set` (different files, unaffected); `ioSetup` and `hardwareOutput` are the only writers of undo steps for these bugs. Early returns omit `__transaction`, so the registry records nothing.
- No collateral damage: unchanged setups skip write and undo but still return the same ioReport/applied/restart_required shape; old stored entries and projects load unchanged; the equality check uses defaulted operators on Bus/Side/Setup (no new limits). hardwareOutput no-op skips setModified, so unchanged projects are not marked modified.
- Verdict FIXED, not ALREADY-FIXED/FALSE-POSITIVE.
- Sibling commands: `audio.io_list` read-only unaffected; `audio.device_list`/`audio.device_set` in other command files unaffected; `audio.io_setup` fixed; `mixer.hardware_output` fixed.
