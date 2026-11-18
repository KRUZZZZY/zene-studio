# Cluster new-p9-605: partial browser feature records

Verdict: **FIXED**

`BrowserIndex.cpp` previously treated every nonempty `f` object as valid. A partial record therefore supplied fabricated zero/default values, and `entryOf()` retained those values when the file was unchanged. `featuresFrom()` now requires all six numeric fields to be finite and checks their analyzer-supported ranges before setting `valid`. The regression writes a partial record for an unchanged audio file and verifies that a scan does not retain it as valid.

## Evidence

| Evidence | Location |
| --- | --- |
| Feature field and range validation | `src/core/BrowserIndex.cpp:58-111` |
| Unchanged-file retention condition | `src/core/BrowserIndex.cpp:151-157` |
| Partial-record regression | `tests/src/core/LibraryBrowserTest.cpp:299-330` |
| Original-code failing assertion | `tests/src/core/LibraryBrowserTest.cpp:329` |

The affected CTest family is `LibraryBrowserTest`. Each invocation ran **1/1 CTest tests**. The original run reported 14 QtTest cases passed and the regression failed with `valid on load=1, retained after unchanged scan=1`.

## Commands and results

Build and test commands wrote output to the named numbered files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs`. CTest commands ran from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-605/build/tests`.

| Stage | Exact command | EXIT |
| --- | --- | ---: |
| Initial build (`zene` and `LibraryBrowserTest`) | `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| RED, original production code | `ctest -R '^LibraryBrowserTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Fixed build (`zene` and `LibraryBrowserTest`) | `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| GREEN, fixed production code | `ctest -R '^LibraryBrowserTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Save fix patch and remove it for inversion proof | `git diff -- src/core/BrowserIndex.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild without fix | `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Inversion RED, fix removed | `ctest -R '^LibraryBrowserTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Reapply fix patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild with fix restored | `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Inversion GREEN, fix restored | `ctest -R '^LibraryBrowserTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-605.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

## Files in the commit

- `src/core/BrowserIndex.cpp`
- `tests/src/core/LibraryBrowserTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-605.md`
