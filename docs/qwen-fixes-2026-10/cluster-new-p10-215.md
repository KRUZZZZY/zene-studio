# Cluster new-p10-215

**Verdict: FIXED**

`oneIn(0)` evaluated modulo zero. The helper now documents that zero is an impossible chance and returns `false` before advancing the random generator. `include/lmms_math.h:187-189` contains the contract and guard. `tests/src/core/MathTest.cpp:52-73` exercises the call in a subprocess and checks for a normal zero exit; this lets the test report the original crash as a Qt assertion failure.

## Evidence

The affected ctest family is `MathTest`: 1 test executed. The original code failed with `CrashExit`; reversing the production patch reproduced that failure. The fixed code passed.

| Check | Exact command and working directory | Exit |
| --- | --- | ---: |
| Initial test build, worktree root | `cmake --build build --target MathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-215.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| RED, original code, `build/tests` | `ctest -R '^MathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-215.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Save patch and reverse it, worktree root | `git diff -- include/lmms_math.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-215.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-215.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| RED, reversed patch, `build/tests` | `ctest -R '^MathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-215.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Reapply patch, worktree root | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-215.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| GREEN build, worktree root | `cmake --build build --target zene MathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-215.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| GREEN test, `build/tests` | `ctest -R '^MathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-215.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Complexity gate, worktree root | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-215.r1/logs/gate-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File length gate, worktree root | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-215.r1/logs/gate-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

Files committed:

- `include/lmms_math.h`
- `tests/src/core/MathTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-215.md`
