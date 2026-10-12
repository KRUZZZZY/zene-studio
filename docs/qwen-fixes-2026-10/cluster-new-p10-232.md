# Cluster new-p10-232

Verdict: FIXED

`lv2_evbuf_write` now computes the padded event size with 64-bit intermediates and checks that the existing atom data fits the capacity before subtracting. The check therefore rejects a write that would exceed capacity after padding, without unsigned subtraction underflow.

Evidence:

- [src/core/lv2/Lv2Evbuf.cpp:175](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-232/src/core/lv2/Lv2Evbuf.cpp:175) computes the padded size; lines 177-180 check used and remaining capacity before writing.
- [tests/src/core/Lv2StateTest.cpp:146](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-232/tests/src/core/Lv2StateTest.cpp:146) covers capacity 33 and a one-byte payload, expecting refusal and unchanged event size.

## Regression evidence

Commands ran from `build/tests`; each CTest run executed 1/1 test (`Lv2StateTest`). Exit codes below are the captured CTest codes. Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-232.r1/logs`.

| State | Command | Exit | Log |
|---|---|---:|---|
| Original implementation, RED | `ctest -R Lv2StateTest --output-on-failure` | 8 | `test-1.log` |
| Fix applied, GREEN | `ctest -R Lv2StateTest --output-on-failure` | 0 | `test-2.log` |
| Fix reversed, RED proof | `ctest -R Lv2StateTest --output-on-failure` | 8 | `test-3.log` |
| Fix restored, GREEN proof | `ctest -R Lv2StateTest --output-on-failure` | 0 | `test-4.log` |

The failing assertion was `Lv2StateTest::rejectsAnEventWhosePaddingDoesNotFit`: the original implementation accepted the capacity-33 write. Reversing the production patch reproduced the same failure.

## Build and gates

| Command | Exit | Log |
|---|---:|---|
| `cmake --build build --target Lv2EvbufTest zene` (initial standalone-target attempt) | 2 | `build-1.log` |
| `cmake --build build --target Lv2StateTest zene` (original code) | 0 | `build-2.log` |
| `cmake --build build --target Lv2StateTest zene` (fix applied) | 0 | `build-3.log` |
| `cmake --build build --target Lv2StateTest zene` (fix reversed) | 0 | `build-4.log` |
| `cmake --build build --target Lv2StateTest zene` (fix restored) | 0 | `build-5.log` |
| `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

The product patch verification commands both exited 0:

- `git diff -- src/core/lv2/Lv2Evbuf.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-232.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-232.r1/logs/fix.patch`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-232.r1/logs/fix.patch`

The initial standalone-target build exited 2 (`No rule to make target 'Lv2EvbufTest'`); that target was removed from the final change and the regression was added to the existing LV2 test executable. No explicit configure command was run. The successful `build-2.log` does show CMake's automatic regeneration after the temporary `tests/CMakeLists.txt` edit; the file was restored and is unchanged in the final diff. The `fff` grep tool was unavailable, so logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/lv2/Lv2Evbuf.cpp`
- `tests/src/core/Lv2StateTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-232.md`
