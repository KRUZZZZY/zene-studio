# Cluster new-p9-548

Verdict: **FIXED**

## Evidence

`src/core/AudioEngineProfiler.cpp:finishPeriod()` no longer formats a `QString` or writes to `QFile` on the audio path. Measurements are pushed into a bounded lock-free ring buffer (`m_buffer` / `m_writeIndex` / `m_readIndex`); a background writer thread (`writerLoop`, started in `setOutputFile`) drains the buffer and performs the disk write. `stopWriter()` joins the thread and drains remaining entries; the destructor does the same.

- `include/AudioEngineProfiler.h`: destructor declared; `writerLoop()`/`stopWriter()`; bounded buffer and thread members.
- `src/core/AudioEngineProfiler.cpp`: bounded-buffer push in `finishPeriod`; writer thread in `setOutputFile`.
- `tests/src/core/AudioEngineProfilerTest.cpp`: static guard that `finishPeriod` references neither `m_outputFile` nor `QString`, plus an end-to-end check that the measurement reaches the file.

Affected family: **1/1 CTest target** (`AudioEngineProfilerTest`).

## RED and GREEN

| Phase | Exact command | Exit |
|---|---|---:|
| RED build | `cmake --build build --target zene AudioEngineProfilerTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/build-12.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED test | `cd build/tests && ctest -R '^AudioEngineProfilerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/test-10.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| GREEN build | `cmake --build build --target zene AudioEngineProfilerTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/build-11.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN test | `cd build/tests && ctest -R '^AudioEngineProfilerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/test-9.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Reverse-patch proof

| Step | Exact command | Exit |
|---|---|---:|
| Save and reverse the product fix | `git diff -- include/AudioEngineProfiler.h src/core/AudioEngineProfiler.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild without the fix | `cmake --build build --target zene AudioEngineProfilerTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/build-12.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Regression fails without the fix | `cd build/tests && ctest -R '^AudioEngineProfilerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/test-10.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Restore the fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild with the fix | `cmake --build build --target zene AudioEngineProfilerTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/build-final.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Regression passes with the fix | `cd build/tests && ctest -R '^AudioEngineProfilerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/test-final.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Ratchet gates

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/gate-final.log 2>&1; rc=$?; echo EXIT=$rc` | 1 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-548.r2/logs/gate-final2.log 2>&1; rc=$?; echo EXIT=$rc` | 1 |

Both gates report only pre-existing regressions in files not touched by this fix (6 complexity, 14 file-length); no new regression was introduced by this change.

## Files committed

- `include/AudioEngineProfiler.h`
- `src/core/AudioEngineProfiler.cpp`
- `tests/src/core/AudioEngineProfilerTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-548.md`
