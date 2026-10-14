# Cluster new-p9-422

Verdict: **FIXED**

`SessionScheduler::PublishedColumns` limited the audio-thread publication and its reader to 64 columns even though the Session View supports 256 tracks. Track 64 could play while the clip launcher and `session.state` saw it as idle.

Evidence:

- `include/SessionScheduler.h:370` sets `PublishedColumns` to 256.
- `src/core/SessionSceneFollow.cpp:313` sizes the packed publication array from that constant; line 316 filters against the same range, and line 336 applies the range when reading.
- `tests/src/core/SessionSceneFollowTest.cpp:105` launches track 64 and verifies the scheduler slot and published phase/scene. The phase assertion is at line 113.

## Regression evidence

The RED and GREEN commands below were run from `build/tests`. The affected CTest family has one registered test; the fixed run executed 1/1.

| Check | Exact command | Exit / result |
|---|---|---|
| Initial RED, original code | `ctest -R '^SessionSceneFollowTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 8; 0/1 passed. `columnState(64)` returned phase 0; expected phase 2. |
| First fixed GREEN | `ctest -R '^SessionSceneFollowTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0; 1/1 passed. |
| RED with production patch reversed | `ctest -R '^SessionSceneFollowTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 8; same assertion failed, 0/1 passed. |
| Final GREEN after reapplying patch | `ctest -R '^SessionSceneFollowTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0; 1/1 passed. |

Both CTest commands used the required working directory, `build/tests`.

## Build, proof, and gates

| Purpose | Exact command | Exit |
|---|---|---:|
| Build regression target before initial RED | `cmake --build build --target SessionSceneFollowTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Post-fix target rebuild before first GREEN | `cmake --build build --target zene SessionSceneFollowTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Save production patch | `git diff -- include/SessionScheduler.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/fix.patch` | 0 |
| Reverse production patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/fix.patch` | 0 |
| Rebuild `zene` and regression target with patch reversed | `BUNNY_JOBS=6 cmake --build build --target zene SessionSceneFollowTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/fix.patch` | 0 |
| Final rebuild of `zene` and regression target | `BUNNY_JOBS=6 cmake --build build --target zene SessionSceneFollowTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/build-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-422.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |

Files committed:

- `include/SessionScheduler.h`
- `tests/src/core/SessionSceneFollowTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-422.md`
