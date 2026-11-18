# Cluster new-p10-218

Verdict: **FIXED** — numeric list sorting now captures an optional `e`/`E` exponent in the leading number before converting and comparing it.

## Evidence

- `src/gui/ListTable.cpp:46` — the numeric-prefix regular expression accepts exponent notation.
- `tests/src/gui/CommandFormTest.cpp:442-451` — `sortsScientificNotationByNumericValue` sorts `1e3 ms` after `20 ms`.
- `tests/upstream-modifications.txt:1054` — records the inherited test-file change.
- The affected ctest family is `CommandFormTest`: 1 ctest executed; the final run passed (1/1).

## Regression and verification commands

All build commands ran from the worktree root. All ctest commands ran from `build/tests`. Exit codes below are the values printed by each command's `echo EXIT=$?`.

| Stage | Exact command | Exit |
|---|---|---:|
| Baseline build | `cmake --build build --target CommandFormTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| Baseline RED | `ctest -R '^CommandFormTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Fixed build | `cmake --build build --target zene CommandFormTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Fixed GREEN | `ctest -R '^CommandFormTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Reverse production patch | `git diff -- src/gui/ListTable.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reversed build | `cmake --build build --target zene CommandFormTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Reversed RED | `ctest -R '^CommandFormTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Final build | `cmake --build build --target zene CommandFormTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Final GREEN | `ctest -R '^CommandFormTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-218.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

The RED runs failed at the regression assertion with actual `1e3 ms` and expected `20 ms`. Reversing the product patch reproduced that same failure after rebuilding `zene` and `CommandFormTest`; applying it restored the pass. The fff grep tool was not available in this session, so each build, test, and gate log was searched with targeted `rg` calls for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/gui/ListTable.cpp`
- `tests/src/gui/CommandFormTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-218.md`
