ALREADY-FIXED

# Cluster new-p9-340

`PeakController`'s default null effect is not dereferenced on this branch. The
guards landed in ancestor commit `fdeea3bbd` (cluster new-p10-317, 2026-10-12),
which is an ancestor of HEAD. This lane makes no product change; it adds a
fork-NEW regression that pins the supported null state.

Round 7 answered Luna's round-6 proof finding. The claim needs a
`regression-test` artifact with a changed test file, `red_exit` non-zero and
`green_exit` 0. The lane now has one:

- New file `tests/src/core/PeakControllerNullEffectTest.cpp` with its own ctest
  target `PeakControllerNullEffectTest`.
- Registered as fork-NEW in `tests/fork-sources.txt:4024`, in the whole-tree
  scope `tests/all-sources.txt:3149`, and in `tests/CMakeLists.txt:341`. It is
  never an inherited-ledger row: `tests/upstream-modifications.txt` still has no
  `ControllerSurfaceTest.cpp` or `PeakControllerNullEffectTest.cpp` row.
- The earlier `ControllerSurfaceTest.cpp` edit stays reverted to base
  `639e1d23e4`; that file is untouched by this round.

## Evidence

- **Default argument is still present, on purpose.** `include/PeakController.h:46-47`
  keeps `PeakControllerEffect *_peak_effect = nullptr`. Null is a valid state, so
  the finding's "remove the default" option is not taken.
- **Constructor guard.** `src/core/PeakController.cpp:58-66`: the effect-specific
  `connect` calls, including `m_peakEffect->attackModel()` and `decayModel()`, sit
  inside `if( m_peakEffect )`. Those were the dereferences the finding described.
- **Other effect uses are guarded.** `src/core/PeakController.cpp:76` (destructor),
  `:85` (coefficient read), `:93` (sample read), `:138-139`
  (`handleDestroyedEffect`), `:148` (`saveSettings`).
- **Null branch.** `src/core/PeakController.cpp:122` fills the value buffer with
  `0`. This path allocates nothing.
- **Regression.** `tests/src/core/PeakControllerNullEffectTest.cpp` has two
  deterministic cases: `defaultNullEffectConstructsAndIsSilent` (the finding's
  exact default-argument construction) and
  `explicitNullEffectStaysSilentAcrossPeriods` (explicit `nullptr`, four periods,
  first and last frame). Both drive `Controller::triggerFrameCounter()`, an
  explicit seam; there is no wall clock, sleep, thread or socket.
- **Ancestry and unchanged product code.**
  `git merge-base --is-ancestor fdeea3bbdd HEAD` EXIT 0;
  `git diff --stat fdeea3bbdd..HEAD -- src/core/PeakController.cpp` is empty, so
  the guard is exactly the ancestor fix.
- **Ledger check.** `rg -n '^tests/src/core/(ControllerSurfaceTest|PeakControllerNullEffectTest)\.cpp'
  tests/upstream-modifications.txt` EXIT 1 (no match); the ledger still ends
  with a newline.

## RED/GREEN reversal proof

The historical fix commit's product hunks were reversed with a patch generated
from `git show fdeea3bbdd -- src/core/PeakController.cpp`, rebuilt, and run. This
removes the constructor guard around the attack/decay model connects. Restoring
the patch and rebuilding returns the new test to green. No product change is left
in the worktree.

| Step | Command | Log | EXIT |
|---|---|---|---|
| Patch | `git show fdeea3bbdd -- src/core/PeakController.cpp > logs/reverse-peak-guard.patch` | reverse-peak-guard.patch | 0 |
| Reverse | `git apply -R logs/reverse-peak-guard.patch` | (stdout) | 0 |
| Build RED | `cmake --build build --target PeakControllerNullEffectTest -j4` | build-3-red.log | 0 |
| RED test | `./PeakControllerNullEffectTest defaultNullEffectConstructsAndIsSilent -v2` | test-2-red.log | 139 (SIGSEGV) |
| Restore | `git apply logs/reverse-peak-guard.patch` | (stdout) | 0 |
| Build GREEN | `cmake --build build --target PeakControllerNullEffectTest -j4` | build-4-green.log | 0 |
| GREEN test | `ctest -R '^PeakControllerNullEffectTest$' --output-on-failure` | test-3-green.log | 0 |

