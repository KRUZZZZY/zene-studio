# Cluster new-p10-428 — Invalid time signatures can cause division by zero

Verdict: **FIXED**

## Finding and fix

`include/TimePos.h:54-55` exposes `TimeSig(int,int)`. Its values feed unchecked
integer divisions in `TimePos::ticksPerBar(const TimeSig&)` (`include/TimePos.h:128`)
and `TimePos::ticksPerBeat` (`include/TimePos.h:99`). The constructor in
`src/core/TimePos.cpp:35-43` now throws `std::invalid_argument` if either value is
nonpositive, so invalid values cannot be used to construct a `TimeSig`.

`tests/src/core/SessionSchedulerTest.cpp:194-202` adds separate regressions for
`TimeSig(4,0)` and `TimeSig(0,4)`. Both assert that the constructor throws. The
`TimeSig(const MeterModel&)` overload is unchanged.

## Regression evidence

The initial test-only run failed before the product change. Both new QtTest slots
reported that `std::invalid_argument` was not thrown. I then removed only the
`src/core/TimePos.cpp` diff using the required `fix.patch` procedure, rebuilt,
and reproduced both failures. Reapplying that same patch, rebuilding, and
rerunning ctest passed.

The affected ctest family ran **1 test** (`SessionSchedulerTest`, CTest test
#306). In the red runs it failed; in the green run it passed (1/1).

| Evidence | Exact command | Exit |
| --- | --- | ---: |
| Initial red build | `cmake --build build --target zene SessionSchedulerTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/build-1-red.log 2>&1; echo EXIT=$?` | 0 |
| Initial red test, from `build/tests` | `QT_QPA_PLATFORM=offscreen ctest -R '^SessionSchedulerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/test-1-red.log 2>&1; echo EXIT=$?` | 8 |
| Remove product diff | `git diff -- src/core/TimePos.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/fix.patch; echo EXIT=$?` | 0 |
| Reverted red build | `cmake --build build --target zene SessionSchedulerTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/build-2-revert.log 2>&1; echo EXIT=$?` | 0 |
| Reverted red test, from `build/tests` | `QT_QPA_PLATFORM=offscreen ctest -R '^SessionSchedulerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/test-2-revert-red.log 2>&1; echo EXIT=$?` | 8 |
| Restore product diff | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/fix.patch; echo EXIT=$?` | 0 |
| Green build | `cmake --build build --target zene SessionSchedulerTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/build-3-green.log 2>&1; echo EXIT=$?` | 0 |
| Green test, from `build/tests` | `QT_QPA_PLATFORM=offscreen ctest -R '^SessionSchedulerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/test-3-green.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/gate-1-complexity.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r2/logs/gate-2-file-length.log 2>&1; echo EXIT=$?` | 0 |

The `fff grep` tool was not exposed in this session, so captured logs were
filtered with `rg` for `error`, `FAILED`, and `Assertion`, and ctest summaries
were checked for the executed test count.

## Round-2 QA correction: test source registration

`git diff --name-status 4e677cb6c6ab..HEAD -- tests/src/core/SessionSchedulerTest.cpp`
reports `A`, so this test is fork-added. I removed it from
`tests/upstream-modifications.txt`, added it in sorted order to
`tests/fork-sources.txt`, and added its name to both copies of the manifest
recipe's `awk` allow-list. The inherited production change remains in the
upstream-modifications manifest.

| Correction evidence | Exact command | Exit |
| --- | --- | ---: |
| Fork source registration gate | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r3/logs/gate-1-fork-sources.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r3/logs/gate-2-complexity.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-428.r3/logs/gate-3-file-length.log 2>&1; echo EXIT=$?` | 0 |

The registration gate reports 1511 fork-new sources, 1195 inherited sources,
48 tooling sources, and no stale entries. The embedded full `Verify it` recipe
still reports unrelated list differences: I ran the committed pre-correction
and corrected copies, and both exited 1. The corrected recipe's diff contains
no `SessionSchedulerTest.cpp` mismatch. Those verifier logs are
`verify-2-original-fork-recipe.log` and `verify-1-fork-sources-recipe.log` in
the same log folder; Gate 9 and both required ratchets pass.

## Files in the QA correction commit

- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-428.md`
