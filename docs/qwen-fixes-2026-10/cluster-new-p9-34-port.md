# Cluster new-p9-34-port: A/B slot record outlives its device

Verdict: **FIXED** (a port of `bugs/qwen-new-p9-34` commit `47fd9d2bf` onto `050/m0-train-5`, tip `b02a0ff2b`).

It is not ALREADY-FIXED: on the new line the regression test fails before the change, so the bug is still present there.

## Finding and change

`abSlots()` in `src/core/ControlCommandsDeviceCopy.cpp:261` is a process-wide map from device id to that device's
A/B slot record: the stored XML, the parameter values and an owner `QPointer`. Nothing erased a record. A device
that was unloaded left its XML and value vectors in the map for the life of the process. The `QPointer` stopped a
dangling dereference but not the retained data.

The fix releases a record when its device is destroyed:

- `forgetDestroyedAbSlots` (`src/core/ControlCommandsDeviceCopy.cpp:269`) erases the record for an id once the
  device being destroyed no longer owns it: the owner is null or still points at that device. A record a later device
  claimed under the same id (ids restart with each document) stays.
- `claimAbSlots` (`src/core/ControlCommandsDeviceCopy.cpp:278`) is the one place a device takes ownership of a record.
  On the first claim it connects the device's `destroyed` signal to `forgetDestroyedAbSlots`.
- `pluginAb` (`:405`), `restoreSlots` (`:336`) and `restoreAb` (`:348`) all take ownership through `claimAbSlots`.
  Read paths are unchanged: `abSlotState` (`:467`) and `abSlotValues` (`:478`) already treat a record whose owner is not
  the device as absent.
- The test-only count accessor `control::abSlotEntryCountForTesting` is declared in
  `src/core/ControlDeviceTools.h:64` and defined at `src/core/ControlCommandsDeviceCopy.cpp:487`. It is not a control
  command, so the `ControlCommandsSnapshot` canary does not move.

### Differences from `47fd9d2bf`

1. **Undo and redo are covered.** `47fd9d2bf` connected `destroyed` only in `pluginAb`. `restoreSlots` and `restoreAb`
   also write records, for example when a store's undo lands on a device that an unload's undo rebuilt. Those records
   were never connected and outlived their device. They now go through `claimAbSlots`. The second new test below covers
   this path, and it fails when only that path is reverted (see the mutant row in the evidence table).
2. **The release test lives in `tests/src/core/DeviceAbUndoTest.cpp`, not `DeviceCopyTest.cpp`.** Adding the test to
   `DeviceCopyTest.cpp` took its code-line count to 503, over the 500-line file-length ratchet. The test moved with its
   body unchanged. The file-length gate shows no entry for either file after the move.
3. **The forget check also compares the owner to the destroyed object.** It does not depend on Qt clearing the
   `QPointer` before `destroyed` is emitted.

### Tests

- `tests/src/core/DeviceAbUndoTest.cpp:184` `abSlotsAreReleasedWhenAnEffectIsDestroyed`: stores slots A and B on a
  device, unloads it, flushes deferred deletion and checks the record count returns to its starting value. This is the
  regression from `47fd9d2bf`, moved.
- `tests/src/core/DeviceAbUndoTest.cpp:160` `aSlotRecordWrittenByAnUndoIsReleasedWithItsDevice`: stores slot A, unloads,
  undoes the unload, then undoes the store, which writes a record onto the recreated device. The count must be one
  above the start, and it must return to the start after that device is unloaded. The test flushes deferred deletion
  after the first unload, so the original device's record is released before the undo runs.

Commits on this branch: `0ec89c024` (RED: count hook and the regression test in `DeviceCopyTest.cpp`) and `1c7624553`
(fix, moved test and undo-path test).

