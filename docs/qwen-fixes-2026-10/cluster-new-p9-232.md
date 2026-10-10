# Cluster new-p9-232: Invalid Hydrogen numbers replace defaults with zero

**Verdict: FIXED**

## Evidence

- [`plugins/HydrogenImport/LocalFileMng.cpp:45-47`](../../plugins/HydrogenImport/LocalFileMng.cpp): float parsing now returns the caller default when `QLocale::toFloat` reports failure.
- [`plugins/HydrogenImport/LocalFileMng.cpp:69-71`](../../plugins/HydrogenImport/LocalFileMng.cpp): integer parsing applies the same conversion-status check.
- [`plugins/HydrogenImport/LocalFileMng.cpp:92-98`](../../plugins/HydrogenImport/LocalFileMng.cpp): only the existing `true` and `false` spellings are accepted; other nonempty text returns the default.
- [`tests/src/core/HydrogenImportTest.cpp:13-45`](../../tests/src/core/HydrogenImportTest.cpp): regression coverage for invalid float/int values, invalid booleans, and recognized boolean values.
- [`tests/CMakeLists.txt:323-324,955-960`](../../tests/CMakeLists.txt): registers the test and compiles the importer implementation into its test target.

The affected ctest family ran **1 test** (`HydrogenImportTest`, ctest `1/1`). The test has four QtTest slots.

## Verification

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs`.

| Result | Exact command | Exit |
|---|---|---:|
| Build expanded regression with fix (`build-8.log`) | `cmake --build build --target zene HydrogenImportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/build-8.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/build-8.log; exit $result` | 0 |
| GREEN with fix (`test-5.log`) | `ctest -R HydrogenImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/test-5.log; exit $result` | 0 |
| Remove production patch | `git diff -- plugins/HydrogenImport/LocalFileMng.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/fix.patch` | 0 |
| Rebuild with fix removed (`build-9.log`) | `cmake --build build --target zene HydrogenImportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/build-9.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/build-9.log; exit $result` | 0 |
| RED with production fix removed (`test-6.log`) | `ctest -R HydrogenImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/test-6.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/test-6.log; exit $result` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/fix.patch` | 0 |
| Rebuild with fix restored (`build-10.log`) | `cmake --build build --target zene HydrogenImportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/build-10.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/build-10.log; exit $result` | 0 |
| Final GREEN (`test-7.log`) | `ctest -R HydrogenImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/test-7.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/test-7.log; exit $result` | 0 |
| Complexity gate (`gate-5.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/gate-5.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/gate-5.log; exit $result` | 0 |
| File-length gate (`gate-6.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/gate-6.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-232.r1/logs/gate-6.log; exit $result` | 0 |

With the production patch removed, `invalidFloatUsesDefault`, `invalidIntUsesDefault`, and `invalidBooleanUsesDefault` each failed independently while recognized boolean parsing passed. Restoring the patch returned the ctest to green.

## Files committed

- `plugins/HydrogenImport/LocalFileMng.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/src/core/HydrogenImportTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-232.md`
