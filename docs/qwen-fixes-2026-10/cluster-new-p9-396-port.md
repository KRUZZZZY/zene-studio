# Cluster new-p9-396-port: failed reinstall leaves stale safe-start state active

**Verdict: FIXED** (port of `bugs/qwen-new-p9-396` onto `050/m0-train-5`, CachyOS toolchain). The bug is still present on the newest line, so this is not ALREADY-FIXED.

## Finding status on this branch

Real. `install()` with a nonexistent directory cleared `installed`, `directory` and the session flags, but left `safeStart` and `skipEnabled` set from the earlier safe session. `shouldSkipPluginInstance()` did not check `installed`, so it kept skipping third-party plugins after the failed reinstall.

## Evidence (file:line on this branch)

- `src/core/SafeStart.cpp:287-302`: `install()`'s `resetInstall` lambda, which runs on every failed attempt and on a fresh successful install. It now also clears `markerWasPresent`, `safeStart`, `safeStartRuns`, `skipEnabled`, `projectPath` and `skipped`, which are the session safe-start decision.
- `src/core/SafeStart.cpp:606`: `shouldSkipPluginInstance()` now requires `s.installed`.
- `tests/src/core/SafeStartLoadPathTest.cpp:100-128`: `failedReinstallStopsSkippingThirdPartyPlugins()`. It starts a safe session, confirms a third-party plugin is skipped, fails a reinstall to a missing directory, then asserts the predicate is false, `isInstalled()` is false and `safeStartActive()` is false.

Deliberate difference from the accepted branch commit `399afe417`: that commit also reset `s.previous`. I left `previous` alone because `lastSession()` (`src/core/SafeStart.cpp:442`) returns it as previous-run history, and the header says history is not lifted by clear or endSession. No test depended on it. `previousRunMarkerFound` is also untouched, as on the accepted branch.

`src/core/SafeStart.cpp` is already registered in `tests/fork-sources.txt`. `SafeStartLoadPathTest.cpp` already exists on HEAD and is already registered in `tests/CMakeLists.txt:310`. It is not on `fork-sources.txt`, which is also true on HEAD and was not changed here. No new files were added and no control commands were added.

## Commands and results

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-396-port.r1/logs`

Build environment note: the first build failed with EXIT=2 (ccache could not create a temp file under `/run/user/1000/ccache-tmp`, a permission error in the `midiexport` plugin step). Every build after that ran with `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9`. The first ctest run (`test-red-1.log`, EXIT=8) executed a stale test binary because its build failed, so it is not used as evidence. The valid RED is `test-red-2.log`.

| Step | Command (run from the worktree root unless noted) | EXIT |
|---|---|---:|
| RED build (invalid, ccache permission error) | `cmake --build build --target zene SafeStartLoadPathTest -j2 > …/build-red-1.log 2>&1` | 2 |
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9 cmake --build build --target zene SafeStartLoadPathTest -j2 > …/build-red-2.log 2>&1` | 0 |
| **RED, unfixed code** | `cd build/tests && ctest -R '^SafeStartLoadPathTest$' --output-on-failure > …/test-red-2.log 2>&1` | **8** |
| Fix applied, build | `cmake --build build --target zene SafeStartLoadPathTest SafeStartTest PluginQuarantineMenuTest -j2 > …/build-green-1.log 2>&1` | 0 |
| **GREEN, fixed code** | `cd build/tests && ctest -R '^SafeStartLoadPathTest$' --output-on-failure > …/test-green-1.log 2>&1` | **0** (1/1) |
| Family, fixed code | `cd build/tests && ctest -R '^(SafeStartLoadPathTest\|SafeStartTest\|PluginQuarantineMenuTest)$' --output-on-failure > …/test-family-green-1.log 2>&1` | 0 (3/3) |
| Patch-reversal: save patch | `git diff -- src/core/SafeStart.cpp > …/fix.patch` | 0 |
| Patch-reversal: revert | `git apply -R …/fix.patch` | 0 |
| Reverted build | `cmake --build build --target zene SafeStartLoadPathTest -j2 > …/build-revert-1.log 2>&1` | 0 |
| **RED, reverted code** | `cd build/tests && ctest -R '^SafeStartLoadPathTest$' --output-on-failure > …/test-revert-red-1.log 2>&1` | **8** |
| Patch-reversal: re-apply | `git apply …/fix.patch` | 0 |
| Re-applied build | `cmake --build build --target zene SafeStartLoadPathTest SafeStartTest PluginQuarantineMenuTest -j2 > …/build-reapply-1.log 2>&1` | 0 |
| **GREEN, re-applied** | `cd build/tests && ctest -R '^(SafeStartLoadPathTest\|SafeStartTest\|PluginQuarantineMenuTest)$' --output-on-failure > …/test-reapply-green-1.log 2>&1` | **0** (3/3) |
| Complexity gate, branch | `bash tests/complexity-gate.sh --check > …/gate-1-complexity.log 2>&1` | **1** |
| File-length gate, branch | `bash tests/file-length-gate.sh --check > …/gate-2-file-length.log 2>&1` | **1** |
| Complexity gate, clean HEAD export | `git archive HEAD` into `/tmp/qwen-p9-head-check`, then `bash tests/complexity-gate.sh --check > …/gate-head-1-complexity.log 2>&1` | **1** |
| File-length gate, clean HEAD export | same export, then `bash tests/file-length-gate.sh --check > …/gate-head-2-file-length.log 2>&1` | **1** |

The RED assertion on unfixed code (`test-red-2.log`): `FAIL! : SafeStartLoadPathTest::failedReinstallStopsSkippingThirdPartyPlugins() '!shouldSkipPluginInstance("thirdparty", pluginPath)' returned FALSE`. The reverted run failed with the same assertion (`test-revert-red-1.log`).

Affected-family test count: 3 tests executed in the family (`SafeStartTest` #78, `SafeStartLoadPathTest` #79, `PluginQuarantineMenuTest` #401), 3/3 passed. The unfixed run executed 1 test (`SafeStartLoadPathTest`) and it failed as shown.

## Gates: red on HEAD too, not caused by this change

Both ratchet gates exit 1 on this branch. The same two gates also exit 1 on a clean `git archive HEAD` export. The regression lists are identical: `diff` of the sorted `REGRESSION` lines reported no difference for either gate. Every reported regression is in a file this commit does not touch (for example `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ProjectRevisions.cpp`). Neither `SafeStart.cpp` nor `SafeStartLoadPathTest.cpp` appears in either gate's output, and the complexity baseline and file-length limits were not touched.

Per the task rules I did not re-anchor or change any gate or baseline, and I did not refactor those unrelated files. Clearing the gates needs a separate change and an owner decision on those 68 functions and 14 files.

## Other notes

- clang-tidy reports `readability-identifier-naming` at `src/core/SafeStart.cpp:112` (`static State s;` in `state()`). That line predates this change.
- The IDE reported `SafeStartLoadPathTest.moc not found` at `SafeStartLoadPathTest.cpp:227`. The build compiles and links the test, so it is a stale diagnostic.
- `git status` before commit: only `src/core/SafeStart.cpp`, `tests/src/core/SafeStartLoadPathTest.cpp` and this report.
