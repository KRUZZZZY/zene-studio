# Cluster new-p8-53: generated clip end range

**Verdict: FIXED**

`sample.generate` bounded `position` and `length` independently, so an in-range pair could create a clip extending past `MaxSongLength`. The command now checks their sum in `qint64` and returns `InvalidArgs` when the end is greater than the limit. The check runs before the journal checkpoint and clip creation. An end exactly at the limit remains valid.

## Evidence

- [ControlCommandsSample.cpp](../../src/core/ControlCommandsSample.cpp#L175): `validateGenerateEnd` computes the wide sum and refuses an end past the limit; [the handler calls it before checkpointing](../../src/core/ControlCommandsSample.cpp#L239).
- [ControlSampleOperatorTest.cpp](../../tests/src/core/ControlSampleOperatorTest.cpp#L498): the regression sends `position = MaxSongLength - 10`, `length = 11` and expects `InvalidArgs`, then confirms `position = MaxSongLength - 1`, `length = 1` creates a clip ending exactly at the limit.
- The first RED run and the fix-removal RED run both failed at `pastLimit.ok`: actual `1`, expected `0`. Each CTest invocation ran one registered test; the QtTest output on RED showed 10 passed and 1 failed. The GREEN runs passed that CTest test.

## Build commands

All build commands exited 0. Their output is in the numbered logs shown.

| State | Exact command | Exit |
|---|---|---:|
| Original tree | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| Original tree | `cmake --build build --target ControlSampleOperatorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Fix applied | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Fix applied | `cmake --build build --target ControlSampleOperatorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Fix removed | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Fix removed | `cmake --build build --target ControlSampleOperatorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| Fix restored | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/build-7.log 2>&1; echo EXIT=$?` | 0 |
| Fix restored | `cmake --build build --target ControlSampleOperatorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/build-8.log 2>&1; echo EXIT=$?` | 0 |

## Regression and fix-removal proof

| Proof | Exact command | Exit | Result |
|---|---|---:|---|
| RED on original code | `ctest -R ControlSampleOperatorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | `pastLimit.ok` was true; 10 QtTest cases passed and the regression failed. |
| GREEN with fix | `ctest -R ControlSampleOperatorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | CTest reported 1/1 passed. |
| Save and remove only the production fix | `git diff -- src/core/ControlCommandsSample.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/fix.patch; echo EXIT=$?` | 0 | The production change was reversed for the proof. |
| RED with fix removed | `ctest -R ControlSampleOperatorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | The same `pastLimit.ok` assertion failed; 10 QtTest cases passed and the regression failed. |
| Restore the production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/fix.patch; echo EXIT=$?` | 0 | The production change was restored. |
| Final GREEN | `ctest -R ControlSampleOperatorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | CTest reported 1/1 passed. |

## Gates

| Gate command | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-53.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

## Files committed

- `src/core/ControlCommandsSample.cpp`
- `tests/src/core/ControlSampleOperatorTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-53.md`

The requested `fff grep` tool was not available in the session, so the numbered logs were searched with targeted `rg` queries.
