# Cluster new-p11-15: duplicate project alternatives

**Verdict: FIXED**

`src/core/ControlCommandsAlternatives.cpp:103` loaded every valid child into `state.names`, including repeated names. The loader now appends a plain name only when it is not already present. Because the 64-entry guard uses the number of stored names, repeated entries no longer consume capacity.

The regression in `tests/src/core/ProjectToolsTest.cpp:238` writes a project with 64 `mix` entries followed by `radio`, opens it through `project.open`, and checks that `project.alternatives` reports exactly `mix` and `radio`.

## Evidence

Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs`. CTest was run from `build/tests`.

| Evidence | Exact command and working directory | Result / exit |
|---|---|---|
| First-draft regression build | From the worktree: `cmake --build build --target ProjectToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0` |
| First-draft baseline run, superseded after moving the regression to the final slot | From `build/tests`: `ctest -R '^ProjectToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=8`; QtTest totals: 9 passed, 2 failed. The intended assertion saw 64 rows; the later alternatives test also failed because the first failure left the loaded state full. |
| Initial baseline RED; `build-2.log` was the test-target build | From the worktree: `cmake --build build --target ProjectToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0` |
| Initial baseline RED; isolated regression | From `build/tests`: `ctest -R '^ProjectToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=8`; assertion reported 64 rows, expected 2. QtTest totals: 10 passed, 1 failed. |
| Fixed GREEN | From the worktree: `cmake --build build --target zene ProjectToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0` |
| Fixed GREEN | From `build/tests`: `ctest -R '^ProjectToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0`; CTest ran 1/1 test. |
| Removal proof: reverse only the production fix | From the worktree: `git diff -- src/core/ControlCommandsAlternatives.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/fix.patch; status=$?; echo EXIT=$status; exit $status` | `EXIT=0` |
| Removal proof build | From the worktree: `cmake --build build --target zene ProjectToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0` |
| Removal proof RED | From `build/tests`: `ctest -R '^ProjectToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=8`; the same assertion reported 64 rows, expected 2. |
| Restore production fix | From the worktree: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/fix.patch; status=$?; echo EXIT=$status; exit $status` | `EXIT=0` |
| Final build | From the worktree: `cmake --build build --target zene ProjectToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/build-5.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0` |
| Final GREEN | From `build/tests`: `ctest -R '^ProjectToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/test-5.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0`; CTest ran 1/1 test. |
| Complexity gate | From the worktree: `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0` |
| File-length gate | From the worktree: `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-15.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0` |

The affected CTest family contains one test, `ProjectToolsTest`; every green run executed 1/1. The baseline QtTest run executed 11 cases (including init and cleanup), with only the regression failing after it was moved to the final slot. The earlier placement was discarded because its failed assertion left shared alternatives state for a later slot.

## Files committed

- `src/core/ControlCommandsAlternatives.cpp`
- `tests/src/core/ProjectToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-15.md`

The production and test files are already registered in `tests/fork-sources.txt`; no source-registration changes were needed.
