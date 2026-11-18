# Cluster new-p9-554: empty `ArrayVector::erase` range

**Verdict: FIXED**

`include/ArrayVector.h:287` now returns the iterator for an empty range before calling `std::move`. Previously, an empty range before `end()` caused move assignment from each element to itself. The regression at `tests/src/core/ArrayVectorTest.cpp:636` uses `Constructible` to detect that mutation and checks the size and returned iterator.

The production header is declared in `tests/upstream-modifications.txt:33` with the regression test as its coverage.

## Evidence

The affected CTest family contains one test, `ArrayVectorTest`. The original implementation produced 38 QtTest passes and one failure in `ArrayVectorTest::eraseRangeTest()`; the failed value was `MoveAssign` (4) where `Default` (0) was expected. The negative control, with only the production patch removed, failed at the same assertion. With the fix applied, CTest passed 1/1.

All build and test output was redirected to new files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs`.

| Purpose | Exact command and working directory | Exit | Result |
|---|---|---:|---|
| Initial regression build (worktree root) | `cmake --build build --target ArrayVectorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Built the test with the regression and unchanged production code. |
| RED before fix (`build/tests`) | `ctest -R '^ArrayVectorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | Expected failure: 38 passed, 1 failed at `eraseRangeTest()`. |
| GREEN rebuild (worktree root) | `cmake --build build --target zene ArrayVectorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `zene` and the regression target. |
| Other direct include targets (worktree root) | `cmake --build build --target sf2player partc_ref_sf2player > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/build-7.log 2>&1; echo EXIT=$?` | 0 | Rebuilt both the plugin and reference module that include `ArrayVector.h`. |
| GREEN test (`build/tests`) | `ctest -R '^ArrayVectorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | CTest passed 1/1. |
| Negative control: remove only the product patch (worktree root) | `git diff -- include/ArrayVector.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/fix.patch; echo EXIT=$?` | 0 | Patch reversed successfully. |
| Negative-control rebuild (worktree root) | `cmake --build build --target zene ArrayVectorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 | Rebuilt without the fix. |
| RED negative control (`build/tests`) | `ctest -R '^ArrayVectorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | The same `eraseRangeTest()` assertion failed again. |
| Restore product patch (worktree root) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/fix.patch; echo EXIT=$?` | 0 | Patch restored. |
| Final rebuild (worktree root) | `cmake --build build --target zene ArrayVectorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 | Rebuilt with the fix restored. |
| Final GREEN test (`build/tests`) | `ctest -R '^ArrayVectorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | CTest passed 1/1. |
| Complexity gate (worktree root) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | PASS. |
| File-length gate (worktree root) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 | PASS. |

The attempted default build, `cmake --build build > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-554.r1/logs/build-2.log 2>&1; echo EXIT=$?`, exited 97 because the local `bunny-guard` requires named targets. The named-target builds above were used instead. One initial named-target rebuild exceeded the tool's output wait and its `EXIT` line was not captured; the same command was rerun with exit 0 in `build-4.log`, and the proof rebuilds also recorded exit 0.

The requested `fff` grep tool was not available (`command -v fff` returned no path), so the logs were searched by individual terms with `rg -n`.

## Files committed

- `include/ArrayVector.h`
- `tests/src/core/ArrayVectorTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-554.md`
