# Cluster new-p9-589: formant interpolation bounds

Verdict: **FIXED**

`include/BasicFilters.h:748-754` now caps the formant position at the last table row and caps the lower interpolation index at row 4. The interpolation therefore uses only rows 0–5, and frequencies at or above the last vowel use the final row. The regression at `tests/src/core/DeviceDspTest.cpp:208-223` compares the 20 kHz filter output with the table endpoint at 17.5 kHz. `tests/upstream-modifications.txt:33` records the inherited header change.

## Test evidence

The affected CTest family is `DeviceDspTest`: **1 CTest test ran**. Before the fix, the run reported 1/1 failed; QtTest identified the coefficient output mismatch (`2.39405e-05` versus `0.000780353`). After the fix, CTest reported 1/1 passed. The pre-fix QtTest totals were 2 passed, 1 failed, and 8 skipped.

All builds ran from the worktree root. All CTest commands ran from `build/tests`.

| Step | Command | Exit | Log |
| --- | --- | ---: | --- |
| Build regression against original code | `cmake --build build --target DeviceDspTest zene` | 0 | `build-1.log` |
| RED | `ctest -R '^DeviceDspTest$' --output-on-failure` | 8 | `test-1.log` |
| Build with fix | `cmake --build build --target DeviceDspTest zene` | 0 | `build-2.log` |
| GREEN | `ctest -R '^DeviceDspTest$' --output-on-failure` | 0 | `test-2.log` |

Reverse-patch proof:

| Step | Command | Exit | Log |
| --- | --- | ---: | --- |
| Save and reverse production patch | `git diff -- include/BasicFilters.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-589.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-589.r1/logs/fix.patch` | 0 | — |
| Rebuild without fix | `cmake --build build --target DeviceDspTest zene --parallel 4` | 0 | `build-3.log` |
| RED without fix | `ctest -R '^DeviceDspTest$' --output-on-failure` | 8 | `test-3.log` |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-589.r1/logs/fix.patch` | 0 | — |
| Rebuild with fix restored | `cmake --build build --target DeviceDspTest zene --parallel 4` | 0 | `build-4.log` |
| GREEN with fix restored | `ctest -R '^DeviceDspTest$' --output-on-failure` | 0 | `test-4.log` |

## Gates

| Command | Exit | Log |
| --- | ---: | --- |
| `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

`fff` was not available in the session, so the numbered logs were searched with `rg` for the required terms and the regression result.

## Files committed

- `include/BasicFilters.h`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-589.md`
