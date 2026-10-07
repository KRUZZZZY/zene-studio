# Cluster `new-p8-227` — FIXED

## Finding and evidence

The finding is real. `pressSlot()` schedules a Trigger re-press as `LaunchPending` at [SessionScheduler.cpp:129](../../src/core/SessionScheduler.cpp:129), replacing the current `Playing` phase. `Song::play()` uses `trackIsSessionActive()` to suppress arrangement playback while the Session View takes over a track ([Song.cpp:501](../../src/core/Song.cpp:501)). The old predicate recognized only `Playing` and `StopPending`, so the arrangement could resume during the retrigger wait.

`trackIsSessionActive()` now also recognizes `LaunchPending` when `startCount > 0` ([SessionScheduler.cpp:435](../../src/core/SessionScheduler.cpp:435)). That retains the existing take during a retrigger while leaving a first, not-yet-fired launch inactive.

The regression [SessionPlaybackTest.cpp:89](../../tests/src/core/SessionPlaybackTest.cpp:89) starts a Trigger slot at tick 0, re-presses it at tick 100, confirms it is `LaunchPending`, and checks that the track remains session-active. The original code failed this assertion. It failed again after the production patch was temporarily removed, then passed after restoration.

## Test evidence

All CTest commands below ran with working directory `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-227/build/tests`. Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs`.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Original code, RED | `ctest -R '^SessionPlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 | The new assertion failed. One test ran. |
| Fixed code, GREEN | <code>ctest -R '^(SessionSchedulerTest&#124;SessionSchedulerRenderTest&#124;SessionPlaybackTest)$' --output-on-failure &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/test-2.log 2&gt;&amp;1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"</code> | 0 | 3 tests passed. |
| Patch removed, RED proof | `ctest -R '^SessionPlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 | The identical assertion failed again. One test ran. |
| Patch restored, GREEN proof | <code>ctest -R '^(SessionSchedulerTest&#124;SessionSchedulerRenderTest&#124;SessionPlaybackTest)$' --output-on-failure &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/test-4.log 2&gt;&amp;1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"</code> | 0 | 3 tests passed; this is the final affected-family run. |

The final family summary was `100% tests passed, 0 tests failed out of 3`.

## Build, proof, and gate commands

All build commands ran from the worktree root. Each build exited 0:

```sh
cmake --build build --target zene SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
cmake --build build --target zene SessionPlaybackTest SessionSchedulerTest SessionSchedulerRenderTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
cmake --build build --target zene SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
cmake --build build --target zene SessionPlaybackTest SessionSchedulerTest SessionSchedulerRenderTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

The temporary removal and restoration used these commands from the worktree root; each exited 0:

```sh
git diff -- src/core/SessionScheduler.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

Both required gates ran from the worktree root and exited 0:

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-227.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |

The requested `fff grep` tool was not available in this session. I searched the log directory with `rg -n`, one term per invocation: `error` and `Assertion` had no matches; `FAILED` matched only the two expected RED CTest summaries.

## Files in the commit

- `src/core/SessionScheduler.cpp`
- `tests/src/core/SessionPlaybackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-227.md`

The source and test are already registered in `tests/fork-sources.txt`; no source registration change was needed.
