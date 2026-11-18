# Cluster new-p10-425

Verdict: FIXED

`fixNotePatterns` now shifts a note key only when `QString::toInt` succeeds and adding 12 stays within `int`. Invalid text and values above the safe upper bound remain unchanged. The largest safely shiftable value still shifts to `INT_MAX`.

Evidence:

- `src/core/UpgradeExtendedNoteRange.cpp:96` checks conversion success; `:97` guards the upper bound; `:99` performs the shift.
- `tests/src/core/UpgradeExtendedNoteRangeTest.cpp:78` checks `key="garbage"`; `:83` checks `INT_MAX`; `:88` checks the largest safely shiftable key. The ordinary-key case is at `:73`.
- Registered in `tests/CMakeLists.txt:602` and `tests/fork-sources.txt:2059`.

## Regression evidence

The affected CTest family ran **1 test target out of 1**. That Qt test target contains four cases.

| Stage | Exact command and working directory | Exit | Result |
|---|---|---:|---|
| RED before the production fix; cwd `build/tests` | `ctest -R UpgradeExtendedNoteRangeTest --output-on-failure` | 8 | `test-3.log`: `malformedKeyIsPreserved` and `maximalKeyIsPreserved` fail. |
| RED with the production patch reversed; cwd `build/tests` | `ctest -R UpgradeExtendedNoteRangeTest --output-on-failure` | 8 | `test-5.log`: the same two cases fail. |
| GREEN after restoring the production patch; cwd `build/tests` | `ctest -R UpgradeExtendedNoteRangeTest --output-on-failure` | 0 | `test-6.log` passes. |
| Final GREEN on the committed source; cwd `build/tests` | `ctest -R UpgradeExtendedNoteRangeTest --output-on-failure` | 0 | `test-8.log` reports 1/1 CTest target passed. |

Build and patch commands for the sensitivity check:

- `cmake --build build --target UpgradeExtendedNoteRangeTest -j2` — EXIT=0 (`build-6.log`, baseline regression binary).
- `git diff -- src/core/UpgradeExtendedNoteRange.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-425.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-425.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene UpgradeExtendedNoteRangeTest -j2` — EXIT=0 with the patch reversed (`build-8.log`).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-425.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene UpgradeExtendedNoteRangeTest -j2` — EXIT=0 after restoring the patch (`build-9.log`).
- `cmake --build build --target zene UpgradeExtendedNoteRangeTest -j2` — EXIT=0 on the final production source (`build-10.log`).
- `cmake --build build --target UpgradeExtendedNoteRangeTest -j2` — EXIT=0 after the final test source edit (`build-11.log`).

Required gates:

- `bash tests/complexity-gate.sh --check` — EXIT=0 (`gate-3.log`).
- `bash tests/file-length-gate.sh --check` — EXIT=0 (`gate-4.log`).

Earlier setup attempts were not test results: the first build request exited 2 because the new CMake target had not yet been generated (`build-1.log`); building `NoteEditRangeTest` exited 0 (`build-2.log`); the first compile of the new test exited 2 because its private header include path needed to be relative (`build-3.log`). The corrected test target compiled before the RED run above.

## Files in this fix commit

- `src/core/UpgradeExtendedNoteRange.cpp`
- `tests/src/core/UpgradeExtendedNoteRangeTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-425.md`
