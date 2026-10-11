# Cluster new-p9-450: malformed Scala ratios

**Verdict: FIXED**

`ratioFromLine()` now refuses slash ratios containing more than one `/`. Integer-only intervals still use the existing denominator of 1. The denominator conversion and the rest of the scale-loading path are unchanged.

Evidence:

- `src/core/SessionTuningScala.cpp:63-70` checks the slash count before reading the denominator.
- `tests/src/core/SessionTuningTest.cpp:605-619` exercises `3/2/7` through `mts.load_scale`, expects a typed refusal, and checks the previously loaded table remains intact. The failure diagnostic now names the tested interval line.
- The CTest family matched one test (`SessionTuningTest`, CTest test #46).

## Verification evidence

| Check | Command/log | Result |
| --- | --- | --- |
| RED before fix | `ctest -R SessionTuningTest --output-on-failure` (`test-2.log`) | EXIT=8; 1/1 failed because `3/2/7` was accepted. |
| GREEN after fix | `ctest -R SessionTuningTest --output-on-failure` (`test-3.log`) | EXIT=0; 1/1 passed. |
| Reverse-patch proof | Reverse only the production diff, rebuild, and rerun the same test (`test-4.log`) | Reverse EXIT=0, build EXIT=0, CTest EXIT=8; the assertion again reports `3/2/7` was accepted. |
| Restored-patch proof | Reapply the saved production diff, rebuild, and rerun (`test-5.log`) | Apply EXIT=0, build EXIT=0, CTest EXIT=0; 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (`gate-1.log`) | EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check` (`gate-2.log`) | EXIT=0. |

All CTest invocations used `ctest -R SessionTuningTest --output-on-failure` from `build/tests`. Build and gate commands ran from the worktree root. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/`.

`fff grep` was not available in the session (no fff tool was exposed and `command -v fff` returned no path). The logs were searched with `rg` for `error`, `FAILED`, and `Assertion`; the RED test results and one corrected test-source compile error were identified explicitly below.

## Commands and observed exit codes

`cmake --build build --target zene SessionTuningTest` commands ran from the worktree root. These are the exact invocations, including log redirection and the captured underlying exit status:

1. `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0.
2. `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=2. The first diagnostic edit omitted a closing parenthesis in `QVERIFY2`; corrected before the next test run.
3. `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0.
4. `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0.
5. `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0.
6. `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0.

CTest commands ran from `build/tests`:

1. `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=8. The new case failed; its initial diagnostic named the final valid `3/2` line, so the assertion message was corrected and the RED run repeated.
2. `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=8. RED: `3/2/7` was accepted.
3. `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0. GREEN: 1/1 passed.
4. `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=8 after removing the production fix; the same `3/2/7` assertion failed again.
5. `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0 after restoring the fix; 1/1 passed.

Reversibility commands, from the worktree root:

- `git diff -- src/core/SessionTuningScala.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit 0` — EXIT=0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit 0` — EXIT=0.

Ratchet gates, from the worktree root:

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0.

## Files in the cluster commit

- `src/core/SessionTuningScala.cpp`
- `tests/src/core/SessionTuningTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-450.md`