## Evidence

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-34-port.r1/logs`. `ctest` ran from
`build/tests`. Builds ran from the worktree with `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-34-port` (see environment notes).

| # | Step | Command | Exit | Result |
| --- | --- | --- | --- | --- |
| 1 | RED, original code | `ctest -R DeviceCopyTest --output-on-failure > test-red-2.log 2>&1; echo EXIT=$?` | 8 | `abSlotsAreReleasedWhenAnEffectIsDestroyed` FAIL: Actual 5, Expected 4 at `DeviceCopyTest.cpp:367` (the count stays at 5 after unload). Totals: 15 passed, 1 failed, 1 skipped. |
| 2 | Reverse the fix (mutation check) | `git apply -R logs/fix.patch` | 0 | `fix.patch` is the product diff against the RED commit (71 lines). |
| 3 | Rebuild without the fix | `cmake --build build --target zene DeviceCopyTest -j2 > build-5-nofix.log 2>&1; echo EXIT=$?` | 0 | |
| 4 | RED, reversed fix | `ctest -R DeviceCopyTest -V > test-red-nofix.log 2>&1; echo EXIT=$?` | 8 | Same failure: Actual 5, Expected 4 at `DeviceCopyTest.cpp:367`. |
| 5 | Reapply the fix | `git apply logs/fix.patch` | 0 | |
| 6 | Rebuild with the fix | `cmake --build build --target zene DeviceCopyTest -j2 > build-6-fix-again.log 2>&1; echo EXIT=$?` | 0 | |
| 7 | GREEN, fix applied | `ctest -R DeviceCopyTest -V > test-green-3.log 2>&1; echo EXIT=$?` | 0 | `abSlotsAreReleasedWhenAnEffectIsDestroyed` PASS. Totals: 16 passed, 0 failed, 1 skipped (the pre-existing "this build has no gate module" skip). |
| 8 | RED for the moved and new tests, original code | `git apply -R logs/fix.patch`, then `cmake --build build --target zene DeviceAbUndoTest DeviceCopyTest -j2 > build-16-moved-red.log 2>&1`, then `ctest -R "^DeviceAbUndoTest$" -V > test-moved-red.log 2>&1; echo EXIT=$?` | 8 | `aSlotRecordWrittenByAnUndoIsReleasedWithItsDevice` FAIL: Actual 3, Expected 2 at `DeviceAbUndoTest.cpp:171`. `abSlotsAreReleasedWhenAnEffectIsDestroyed` FAIL: Actual 4, Expected 3 at `:205`. Totals: 3 passed, 2 failed. |
| 9 | GREEN for the moved and new tests | `git apply logs/fix.patch`, rebuild (`build-17-moved-green.log`), `ctest -R "^(DeviceAbUndoTest\|DeviceCopyTest)$" -V > test-moved-green.log 2>&1; echo EXIT=$?` | 0 | DeviceAbUndoTest: 5 passed, 0 failed. DeviceCopyTest: 15 passed, 0 failed, 1 skipped (pre-existing). |
| 10 | Restore-path mutant | Lines `ControlCommandsDeviceCopy.cpp:336` and `:348` changed back to `abSlots()[effect->id()]` (no claim). `ctest -R "^DeviceAbUndoTest$" -V > test-restore-mutant-final.log 2>&1; echo EXIT=$?` | 8 | `aSlotRecordWrittenByAnUndoIsReleasedWithItsDevice` FAIL: Actual 2, Expected 1 at `DeviceAbUndoTest.cpp:179`. The other 4 tests pass. Both lines then restored; `git diff` vs `fix.patch` is byte-identical (`cmp`). |
| 11 | Final A/B GREEN after restore | `ctest -R "^(DeviceAbUndoTest\|DeviceCopyTest)$" -V > test-final-ab.log 2>&1; echo EXIT=$?` | 0 | DeviceAbUndoTest 5 passed, 0 failed. DeviceCopyTest 15 passed, 0 failed, 1 skipped. |
| 12 | Device family, final | `ctest -R "^(DeviceAbUndoTest\|DeviceCopyTest\|DeviceDspTest\|DeviceMovesUndoTest\|DeviceOrganiserTest\|DeviceToolsChainTest\|DeviceToolsTest\|RackDeferredLifetimeTest\|UndoHonestyTest\|UndoHonestyStateTest\|W3EngineTest\|DevicePanelTest\|EffectDeviceMenuTest\|PluginNativeEditorTest)$" --output-on-failure > test-family-final.log 2>&1; echo EXIT=$?` | 8 | 14 tests executed: 13 passed. One failure: `UndoHonestyTest::armingSeveralTracksIsOneStep` (`UndoHonestyTest.cpp:414`). The take file `~/Documents/Zene Studio/zene-take-trk21.wav` cannot be opened for writing (see below). |
| 13 | Same failure without the fix | `git apply -R`, rebuild `zene UndoHonestyTest` (`build-14-undo-base.log`), `ctest -R "^UndoHonestyTest$" -V > test-undohonesty-base.log 2>&1; echo EXIT=$?` | 8 | Same `armingSeveralTracksIsOneStep` failure, same message. So it predates this fix. The fix was reapplied (`cmp` identical). |
| 14 | DeviceDspTest, before and after the module build | `ctest -R DeviceDspTest` in `test-family-1.log` (EXIT=8: `theFrequencyShifterMuteRouteDoesNotPassDryInput`, "the frequency shifter module is available" false). After `cmake --build build --target frequencyshifter` (`build-13-freqshift.log`, EXIT=0), `test-dsp-2.log` EXIT=0. | 0 | 26 passed, 0 failed, 12 skipped. It passes in the final family run (step 12). |
| 15 | File-length gate, base | `bash tests/file-length-gate.sh --check > gate-file-length-0.log 2>&1; echo EXIT=$?` (before any edit) | 1 | 14 REGRESSION lines, none in a touched file. |
| 16 | Complexity gate, base | `bash tests/complexity-gate.sh --check > gate-complexity-0.log 2>&1; echo EXIT=$?` (before any edit) | 1 | 6 REGRESSION lines, none in a touched file. |
| 17 | File-length gate, final | `bash tests/file-length-gate.sh --check > gate-file-length-final3.log 2>&1; echo EXIT=$?` | 1 | 14 REGRESSION lines, identical to step 15 (`diff`: same). No `DeviceCopyTest` or `ControlCommandsDeviceCopy` entry. |
| 18 | Complexity gate, final | `bash tests/complexity-gate.sh --check > gate-complexity-final3.log 2>&1; echo EXIT=$?` | 1 | 6 REGRESSION lines, identical to step 16 (`diff`: same). No new function over CCN 10. |

Intermediate gate run `gate-file-length-final.log` (EXIT=1) showed one new regression from this branch: `DeviceCopyTest.cpp` at 503
code lines, from the RED commit's test. Moving the test (above) cleared it. Steps 15 to 18 are the gate results that matter.

### Gate status: red, and not caused by this change

Both required ratchet gates exit 1 on the untouched base (`b02a0ff2b`, step 15 and 16). They exit 1 on the final tree
(steps 17 and 18) with the same REGRESSION lines, so this change adds no gate entries. The failing entries are in other
files: 12 test files and 2 product files (`src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`) over 500 code
lines, and complexity over CCN 10 in `src/core/ProjectRevisions.cpp` and `src/gui/ControlCommandsShortcuts.cpp`.
Fixing them is outside this bug, and this report does not re-anchor or edit any baseline. The gates need an owner's
decision on those files before they can go green.

## Environment notes

- `ccache` could not write to `/run/user/1000/ccache-tmp` from the sandbox (`build-1.log`, EXIT=2, "Permission denied").
  Every build after that used `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-34-port` (`build-2.log`, EXIT=0). The compiler,
  flags and source are unchanged, so this only moves ccache's temp files.
- The tree had no effect-plugin modules built, so `DeviceCopyTest` and the device tests skip with "this build loads no
  effect" (`test-red-1.log`, EXIT=8, which was a false RED: the new test skipped rather than failed). I built the
  `amplifier` and `bassbooster` targets (`build-3-plugins.log`, EXIT=0), then `frequencyshifter`
  (`build-13-freqshift.log`, EXIT=0). Those are build artifacts only; no source was changed for them.
- `UndoHonestyTest` writes take files into `~/Documents/Zene Studio`, which the sandbox does not allow. It fails the same
  way on the unfixed code (step 13). I did not work around it.
- Clang and clangd diagnostics in the editor reported the test files as broken (`constexpr` in included headers,
  missing `.moc`). The compiler built every target without errors, so those diagnostics are editor-only.
