# Cluster new-p8-295: form fields preserve meaningful whitespace

**Verdict: FIXED**

`src/gui/CommandFormFields.cpp:352-360` now preserves a line edit's exact text when its schema type is `string`, which includes paths. Other line edits still trim before typed parsing, and combo and key-sequence handling is unchanged. `readValue()` passes the schema to `editorText()` at line 459.

The regression checks are in `tests/src/gui/CommandFormTest.cpp:197-225`. Both `stringFieldsPreserveEdgeWhitespace` and `pathFieldsPreserveEdgeWhitespace` failed against the original implementation, passed with the fix, failed again when only the production patch was reverted, and passed after it was restored.

## Evidence

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED, original code | `ctest -R CommandFormTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 | Both new whitespace checks failed; the other 16 QtTest results passed. |
| GREEN, fix applied | `ctest -R CommandFormTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Passed. |
| Revert proof, fix removed | `ctest -R CommandFormTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 | Both whitespace checks failed again; the other 16 QtTest results passed. |
| Revert proof, fix restored | `ctest -R CommandFormTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Passed. |
| Affected test family | `ctest -R CommandFormTest` from `build/tests` | — | One CTest target. |

## Build and ratchet commands

Each build command rebuilt both `zene` and `CommandFormTest` from the worktree. Build output is in the named log file.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Before fix | `cmake --build build --target zene CommandFormTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Fix applied | `cmake --build build --target zene CommandFormTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Fix reverted | `cmake --build build --target zene CommandFormTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Fix restored | `cmake --build build --target zene CommandFormTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |

The revert proof used these commands, each with exit 0:

```sh
git diff -- src/gui/CommandFormFields.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-295.r1/logs/fix.patch
```

## Files committed

- `src/gui/CommandFormFields.cpp`
- `tests/src/gui/CommandFormTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-295.md`
