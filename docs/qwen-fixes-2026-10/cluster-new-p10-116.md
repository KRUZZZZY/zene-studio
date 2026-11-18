# Cluster new-p10-116: anchored searches for favourite effects

Verdict: **FIXED**.

`pickerorganiser::markRow` prefixes favourite display names with `★ ` and records `FavouriteRole` in [EffectPickerOrganiser.cpp](../../src/gui/modals/EffectPickerOrganiser.cpp:72). The proxy now removes that prefix for matching only when the favourite role is set, so an anchored search such as `^Zita` sees the plugin name. The regression test builds that decorated row and checks the proxy's visible row count in [EffectSelectDialogFilterTest.cpp](../../tests/src/gui/EffectSelectDialogFilterTest.cpp:36).

## Evidence

| Evidence | Location |
| --- | --- |
| Favourite decoration and role assignment | `src/gui/modals/EffectPickerOrganiser.cpp:72-74` |
| Decoration removal before the regex match | `include/EffectSelectDialog.h:74-84` |
| Regression fixture and `^Zita` assertion | `tests/src/gui/EffectSelectDialogFilterTest.cpp:38-48` |

The affected ctest family contains **1 test**. The red run reports `proxy.rowCount() == 0` instead of `1`; the restored run passes 1/1.

## Commands and exit codes

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs`. CTest commands ran from `build/tests`. The first combined build caused CMake to regenerate the target graph and then exited before the new target was available to that Make invocation; separate rebuilds succeeded.

| Phase | Exact command | Exit |
| --- | --- | ---: |
| Initial target graph regeneration | `cmake --build build --target zene EffectSelectDialogFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/build-red-1.log 2>&1; echo EXIT=$?` | 2 |
| Fresh application build | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/build-red-2.log 2>&1; echo EXIT=$?` | 0 |
| Fresh regression build | `cmake --build build --target EffectSelectDialogFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/build-red-3.log 2>&1; echo EXIT=$?` | 0 |
| RED regression | `ctest -R '^EffectSelectDialogFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| GREEN application rebuild | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/build-green-1.log 2>&1; echo EXIT=$?` | 0 |
| GREEN regression rebuild | `cmake --build build --target EffectSelectDialogFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/build-green-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN regression | `ctest -R '^EffectSelectDialogFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/test-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Reverse the production patch | `git diff -- include/EffectSelectDialog.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Negative-control application rebuild | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/build-proof-red-1.log 2>&1; echo EXIT=$?` | 0 |
| Negative-control regression rebuild | `cmake --build build --target EffectSelectDialogFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/build-proof-red-2.log 2>&1; echo EXIT=$?` | 0 |
| Negative-control regression (must fail without fix) | `ctest -R '^EffectSelectDialogFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/test-proof-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Restore the production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Restored application rebuild | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/build-proof-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Restored regression rebuild | `cmake --build build --target EffectSelectDialogFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/build-proof-green-2.log 2>&1; echo EXIT=$?` | 0 |
| Restored regression | `ctest -R '^EffectSelectDialogFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/test-proof-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/gate-complexity-1.log 2>&1; echo EXIT=$?` | 0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/gate-file-length-1.log 2>&1; echo EXIT=$?` | 0 |

The `fff grep` tool was not available in this session, so the log search used `rg -n -i 'error|FAILED|Assertion' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-116.r1/logs/*.log`. It found the expected RED and negative-control failures; the green ctest and both gates passed.

## Files in the commit

- `include/EffectSelectDialog.h`
- `tests/src/gui/EffectSelectDialogFilterTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-116.md`
