# Cluster new-p10-187-port

Verdict: **FIXED**

`plugins/Delay/Lfo.h:44-68` rejects non-finite frequencies and non-positive sample rates before changing LFO state. `plugins/Delay/Lfo.cpp:34-41` initializes every state field and routes constructor sample-rate setup through the guarded setter.

`tests/src/core/MathTest.cpp` uses a translation-unit-local access shim to set every LFO state field to known finite values before either input sequence. Lines exercise NaN frequency, and zero sample rate followed by zero frequency. This removes placement storage and Q_NEVER_INLINE setup; test state is deterministic with either production revision.

## Evidence

The affected CTest family contains one test (MathTest). The RED run executed 1/1 CTest and both LFO regression methods failed on the original production hunks. The restored implementation passed 1/1 CTest; both regression methods passed.

| Check | Exact command and working directory | Exit | Result |
|---|---|---:|---|
| Reverse committed production hunks; cwd worktree | `git diff -- plugins/Delay/Lfo.h plugins/Delay/Lfo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-187-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-187-port.r1/logs/fix.patch; status=$?; echo EXIT=$status` | 0 | Original implementation restored for RED proof. |
| Build original implementation; cwd worktree | `cmake --build build --target MathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-187-port.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status` | 0 | MathTest rebuilt. |
| RED; cwd build/tests | `ctest -R '^MathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-187-port.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status` | 8 | NaN and zero-sample-rate regressions both failed on a non-finite tick. |
| Restore committed production hunks; cwd worktree | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-187-port.r1/logs/fix.patch; status=$?; echo EXIT=$status` | 0 | Production fix restored. |
| Build restored implementation; cwd worktree | `cmake --build build --target MathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-187-port.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status` | 0 | MathTest rebuilt. |
| GREEN; cwd build/tests | `ctest -R '^MathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-187-port.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status` | 0 | 1/1 CTest passed; both LFO regression methods passed. |
| Build zene/delay/migration; cwd worktree | `cmake --build build --target zene delay PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-187-port.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status` | 0 | zene, delay, and PluginPortsMigrationTest rebuilt. |
| Complexity gate; cwd worktree | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-187-port.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status` | 1 | Pre-existing branch regressions (6 functions) unrelated to this change; no new regression introduced. |
| File-length gate; cwd worktree | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-187-port.r1/logs/gate-3.log 2>&1; status=$?; echo EXIT=$status` | 1 | Pre-existing branch regressions (14 files) unrelated to this change; no new regression introduced. |

## Explicit files committed

- `plugins/Delay/Lfo.cpp`
- `plugins/Delay/Lfo.h`
- `tests/src/core/MathTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-187-port.md`
