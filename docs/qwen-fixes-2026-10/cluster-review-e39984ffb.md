# cluster review-e39984ffb

Verdict: FIXED

## Evidence

- File: src/core/ControlCommandsRecordTools.cpp, function groupByVolume.
- Bug: grouping armed recording destinations by QStorageInfo::device() merged independent mounts
  sharing a device label (e.g. two tmpfs mounts) into one free-space budget, and (in the prior
  rootPath() variant) split paths sharing one underlying filesystem. Filesystem identity is st_dev.
- Fix: group by st_dev (via stat) on POSIX, falling back to rootPath() on Windows/when stat fails.

## Tests

- tests/src/core/RecordToolsTest.cpp: armedRoutesOnOneVolumeShareItsFreeSpace (existing) still passes.
- tests/src/core/RecordToolsTest.cpp: armedRoutesOnIndependentMountsDoNotShare (new): independent mounts
  sharing a device label are separate budgets.
- tests/src/core/RecordToolsTest.cpp: armedRoutesOnSameFilesystemCoalesce (new): paths sharing one
  filesystem (directory + link) are pooled.

## Commands

- RED (device grouping restored): cmake --build build --target RecordToolsTest EXIT=0;
  ctest -R RecordToolsTest EXIT=8 (armedRoutesOnIndependentMountsDoNotShare: seconds 62108.7 vs expected 111868)
- GREEN (st_dev grouping): cmake --build build --target RecordToolsTest EXIT=0;
  ctest -R RecordToolsTest EXIT=0 (3/3 tests passed)
- ctest RecordToolsNoEngineTest EXIT=0 (1/1 passed)
- bash tests/complexity-gate.sh --check EXIT=1 (pre-existing baseline regressions, not introduced)
- bash tests/file-length-gate.sh --check EXIT=1 (pre-existing baseline regressions, not introduced)

## Files committed

- src/core/ControlCommandsRecordTools.cpp
- tests/src/core/RecordToolsTest.cpp
- docs/qwen-fixes-2026-10/cluster-review-e39984ffb.md
