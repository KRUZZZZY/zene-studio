# Cluster new-p9-156

**Verdict: FIXED**

The original bidirectional reader fix remains in place: it reads loop segments
in the correct direction and reflects at the exclusive loop end. The review's
one-frame edge case is fixed in `loadBidirectionalSample()`: when
`halfPeriod == 0`, `repeatSampleFrame()` reads only `loopStart` once and copies
that frame across all requested post-loop frames. It does not read adjacent
source frames (`plugins/GigPlayer/GigPlayer.cpp:527-542, 548-581`).

The longer-loop/exclusive-end regression remains
`PluginPortsMigrationTest::gigplayerBidirectionalLoopReflectsAtExclusiveEnd()`
(`tests/src/plugins/PluginPortsMigrationTest.cpp:411-468`). The added
`gigplayerSingleFrameLoopRepeatsFrame()` uses a `[64, 65)` loop and distinct
neighboring PCM, and compares two nonzero post-loop output samples
(`tests/src/plugins/PluginPortsMigrationTest.cpp:470-521`). Before the fix the
assertion failed with `0.130387` versus `0.156997`; reversing only the production
patch reproduced the same failure. With the fix restored, the targeted CTest
passed. The affected family is one CTest test (`PluginPortsMigrationTest`),
which executes the QtTest slots in that binary.

## Evidence

All build and test output was written to new files under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs`.
This session did not expose the requested fff grep tool, so logs were searched
with `rg`.

| Phase | Exact command | Exit |
|---|---|---:|
| Build one-frame fixture generator | `g++ -std=c++17 -O2 tests/data/gigproof/make_gig.cpp -o /tmp/zene-gigproof-make-gig $(pkg-config --cflags --libs gig) > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/fixture-build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Generate one-frame fixture | `/tmp/zene-gigproof-make-gig --single-frame-loop tests/data/gigproof/single-frame-loop.gig > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/fixture-gen-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Build before RED | `cmake --build build --target zene PluginPortsMigrationTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Initial RED | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/test-1-red.log 2>&1; rc=$?; echo EXIT=$rc` | 8 (9 passed, 1 failed) |
| Build with fix | `cmake --build build --target zene PluginPortsMigrationTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/build-2-green.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/test-2-green.log 2>&1; rc=$?; echo EXIT=$rc` | 0 (1/1) |
| Reverse production patch | `git diff -- plugins/GigPlayer/GigPlayer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild reversed version | `cmake --build build --target zene PluginPortsMigrationTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/build-3-reverse.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Reverse-patch RED | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/test-3-reverse-red.log 2>&1; rc=$?; echo EXIT=$rc` | 8 (same assertion; 9 passed, 1 failed) |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Final rebuild | `cmake --build build --target zene PluginPortsMigrationTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/build-4-final.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Final GREEN | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/test-4-final.log 2>&1; rc=$?; echo EXIT=$rc` | 0 (1/1) |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/gate-3-complexity-final.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r2/logs/gate-4-file-length-final.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Files in this follow-up commit

- `plugins/GigPlayer/GigPlayer.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/data/gigproof/make_gig.cpp`
- `tests/data/gigproof/README.md`
- `tests/data/gigproof/single-frame-loop.gig`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-156.md`
