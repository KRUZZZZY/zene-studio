# Cluster new-p8-17-port

**Verdict: FIXED (port).** The fix accepted on `bugs/qwen-new-p8-17` (commits `c255adea4`, `b87e4d793`) was re-applied by hand on the newest line (`bugs/qwen-new-p8-17-port`, based on `4eb8d6e8a`). Nothing was merged, cherry-picked or rebased. The bug is not already fixed on this line: the unpatched code at `src/core/ControlCommandsDocuments.cpp` called `project.new` before any check of the cache template, and the RED run below reproduces the finding's assertion on it.

Finding: "A failed built-in document creation can leave the old session replaced" (`newFromRecipe`, historical line 157).

## What changed

- `src/core/ControlCommandsDocuments.cpp`
  - `recipeTemplateWritable()` (line 159): probes the template destination before `project.new` can discard the session.
  - `restoreSession()` (line 173): reloads the saved session (file, modified flag, document type).
  - `restoredOr()` (line 189): keeps the original failure, adding the restore failure when the restore itself fails. A small helper so the three failure paths share one message shape; the complexity gate reports no entry for any of the new functions.
  - `newFromRecipe()` (line 198): preflight, then saves the current session to a temporary snapshot, then `project.new`. Each post-mutation failure (new session, template save, template open) restores the snapshot.
- `src/core/Song.cpp`
  - `RecentProjectSuppression` and `loadProjectSnapshotWithoutRecent()` (lines 88-115): load a rollback snapshot without adding its temporary path to the recent projects. A thread-local flag is used rather than a new `Song.h` member, so no header changes and no wide rebuild.
  - `Song::loadProject` (line 1972): the recent-project add also checks the new flag; `m_restoringSnapshot` is kept as it was.
- `tests/src/core/ControlSuccessPathsTest.cpp`: two new slots, `failedBuiltInDocumentKeepsCurrentSession` (line 433) and `failedBuiltInDocumentSaveKeepsCurrentSession` (line 467), ported from the accepted fix. `initTestCase` gains `QStandardPaths::setTestModeEnabled(true)`, and the includes are extended.
- `tests/upstream-modifications.txt`: one entry for the inherited `src/core/Song.cpp` change, with reason.
- `tests/fork-sources.txt`: `tests/src/core/ControlSuccessPathsTest.cpp` added in sorted position. The file is fork-added (commit `f2aedbeab`) but was missing from the list; the accepted fix registered it the same way.

No new control commands; no `ControlCommandsSnapshot` change; no header changed.

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-17-port.r1/logs`. `HOME` is set to `/tmp/zene-home-p8-17` for every ctest run (reason below). ccache temp dir is set to `/tmp/zene-ccache-tmp-p8-17` for every build (reason below).

| Step | Exact command (run from worktree or `build/tests`) | Exit | Log | Result |
|---|---|---:|---|---|
| Build test, first try | `cmake --build build --target ControlSuccessPathsTest -j2` | 2 | `build-1-red-test.log` | ccache could not create temp files under `/run/user/1000` (`Permission denied`). Environment, not code. |
| Build test (RED) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target ControlSuccessPathsTest -j2` | 0 | `build-2-red-test.log` | Test compiled against the **unmodified** product code. |
| First RED attempt, no HOME override | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` | 8 | `test-2-red.log` | Not valid RED: 3 failures, 2 are sandbox/environment (below). Shown for transparency. |
| **RED** (original product code) | `HOME=/tmp/zene-home-p8-17 ctest -R '^ControlSuccessPathsTest$' --output-on-failure` | **8** | `test-3-red-home.log` | `failedBuiltInDocumentKeepsCurrentSession()` FAIL: `'preservedAfterFailure' returned FALSE. (a failed built-in document creation replaced the current session)` (test line 464). `failedBuiltInDocumentSaveKeepsCurrentSession()` FAIL: `'preservedAfterFailure' returned FALSE. (a save-time failure replaced the current session)` (test line 520). Totals: 15 passed, 2 failed. |
| Build fix | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j2` | 0 | `build-3-fix.log` | Only pre-existing deprecation warnings (Song.cpp:2818-2846). |
| **GREEN** | `HOME=/tmp/zene-home-p8-17 ctest -R '^ControlSuccessPathsTest$' --output-on-failure` (from `build/tests`) | **0** | `test-4-green.log` | 1/1 passed. |
| Proof: save patch | `git diff -- src/core/ControlCommandsDocuments.cpp src/core/Song.cpp > <logs>/fix.patch` | 0 | `fix.patch` | |
| Proof: reverse fix | `git apply -R <logs>/fix.patch` | 0 | | |
| Proof: rebuild reversed | `CCACHE_TEMPDIR=... cmake --build build --target zene ControlSuccessPathsTest -j2` | 0 | `build-4-proof-reversed.log` | |
| **Proof RED** (fix reversed) | `HOME=/tmp/zene-home-p8-17 ctest -R '^ControlSuccessPathsTest$' --output-on-failure` | **8** | `test-5-proof-reversed-red.log` | Same two assertions as RED above. Totals: 15 passed, 2 failed. |
| Proof: re-apply fix | `git apply <logs>/fix.patch` | 0 | | |
| Proof: rebuild fixed | `CCACHE_TEMPDIR=... cmake --build build --target zene ControlSuccessPathsTest -j2` | 0 | `build-5-proof-reapplied.log` | |
| **Proof GREEN** (verbose) | `HOME=/tmp/zene-home-p8-17 ctest -R '^ControlSuccessPathsTest$' -V --output-on-failure` | **0** | `test-6-proof-reapplied-green.log` | `Totals: 17 passed, 0 failed, 0 skipped` (the 17 slots of the class). |
| Complexity gate | `bash tests/complexity-gate.sh --check` | **1** | `gate-1-complexity.log` | See gate status below. |
| File-length gate | `bash tests/file-length-gate.sh --check` | **1** | `gate-2-file-length.log` | See gate status below. |

