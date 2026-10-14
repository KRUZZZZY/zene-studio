# Cluster new-p10-7

**Verdict: FIXED**

Plain-list fields accepted JSON arrays without validating their item schema, and the comma-separated parser checked scalar type but skipped item bounds. The form now checks each parsed item against its declared type and numeric range, then checks `minItems` and `maxItems` for both input syntaxes before returning a value.

## Evidence

- [CommandFormFields.cpp:321](../../src/gui/CommandFormFields.cpp#L321) checks string, number, and integer item types; integer items must be whole numbers.
- [CommandFormFields.cpp:328](../../src/gui/CommandFormFields.cpp#L328) checks array length and each item's declared numeric range.
- [CommandFormFields.cpp:343](../../src/gui/CommandFormFields.cpp#L343) runs the same schema checks after either JSON or comma-separated parsing.
- [CommandFormTest.cpp:345](../../tests/src/gui/CommandFormTest.cpp#L345) adds direct regressions for item range, JSON integer type, and array length constraints.

The affected CTest family contained one registered test (`CommandFormTest`). Its red run reported 16 passed and 3 failed QtTest cases, including all three new regression slots. After the fix, CTest reported 1/1 passed.

## Commands and results

Build and test commands were run with output redirected to the named log and the exit status captured with `set +e; <command> > <log> 2>&1; rc=$?; echo EXIT=$rc; exit $rc`. CTest ran from `build/tests`; builds and gates ran from the worktree root.

| Stage | Command | EXIT | Evidence |
|---|---|---:|---|
| New target probe | `cmake --build build --target CommandFormFieldsTest` | 2 | `build-1.log`: no generated target in the configured tree; regression was placed in the existing target. |
| Target listing | `cmake --build build --target help` | 0 | `build-2.log` |
| Original-code test build | `cmake --build build --target CommandFormTest` | 0 | `build-3.log` |
| RED | `ctest -R CommandFormTest --output-on-failure` | 8 | `test-1.log`: 1/1 CTest failed; 3 regression slots failed. |
| Fixed build | `cmake --build build --target zene CommandFormTest` | 0 | `build-4.log` |
| GREEN | `ctest -R CommandFormTest --output-on-failure` | 0 | `test-2.log`: 1/1 passed. |
| Remove production patch | `git diff -- src/gui/CommandFormFields.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-7.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-7.r1/logs/fix.patch` | 0 | Reversal proof. |
| Reversed build | `cmake --build build --target zene CommandFormTest` | 0 | `build-5.log` |
| Reversal RED | `ctest -R CommandFormTest --output-on-failure` | 8 | `test-3.log`: the same 3 regression slots failed. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-7.r1/logs/fix.patch` | 0 | Reversal proof. |
| Final build | `cmake --build build --target zene CommandFormTest` | 0 | `build-6.log` |
| Final GREEN | `ctest -R CommandFormTest --output-on-failure` | 0 | `test-4.log`: 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log`: PASS. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log`: PASS. |

All numbered logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-7.r1/logs`.

## Files committed

- `src/gui/CommandFormFields.cpp`
- `tests/src/gui/CommandFormTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-7.md`
