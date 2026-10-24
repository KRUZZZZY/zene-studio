FIXED
# Cluster 5039-port-port-port-port-port-port: per-thread alternate signal stacks

## Verdict and code evidence

Verdict: **REAL**. POSIX signal dispositions are process-wide, but `sigaltstack()` is per-thread. Before the fix, `install()` registered one process-wide `SA_ONSTACK` handler and only configured the installing thread's stack. A worker thread could therefore enter the handler on its exhausted ordinary stack.

The product fix is already present in commit `46c3011ed` on this branch. `src/core/CrashReporter.cpp:100` provides thread-local stack storage; `:266-277` registers that storage with `sigaltstack()` once per calling thread. `install()` invokes the helper before its already-installed early return at `:280-285`, and installs handlers with `SA_ONSTACK` at `:292`. Audio and render entry points register their threads at `src/core/AudioEngineWorkerThread.cpp:307`, `src/core/AudioEngine.cpp:976`, `src/core/ProjectRenderer.cpp:281`, `include/AudioDummy.h:105`, and the audio backend callbacks and loops. The documented limit for private plug-in threads remains at `docs/CRASH-REPORTER.md:445-452`.

The round-two test change makes the proof follow the public `install()` path. `tests/src/core/CrashReporterTest.cpp:532-596` installs the reporter on the test thread, confirms the process-wide crash handler requests `SA_ONSTACK`, then calls `install()` from a second thread and checks that a `SA_ONSTACK` signal handler actually runs inside that thread's registered stack. It also confirms an otherwise unregistered thread has no alternate stack. This fails if the product patch is reverted.

## Regression evidence

Commands below ran from the worktree root except `ctest`, which ran from `build/tests`. The RED cycle reversed the product-only patch saved as `logs/fix.patch`; the tests remained in the worktree.

| Phase | Command | Exit | Result |
|---|---|---:|---|
| RED build | `cmake --build build --target zene CrashReporterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r2/logs/build-red-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Built the reverted product and regression test. |
| RED test | `ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r2/logs/test-red-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 | Failed at `hasStack`: the second thread had no alternate stack (13 passed, 1 failed). |
| GREEN build | `cmake --build build --target zene CrashReporterTest CrashReporterArmTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r2/logs/build-green-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Rebuilt with the product patch restored. |
| GREEN test | `ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r2/logs/test-green-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Passed. |
| Sibling test | `ctest -R '^CrashReporterArmTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r2/logs/test-arm-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Passed. |

Affected family count: **2 ctest executables ran**, `CrashReporterTest` and `CrashReporterArmTest`; neither run had zero tests. Test output is in `build-red-2.log`, `test-red-2.log`, `build-green-3.log`, `test-green-2.log`, and `test-arm-2.log` in the run log folder.

The proof patch was generated from the committed product change, excluding test files, and was applied and reversed successfully:

```text
git diff 46c3011ed^ 46c3011ed -- docs/CRASH-REPORTER.md include/AudioDummy.h include/CrashReporter.h src/core/AudioEngine.cpp src/core/AudioEngineWorkerThread.cpp src/core/CrashReporter.cpp src/core/CrashReporterWindows.cpp src/core/ProjectRenderer.cpp src/core/audio/AudioAlsa.cpp src/core/audio/AudioJack.cpp src/core/audio/AudioOss.cpp src/core/audio/AudioPortAudio.cpp src/core/audio/AudioPulseAudio.cpp src/core/audio/AudioSdl.cpp src/core/audio/AudioSndio.cpp src/core/audio/AudioSoundIo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r2/logs/fix.patch
EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r2/logs/fix.patch
EXIT=0
```

## Feature gates

Command: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5039-port-port-port-port-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r2/logs/feature-check-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — **EXIT=0**. The run produced the table below and `RESULT: PASS`.

| Gate | Result |
|---|---|
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS — no new item in changed files |
| file-length | PASS — no new item in changed files |

## Self-check

- Finding verdict is REAL: `sigaltstack()` is registered per thread, while `SA_ONSTACK` is process-wide (`src/core/CrashReporter.cpp:266-292`).
- Regression is deterministic: it uses `sigaltstack()` state and synchronous `raise(SIGUSR1)`; no sleep, timing threshold, or racing assertion is used.
- Negative control proves the regression: with the product-only patch reversed, `CrashReporterTest` failed at `hasStack`; restoring it made the same test pass.
- Affected test family: both registered crash reporter tests ran and passed after restoration.
- Caller coverage: the shared registration helper is used by the audio engine worker, engine render path, project renderer, dummy backend, and audio backend run/capture/callback paths (`46c3011ed`; examples at the evidence lines above). `docs/CRASH-REPORTER.md:445-452` states the remaining private plug-in-thread limitation.
- Collateral behavior: the fix changes only per-thread POSIX signal-stack registration and handler delivery; it does not touch project loading, saving, or render data. No project format limits or saved fields changed.
- Realtime, undo, saved projects, and commands: this follow-up changes only the regression test and this report; no product runtime behavior, project format, undo behavior, or control command changed.
- Audio-thread work: registration uses fixed thread-local stack storage and `sigaltstack()`; repeat calls return on the per-thread registered flag. The follow-up adds no audio-thread code. No command was added or changed, so command reversibility is not applicable.
- Registration: `tests/src/core/CrashReporterTest.cpp` is already listed in `tests/CMakeLists.txt` and `tests/upstream-modifications.txt`.
- Files in this follow-up commit: `tests/src/core/CrashReporterTest.cpp` and `docs/qwen-fixes-2026-10/cluster-5039-port-port-port-port-port-port.md`. The product fix remains in the existing `46c3011ed` commit.
