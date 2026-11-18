# Cluster `new-p8-227` — FIXED

## Finding and correction

The finding is real. The previous patch made `trackIsSessionActive()` keep the track under Session View control during a retrigger, but `playbackForColumn()` still rejected `LaunchPending`. As a result, `Song::play()` suppressed the arrangement and received no session clip to render during the wait. The earlier report's claim that the existing take was retained was incorrect at the playback level.

`playbackForColumn()` now treats a `LaunchPending` slot with `startCount > 0` as the current take. Since the pending relaunch has not changed `startedTick`, the reported position continues from the previous start until the grid line. A first pending launch has `startCount == 0` and remains inactive. The helper at [SessionPlayback.cpp:25](../../src/core/SessionPlayback.cpp#L25) keeps the predicate small; the render view uses it at [SessionPlayback.cpp:129](../../src/core/SessionPlayback.cpp#L129). The existing takeover predicate is at [SessionScheduler.cpp:435](../../src/core/SessionScheduler.cpp#L435), and `Song::play()` uses the takeover and playback view at [Song.cpp:501](../../src/core/Song.cpp#L501).

The regression [SessionPlaybackTest.cpp:89](../../tests/src/core/SessionPlaybackTest.cpp#L89) publishes pattern 7, starts it at tick 0, requests a Bar-quantized retrigger at tick 100, and verifies that takeover remains active and pattern 7 is returned at ticks 100 and 120. [SessionPlaybackTest.cpp:119](../../tests/src/core/SessionPlaybackTest.cpp#L119) verifies that a first pending launch remains inactive.

## Evidence

All build and gate commands ran from the worktree root. CTest commands ran from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-227/build/tests`. Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs`.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Regression RED build | `cmake --build build --target zene SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt `zene` and `SessionPlaybackTest` before the RED run. |
| Regression before playback fix, RED | `ctest -R '^SessionPlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | The new `waiting.playing` assertion failed; 13 QTest cases passed and 1 failed. |
| Final production patch removed, RED proof | <code>ctest -R '^(SessionPlaybackTest&#124;SessionPlaybackRenderTest)$' --output-on-failure &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/test-6.log 2&gt;&amp;1; rc=$?; echo EXIT=$rc</code> | 8 | The same assertion failed; `SessionPlaybackRenderTest` passed. |
| Final production patch restored, GREEN proof | <code>ctest -R '^(SessionPlaybackTest&#124;SessionPlaybackRenderTest)$' --output-on-failure &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/test-7.log 2&gt;&amp;1; rc=$?; echo EXIT=$rc</code> | 0 | Both affected-family CTest tests passed (2/2). |

The final patch-removal proof used these commands from the worktree root; each exited 0:

```sh
git diff -- src/core/SessionPlayback.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/fix.patch
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/fix.patch
```

The final builds before the proof RED and GREEN each exited 0:

```sh
cmake --build build --target zene SessionPlaybackTest SessionPlaybackRenderTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc
cmake --build build --target zene SessionPlaybackTest SessionPlaybackRenderTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc
```

Both required final gates passed:

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

The first complexity run, `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r2/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc`, exited 1 because `playbackForColumn()` reached CCN 12. Factoring the phase check into `hasCurrentPlaybackTake()` brought it under the limit. The final complexity gate passed without changing its baseline. The requested `fff grep` tool was unavailable, so I searched logs with `rg -n` instead.

## Files in this commit

- `src/core/SessionPlayback.cpp`
- `tests/src/core/SessionPlaybackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-227.md`

The test source is already registered in `tests/fork-sources.txt`; no registration change was needed.
