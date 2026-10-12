# Cluster new-p10-344: very long decoded files can become silent

**Verdict: FIXED**

`decodeSampleSF()` used an `int` loop bound made by converting `result.size()` to `int`. For a decoded buffer with more than `INT_MAX` frames, the bound can become negative and leave the allocated output unfilled. It now visits frames with `std::size_t`, and calculates interleaved channel offsets with `std::size_t` as well. The changed inherited decoder is recorded in `tests/upstream-modifications.txt`; the new internal helper is listed in `tests/fork-sources.txt`.

## File and regression evidence

- [SampleDecoder.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-344/src/core/SampleDecoder.cpp:79) uses the shared frame iterator for the decoded output. Its interleaved channel offset is calculated at line 90.
- [SampleDecoderFrameLoop.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-344/src/core/SampleDecoderFrameLoop.h:22) iterates with `std::size_t` and accepts an early-stopping visitor.
- [ImportDetectionTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-344/tests/src/core/ImportDetectionTest.cpp:190) checks a count of `INT_MAX + 1`. It stops after the first visit, so the test needs no multi-gigabyte audio fixture.
- Before the fix, the regression failed at `QVERIFY(visited)`: the original narrow-bound iteration did not call the visitor. The same test passed after the decoder began using the fixed shared iterator.

The affected CTest family is `ImportDetectionTest`: one registered CTest target ran. The initial RED run had one failed Qt assertion; the green run passed the target.

## Commands and results

Build commands ran from the worktree root. CTest commands ran from `build/tests`. The `EXIT` value is the captured status of the command before `echo`.

| Run | Exact command | EXIT |
|---|---|---:|
| Initial RED build | `cmake --build build --target zene ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/build-1.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| Initial RED test | `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/test-1.log 2>&1; code=$?; echo EXIT=$code` | 8 |
| Initial GREEN build | `cmake --build build --target zene ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/build-2.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| Initial GREEN test | `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/test-2.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| Reverse fix patch | `git diff -- src/core/SampleDecoder.cpp src/core/SampleDecoderFrameLoop.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/fix.patch; code=$?; echo EXIT=$code` | 0 |
| Reversed build | `cmake --build build --target zene ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/build-3.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| Reversed test | `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/test-3.log 2>&1; code=$?; echo EXIT=$code` | 8 |
| Restore fix patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/fix.patch; code=$?; echo EXIT=$code` | 0 |
| Restored build | `cmake --build build --target zene ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/build-4.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| Restored GREEN test | `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/test-4.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/gate-1.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-344.r1/logs/gate-2.log 2>&1; code=$?; echo EXIT=$code` | 0 |

Log searches used `rg` because no `fff` tool or executable was available in this session.

## Files committed

- `src/core/SampleDecoder.cpp`
- `src/core/SampleDecoderFrameLoop.h`
- `tests/src/core/ImportDetectionTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-344.md`
