# Cluster new-p9-340

Verdict: **ALREADY-FIXED**

`PeakController`'s default null effect is not dereferenced on this branch. The
guard landed in commit `fdeea3bbd` (cluster new-p10-317, 2026-10-12), which is an
ancestor of HEAD. This lane adds one test-only regression for the explicit-`nullptr`
and repeated-period path. It makes no product change.

## Evidence

- **Default argument is still present, on purpose.** `include/PeakController.h:46-47`
  keeps `PeakControllerEffect *_peak_effect = nullptr`. Null is a valid state here, so
  the finding's "remove the default" option is not taken.
- **Constructor guard.** `src/core/PeakController.cpp:58-66`: the effect-specific
  `connect` calls, including `m_peakEffect->attackModel()` and `decayModel()`, sit
  inside `if( m_peakEffect )`. These were the dereferences the finding describes.
- **Other effect uses are guarded.** `src/core/PeakController.cpp:76` (destructor),
  `:85` (coefficient read), `:93` (sample read), `:138-139` (`handleDestroyedEffect`),
  `:148` (`saveSettings`). Only `:172` assigns the effect, and it takes an entry from
  `s_effects`, so it is never null.
- **Null branch.** `src/core/PeakController.cpp:122` fills the buffer with `0`. This
  path allocates nothing.
- **Regression tests.** `tests/src/core/ControllerSurfaceTest.cpp:162-167`
  (`defaultPeakControllerWithoutEffectReturnsSilence`, from `fdeea3bbd`) and
  `tests/src/core/ControllerSurfaceTest.cpp:169-182` (new in this lane:
  `explicitNullPeakEffectStaysSilentAcrossPeriods`, explicit `nullptr`, four periods,
  first and last frame).
- **Registration.** `tests/CMakeLists.txt:274` registers `ControllerSurfaceTest.cpp`,
  and `tests/all-sources.txt:2710` lists it. The file is fork-authored, so it has no
  row in the inherited-file ledger `tests/upstream-modifications.txt` (see Round 2).
  No new file was added.

## Reversal proof

The committed fix was reversed as a patch and then restored. The patch is the
`src/core/PeakController.cpp` diff of `fdeea3bbd`, saved as
`logs/fix.patch`. Procedure step 4b's `git diff --` form would be empty here because
the fix is already committed.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-340.r1/logs`.
Working directory for `ctest` is `build/tests`. Unless noted, `QT_QPA_PLATFORM` is
the environment default and `LMMS_PLUGIN_DIR` is not set.

| Step | Command (abbrev.) | Log | EXIT |
|---|---|---|---|
| Build 1 (failed, sandbox) | `cmake --build build --target ControllerSurfaceTest -j4` | build-1.log | 2 |
| Build 2 | same, with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p9-340` | build-2.log | 0 |
| ctest, first try | `ctest -R '^ControllerSurfaceTest$' --output-on-failure` | test-1.log | 8 |
| ctest, GREEN baseline | same, with `HOME=$TMPDIR/home-new-p9-340` | test-2.log | 0 |
| Named tests, GREEN | `./ControllerSurfaceTest explicitNullPeakEffectStaysSilentAcrossPeriods defaultPeakControllerWithoutEffectReturnsSilence -v2` | test-3-green-names.log | 0 |
| Reversal apply | `git apply -R fix.patch` | (stdout) | 0 |
| Build, RED | `cmake --build build --target ControllerSurfaceTest -j4` | build-3-red.log | 0 |
| RED, default test | `./ControllerSurfaceTest defaultPeakControllerWithoutEffectReturnsSilence -v2` | test-4-red-default.log | 139 (SIGSEGV) |
| RED, new test | `./ControllerSurfaceTest explicitNullPeakEffectStaysSilentAcrossPeriods -v2` | test-5-red-explicit.log | 139 (SIGSEGV) |
| Restore | `git apply fix.patch` | (stdout) | 0 |
| Build, GREEN | `cmake --build build --target ControllerSurfaceTest -j4` | build-4-green.log | 0 |
| ctest, GREEN | `ctest -R '^ControllerSurfaceTest$' --output-on-failure` | test-6-green.log | 0 |
| Family list | `ctest -N -R 'Controller\|Peak'` | test-7-family-list.log | 0 (6 tests) |
| Family, unbuilt | `ctest -R 'Controller\|Peak'` (5 targets not yet built: Not Run) | test-8-family.log | 8 |
| Build family targets | `cmake --build build --target LfoControllerUserWaveTest MidiClipControllersLoadTest MidiControllerMenuTest ControllerTemplateMenuTest EqPeakHandoffTest -j4` | build-5-family.log | 0 |
| Family, GREEN | `ctest -R 'Controller\|Peak' --output-on-failure` | test-9-family.log | 0 (6/6) |
| Gate, as written | `python3 feature_check.py <worktree>` | (no log: PermissionError on sibling log dir) | 1 |
| Gate, redirected logs | same script copy, `ROOT` and log folder redirected only | gate-2-feature-check.log | 0 |

