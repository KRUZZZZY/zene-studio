# Cluster new-p9-505

Verdict: **FIXED**

## Evidence

- [src/gui/VideoLane.cpp:94-105](../../src/gui/VideoLane.cpp#L94) now sends an empty-path request when `reload()` finds no attachment. It uses a stale generation tag, so [VideoLane::take](../../src/gui/VideoLane.cpp#L113) ignores the null close response instead of treating it as a thumbnail.
- [src/core/VideoFramePump.cpp:71-77](../../src/core/VideoFramePump.cpp#L71) closes its decoder when a changed request path is empty.
- [tests/src/core/VideoTrackTest.cpp:270-290](../../tests/src/core/VideoTrackTest.cpp#L270) waits for all lane thumbnails, detaches, and requires the pump's next answer to be null.
- Before the fix, the regression failed at `answers.count()` (actual 0, expected 1). Reversing only the production patch and rebuilding both `zene` and `VideoTrackTest` reproduced that failure. With the patch restored, the same test passed.
- A verbose passing family run reported 13 passed, 0 failed, 1 skipped. CTest ran 1/1 target (`VideoTrackTest`).

| Required evidence | Exact command and result |
| --- | --- |
| RED, patch removed after rebuilding `zene` and `VideoTrackTest` | From `build/tests`: `ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/test-6.log 2>&1; echo EXIT=$?` → EXIT=8; the detach-response assertion failed. |
| GREEN, patch restored after rebuilding `zene` and `VideoTrackTest` | From `build/tests`: `ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/test-7.log 2>&1; echo EXIT=$?` → EXIT=0. |
| Affected-family count | From `build/tests`: `ctest -R VideoTrackTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/test-5.log 2>&1; echo EXIT=$?` → EXIT=0; 1/1 CTest target, QtTest totals 13 passed, 0 failed, 1 skipped. |
| Ratchets | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/gate-5.log 2>&1; echo EXIT=$?` → EXIT=0; `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/gate-6.log 2>&1; echo EXIT=$?` → EXIT=0. |
| Files committed for this cluster | `src/gui/VideoLane.cpp`, `tests/src/core/VideoTrackTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p9-505.md`. |

## Commands and results

All build commands below ran from the worktree root. All CTest commands ran from `build/tests`. Each command wrote its output to the named new log in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs` and printed the recorded exit code.

Initial regression on the unmodified production code (RED):

```text
cmake --build build --target VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/build-1.log 2>&1; echo EXIT=$?  => EXIT=0
ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/test-1.log 2>&1; echo EXIT=$?  => EXIT=8
```

First fixed run (GREEN):

```text
cmake --build build --target VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/build-2.log 2>&1; echo EXIT=$?  => EXIT=0
ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/test-2.log 2>&1; echo EXIT=$?  => EXIT=0
```

Required patch reversal proof (RED, then GREEN):

```text
git diff -- src/gui/VideoLane.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/fix.patch; echo EXIT=$?  => EXIT=0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/fix.patch; echo EXIT=$?  => EXIT=0
cmake --build build --target VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/build-3.log 2>&1; echo EXIT=$?  => EXIT=0
ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/test-3.log 2>&1; echo EXIT=$?  => EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/fix.patch; echo EXIT=$?  => EXIT=0
cmake --build build --target VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/build-4.log 2>&1; echo EXIT=$?  => EXIT=0
ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/test-4.log 2>&1; echo EXIT=$?  => EXIT=0
ctest -R VideoTrackTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/test-5.log 2>&1; echo EXIT=$?  => EXIT=0
```

The explicit `zene` rebuild during the repeated negative control also failed as expected without the patch and passed after restoration:

```text
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/fix.patch; echo EXIT=$?  => EXIT=0
cmake --build build --target zene VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/build-5.log 2>&1; echo EXIT=$?  => EXIT=0
ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/test-6.log 2>&1; echo EXIT=$?  => EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/fix.patch; echo EXIT=$?  => EXIT=0
cmake --build build --target zene VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/build-6.log 2>&1; echo EXIT=$?  => EXIT=0
ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/test-7.log 2>&1; echo EXIT=$?  => EXIT=0
```

The required ratchets passed:

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/gate-1.log 2>&1; echo EXIT=$?  => EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/gate-2.log 2>&1; echo EXIT=$?  => EXIT=0
```

Both gates were run again after this report was written:

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/gate-3.log 2>&1; echo EXIT=$?  => EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-505.r1/logs/gate-4.log 2>&1; echo EXIT=$?  => EXIT=0
```

The `fff grep` tool was not exposed in this session; `command -v fff >/dev/null 2>&1; echo EXIT=$?` returned EXIT=1. The recorded test logs were searched with `rg -n` instead.

## Files committed

- `src/gui/VideoLane.cpp`
- `tests/src/core/VideoTrackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-505.md`
