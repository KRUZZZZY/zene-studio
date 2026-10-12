# Cluster new-p10-274

Verdict: **FIXED**.

## Evidence

`NamModel::processChunk()` writes only `finalHead(0, i)` to the mono output at
[`NamModel.cpp:274`](../../plugins/NeuralAmp/nam/NamModel.cpp:274). The loader now
rejects a final array whose `head_size` is not one at
[`NamModelLoader.cpp:282`](../../plugins/NeuralAmp/nam/NamModelLoader.cpp:282).
The regression loads a valid one-channel fixture, then checks that a two-channel
final head is rejected with the mono-output reason in
[`NamModelMonoOutputTest.cpp:47`](../../plugins/NeuralAmp/tests/NamModelMonoOutputTest.cpp:47).
CTest registration is at [`tests/CMakeLists.txt:1156`](../../tests/CMakeLists.txt:1156).

The test failed before the production change because `head_size: 2` loaded
successfully. It passed after the change. Reversing only the production patch
made the same test fail again; restoring the patch made it pass.

## Verification evidence

| Check | Result |
| --- | --- |
| Initial regression (RED) | `ctest -R NamModelMonoOutputTest --output-on-failure`, exit 8; 1/1 failed because final `head_size 2` was accepted. |
| Regression after fix (GREEN) | Same CTest command, exit 0; 1/1 passed. |
| Patch reversal proof | Same test after `git apply -R`, exit 8; 1/1 failed on the same assertion. |
| Restored patch proof | Same test after `git apply`, exit 0; 1/1 passed. |
| Affected-family count | 1 test executed in each CTest run. |
| Complexity gate | `bash tests/complexity-gate.sh --check`, exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check`, exit 0. |

The exact CTest invocations and results were:

```text
ctest -R NamModelMonoOutputTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result
EXIT=8

ctest -R NamModelMonoOutputTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

ctest -R NamModelMonoOutputTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result
EXIT=8

ctest -R NamModelMonoOutputTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/test-6.log 2>&1; result=$?; echo EXIT=$result
EXIT=0
```

The production patch reversal and restoration both exited zero:

```text
git diff -- plugins/NeuralAmp/nam/NamModelLoader.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/fix.patch
EXIT=0

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/fix.patch
EXIT=0
```

## Build record

The configured build tree had no generated rule for the new target. Its normal
target build failed, and I did not invoke a configure command.
The test was compiled directly from its source and the NAM engine sources, then
run through CTest from `build/tests`. The source CMake registration is committed
for the next configured build. For this run, the generated `build/tests`
`CTestTestfile.cmake` received a temporary `add_test` entry.

```text
cmake --build build --target NamModelMonoOutputTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result
EXIT=2 (No rule to make target 'NamModelMonoOutputTest')

g++ -std=c++20 -O2 -I plugins/NeuralAmp/rtneural/modules/Eigen -I plugins/NeuralAmp/rtneural/modules/json plugins/NeuralAmp/tests/NamModelMonoOutputTest.cpp plugins/NeuralAmp/nam/NamModel.cpp plugins/NeuralAmp/nam/NamModelLoader.cpp -ldl -lm -o build/tests/NamModelMonoOutputTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result
EXIT=0 (the same compile command also exited 0 with logs build-5.log, build-7.log and build-9.log)

cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result
EXIT=0 (the same command also exited 0 with logs build-6.log and build-8.log)

bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-274.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result
EXIT=0
```

The log-search tool `fff grep` was not available in this session; I searched the
specified log terms with `rg` and did not read whole logs. An initial
`ctest -N -R 'NeuralAmp|Nam'` inventory probe was mistakenly run from the
worktree root and exited 97; it ran no tests. All four actual CTest runs above
were run from `build/tests`.

## Files committed

- `plugins/NeuralAmp/nam/NamModelLoader.cpp`
- `plugins/NeuralAmp/tests/NamModelMonoOutputTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-274.md`
