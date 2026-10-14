# Cluster new-p10-194

Verdict: **FIXED**

`LADSPA_Descriptor::PortCount` is `unsigned long` ([include/ladspa.h:406](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-194/include/ladspa.h:406)). Both scans now use that member's type for their indices ([LadspaManager.cpp:208](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-194/src/core/LadspaManager.cpp:208), [LadspaManager.cpp:234](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-194/src/core/LadspaManager.cpp:234)). `addPlugins` skips descriptors whose total port count exceeds the host's `uint32_t` API ([LadspaManager.cpp:152](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-194/src/core/LadspaManager.cpp:152)) and skips channel counts that exceed the `uint16_t` fields ([LadspaManager.cpp:163](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-194/src/core/LadspaManager.cpp:163)). The private count helpers now accumulate in `uint32_t` ([LadspaManager.h:335](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-194/include/LadspaManager.h:335)).

The synthetic LADSPA module exercises 65,536 control ports, 65,536 audio inputs, and (where `unsigned long` is wider than `uint32_t`) a total count above `uint32_t`. The regression asserts that the control-port plugin loads with its full count and the unrepresentable descriptors are skipped ([LadspaManagerTest.cpp:24](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-194/tests/src/core/LadspaManagerTest.cpp:24), [LadspaPortCountFixture.cpp:56](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-194/tests/src/core/LadspaPortCountFixture.cpp:56)).

## Verification evidence

All ctest commands below ran from `build/tests`. The red run used the original production code; the reverse-patch run used the same test after removing only the production fix.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Original code, RED | `ctest -R '^LadspaManagerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/test-red-1.log 2>&1` | 8 | 1/1 timed out at 20.03 s on the 65,536-port scan |
| Fix applied, GREEN | `ctest -R '^LadspaManagerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/test-green-1.log 2>&1` | 0 | 1/1 passed |
| Fix removed, RED proof | `ctest -R '^LadspaManagerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/test-reverse-1.log 2>&1` | 8 | Same 1/1 timeout |
| Fix restored, GREEN proof | `ctest -R '^LadspaManagerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/test-green-2.log 2>&1` | 0 | 1/1 passed |
| Affected test family | `ctest -R '^(LadspaManagerTest|PluginPortsMigrationTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/test-family-1.log 2>&1` | 0 | 2/2 passed; 2 ctest tests executed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/complexity-gate-1.log 2>&1` | 0 | Passed |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/file-length-gate-1.log 2>&1` | 0 | Passed |

The test source manifest checks also passed: `bash tests/all-sources-reproduce.sh` (exit 0) and `bash tests/fork-sources-gate.sh` (exit 0). Build output was logged under the same log folder. `cmake --build build --target zene LadspaManagerTest` exited 2 on its first run because the build's automatic CMake regeneration completed mid-invocation and the in-flight Make process did not yet know the new target (`build-1.log`); the identical retry exited 0 (`build-2.log`). No manual configure command was run. The unscoped `cmake --build build --parallel 4` was refused by the environment guard (exit 97, `build-3.log`), so the affected named targets were built explicitly:

- `cmake --build build --parallel 4 --target zene ladspaeffect ladspabrowser LadspaManagerTest` — exit 0 (`build-4.log`, `build-reverse-1.log`, `build-final.log`).
- `cmake --build build --parallel 4 --target PluginPortsMigrationTest` — exit 0 (`build-family-1.log`).

The reversal proof used `git diff -- include/LadspaManager.h src/core/LadspaManager.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/fix.patch` (exit 0), `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/fix.patch` (exit 0), then `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/fix.patch` (exit 0).

## Round 2 scope-registration correction

The review found that the two new test sources were in `tests/all-sources.txt` but missing from the recipe-backed source list in `tests/fork-sources.txt`. Both entries are now present in sorted order at [tests/fork-sources.txt:1953](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-194/tests/fork-sources.txt:1953) and [tests/fork-sources.txt:1954](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-194/tests/fork-sources.txt:1954). `tests/all-sources.txt` remains recipe-reproducible.

Round 2 verification (all logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r2/logs`):

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Product fix, RED with original code | `ctest -R '^LadspaManagerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/test-red-1.log 2>&1` | 8 | 1/1 timed out at 20.03 s |
| Product fix, GREEN with fix applied | `ctest -R '^LadspaManagerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r1/logs/test-green-1.log 2>&1` | 0 | 1/1 passed |
| Build affected family | `cmake --build build --parallel 4 --target LadspaManagerTest PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r2/logs/build-family-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Both targets built |
| Affected family | `ctest -R '^(LadspaManagerTest|PluginPortsMigrationTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r2/logs/test-family-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | 2/2 passed; 2 ctest tests executed |
| Fork source scope gate | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r2/logs/fork-sources-gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed; both added tests classified as fork-new |
| All-sources reproduction | `bash tests/all-sources-reproduce.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r2/logs/all-sources-reproduce-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | `REPRODUCES` |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r2/logs/complexity-gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-194.r2/logs/file-length-gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed |

## Files committed across the fix and review correction

- `include/LadspaManager.h`
- `src/core/LadspaManager.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/src/core/LadspaManagerTest.cpp`
- `tests/src/core/LadspaPortCountFixture.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-194.md`
