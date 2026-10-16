# cluster new-p9-604

Verdict: **FIXED**

## Evidence

- `src/core/BounceInPlace.cpp:235-238` retains the preparation-time refusal for an output that already aliases the open project.
- `src/core/BounceInPlace.cpp:417-424` checks the live `Engine::getSong()->projectFileName()` through `namesOpenProject()` before moving or replacing the output. This catches a same-document save to the bounce output while the child render is running.
- `tests/src/core/ControlRenderJobTest.cpp:448-484` queues a bounce to a path separate from the open project, waits for the child render PID, saves the same document to that path with `project.save`, and checks that completion fails while the saved project bytes, active filename, and source track remain intact.

The regression was red before the install-time guard: completion reported `done` instead of `failed`. Reversing only the install-time production guard reproduced the same failure; restoring it made the test pass.

## Commands and results

Commands ran from the worktree root unless noted. The ctest commands ran from `build/tests`.

| Check | Command | Exit |
| --- | --- | ---: |
| Build regression before production change | `cmake --build build --target ControlRenderJobTest` | 0 |
| Regression before install-time guard | `ctest -R '^ControlRenderJobTest$' --output-on-failure` | 8 (expected red at `ControlRenderJobTest.cpp:476`; job state was `done`) |
| Build with fix | `cmake --build build --target zene BounceInPlaceTest ControlBounceRenderTest ControlRenderJobTest` | 0 |
| Regression with fix | `ctest -R '^ControlRenderJobTest$' --output-on-failure` | 0 |
| Reverse only the new production guard | `git diff -- src/core/BounceInPlace.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-604.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-604.r2/logs/fix.patch` | 0 |
| Rebuild after guard reversal | `cmake --build build --target ControlRenderJobTest` | 0 |
| Regression with only the new guard reversed | `ctest -R '^ControlRenderJobTest$' --output-on-failure` | 8 (expected red; job state was `done`) |
| Restore the production guard | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-604.r2/logs/fix.patch` | 0 |
| Final build | `cmake --build build --target zene BounceInPlaceTest ControlBounceRenderTest ControlRenderJobTest` | 0 |
| Regression after restoration | `ctest -R '^ControlRenderJobTest$' --output-on-failure` | 0 |
| Affected-family tests | `ctest -R '^(BounceInPlaceTest|ControlBounceRenderTest|ControlRenderJobTest)$' --output-on-failure` | 0; 3/3 ctest targets ran and passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 |
| Whitespace check | `git diff --check` | 0 |

Both gates reported unrelated existing entries: the complexity gate lists six new over-target functions outside the changed files, and the file-length gate lists eleven over-500-line new sources outside the changed files. Neither gate baseline was changed. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-604.r2/logs` (`build-1.log` through `build-4.log`, `test-1.log` through `test-5.log`, and the two gate logs). No fff grep tool or executable was available in this session, so the requested `error`, `FAILED`, and `Assertion` log searches used `rg` separately.

## Files committed

- `src/core/BounceInPlace.cpp`
- `tests/src/core/ControlRenderJobTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-604.md`
