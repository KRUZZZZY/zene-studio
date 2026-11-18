# Cluster new-p9-590: second formant feedback history

Verdict: **FIXED**

`include/BasicFilters.h:646` fed the second formant's first stage from `m_vfbp[0]`, even though that stage keeps its band-pass history at index 1. It now reads `m_vfbp[1]`. The behavior applies to both `Formantfilter` and `FastFormant`, which share this update path.

The regression at `tests/src/core/DeviceDspTest.cpp:208` processes `0.5`, `0.0`, then `0.25` through `Formantfilter` and checks the expected third output. With the bug, the output is `0.0201515257`; with the fix, it is within `1e-5` of `0.02071115`.

## Evidence

CTest commands below were run from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-590/build/tests`. All output was redirected to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs`.

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Original code, RED | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 8 | New assertion failed at `0.0201515257`; 1/1 ctest failed. |
| Fixed code, GREEN | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | 1/1 ctest passed. |
| Fix removed, RED | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 8 | The same assertion failed again at `0.0201515257`; 1/1 ctest failed. |
| Fix restored, GREEN | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | 1/1 ctest passed. |
| Affected ctest family count | — | — | The logged `DeviceDspTest` runs executed 1 test out of 1 selected. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | Passed. |

The validated target build command was `cmake --build build --target zene DeviceDspTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` (exit 0). The same command with `build-4.log` and `build-5.log` also exited 0 for the fix-removal and fix-restoration checks.

The first post-fix build invocation yielded before its exit was surfaced. I repeated the target build and used the recorded `build-3.log` result as the build evidence.

`git diff -- include/BasicFilters.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs/fix.patch` exited 0. Restoring it with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-590.r1/logs/fix.patch` also exited 0.

## Files in the commit

- `include/BasicFilters.h`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-590.md`
