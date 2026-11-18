# Cluster new-p10-105

**Verdict: FIXED**

`CombFeedback::setDelay()` now rejects non-finite, negative, and values above `INT_MAX` before converting or changing either stored delay field. A rejection therefore preserves the previous valid delay. The regression instantiates `CombFeedback<1>`, configures a one-frame delay, tries `-1`, `-0.25`, NaN, positive infinity, and `INT_MAX + 1`, then verifies the expected impulse timing after each rejection.

Direct instantiation also exposed broken member initializers throughout `include/Delay.h`: constructors named nonexistent `m_feedBack`, `m_delay`, and `m_fraction` fields, so the header did not compile. Those initializers now name the declared gain, delay, and fraction fields and initialize them to their intended zero defaults. The inherited-file change is registered in `tests/upstream-modifications.txt`; `DeviceDspTest.cpp` was already registered in `tests/fork-sources.txt`.

**Reachability limitation:** a source search found no product include or call site for `CombFeedback` outside `include/Delay.h`; the only new use is the regression test. The source defect is reproduced and fixed, but current product-path reachability is absent.

## File evidence

- `include/Delay.h:70-75` initializes `CombFeedback`'s declared `m_gain`; `include/Delay.h:102-110` validates before conversion and assignment; `include/Delay.h:118-123` uses the stored delay to read the buffer.
- `include/Delay.h:146-151,217-224,300-305` corrects the other broken constructor initializers required for this header to compile.
- `tests/src/core/DeviceDspTest.cpp:210-229` contains the regression.
- `tests/upstream-modifications.txt` registers the inherited header change. `tests/fork-sources.txt:1942` already registers the test source.

## Evidence table

CTest commands below ran from `build/tests`. Build, test, and gate logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs`.

| Check | Exact command | Exit / result |
| --- | --- | --- |
| Initial direct-instantiation build | `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/build-1.log; printf 'EXIT=%s\n' "$rc"` | `2`; compiler reported the pre-existing invalid `Delay.h` initializers. |
| Build after initializer corrections, before delay fix | `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/build-2.log; printf 'EXIT=%s\n' "$rc"` | `0`. |
| RED before delay fix | `ctest -R '^DeviceDspTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/test-1.log; printf 'EXIT=%s\n' "$rc"` | `8`; the regression failed on the first invalid value, `-1`, because the prior one-frame delay behavior was not preserved. |
| Fixed build | `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/build-3.log; printf 'EXIT=%s\n' "$rc"` | `0`. |
| GREEN after fix | `ctest -R '^DeviceDspTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/test-2.log; printf 'EXIT=%s\n' "$rc"` | `0`; 1/1 CTest test ran. The regression passed; Qt totals were 3 passed, 0 failed, 8 skipped. |
| Reverse-fix command | `git diff -- include/Delay.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/fix.patch` | `0`; the unstaged patch contained the delay validation hunk and its `<limits>` include, leaving the staged compile-enabling initializer corrections in place. |
| Build with fix reversed | `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/build-4.log; printf 'EXIT=%s\n' "$rc"` | `0`. |
| Reverse-fix RED | `ctest -R '^DeviceDspTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/test-3.log; printf 'EXIT=%s\n' "$rc"` | `8`; the same regression failed again. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | `0`. |
| Restored build | `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/build-5.log; printf 'EXIT=%s\n' "$rc"` | `0`. |
| Restored GREEN | `ctest -R '^DeviceDspTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/test-4.log; printf 'EXIT=%s\n' "$rc"` | `0`; 1/1 CTest test ran and the regression passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/gate-1.log; printf 'EXIT=%s\n' "$rc"` | `0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r2/logs/gate-2.log; printf 'EXIT=%s\n' "$rc"` | `0`. |

**Files committed:** `include/Delay.h`, `tests/src/core/DeviceDspTest.cpp`, `tests/upstream-modifications.txt`, and `docs/qwen-fixes-2026-10/cluster-new-p10-105.md`.