The RED stack trace names
`lmms::PeakController::PeakController(lmms::Model*, lmms::PeakControllerEffect*)`;
the GREEN run passes 1/1.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-340.r7/logs`.
`ctest` runs from `build/tests`; `HOME` is the scratch
`$TMPDIR/home-new-p9-340` for the family run, and `LMMS_PLUGIN_DIR` is not set.

| Step | Command (abbrev.) | Log | EXIT |
|---|---|---|---|
| Existing build tree, before reconfigure | `cmake --build build --target PeakControllerNullEffectTest -j4` | build-1.log | 2 (no such target: the tree had not seen the new source) |
| Reconfigure check | `cmake --build build --target cmake_check_build_system` | config-check-1.log | 0 |
| Build new target | `cmake --build build --target PeakControllerNullEffectTest -j4` | build-2.log | 0 |
| GREEN ctest | `ctest -R '^PeakControllerNullEffectTest$' --output-on-failure` | test-1-green.log | 0 |
| Rebuild ControllerSurfaceTest | `cmake --build build --target ControllerSurfaceTest -j4` | build-5-controller.log | 0 |
| Family list | `ctest -N -R 'Controller\|Peak'` | test-4-family-list.log | 0 (12 tests) |
| Build family | `cmake --build build --target ControllerConversionTest ControllerMappingEditorTest ControllerScriptTest PeakControllerEffectZeroFrameTest PeakJobsTest -j4` | build-6-family.log | 0 |
| Family GREEN | `ctest -R 'Controller\|Peak' --output-on-failure` | test-5-family.log | 0 (12/12 passed) |
| Gate | `python3 feature_check.py <worktree>` | gate-1-feature-check.log | 0 (all 8 rows PASS) |

Note on `build-1.log`: it is the pre-reconfigure attempt and is reported as it
happened. The build tree was configured before this round's new target existed;
`cmake_check_build_system` regenerated it (`config-check-1.log`, EXIT 0), and
every later build is green.

## Self-check

1. **The regression FAILS with the product change reverted and PASSES with it.**
   RED: `git apply -R` of the guard patch (EXIT 0), build (EXIT 0), then
   `defaultNullEffectConstructsAndIsSilent` exits 139 (SIGSEGV). GREEN: guard
   restored (EXIT 0), build (EXIT 0), `ctest` exits 0. Both runs are in this
   session's log folder.
2. **Deterministic.** No wall clock, sleep, thread or socket. The test calls
   `Controller::triggerFrameCounter()` a fixed number of times and reads
   `currentValue(0)` and `currentValue(lastFrame)`; the frame counter is the
   explicit seam.
3. **Fix at the cause and complete.** No product change was needed. Every
   `m_peakEffect` use was checked at the lines listed under Evidence.
4. **No collateral damage.** No product code, header or saved-data behaviour
   changed. The 12 controller/peak family tests pass, including the inherited
   `ControllerSurfaceTest`.
5. **ALREADY-FIXED evidence.** `src/core/PeakController.cpp:58-66, 85, 122` show
   the guards and the zero fill; `fdeea3bbdd` is an ancestor and its product diff
   is unchanged in HEAD; the new regression is at
   `tests/src/core/PeakControllerNullEffectTest.cpp` and is registered.
6. **Sibling commands.** This lane changes no command file: its only source edit
   is the new test `tests/src/core/PeakControllerNullEffectTest.cpp`. The
   command files that appear in the base diff come from the freshen merge
   `ed94ce9448` (`050/m0-train-43`), not from this lane; no `app.*`,
   `arrangement.*` or other command handler was touched, so none is affected by
   this test-only change.

## Not verified here

- **Qt5 portability.** Only the Qt 6 build tree was built. The test uses only
  `QCOMPARE`, `QVERIFY` and existing Qt-neutral API.
- **Realtime instrumentation.** No RTSan run. The null branch at
  `src/core/PeakController.cpp:122` only fills the buffer.
- **Attached-effect path.** Not re-tested in this lane; product code on that path
  is unchanged.

## Other findings

None.
