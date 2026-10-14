# Cluster new-p10-450: negative saved video duration

**Verdict: FIXED**

`VideoReference::load()` previously accepted negative or non-finite `<video duration>` values. `endTick()` then used that value to place the lane end before its offset. Loading now keeps finite nonnegative durations and re-probes the media for invalid or unparsable timing metadata; if the probe cannot provide a finite duration, it stores zero. The regression uses a valid two-second test clip and checks negative, `nan`, and `inf` saved values recover both the duration and an end tick after the offset.

## File evidence

- `src/core/VideoReference.cpp:109` parses and validates the saved duration; lines 115–120 re-probe invalid values.
- `tests/src/core/VideoTrackTest.cpp:139` covers negative and non-finite durations and verifies the resulting lane end.
- `include/VideoReference.h:57` documents the load-time probe.

## Evidence

Commands below ran from the worktree unless noted. CTest commands ran from `build/tests`. The initial RED assertion was `VideoTrackTest::invalidSavedDurationIsReprobed`: `qAbs(loaded.duration - 2.0) < 0.1` returned false for `-1`.

| Phase / evidence | Exact command or files | Exit / result |
|---|---|---:|
| Initial RED build | `cmake --build build --target VideoTrackTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/red-build-1.log 2>&1; echo EXIT=$?` | 0 |
| Initial RED test | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/red-test-1.log 2>&1; echo EXIT=$?` | 8 |
| Proof: reverse production patch | `git diff -- src/core/VideoReference.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Proof RED build | `cmake --build build --target VideoTrackTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/proof-red-build-1.log 2>&1; echo EXIT=$?` | 0 |
| Proof RED test | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/proof-red-test-1.log 2>&1; echo EXIT=$?` | 8 |
| Proof: restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Proof GREEN build | `cmake --build build --target VideoTrackTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/proof-green-build-1.log 2>&1; echo EXIT=$?` | 0 |
| Proof GREEN test | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/proof-green-test-1.log 2>&1; echo EXIT=$?` | 0 |
| Final dependent build | `cmake --build build --target zene VideoTrackTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/final-build-1.log 2>&1; echo EXIT=$?` | 0 |
| Final family test | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/final-test-1.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/complexity-gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/file-length-gate-1.log 2>&1; echo EXIT=$?` | 0 |
| Affected-family count | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-450.r1/logs/final-test-1.log 2>&1; echo EXIT=$?` | 1 registered CTest test; 1/1 passed |
| Files committed | `include/VideoReference.h`; `src/core/VideoReference.cpp`; `tests/src/core/VideoTrackTest.cpp`; `docs/qwen-fixes-2026-10/cluster-new-p10-450.md` | 4 |

**Affected-family count:** `VideoTrackTest` is one registered CTest test; the final run executed 1/1 and passed. The regression slot failed both before the fix and with the production patch reversed, and passed after restoring it.

**Files committed:** `include/VideoReference.h`, `src/core/VideoReference.cpp`, `tests/src/core/VideoTrackTest.cpp`, and this report.
