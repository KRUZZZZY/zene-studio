# Cluster new-p10-129 — expression time wraps during a long note

**Verdict: FIXED**

## Evidence

`ExprSynth` kept its note and release frame positions in 32-bit unsigned integers. At the boundary, the note position returned to zero, and the `t` expression output jumped back. The note counters, `ExprFront::setIntegrate` frame pointer and its initialization snapshot now use `f_cnt_t` (64-bit). Xpressive's graph-preview counter uses the same type.

The relevant production code is [ExprSynth.h](../../plugins/Xpressive/ExprSynth.h:55) (`setIntegrate`) and [ExprSynth.h](../../plugins/Xpressive/ExprSynth.h:108) (note and release counters); [ExprSynth.cpp](../../plugins/Xpressive/ExprSynth.cpp:83) and [ExprSynth.cpp](../../plugins/Xpressive/ExprSynth.cpp:677) carry that width through `integrate()`, while [ExprSynth.cpp](../../plugins/Xpressive/ExprSynth.cpp:788) updates expression time after each rendered frame. The graph preview call site is [Xpressive.cpp](../../plugins/Xpressive/Xpressive.cpp:555).

The regression in [ExprSynthTest.cpp](../../tests/src/plugins/ExprSynthTest.cpp:48) renders from frame `2^32−1` to the next frame, then checks the rendered `t + integrate(1)` output remains above 80,000 seconds. It failed on the original code and passed with the fix. The pre-existing `tests/reference/Xpressive` files are frozen upstream copies; they were left unchanged. This configured tree had no CTest named `ExprSynth` or `Xpressive`, so the new `ExprSynth` test is registered in [tests/CMakeLists.txt](../../tests/CMakeLists.txt:1157).

## Required evidence table

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline build | `cmake --build build --target zene xpressive ExprSynth -- -j12` | 0 | Rebuilt `zene`, Xpressive and the RED test target with the production fix reversed. Log: `build-final-red.log`. |
| RED on original code | `ctest -R '^ExprSynth$' --output-on-failure` from `build/tests` | 8 | 1 test ran; `output[0].left() > 80000.f` failed because `t` wrapped. Log: `test-final-red.log`. |
| Fixed build | `cmake --build build --target zene xpressive ExprSynth -- -j12` | 0 | Rebuilt `zene`, Xpressive and the regression target with the fix reapplied. Log: `build-final-green.log`. |
| GREEN with fix | `ctest -R '^ExprSynth$' --output-on-failure` from `build/tests` | 0 | 1 test ran and passed. Log: `test-final-green.log`. |
| Affected CTest family count | `ctest -R '^ExprSynth$' --output-on-failure` | — | 1 test executed. |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 0 | Pass. Final rerun: `gate-complexity-4.log`. |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 0 | Pass. Final rerun: `gate-file-length-4.log`. |

Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-129.r1/logs`.

## Reversal proof

The production patch was reversed, rebuilt, and the identical regression failed again. Reapplying the patch, rebuilding `zene`, Xpressive and the test target, then rerunning the same CTest passed.

| Step | Exact command | Exit |
|---|---|---:|
| Save and reverse production patch | `git diff -- plugins/Xpressive/ExprSynth.cpp plugins/Xpressive/ExprSynth.h plugins/Xpressive/Xpressive.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-129.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-129.r1/logs/fix.patch` | 0 |
| Rebuild without fix | `cmake --build build --target zene xpressive ExprSynth -- -j12` | 0 (`build-final-red.log`) |
| Same regression without fix | `ctest -R '^ExprSynth$' --output-on-failure` from `build/tests` | 8 (`test-final-red.log`) |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-129.r1/logs/fix.patch` | 0 |
| Rebuild with fix | `cmake --build build --target zene xpressive ExprSynth -- -j12` | 0 (`build-final-green.log`) |
| Same regression with fix | `ctest -R '^ExprSynth$' --output-on-failure` from `build/tests` | 0 (`test-final-green.log`) |

## Other build attempts

The new target was added to the already-configured tree. The first build invocation exited 2 because Make reported no rule for `ExprSynth`; a subsequent build-system regeneration made the target available. The next invocation exited 2 on a test compile error (`Song` was incomplete), corrected by including `Song.h`. These setup attempts are logged as `build-red-1.log` (2), `build-red-2.log` (2), and `build-red-3.log` (0). The first test draft also exited 8 at its direct counter assertion (`test-red-1.log`); the final draft checks the rendered expression output and exits 8 on the original code. The final fixed and reversed builds/tests are `build-final-red.log` (0), `test-final-red.log` (8), `build-final-green.log` (0), and `test-final-green.log` (0).

## Files committed

- `plugins/Xpressive/ExprSynth.cpp`
- `plugins/Xpressive/ExprSynth.h`
- `plugins/Xpressive/Xpressive.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `tests/src/plugins/ExprSynthTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-129.md`
