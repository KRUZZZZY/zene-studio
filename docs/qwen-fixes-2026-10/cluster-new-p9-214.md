# Cluster new-p9-214: FIXED

## Finding and change

`LadspaEffect::changeSampleRate()` previously assumed `m_controls` existed both before saving state and after re-instantiating the LADSPA plugin. Failed instantiation leaves the pointer null. The handler now returns before saving when controls are absent; if a later instantiation fails, it notifies the old controls with `nullptr`, deletes them, and returns before restoring state.

Evidence in the source:

- `plugins/LadspaEffect/LadspaEffect.cpp:107` is the handler; the initial null guard is at line 109, and the failed re-instantiation guard is at line 125.
- `plugins/LadspaEffect/LadspaEffect.cpp:505` calls LADSPA `instantiate`; a null result sets the effect to failed at line 519. Controls are created only after successful setup at line 558.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:288` adds a regression that makes LADSPA instantiation fail through the manager descriptor callback. It exercises both construction failure followed by `sampleRateChanged()` and re-instantiation failure during that signal.

The configured CTest inventory has no `LadspaEffect` or `LadspaControls` test name (`ctest -N` returned 0 and listed 402 tests), so the regression runs in the existing `PluginPortsMigrationTest` family.

## Validation evidence

Commands below use the worktree root unless a working directory is stated. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-214.r1/logs`.

| Check | Exact command | Exit and result |
| --- | --- | --- |
| RED, with the fix reverted | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` | 8; 1/1 CTest test segfaulted at address 0 in `LadspaEffect::changeSampleRate()` (`test-proof-final-red-1.log`). |
| GREEN, with the fix restored | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` | 0; 1/1 passed (`test-proof-final-green-1.log`). |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 (`gate-complexity-2.log`). |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 (`gate-file-length-2.log`). |

The requested reverse-patch proof was also run against this regression:

1. `git diff -- plugins/LadspaEffect/LadspaEffect.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-214.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-214.r1/logs/fix.patch` — exit 0 (`proof-patch-final-1.log`).
2. `cmake --build build --target ladspaeffect PluginPortsMigrationTest` — exit 0 (`build-proof-final-red-1.log`).
3. `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` — exit 8, reproducing the null dereference (`test-proof-final-red-1.log`).
4. `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-214.r1/logs/fix.patch` — exit 0 (`proof-patch-final-2.log`).
5. `cmake --build build --target ladspaeffect PluginPortsMigrationTest` — exit 0 (`build-proof-final-green-1.log`).
6. `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` — exit 0, 1/1 passed (`test-proof-final-green-1.log`).

The final regression build and run returned exit 0 as well (`build-green-2.log`, `test-green-2.log`). Two earlier test-harness build attempts returned exit 2 while the descriptor-failure helper was being corrected (`build-red-2.log`, `build-red-3.log`); the corrected harness built and its original-code run reproduced the crash (`build-red-4.log`, `test-red-2.log`).

## Files in this commit

- `plugins/LadspaEffect/LadspaEffect.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-214.md`