Redirected gate run: the copy in `$TMPDIR` changes only two lines. `ROOT` points at
the original qwen folder, and the log folder points at this run's `gate-logs/`. The
feature-check logic is unchanged.

Notes on the failed or sandbox-affected steps:

- Build 1 failed with `ccache: error: failed to create temporary file ... Permission
  denied`. ccache's temp dir lies outside the write-jail. Build 2 changed only
  `CCACHE_TEMPDIR` and built clean.
- ctest, first try: the only failure is `TemplatesSaveListReadAndApply`
  (`ControllerSurfaceTest.cpp:499`, `saveTemplate` returned FALSE). Its template
  directory is `~/Documents/Zene Studio/presets/controller-templates`, which the
  sandbox denies (`Permission denied`). The PeakController tests pass in that run. The
  green run uses a scratch `HOME` so that template write lands in scratch, with no test
  change.
- Five family tests were "Not Run" until their targets were built. Their exit status
  was not evidence until `test-9-family.log`.

## Self-check

Answers to the ASK items, with evidence:

1. **Regression FAILS with the product change reverted and PASSES with it.** RED:
   `test-4-red-default.log` and `test-5-red-explicit.log`, both EXIT 139. The stack
   trace shows `lmms::PeakController::PeakController(lmms::Model*, lmms::PeakControllerEffect*)`.
   GREEN: `test-3-green-names.log` (EXIT 0, 4 passed, 0 failed) and `test-6-green.log`
   (EXIT 0).
2. **Deterministic.** No wall clock, sleep, or thread. The test drives the frame
   counter (`Controller::triggerFrameCounter`) for a fixed count of four periods and
   reads `currentValue` at frame 0 and at the last frame.
3. **Fix at the cause and complete.** No product change was needed. Every
   `m_peakEffect` use was checked (`src/core/PeakController.cpp` lines listed in
   Evidence).
4. **No collateral damage.** No product code, header, or saved-data behaviour changed.
   The 5 family tests that were "Not Run" were built, not edited, and all 6 pass.
5. **ALREADY-FIXED evidence.** Code: `src/core/PeakController.cpp:58-66, 85, 122` and
   the committed guard in `fdeea3bbd`. Ancestry: `git merge-base --is-ancestor
   fdeea3bbd HEAD` returned yes. Unchanged since: `git diff --stat fdeea3bbd HEAD --
   src/core/PeakController.cpp` is empty.

Not verified here:

- **Qt5 portability.** Only the Qt 6.12 build tree (QtTest 6.12.0 in the log) was
  built. The Qt 5 CI configuration was not run. The new test uses only `QCOMPARE` and
  existing Qt-neutral API.
- **Realtime.** No RTSan or allocation instrumentation was run on the null branch.
  The reading is from `src/core/PeakController.cpp:122`, which only fills the buffer.
- **Attached-effect path.** Not re-tested in this lane. The product code on that path
  is unchanged. The family run passes, but it does not exercise attack/decay model
  updates.

## Round 2 (review rework)

Luna's review (`qa/reviews/new-p9-340.r1.md`, verdict REWORK) had one blocking item:
`tests/upstream-modifications.txt` carried a row for the fork-authored
`tests/src/core/ControllerSurfaceTest.cpp`. The row came from workflow commit
`db8c2c047` ("derive source registries from tracked files"), which ran after this
lane's commit `d92cf3c7b`. The review cites QA commit `07c6281bc`, which removes the
row, but that commit is not on this branch. The same one-line removal was made
directly in this worktree.

- **Change:** `tests/upstream-modifications.txt` loses the
  `tests/src/core/ControllerSurfaceTest.cpp` row (old line 5284). Nothing else
  changed. `git diff` shows one deletion, and the file still ends with a newline.
- **Ledger check:** `rg -n "^tests/src/core/ControllerSurfaceTest.cpp"
  tests/upstream-modifications.txt` has no match. Line 1046 still mentions the name
  inside another row's prose; that is not a row for this file.
- **Log folder:** `runs/new-p9-340.r2/logs`. The round-1 logs stay where they were.

