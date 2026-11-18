# Cluster new-p9-215: failed LADSPA initialization leaks resources

**Verdict: FIXED**

## Evidence

- [LadspaEffect.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-215/plugins/LadspaEffect/LadspaEffect.cpp:494) returns after a null processor and sets the effect not okay at line 507. Previously, [pluginDestruction()](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-215/plugins/LadspaEffect/LadspaEffect.cpp:553) returned immediately for not-okay effects.
- Destruction now cleans every acquired handle and every allocated port across `m_ports`, independently of `isOkay()`. It deactivates handles only when initialization reached activation; [LadspaEffect.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-215/plugins/LadspaEffect/LadspaEffect.h:88) tracks that state.
- [The regression](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-215/tests/src/plugins/PluginPortsMigrationTest.cpp:587) loads a deterministic two-processor LADSPA fixture. The first instantiate succeeds, the second returns null, and the test requires cleanup of the first handle. The fixture behavior is at [PartialInitFailureLadspa.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-215/tests/src/plugins/PartialInitFailureLadspa.cpp:17).

The configured ctest inventory matched **1 affected-family test**, `PluginPortsMigrationTest`; each ctest run executed 1/1. Ctest commands below ran from `build/tests`.

| Evidence | Exact command | Exit |
|---|---|---:|
| Affected-family count | Inventory log `inventory-1.log` lists only `PluginPortsMigrationTest` for this family | 1/1 |
| Initial RED, before production fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/test-1.log" 2>&1; result=$?; echo EXIT=$result` | 8 |
| Initial GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/test-2.log" 2>&1; result=$?; echo EXIT=$result` | 0 |
| Remove production patch | `git diff -- plugins/LadspaEffect/LadspaEffect.cpp plugins/LadspaEffect/LadspaEffect.h > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/fix.patch" && git apply -R "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/fix.patch"; result=$?; echo EXIT=$result` | 0 |
| RED with production patch removed | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/test-3.log" 2>&1; result=$?; echo EXIT=$result` | 8 |
| Restore production patch | `git apply "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/fix.patch"; result=$?; echo EXIT=$result` | 0 |
| Restored GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/test-4.log" 2>&1; result=$?; echo EXIT=$result` | 0 |
| Final GREEN after test portability guard | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/test-5.log" 2>&1; result=$?; echo EXIT=$result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/gate-3.log" 2>&1; result=$?; echo EXIT=$result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/gate-4.log" 2>&1; result=$?; echo EXIT=$result` | 0 |

The RED logs show the new assertion failing with `Actual (cleanups()): 0`. The final GREEN ctest passed, and both required gates passed.

## Build commands

Every build completed with exit 0:

- `cmake --build build --target PluginPortsMigrationTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/build-1.log" 2>&1; result=$?; echo EXIT=$result`
- `cmake --build build --target zene PluginPortsMigrationTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/build-2.log" 2>&1; result=$?; echo EXIT=$result`
- `cmake --build build --target zene PluginPortsMigrationTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/build-3.log" 2>&1; result=$?; echo EXIT=$result`
- `cmake --build build --target zene PluginPortsMigrationTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/build-4.log" 2>&1; result=$?; echo EXIT=$result`
- `cmake --build build --target zene PluginPortsMigrationTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-215.r1/logs/build-5.log" 2>&1; result=$?; echo EXIT=$result`

## Files in the commit

- `plugins/LadspaEffect/LadspaEffect.cpp`
- `plugins/LadspaEffect/LadspaEffect.h`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/src/plugins/PartialInitFailureLadspa.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-215.md`