**Affected ctest family:** `ControlSuccessPathsTest`, 1 registered test, 17 Qt slots executed in each run (0 would be an error). Counts: RED 15 passed / 2 failed; GREEN 17 passed / 0 failed.

### Why `HOME` is redirected for ctest

Two sandbox restrictions, both observed in this run, make the unredirected test fail for reasons unrelated to this fix:

1. `QStandardPaths::setTestModeEnabled(true)` puts the test's cache under `~/.qttest/cache/ControlSuccessPathsTest`. A leftover, empty `document-templates` directory from an earlier run is there, and `QDir::removeRecursively()` on it returns false (`test-2-red.log`, line 27). I did not delete it. The likely cause is the write-jail, which forbids changes outside the worktree, `/tmp` and the build caches, but I did not confirm that directly. Either way, the test can't convert that directory into the blocker file.
2. `controllerFeedbackTakeoverAndTemplates` writes to `~/Documents/Zene Studio/presets/controller-templates/`. `touch` on that directory returns `Permission denied` for this process, so the slot fails regardless of product code. (`test-2-red.log`, line 14.)

With `HOME=/tmp/zene-home-p8-17`, the test uses a clean cache under `/tmp` and all 17 slots run and pass on the fixed code. The test code does not depend on `HOME`; on a normal machine, the `removeRecursively` step works and the controller slot writes to the real directory. Nothing outside the jail was deleted or modified.

## Ratchet gates: failing, same as the base commit

- `bash tests/complexity-gate.sh --check` exits **1**, and `bash tests/file-length-gate.sh --check` exits **1**.
- On the untouched base `HEAD` (`4eb8d6e8a`, exported with `git archive` to `/tmp/zene-base-p8-17`), both gates also exit **1**, with **identical** `REGRESSION` lines (`gate-base-1-complexity.log`, `gate-base-2-file-length.log`). The diff of the regression lines between base and this tree is empty.
- The regressions are other files and functions (for example `tests/src/gui/ArrangerTimelineTest.cpp` 689 lines, `XyPadPairTest.cpp dotCentreAlong` CCN 16). None involves the functions or files this port changes, and none is a new entry because of this change.
- I did not change any gate limit or baseline, as the rules require. Fixing the pre-existing regressions means touching about fifteen unrelated files and is outside this bug. Those gates need an owner decision on the stale baseline for this line.

## Not verified

- The rest of the `ctest` suite was not run; only the family that covers the change (`ControlSuccessPathsTest`).
- The `ControlSuccessPathsTest.cpp` file is now 532 lines in total, but its code-line count is what the gate measures; it does not appear in the file-length regression list.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p8-17-port.md`
- `src/core/ControlCommandsDocuments.cpp`
- `src/core/Song.cpp`
- `tests/src/core/ControlSuccessPathsTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
