# Cluster new-p9-242-port: LV2 help-window lifetime

Verdict: **FIXED** (port of `517a1f23c` from `bugs/qwen-new-p9-242`, written by hand onto `050/m0-train-5` at `b02a0ff2b`).

The bug was present on the newest line: `src/gui/Lv2ViewBase.cpp:187` still sets `Qt::WA_DeleteOnClose` to false, and the destructor (`:206-209`) only called `closeHelpWindow()`, so the help `SubWindow` owned by the main workspace outlived its view. The fix adds `delete m_helpWindow;` at `src/gui/Lv2ViewBase.cpp:208`, after `closeHelpWindow()` at `:207`.

Regression: `tests/src/gui/Lv2ViewBaseLifetimeTest.cpp:98` builds a view over a real installed LV2 effect that has help text, checks at `:120` that its help window is hidden, destroys the view at `:122`, and requires at `:123` that the window is gone. It is registered in `tests/CMakeLists.txt:963` (test list) and `:2220` (offscreen environment), and in `tests/all-sources.txt:2838`. The `src/gui/Lv2ViewBase.cpp` modification is recorded in `tests/upstream-modifications.txt` (last line).

The regression test file is a file-level copy of `517a1f23c:tests/src/gui/Lv2ViewBaseLifetimeTest.cpp`; it compiled unchanged against the current tree. No merge, cherry-pick or rebase was used.

## Verification

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-242-port.r1/logs` (abbreviated `LOG` below). All commands ran in the worktree unless noted.

Build note: the first build (`build-1.log`) failed with `EXIT=2` because ccache could not write to `/run/user/1000/ccache-tmp` (permission denied). This is an environment problem, not a code problem. Every later build used `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-242`. No configuration was changed.

| Step | Exact command | EXIT / result |
|---|---|---|
| Build zene + test, before fix (ccache retry) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-242 cmake --build build --target zene Lv2ViewBaseLifetimeTest > LOG/build-2.log 2>&1; echo EXIT=$?` | 0 |
| **RED** (before fix, from `build/tests`) | `ctest -R Lv2ViewBaseLifetimeTest --output-on-failure > LOG/test-red-1.log 2>&1; echo EXIT=$?` | **8**; `FAIL! : ...helpWindowIsDestroyedWithItsView() 'helpWindow.isNull()' returned FALSE`; 1 test executed, 1 failed |
| Apply fix | (edit `src/gui/Lv2ViewBase.cpp:208`) | — |
| Build after fix | `CCACHE_TEMPDIR=... cmake --build build --target zene Lv2ViewBaseLifetimeTest > LOG/build-3.log 2>&1; echo EXIT=$?` | 0 |
| **GREEN** (after fix, from `build/tests`) | `ctest -R Lv2ViewBaseLifetimeTest --output-on-failure > LOG/test-green-1.log 2>&1; echo EXIT=$?` | **0**; 1/1 passed |
| Direct run, not skipped | `QT_QPA_PLATFORM=offscreen ./Lv2ViewBaseLifetimeTest > LOG/test-direct-green-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | 0; `Totals: 3 passed, 0 failed, 0 skipped` |
| Proof step 1: save fix patch | `git diff -- src/gui/Lv2ViewBase.cpp > LOG/fix.patch; echo EXIT=$?` | 0 |
| Proof step 2: reverse fix | `git apply -R LOG/fix.patch; echo EXIT=$?` | 0 |
| Rebuild without fix | `CCACHE_TEMPDIR=... cmake --build build --target zene Lv2ViewBaseLifetimeTest > LOG/build-4.log 2>&1; echo EXIT=$?` | 0 |
| **Reversed run** (must fail) | `ctest -R Lv2ViewBaseLifetimeTest --output-on-failure > LOG/test-reverse-red-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | **8**; same `helpWindow.isNull()` failure |
| Proof step 3: reapply fix | `git apply LOG/fix.patch; echo EXIT=$?` | 0 |
| Rebuild with fix | `CCACHE_TEMPDIR=... cmake --build build --target zene Lv2ViewBaseLifetimeTest > LOG/build-5.log 2>&1; echo EXIT=$?` | 0 |
| **Reapplied run** (must pass) | `ctest -R Lv2ViewBaseLifetimeTest --output-on-failure > LOG/test-reapply-green-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | **0**; 1/1 passed |
| LV2 family build | `CCACHE_TEMPDIR=... cmake --build build --target zene Lv2ViewBaseLifetimeTest Lv2StateTest > LOG/build-6.log 2>&1; echo EXIT=$?` | 0 |
| LV2 family, first run | `ctest -R Lv2 --output-on-failure > LOG/test-family-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | 8; `Lv2InstrumentKeyRegressionTest` **Not Run**: its binary `ControlDeviceCatalogueTest` was not built |
| Build the missing dependency | `CCACHE_TEMPDIR=... cmake --build build --target ControlDeviceCatalogueTest > LOG/build-7.log 2>&1; echo EXIT=$?` | 0 |
| **LV2 family, final** (from `build/tests`) | `ctest -R Lv2 --output-on-failure > LOG/test-family-2.log 2>&1; echo EXIT=$?` | **0**; 3/3 passed (`Lv2StateTest`, `Lv2ViewBaseLifetimeTest`, `Lv2InstrumentKeyRegressionTest`) |
| Complexity gate (CCN ≤ 10 on new functions) | `bash tests/complexity-gate.sh --check > LOG/gate-1.log 2>&1; echo EXIT=$?` | **1** (see below: pre-existing) |
| File-length gate | `bash tests/file-length-gate.sh --check > LOG/gate-2.log 2>&1; echo EXIT=$?` | **1** (see below: pre-existing) |
| Whitespace check | `git diff --check; echo EXIT=$?` | 0 |

### Gate failures are pre-existing, not from this change

Both gates fail on the untouched base commit as well. I exported `HEAD` (`b02a0ff2b`) to `/tmp/qwen-new-p9-242-base`, ran the same two gates there, then removed the export:

| Gate on base `b02a0ff2b` | Exact command | EXIT | REGRESSION lines |
|---|---|---|---|
| Complexity | `bash tests/complexity-gate.sh --check > LOG/gate-base-1.log 2>&1; echo EXIT=$?` | 1 | 6 |
| File length | `bash tests/file-length-gate.sh --check > LOG/gate-base-2.log 2>&1; echo EXIT=$?` | 1 | 14 |

`diff` of the `REGRESSION:` lines between base and this branch was empty for both gates. The regressions are in files this fix does not touch (for example `src/core/ControlCommandsMixerTools.cpp`, `tests/src/core/DeviceDspTest.cpp`). The new files (`Lv2ViewBaseLifetimeTest.cpp`, 132 lines, and the one-line change to `Lv2ViewBase.cpp`) appear in neither gate's output. Fixing 6 complexity and 14 length regressions in unrelated files is outside this one-bug port, and the gate limits and baselines must not be changed, so they are left for the owner. Both gates would still exit 1 on this branch with or without this commit.

## Files committed

- `src/gui/Lv2ViewBase.cpp`
- `tests/src/gui/Lv2ViewBaseLifetimeTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-242-port.md`

`tests/fork-sources.txt` was not changed. Its regeneration rule covers product sources; test files such as `LinkedModelGroupViewLifetimeTest.cpp` are registered only in `tests/CMakeLists.txt` and `tests/all-sources.txt`, and this port follows that precedent.
