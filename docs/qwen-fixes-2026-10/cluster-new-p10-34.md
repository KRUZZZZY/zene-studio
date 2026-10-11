# Cluster new-p10-34

Verdict: **FIXED**

## Evidence

- `src/core/ControlCommandsMixerRoutes.cpp:267-280` captures a regular send's amount, pre-fader flag, pan and enabled state. Its undo recreates the route, then restores pan and enabled state.
- `tests/src/core/MixingRoutingTest.cpp:186-206` pans a send to `-0.65`, disables it, removes it, undoes the removal, and checks that both values return.
- The regression failed before the production change and failed again with the production patch reversed. In both cases the restored pan was `0`, expected `-0.65`. It passed with the patch applied.

## Commands and results

Build command, run from the worktree root (all four invocations exited 0):

```sh
cmake --build build --target zene MixingRoutingTest
```

Each build's output was redirected to a new log: `build-1.log`, `build-2.log`, `build-3.log`, and `build-4.log` in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r1/logs`.

The affected family is `MixingRoutingTest`: CTest ran 1 registered target per invocation. The original and reversed-patch runs each exited 8 and reported 10 passed, 1 failed inside QtTest; the assertion showed actual pan `0`, expected `-0.65`. The fixed runs each exited 0 and CTest reported 1/1 passed.

| Phase | Command (run from `build/tests`) | Exit |
| --- | --- | ---: |
| Original code, RED | `ctest -R MixingRoutingTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r1/logs/test-1.log 2>&1` | 8 |
| Fix applied, GREEN | `ctest -R MixingRoutingTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r1/logs/test-2.log 2>&1` | 0 |
| Production patch reversed, RED | `ctest -R MixingRoutingTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r1/logs/test-3.log 2>&1` | 8 |
| Patch reapplied, GREEN | `ctest -R MixingRoutingTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r1/logs/test-4.log 2>&1` | 0 |

Reversal proof commands and exits:

```sh
git diff -- src/core/ControlCommandsMixerRoutes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r1/logs/fix.patch  # EXIT=0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r1/logs/fix.patch  # EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r1/logs/fix.patch  # EXIT=0
```

Ratchet gates, run from the worktree root after the code and test edits:

| Command | Exit | Log |
| --- | ---: | --- |
| `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

No `fff` grep tool or executable was available in this session, so the logs were searched with filtered `rg` queries for `error`, `FAILED`, and `Assertion`.

## Files in the commit

- `src/core/ControlCommandsMixerRoutes.cpp`
- `tests/src/core/MixingRoutingTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-34.md`
