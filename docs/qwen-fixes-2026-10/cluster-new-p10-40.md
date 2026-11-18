# Cluster new-p10-40: duplicate note IDs in pitch curves

**Verdict: FIXED**

`targetNotes()` in `src/core/ControlCommandsNoteCurves.cpp:104-117` now skips a resolved `Note*` already in its output vector (`:114`) before adding it (`:115`). `setCurve()` uses this vector to capture the before-state, apply the curve, and report the changed-note count (`:153-165`). Duplicate IDs therefore contribute once to each operation.

The regression `NoteCurvesAcrossTest::duplicateNoteIdsCountOnlyOneChangedNote()` in `tests/src/core/NoteCurvesAcrossTest.cpp:148-164` passes the same note ID twice. It checks the reply count, transaction before-state, and resulting curve. The `NoteCurvesAcrossTest` family contains **1 CTest test** (CTest test #178).

The test already appears in `tests/fork-sources.txt:1973`, and the product source appears in `tests/fork-sources.txt:1198`; no source-list edit was needed.

## Regression and proof commands

Builds and actual test runs were redirected to new files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs`. The `fff` grep tool was unavailable in this session; I searched those files with `rg` for `error`, `FAILED`, and `Assertion`.

Discovery command guard: I first ran `ctest -N -R NoteCurvesAcrossTest` from the worktree root; the repository guard rejected it with exit 97 before any tests ran. All actual CTest runs below were run from `build/tests`.

Original implementation, RED (build exit 0; CTest exit 8; actual count 2, expected 1):

```sh
cmake --build build --target NoteCurvesAcrossTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/build-1-red.log 2>&1; build_status=$?; echo EXIT=$build_status
EXIT=0
ctest -R NoteCurvesAcrossTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/test-1-red.log 2>&1; test_status=$?; echo EXIT=$test_status
EXIT=8
```

The final regression against the fix reversed, followed by the same regression after reapplying the fix (both builds exit 0; CTest exits 8 then 0):

```sh
git diff -- src/core/ControlCommandsNoteCurves.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/fix.patch; revert_status=$?; echo EXIT=$revert_status
EXIT=0
cmake --build build --target zene NoteCurvesAcrossTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/build-5-proof-red.log 2>&1; build_status=$?; echo EXIT=$build_status
EXIT=0
ctest -R NoteCurvesAcrossTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/test-5-proof-red.log 2>&1; test_status=$?; echo EXIT=$test_status
EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/fix.patch; apply_status=$?; echo EXIT=$apply_status
EXIT=0
cmake --build build --target zene NoteCurvesAcrossTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/build-6-proof-green.log 2>&1; build_status=$?; echo EXIT=$build_status
EXIT=0
ctest -R NoteCurvesAcrossTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/test-6-proof-green.log 2>&1; test_status=$?; echo EXIT=$test_status
EXIT=0
```

The fixed implementation also passed the affected test after rebuilding both `zene` and the test target:

```sh
cmake --build build --target zene NoteCurvesAcrossTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/build-4-green.log 2>&1; build_status=$?; echo EXIT=$build_status
EXIT=0
ctest -R NoteCurvesAcrossTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/test-4-green.log 2>&1; test_status=$?; echo EXIT=$test_status
EXIT=0
```

Earlier regression-authoring attempts were corrected and are retained in the logs: `build-2-green.log` exited 0 and `test-2-green.log` exited 8 because the test initially read internal `__transaction` data from the public command result; `build-3-green.log` exited 2 because Qt6 `QJsonArray` has no `constLast()`. The final test reads the transaction ledger and uses indexed access.

## Gates

Both required gates passed (exit 0):

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/gate-1-complexity.log 2>&1; gate_status=$?; echo EXIT=$gate_status
EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-40.r1/logs/gate-1-file-length.log 2>&1; gate_status=$?; echo EXIT=$gate_status
EXIT=0
```

## Files committed

- `src/core/ControlCommandsNoteCurves.cpp`
- `tests/src/core/NoteCurvesAcrossTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-40.md`
