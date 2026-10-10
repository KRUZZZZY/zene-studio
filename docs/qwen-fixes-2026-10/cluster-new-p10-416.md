# Cluster new-p10-416

**Verdict: FIXED**

`TempoSyncKnobModel::loadSettings()` now checks the serialized integer against the inclusive `None`–`Custom` range before casting. Negative and too-large values fall back to `SyncMode::None`. The valid `Custom` boundary remains accepted.

## Evidence

- [src/core/TempoSyncKnobModel.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-416/src/core/TempoSyncKnobModel.cpp:145): range validation and fallback before `setSyncMode()`.
- [tests/src/core/TempoMapPersistenceTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-416/tests/src/core/TempoMapPersistenceTest.cpp:162): XML load regression for `-1` and `999`, plus a valid `Custom` check.

## Test evidence

Affected-family count: `ctest -R TempoMapPersistenceTest` runs 1 CTest test. Each passing run reported 1/1 passed. Before the fix, both invalid-mode data rows failed with `syncMode() == SyncMode::None` false; the valid `Custom` check passed.

| Run | Exact command | Exit |
| --- | --- | ---: |
| RED, original code | `ctest -R TempoMapPersistenceTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| GREEN, fixed code | `ctest -R TempoMapPersistenceTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Reverse proof, fix removed | `ctest -R TempoMapPersistenceTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Reverse proof, fix restored | `ctest -R TempoMapPersistenceTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |

The reversal was done with the required product-only patch and restored afterwards:

```sh
git diff -- src/core/TempoSyncKnobModel.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/fix.patch
EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/fix.patch
EXIT=0
```

## Builds and gates

| Exact command | Exit |
| --- | ---: |
| `cmake --build build --target TempoSyncKnobModelTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 2 |
| `cmake --build build --target TempoMapPersistenceTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target zene TempoMapPersistenceTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target zene TempoMapPersistenceTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target zene TempoMapPersistenceTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-416.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

The first build attempted a new standalone test target, which was absent from the preconfigured build (`No rule to make target 'TempoSyncKnobModelTest'`). To honor the no-configure instruction, the regression was placed in the existing `TempoMapPersistenceTest` target; no CMake changes were needed.

The requested `fff` grep tool was not available in this session (`command -v fff` returned no path and no such tool was exposed). I searched each build, test, and gate log for `error`, `FAILED`, and `Assertion` with targeted `rg` queries instead of reading the logs wholesale.

## Files in the commit

- `src/core/TempoSyncKnobModel.cpp`
- `tests/src/core/TempoMapPersistenceTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-416.md`
