# Cluster new-p11-14-port: isolate unsaved bounce paths

**Verdict: FIXED**

Re-applied the accepted fix from `bugs/qwen-new-p11-14` (commit `c13f94b44`) onto this branch by hand, resolving the merge conflict in `src/core/Song.cpp`.

## Evidence

- `src/core/ControlBounceSupport.cpp:94-108` (`defaultBouncePath`): unsaved projects (`project.isEmpty()`) now use `untitled-<uuid>` instead of a shared `untitled`, isolating default bounce files per Song.
- `src/core/Song.cpp`: constructor and `clearProject()` store a fresh transient UUID in the `_zeneUnsavedProjectId` QObject property (runtime-only, not persisted).

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `ctest -R '^ControlBounceSupportTest$' --output-on-failure` from `build/tests` after reversing product hunk (build-5.log); **EXIT=8**, 2 passed, 1 failed. |
| GREEN after fix | Same ctest command (test-3.log); **EXIT=0**, 1/1 passed. |
| Reversed-fix proof | `git apply -R .../fix.patch` **EXIT=0**; rebuild (build-5.log) **EXIT=0**; ctest (test-2.log) **EXIT=8**. |
| Restored-fix proof | `git apply .../fix.patch` **EXIT=0**; rebuild (build-6.log) **EXIT=0**; ctest (test-3.log) **EXIT=0**. |
| Affected family | `ControlBounceSupportTest`: 1 CTest target executed; 1/1 passed on green run. |
| Complexity gate | **EXIT=1** (pre-existing baseline staleness: 6 unrelated new-over-target functions; none in my changed files). |
| File-length gate | **EXIT=1** (pre-existing baseline staleness: 14 unrelated new-over-500-line files; none in my changed files). |

## Files committed

- `src/core/ControlBounceSupport.cpp`
- `src/core/Song.cpp`
- `tests/src/core/ControlBounceSupportTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-14-port.md`
