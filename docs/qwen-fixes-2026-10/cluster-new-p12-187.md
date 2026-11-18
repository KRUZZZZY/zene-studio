# Cluster new-p12-187: rack zone surface test depends on earlier test

**Verdict: FIXED**

## Finding and change

`RackZonesTest::initTestCase()` creates one fixture for the test process at `tests/src/core/RackZonesTest.cpp:64-71`. The earlier `zonesResolveKeyAndVelocityRanges()` slot adds two zones at `:75-97`. The surface slot previously asserted a fixed count of three at `:176-179`, so running it alone failed with one zone present.

The surface slot now records the count at its start (`:128-131`) and verifies that `rack.zone_add` increased it by one (`:177-179`). This works whether the slot starts with an empty fixture or zones left by an earlier slot. No production source changed. `RackZonesTest.cpp` is already registered in `tests/fork-sources.txt:2005`.

## Evidence

Commands below were run from the stated working directory. Build and test output is in the listed log. The `EXIT=` value is the underlying build or test command's exit code.

| Check | Working directory and exact command | Result |
|---|---|---|
| Baseline build (`zene` and test target) | Worktree: `cmake --build build --target zene RackZonesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/build-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=0` |
| RED, original assertion | `build/tests`: `ZENE_CONTROL_CHECK_RESULTS=1 ./RackZonesTest theZoneSurfaceAddsResolvesAndUndoes > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/test-red-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=1`; assertion reports actual `1`, expected `3` |
| Rebuild after change | Worktree: `cmake --build build --target RackZonesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/build-2.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=0` |
| GREEN, corrected assertion | `build/tests`: `ZENE_CONTROL_CHECK_RESULTS=1 ./RackZonesTest theZoneSurfaceAddsResolvesAndUndoes > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/test-green-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=0`; 3 passed, 0 failed (setup, selected slot, teardown) |
| Save the test-only fix patch | Worktree: `git diff -- tests/src/core/RackZonesTest.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/fix.patch` | `DIFF_EXIT=0` |
| Reverse the test-only fix | Worktree: `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/fix.patch` | `REVERSE_EXIT=0` |
| Rebuild with original assertion restored | Worktree: `cmake --build build --target RackZonesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/build-proof-red-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=0` |
| Recheck RED after reversing fix | `build/tests`: `ZENE_CONTROL_CHECK_RESULTS=1 ./RackZonesTest theZoneSurfaceAddsResolvesAndUndoes > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/test-red-proof-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=1`; same actual `1`, expected `3` assertion |
| Reapply test-only fix | Worktree: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/fix.patch; code=$?; echo APPLY_EXIT=$code; exit 0` | `APPLY_EXIT=0` |
| Rebuild restored fix | Worktree: `cmake --build build --target RackZonesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/build-proof-green-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=0` |
| Recheck GREEN after reapplying fix | `build/tests`: `ZENE_CONTROL_CHECK_RESULTS=1 ./RackZonesTest theZoneSurfaceAddsResolvesAndUndoes > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/test-green-proof-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=0`; 3 passed, 0 failed |
| Affected test family | `build/tests`: `ctest -R '^RackZonesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/test-family-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=0`; 1/1 registered CTest case passed |
| Complexity gate | Worktree: `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/gate-complexity-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=0` |
| File length gate | Worktree: `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-187.r1/logs/gate-file-length-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | `EXIT=0` |

The requested `fff grep` tool was not available in the session (no matching tool or `fff` executable was exposed). I searched the relevant log lines with `rg` instead.

## Files committed

- `tests/src/core/RackZonesTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-187.md`
