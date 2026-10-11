# Cluster new-p9-156

**Verdict: FIXED**

`plugins/GigPlayer/GigPlayer.cpp:499-584, 619-625` now reads bidirectional
loops in forward and reverse segments. The reverse segment is read from its low
frame and reversed by whole sample frames, preserving channel and sample-byte order.
`getPingPongIndex()` at `plugins/GigPlayer/GigPlayer.cpp:737-755` reflects within
the exclusive loop range; the first frame after `end` maps to `end - 2`.
The new private reader is declared at `plugins/GigPlayer/GigPlayer.h:313-314`.

The regression is
`PluginPortsMigrationTest::gigplayerBidirectionalLoopReflectsAtExclusiveEnd()`
at `tests/src/plugins/PluginPortsMigrationTest.cpp:409, 456-461`. It loads the
generated `tests/data/gigproof/pingpong.gig` and compares a post-boundary ramp
sample against its corresponding first-pass sample and checks that the first
reflected segment does not read the exclusive endpoint. With the production
patch reversed, `endSample` was `-0.0572107` and the assertion failed. With the
fix, the affected CTest family executed 1/1 test and passed.

The configured build has no CTest named `GigPlayer` (`ctest -N -R '^GigPlayer$'`
reported zero registered tests), so verification used the existing
`PluginPortsMigrationTest` family, which builds and exercises the GigPlayer
module.

## Evidence

| Phase | Exact command | Exit |
|---|---|---:|
| Fixture generator build | `g++ -std=c++17 -O2 tests/data/gigproof/make_gig.cpp -o build/tests/make_gig $(pkg-config --cflags --libs gig) > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/build-10.log 2>&1; echo EXIT=$?` | 0 |
| Fixture generation | `build/tests/make_gig --pingpong tests/data/gigproof/pingpong.gig > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/test-9.log 2>&1; echo EXIT=$?` | 0 |
| Baseline build | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Baseline RED | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Fixed build | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Fixed GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Reverse production patch | `git diff -- plugins/GigPlayer/GigPlayer.cpp plugins/GigPlayer/GigPlayer.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild without fix | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/build-13.log 2>&1; echo EXIT=$?` | 0 |
| Final regression RED | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/test-12.log 2>&1; echo EXIT=$?` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild with fix | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/build-14.log 2>&1; echo EXIT=$?` | 0 |
| Final regression GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/test-13.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/gate-7.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-156.r1/logs/gate-8.log 2>&1; echo EXIT=$?` | 0 |

## Files committed

- `plugins/GigPlayer/GigPlayer.cpp`
- `plugins/GigPlayer/GigPlayer.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/data/gigproof/make_gig.cpp`
- `tests/data/gigproof/pingpong.gig`
- `tests/data/gigproof/README.md`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-156.md`
