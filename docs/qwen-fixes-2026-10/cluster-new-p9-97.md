# Cluster new-p9-97

**Verdict: FIXED**

Both parsing constructors in `src/core/DataFile.cpp` now initialize `m_type` to
`DataFile::Type::Unknown` before opening or parsing input. The seeded-storage
regressions prove the missing-file and malformed-byte-array cases previously
exposed `-1` through `type()` rather than the defined `Unknown` value (`0`).
They also verify the existing `Unknown` validation behavior for `wav`.

## Evidence

| Location | Evidence |
|---|---|
| `src/core/DataFile.cpp:390-396` | The path constructor initializes `m_type` before its failed-open return. |
| `src/core/DataFile.cpp:434-440` | The byte-array constructor initializes `m_type` before parsing. |
| `tests/src/core/DataFileFormatTest.cpp:215` | Missing-file regression. |
| `tests/src/core/DataFileFormatTest.cpp:225` | Malformed-byte-array regression. |
| `tests/upstream-modifications.txt:776` | Reason recorded for the inherited production file change. |

The original regression run failed both new slots: actual type `-1`, expected
`0`. Reversing only the production patch and rebuilding reproduced both failures.

## Commands and results

All build and test output was captured in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-97.r1/logs`.
CTest commands ran from `build/tests`.

| Purpose | Command | Exit |
|---|---|---:|
| Build regression target before fix | `cmake --build build --target DataFileFormatTest` (`build-1.log`) | 0 |
| RED on original code | `ctest -R '^DataFileFormatTest$' --output-on-failure` (`test-1.log`) | 8 |
| Build after fix | `cmake --build build --target zene DataFileFormatTest DataFileSaveIntegrityTest` (`build-2.log`) | 0 |
| GREEN after fix | `ctest -R '^DataFileFormatTest$' --output-on-failure` (`test-2.log`) | 0 |
| DataFile family after fix | `ctest -R '^DataFile' --output-on-failure` (`test-3.log`) | 0, 2 tests passed |
| Remove production fix: `git diff -- src/core/DataFile.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-97.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-97.r1/logs/fix.patch` | — | 0 |
| Rebuild with production fix removed | `cmake --build build --target zene DataFileFormatTest` (`build-3.log`) | 0 |
| Sensitivity RED with production fix removed | `ctest -R '^DataFileFormatTest$' --output-on-failure` (`test-4.log`) | 8 |
| Restore production fix: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-97.r1/logs/fix.patch` | — | 0 |
| Rebuild after restoring fix | `cmake --build build --target zene DataFileFormatTest DataFileSaveIntegrityTest` (`build-4.log`) | 0 |
| GREEN after restoring fix | `ctest -R '^DataFileFormatTest$' --output-on-failure` (`test-5.log`) | 0 |
| DataFile family after restoring fix | `ctest -R '^DataFile' --output-on-failure` (`test-6.log`) | 0, 2 tests passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` (`complexity-gate-1.log`) | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` (`file-length-gate-1.log`) | 0 |

## Files committed

- `src/core/DataFile.cpp`
- `tests/src/core/DataFileFormatTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-97.md`