| Step | Command (abbrev.) | Log | EXIT |
|---|---|---|---|
| Build targets | `cmake --build build --target ControllerSurfaceTest LfoControllerUserWaveTest MidiClipControllersLoadTest MidiControllerMenuTest ControllerTemplateMenuTest EqPeakHandoffTest -j4` (with `CCACHE_TEMPDIR` as in round 1) | r2-build-1.log | 0 |
| Family, GREEN | `ctest -R 'Controller\|Peak' --output-on-failure` from `build/tests`, with the scratch `HOME` from round 1 | r2-test-1-family.log | 0 (6/6 passed) |
| Gate | unchanged `feature_check.py` logic, log folder redirected to `gate-logs/` as in round 1 | r2-gate-1.log | 0 (all 8 rows PASS) |

The product code, the test binary's source, and the null regression are unchanged
since round 1. The RED/GREEN reversal from round 1 (`test-4`, `test-5`, `test-6`)
still holds for the same committed guard; it was not rerun in round 2.

## Round 3 (review rework)

Luna's round-2 review (`qa/reviews/new-p9-340.r2.md`, verdict DISPUTE) had one
blocking item: `tests/src/core/ControllerSurfaceTest.cpp` was back in
`tests/upstream-modifications.txt`. Commit `eaa81e071` (workflow, "derive source
registries from tracked files") re-added it. It ran about 30 seconds after
`849be5a02`, which had removed the row.

**Cause of the re-add.** The loop's housekeeping
(`qwen/workflow_evidence.py`, `housekeeping`) treats a changed source file as
inherited when it is not an active line in `tests/fork-sources.txt`, not added on
the branch, and not already in the ledger. It does not count `tests/all-sources.txt`
as a registration. This test is in `all-sources.txt` at line 2710, and
`tests/fork-sources.txt` deliberately moved it out (the comment at lines 560-575
says the three `ControlAutomationModesTest` / `ControllerSurfaceTest` /
`DawProjectInterchangeRoundTripTest` sources were moved to `all-sources.txt`, which
Gate 9 accepts). So any commit that changes this
file gets the row re-added, and this worktree cannot change that housekeeping code.

- **Change in this round:** the ledger row (old line 5284) is removed again. Nothing
  else changed. `git diff` shows one deletion.
- **Registry check:** `fork-sources-gate` and `all-sources-reproduce` pass without
  the row (see the gate run below).

| Step | Command (abbrev.) | Log | EXIT |
|---|---|---|---|
| Build targets | `cmake --build build --target ControllerSurfaceTest LfoControllerUserWaveTest MidiClipControllersLoadTest MidiControllerMenuTest ControllerTemplateMenuTest EqPeakHandoffTest -j4` | r3-build-1.log | 0 |
| Family, GREEN | `ctest -R 'Controller\|Peak' --output-on-failure` from `build/tests`, scratch `HOME` as in round 1 | r3-test-1-family.log | 0 (6/6 passed) |
| Gate | unchanged `feature_check.py` logic, log folder redirected to `gate-logs/` | r3-gate-1.log | 0 (all 8 rows PASS) |

Run folder for round 3: `runs/new-p9-340.r3/logs`.

## Round 4 (review rework)

Luna's round-3 review (`qa/reviews/new-p9-340.r3.md`, verdict DISPUTE) blocked on the
same row again: commit `55c9b5a50` (workflow, "derive source registries from tracked
files") re-added `tests/src/core/ControllerSurfaceTest.cpp` to
`tests/upstream-modifications.txt`, 1 minute 43 seconds after `859493763` removed it.
This is the fourth recurrence (`db8c2c047`, `eaa81e071`, `55c9b5a50`), and each one
follows a commit on this branch. The cause is the one in Round 3 (loop housekeeping,
`qwen/workflow_evidence.py`, which does not count `tests/all-sources.txt`).

- **Change in this round:** the row is removed again. `git diff` shows one deletion.
- **Checks:** `cmake --build` of the six targets exits 0 (`r4-build-1.log`), the
  controller-family ctest passes 6/6 (`r4-test-1-family.log`, exit 0), and the gate
  passes all eight rows without the row (`r4-gate-1.log`, exit 0).
- **Run folder:** `runs/new-p9-340.r4/logs`.

A row removed by a commit on this branch will return on the next housekeeping run.
Two ways to end the loop, both outside this worktree:
1. Change `workflow_evidence.py` so an inherited file that is listed in
   `tests/all-sources.txt` is not added to the ledger.
2. Register the file as an active `tests/fork-sources.txt` line. The fork-sources
   comment (lines 560-575) says this file was moved out on purpose, and the
   fork-sources gate requires the list to match the recipe byte for byte, so this is
   an owner decision, not a worktree change.
