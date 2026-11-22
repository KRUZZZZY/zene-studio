ALREADY-FIXED

# Cluster new-p9-340

`PeakController`'s default null effect is not dereferenced on this branch. The
guard landed in commit `fdeea3bbd` (cluster new-p10-317, 2026-10-12), an ancestor
of HEAD. The branch carries the registered regression from that commit; this lane
makes no product change.

Round 5 addressed Luna's blocking item and the diagnostic checkpoint:

- The lane's 15 test-only lines in `tests/src/core/ControllerSurfaceTest.cpp`
  (the explicit-`nullptr` / repeated-period test) are reverted to the base version
  `639e1d23e4`. The base already contains the registered default-null regression
  `defaultPeakControllerWithoutEffectReturnsSilence` from `fdeea3bbdd`.
- The `tests/src/core/ControllerSurfaceTest.cpp` row is removed from the inherited
  ledger `tests/upstream-modifications.txt`. That removal is the last source-registry
  edit; no registry derivation runs after it.
- Net diff `639e1d23e4..HEAD` contains only this report under `docs/`, no source
  files. Housekeeping therefore has no changed source file to re-classify.

## Evidence

- **Default argument is still present, on purpose.** `include/PeakController.h:46-47`
  keeps `PeakControllerEffect *_peak_effect = nullptr`. Null is a valid state, so the
  finding's "remove the default" option is not taken.
- **Constructor guard.** `src/core/PeakController.cpp:58-66`: the effect-specific
  `connect` calls, including `m_peakEffect->attackModel()` and `decayModel()`, sit
  inside `if( m_peakEffect )`. Those were the dereferences the finding described.
- **Other effect uses are guarded.** `src/core/PeakController.cpp:76` (destructor),
  `:85` (coefficient read), `:93` (sample read), `:138-139`
  (`handleDestroyedEffect`), `:148` (`saveSettings`). Only `:172` assigns the effect,
  and it takes an entry from `s_effects`, so it is never null.
- **Null branch.** `src/core/PeakController.cpp:122` fills the buffer with `0`. This
  path allocates nothing.
- **Regression test.** `tests/src/core/ControllerSurfaceTest.cpp:162-167`
  (`defaultPeakControllerWithoutEffectReturnsSilence`, from `fdeea3bbdd`) constructs
  `PeakController controller(Engine::getSong())` and requires `currentValue(0) == 0`.
- **Registration.** `tests/CMakeLists.txt:274` registers `ControllerSurfaceTest.cpp`,
  and `tests/all-sources.txt:2710` lists it. It is fork-authored, so it has no active
  row in `tests/upstream-modifications.txt`.
- **Ancestry and unchanged product code.**
  `git merge-base --is-ancestor fdeea3bbdd HEAD` EXIT 0;
  `git diff --stat fdeea3bbdd..HEAD -- src/core/PeakController.cpp` is empty.
- **Ledger check.** `rg -n '^tests/src/core/ControllerSurfaceTest\.cpp'
  tests/upstream-modifications.txt` EXIT 1 (no match), and the ledger still ends
  with a newline (`tail -c 1 ... | xxd` -> `0a`).
- **Base boundary.** `git cat-file -e 4e677cb6c6ab:tests/src/core/ControllerSurfaceTest.cpp`
  EXIT 128 confirms the file is not inherited from that fork point; it is
  fork-authored and its `all-sources.txt` entry is kept.

## RED/GREEN reversal proof

The historical fix commit's product hunks were reversed with a patch generated from
`git show fdeea3bbdd -- src/core/PeakController.cpp`, rebuilt, and run. This removes
the constructor guard around the attack/decay model connects. Restoring the patch
and rebuilding returns the test to green. No product change is left in the worktree.

