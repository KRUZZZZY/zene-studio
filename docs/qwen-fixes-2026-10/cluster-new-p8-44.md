# Cluster new-p8-44

Verdict: **FIXED**

`partsOf()` accepted an explicitly empty `parts` array and built a `Parts` value with all fields false. `recallSnapshot()` then recorded an undo action and marked the song modified despite applying no mixer changes. `partsOf()` now rejects the empty selection with `InvalidArgs` before recall can reach either side effect.

## File evidence

- [src/core/ControlCommandsMixerSnapshots.cpp:70](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-44/src/core/ControlCommandsMixerSnapshots.cpp:70): rejects an empty selection.
- [src/core/ControlCommandsMixerSnapshots.cpp:126](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-44/src/core/ControlCommandsMixerSnapshots.cpp:126): recall path; the undo action and `setModified()` follow the validated `partsOf()` call.
- [tests/src/core/ProjectStateTest.cpp:118](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-44/tests/src/core/ProjectStateTest.cpp:118): regression checks `InvalidArgs`, unchanged undo depth, and unchanged modified state.

## Evidence

| Check | Command | Exit | Result |
|---|---|---:|---|
| RED on original code | `ctest -R ProjectStateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-3.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-3.log; exit "$code"` | 8 | One regression failure: empty parts returned success (`8 passed, 1 failed` QtTest slots). |
| GREEN with fix restored | `ctest -R ProjectStateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-6.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-6.log; exit "$code"` | 0 | `1/1` affected CTest target passed; all 9 QtTest slots ran. |
| 4b, product patch reversed | `ctest -R ProjectStateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-5.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-5.log; exit "$code"` | 8 | Reproduced the same regression failure after rebuilding without the fix (`8 passed, 1 failed`). |
| Affected-family count | `ctest -R ProjectStateTest --output-on-failure` from `build/tests` | 0 on final run | One CTest target executed and passed (`ProjectStateTest`; 9 QtTest slots). |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/gate-1.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/gate-1.log; exit "$code"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/gate-2.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/gate-2.log; exit "$code"` | 0 | Passed. |

## Build and test command record

Builds and tests were run in order. Every build exited 0. The first test attempt exposed a fixture-cleanup issue when QtTest stopped at the first failed assertion; cleanup was moved before assertions. The isolated RED runs below then failed only on the empty-parts regression.

```text
cmake --build build --target ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-1.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-1.log; exit "$code"  EXIT=0
ctest -R ProjectStateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-1.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-1.log; exit "$code"  EXIT=8 (initial fixture cleanup issue)
cmake --build build --target ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-2.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-2.log; exit "$code"  EXIT=0
ctest -R ProjectStateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-2.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-2.log; exit "$code"  EXIT=8 (isolated RED)
cmake --build build --target zene ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-3.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-3.log; exit "$code"  EXIT=0
ctest -R ProjectStateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-3.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-3.log; exit "$code"  EXIT=8 (RED after rebuilding zene)
cmake --build build --target zene ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-4.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-4.log; exit "$code"  EXIT=0
ctest -R ProjectStateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-4.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-4.log; exit "$code"  EXIT=0 (GREEN)
git diff -- src/core/ControlCommandsMixerSnapshots.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/fix.patch  EXIT=0
cmake --build build --target zene ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-5.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-5.log; exit "$code"  EXIT=0
ctest -R ProjectStateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-5.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-5.log; exit "$code"  EXIT=8 (4b RED)
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/fix.patch  EXIT=0
cmake --build build --target zene ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-6.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/build-6.log; exit "$code"  EXIT=0
ctest -R ProjectStateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-6.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/test-6.log; exit "$code"  EXIT=0
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/gate-1.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/gate-1.log; exit "$code"  EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/gate-2.log 2>&1; code=$?; printf 'EXIT=%s\n' "$code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-44.r1/logs/gate-2.log; exit "$code"  EXIT=0
```

`fff` was not available as a tool or executable in this environment, so the build and test logs were searched with `rg` for the requested failure terms and exit markers.

## Files included in the cluster commit

- `src/core/ControlCommandsMixerSnapshots.cpp`
- `tests/src/core/ProjectStateTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-44.md`

The product source and existing test file are already registered in `tests/fork-sources.txt`; no new product source file was added.
