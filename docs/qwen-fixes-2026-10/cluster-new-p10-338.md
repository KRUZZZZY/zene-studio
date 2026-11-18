# Cluster new-p10-338

**Verdict: FIXED**

`RowTableView::setModel()` forwarded a null pointer to `QTableView` and then dereferenced it in the row loop. It now returns after the base call when `model == nullptr` ([src/gui/RowTableView.cpp:113](../../src/gui/RowTableView.cpp#L113), guard at line 116). The regression test sets a one-row model, clears it, and verifies that the view has no model ([tests/src/gui/RowTableViewTest.cpp:37](../../tests/src/gui/RowTableViewTest.cpp#L37)).

The affected ctest family matched and executed **1 test** (`RowTableViewTest`). Both RED runs failed with a segmentation fault; both GREEN runs passed. Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs`.

| Evidence | Exact command | EXIT / result |
|---|---|---|
| Original build (`build-2.log`) | `cmake --build build --target zene RowTableViewTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED (`test-1.log`) | `cd build/tests && ctest -R RowTableViewTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8; 1/1 segfault |
| Fixed build (`build-3.log`) | `cmake --build build --target zene RowTableViewTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN (`test-2.log`) | `cd build/tests && ctest -R RowTableViewTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0; 1/1 passed |
| Reverse the production patch (`fix.patch`) | `git diff -- src/gui/RowTableView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Reversed build (`build-4.log`) | `cmake --build build --target zene RowTableViewTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Reversed RED (`test-3.log`) | `cd build/tests && ctest -R RowTableViewTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8; 1/1 segfault |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Restored build (`build-5.log`) | `cmake --build build --target zene RowTableViewTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Restored GREEN (`test-4.log`) | `cd build/tests && ctest -R RowTableViewTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0; 1/1 passed |
| Source registration gate (`gate-1.log`) | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate (`gate-2.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate (`gate-3.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-338.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

The first build launch was rerun as the recorded `build-2.log` command because its asynchronous tool session returned without exposing its exit code; that unrecorded status was not used as evidence.

Committed with message `fix(gui): handle clearing RowTableView model (cluster new-p10-338)`.
