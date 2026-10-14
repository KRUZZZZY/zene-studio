# Cluster new-p12-338: double-filter history reset

**Verdict: FIXED**

## Evidence

`include/BasicFilters.h:299-333` shows `clearHistory()` resetting the parent filter state. It now also calls `m_subFilter->clearHistory()` when the second stage exists. The regression at `tests/src/plugins/PluginPortsMigrationTest.cpp:221-230` feeds an impulse through a `DoubleLowPass`, clears history, then asserts that a zero input produces exact silence.

The regression failed before the change and passed after it. Removing only the product patch reproduced the same failure, proving the assertion catches the bug. The affected CTest family is `PluginPortsMigrationTest`: one registered CTest test per run (CTest test #313). In RED runs its QtTest summary was 8 passed and 1 failed; the failure was `doubleFilterClearHistoryClearsSubfilter` with “Compared floats are not the same (fuzzy compare)”. Both GREEN runs passed 1/1 CTest tests.

## Validation record

Build commands ran from the worktree root. CTest commands ran from `build/tests`. Every build and test log is a new file under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs`.

| Phase | Exact command | EXIT |
|---|---|---:|
| Test-only build before RED | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| Original-code RED | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Attempted unrestricted build (guard refused; named targets are required) | `cmake --build build > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 97 |
| Build with fix | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Save and remove only the product patch | `git diff -- include/BasicFilters.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild with fix removed | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Reproduced RED without fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild after restore | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after restore | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-338.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |
| Whitespace check | `git diff --check` | 0 |

The `fff grep` tool was not exposed in the available tool set; log searches used `rg` for `error`, `FAILED`, and `Assertion` as separate terms.

## Files in the cluster commit

- `include/BasicFilters.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-338.md`
