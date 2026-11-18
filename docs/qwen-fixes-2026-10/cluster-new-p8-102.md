# Cluster new-p8-102: individually locked clips and track-level edits

**Verdict: FIXED**

`EditLocks::refuses()` already checked individually locked clips for `track.remove`, but the shared track-level clip check did not cover `track.flatten` or `project.subproject_refresh`. The latter repoints every `SampleClip` on the linked track. The preflight now checks all clips removed by `track.remove` and `track.flatten`, and sample clips repointed by `project.subproject_refresh` ([EditLocks.cpp](../../src/core/EditLocks.cpp#L129), [dispatch](../../src/core/EditLocks.cpp#L208)). It leaves unrelated clip types out of the refresh check because that handler does not repoint them.

Regressions in [ProjectToolsTest.cpp](../../tests/src/core/ProjectToolsTest.cpp#L239) verify locked clips produce a lock-specific refusal for both commands. Unlocked clips pass the lock preflight; the refresh test also verifies the handler returns its ordinary `NotFound` result for a track that is not linked as a subproject. The test process cannot complete an unlocked flatten render: `BounceInPlace` launches the current executable as the CLI renderer, and in this test that executable is `ProjectToolsTest`. The lock-specific unlocked behavior is therefore verified at the preflight boundary. The existing [track-removal regression](../../tests/src/core/ProjectStateTest.cpp#L216) still passes.

## Verification evidence

Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs`. All ctest commands ran from `build/tests`.

| Stage | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Reverse final production diff | `git diff -- src/core/EditLocks.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/fix.patch` | 0 | Final production change removed for red proof |
| Reversed build | `cmake --build build --target zene ProjectToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/build-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Rebuilt `zene` and `ProjectToolsTest` without the fix |
| RED regression | `ctest -R '^ProjectToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/test-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 | Both locked-operation regressions failed; 12 passed, 2 failed |
| Restore final production diff | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/fix.patch` | 0 | Fix restored |
| Restored build | `cmake --build build --target zene ProjectToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/build-8.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Rebuilt `zene` and `ProjectToolsTest` with the fix |
| GREEN regression | `ctest -R '^ProjectToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/test-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Same regression family passed |
| Final affected build | `cmake --build build --target zene ProjectToolsTest ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/build-9.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Rebuilt both affected test targets and `zene` |
| Final affected tests | `ctest -R '^(ProjectToolsTest|ProjectStateTest)$' --verbose --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/test-9.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | 2/2 ctest targets passed; 23 QtTest cases passed (14 + 9) |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/gate-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Passed |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r2/logs/gate-8.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Passed |

The `fff grep` tool was not available in this session (it was absent from callable tools and `command -v fff` returned no path). I searched build, test, and gate logs with `rg -n`, one term at a time (`error`, `FAILED`, and `Assertion`).

## Files committed

- `src/core/EditLocks.cpp`
- `tests/src/core/ProjectToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-102.md`
