# cluster review-e39984ffb

Verdict: FIXED

## Evidence

- File: src/core/ControlCommandsRecordTools.cpp, function groupByVolume (lines ~146-166).
- Bug: grouping armed recording destinations by QStorageInfo::device() merged distinct mounts that
  share a device name (e.g. multiple tmpfs mounts) and split the same mount only by accident of
  device string; filesystem identity is the mount (rootPath/st_dev).
- Fix: group by storage.rootPath() (the mount point / filesystem identity) instead of
  storage.device().

## Tests

- tests/src/core/RecordToolsTest.cpp: armedRoutesOnOneVolumeShareItsFreeSpace (existing) still passes.
- tests/src/core/RecordToolsTest.cpp: armedRoutesOnDifferentMountsDoNotShare (new) fails without fix
  (merged mounts) and passes with fix.

## Commands

- RED (fix reverted): cmake --build build --target RecordToolsTest EXIT=0; ctest -R RecordToolsTest EXIT=8
  (armedRoutesOnDifferentMountsDoNotShare failed: seconds 62121.6 vs expected 64930.5)
- GREEN (fix applied): cmake --build build --target RecordToolsTest EXIT=0; ctest -R RecordToolsTest EXIT=0
  (1/1 tests passed)
- ctest RecordToolsNoEngineTest EXIT=0 (1/1 passed)
- bash tests/complexity-gate.sh --check EXIT=1 (pre-existing baseline regressions, not introduced)
- bash tests/file-length-gate.sh --check EXIT=1 (pre-existing baseline regressions, not introduced)

## Files committed

- src/core/ControlCommandsRecordTools.cpp
- tests/src/core/RecordToolsTest.cpp
- docs/qwen-fixes-2026-10/cluster-review-e39984ffb.md