| Step | Command | Log | EXIT |
|---|---|---|---|
| Patch | `git show fdeea3bbdd -- src/core/PeakController.cpp > logs/reverse-peak-guard.patch` | reverse-peak-guard.patch | 0 |
| Reverse | `git apply -R logs/reverse-peak-guard.patch` | (stdout) | 0 |
| Build RED | `cmake --build build --target ControllerSurfaceTest -j4` | build-2-red.log | 0 |
| RED test | `./ControllerSurfaceTest defaultPeakControllerWithoutEffectReturnsSilence -v2` | test-3-red-default.log | 139 (SIGSEGV) |
| Restore | `git apply logs/reverse-peak-guard.patch` | (stdout) | 0 |
| Build GREEN | `cmake --build build --target ControllerSurfaceTest -j4` | build-3-green.log | 0 |
| GREEN test | `./ControllerSurfaceTest defaultPeakControllerWithoutEffectReturnsSilence -v2` | test-4-green-default.log | 0 |

The RED stack trace names
`lmms::PeakController::PeakController(lmms::Model*, lmms::PeakControllerEffect*)`;
the GREEN run reports `3 passed, 0 failed`.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-340.r6/logs`.
`ctest` runs from `build/tests`; `HOME` is the scratch
`$TMPDIR/home-new-p9-340` for template writes, and `LMMS_PLUGIN_DIR` is not set.

| Step | Command (abbrev.) | Log | EXIT |
|---|---|---|---|
| Build baseline | `CCACHE_TEMPDIR=$TMPDIR/ccache-new-p9-340 cmake --build build --target ControllerSurfaceTest -j4` | build-1.log | 0 |
| ctest, real HOME | `ctest -R '^ControllerSurfaceTest$' --output-on-failure` | test-1.log | 8 (template dir permission denied; 14 passed, 1 failed) |
| ctest, scratch HOME | same, `HOME=$TMPDIR/home-new-p9-340` | test-2.log | 0 |
| ctest, GREEN restored | same | test-5-green.log | 0 |
| Family list | `ctest -N -R 'Controller\|Peak'` | test-6-family-list.log | 0 (6 tests) |
| Build family | `cmake --build build --target LfoControllerUserWaveTest MidiClipControllersLoadTest MidiControllerMenuTest ControllerTemplateMenuTest EqPeakHandoffTest -j4` | build-4-family.log | 0 |
| Family GREEN | `ctest -R 'Controller\|Peak' --output-on-failure` | test-7-family.log | 0 (6/6 passed) |
| Gate | `python3 feature_check.py <worktree>` | gate-1-feature-check.log | 0 (all 8 rows PASS) |

## Self-check

1. **The regression FAILS with the product change reverted and PASSES with it.**
   RED: `git apply -R` of the guard patch (EXIT 0), build (EXIT 0), then
   `defaultPeakControllerWithoutEffectReturnsSilence` exits 139 (SIGSEGV). GREEN:
   guard patch restored (EXIT 0), build (EXIT 0), the same test exits 0 and ctest
   exits 0. Both runs are in this session's log folder.
2. **Deterministic.** No wall clock, sleep, thread or socket. The base regression
   calls `Controller::triggerFrameCounter()` once and reads `currentValue(0)`; the
   frame counter is an explicit seam.
3. **Fix at the cause and complete.** No product change was needed. Every
   `m_peakEffect` use was checked at the lines listed under Evidence; the only
   unguarded assignment takes an entry from `s_effects`.
4. **No collateral damage.** No product code, header or saved-data behaviour changed
   in this lane. The 6 controller/peak family tests pass after the lane's revert.
5. **ALREADY-FIXED evidence.** `src/core/PeakController.cpp:58-66, 85, 122` show the
   guards and the zero fill; `fdeea3bbdd` is an ancestor and its product diff is
   unchanged in HEAD; the registered regression is at
   `tests/src/core/ControllerSurfaceTest.cpp:162-167`.

## Not verified here

- **Qt5 portability.** Only the Qt 6 build tree was built. The regression uses only
  `QCOMPARE` and existing Qt-neutral API.
- **Realtime instrumentation.** No RTSan run. The null branch at
  `src/core/PeakController.cpp:122` only fills the buffer.
- **Attached-effect path.** Not re-tested in this lane; product code on that path is
  unchanged.

## Other findings

None.
