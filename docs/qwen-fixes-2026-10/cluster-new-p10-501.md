# Cluster new-p10-501: Ogg initialization failure

**Verdict: FIXED**

## Evidence

- [include/AudioFileOgg.h:68](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-501/include/AudioFileOgg.h:68) tracks initialization of the info, DSP, comment, block, and Ogg stream structs.
- [src/core/audio/AudioFileOgg.cpp:51](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-501/src/core/audio/AudioFileOgg.cpp:51) returns when Vorbis rejects encoder setup. Later init calls record successful stages; [src/core/audio/AudioFileOgg.cpp:98](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-501/src/core/audio/AudioFileOgg.cpp:98) flushes only a complete writer and clears only initialized structs.
- [tests/src/core/AudioFileOggInitTest.cpp:55](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-501/tests/src/core/AudioFileOggInitTest.cpp:55) requests zero channels to force encoder rejection and poisons the untouched codec storage. On the original code, the destructor segfaulted; with the fix it passes.
- [tests/CMakeLists.txt:872](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-501/tests/CMakeLists.txt:872) registers the regression only when optional Ogg/Vorbis support is enabled.

## RED/GREEN and affected family

| Run | Exact ctest command | Exit | Evidence |
| --- | --- | ---: | --- |
| RED, original code | `ctest -R AudioFileOggInitTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/test-1.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status" \| tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/test-1.log; exit "$test_status"` | 8 | 1/1 test executed; SIGSEGV in `failedEncoderInitializationCanBeDestroyed()` |
| GREEN, fixed code | `ctest -R AudioFileOggInitTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/test-2.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status" \| tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/test-2.log; exit "$test_status"` | 0 | 1/1 passed |
| RED, production patch reversed | `ctest -R AudioFileOggInitTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/test-3.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status" \| tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/test-3.log; exit "$test_status"` | 8 | Rebuilt without the fix; 1/1 test segfaulted again |
| GREEN, patch restored | `ctest -R AudioFileOggInitTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/test-4.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status" \| tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/test-4.log; exit "$test_status"` | 0 | Rebuilt with the fix; 1/1 passed |

Affected export family: `ctest -R 'AudioFileOggInitTest|ExportDitherTest|ExportWavDitherTest|ExportNoiseShaperTest' --output-on-failure` — exit 0, **4/4 tests passed** in both `test-5.log` and the final registration run `test-6.log` (run from `build/tests`).

## Build and gates

The build command was `cmake --build build --target zene AudioFileOggInitTest`. Its first invocation (log `build-1.log`) exited 2 because CMake had just refreshed the generated Makefiles and that Make process still lacked the newly added target. The same command then exited 0 in `build-2.log`, `build-3.log`, `build-4.log`, and `build-5.log`. The affected-family build, `cmake --build build --target zene AudioFileOggInitTest ExportDitherTest ExportWavDitherTest ExportNoiseShaperTest`, exited 0 in both `build-6.log` and the final registration run `build-7.log`.

- `bash tests/complexity-gate.sh --check` — exit 0 (`gate-3.log`).
- `bash tests/file-length-gate.sh --check` — exit 0 (`gate-4.log`).
- `git diff --check` — exit 0.

The reverse-patch proof used `git diff -- include/AudioFileOgg.h src/core/audio/AudioFileOgg.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/fix.patch` (exit 0), then restored it with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-501.r1/logs/fix.patch` (exit 0).

## Files in the cluster commit

- `include/AudioFileOgg.h`
- `src/core/audio/AudioFileOgg.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `tests/src/core/AudioFileOggInitTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-501.md`
