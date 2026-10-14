# Cluster new-p9-62: malformed shortcut imports

**Verdict: FIXED.** The finding was real. `importedOverrides()` accepted a matching format without validating `version` or requiring an object-valued `overrides`. `toObject()` then returned an empty object for a missing or wrongly typed field, and `replace:true` could erase the saved bindings.

## Evidence

- [src/gui/ControlCommandsShortcuts.cpp:169](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-62/src/gui/ControlCommandsShortcuts.cpp:169) validates that `version` is a positive integer, rejects versions newer than this build, and checks `overrides.isObject()` before returning it.
- [tests/control-customisation.py:80](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-62/tests/control-customisation.py:80) exercises missing and non-object `overrides`, plus missing, string, and fractional versions. Each import uses `replace:true` and asserts `invalid_args` while the existing binding remains set.
- The final negative-control run reversed only the production patch. All five malformed-file assertions failed; restoring the patch made the same ctest pass.

## Test evidence

Each ctest invocation ran from `build/tests`. The affected family contains one ctest, `ControlCustomisation`.

| Run | Exact command | EXIT | Result |
|---|---|---:|---|
| Original code, regression RED | `ctest -R '^ControlCustomisation$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | 1/1 failed; four malformed-file checks failed |
| Final patch reversed, RED proof | `ctest -R '^ControlCustomisation$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 8 | 1/1 failed; all five malformed-file checks failed |
| Final patch restored, GREEN proof | `ctest -R '^ControlCustomisation$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/test-7.log 2>&1; echo EXIT=$?` | 0 | 1/1 passed |
| Affected-family count | `ctest -R '^ControlCustomisation$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/test-7.log 2>&1; echo EXIT=$?` | 0 | 1 test executed |

The same ctest command and exit codes for the intermediate iterations are recorded here:

| Log | Exact command | EXIT |
|---|---|---:|
| test-2.log | `ctest -R '^ControlCustomisation$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| test-3.log | `ctest -R '^ControlCustomisation$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| test-4.log | `ctest -R '^ControlCustomisation$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| test-5.log | `ctest -R '^ControlCustomisation$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0 |

## Builds, patch proof, and gates

All seven logged `cmake --build build --target zene` invocations exited 0. Their exact commands were:

| Log | Exact command | EXIT |
|---|---|---:|
| build-1.log | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| build-2.log | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| build-3.log | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| build-4.log | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| build-5.log | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| build-6.log | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| build-7.log | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/build-7.log 2>&1; echo EXIT=$?` | 0 |

Final patch reversal and restoration, both EXIT 0:

```sh
git diff -- src/gui/ControlCommandsShortcuts.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/fix.patch; echo EXIT=$?
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/fix.patch; echo EXIT=$?
```

| Gate | Exact command | EXIT |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-62.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |
| Diff whitespace | `git diff --check; echo EXIT=$?` | 0 |

The `fff grep` tool was not exposed in this run, so the requested `error`, `FAILED`, and `Assertion` log searches used `rg -n` against the specified log folder instead.

## Files committed

- `src/gui/ControlCommandsShortcuts.cpp`
- `tests/control-customisation.py`
- `docs/qwen-fixes-2026-10/cluster-new-p9-62.md`
