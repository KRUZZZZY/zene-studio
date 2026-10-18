# Cluster new-p8-309-port

Port of `b81fc875f` (accepted on `bugs/qwen-new-p8-309`) onto the newest line `050/m0-train-5`
(`b02a0ff2b`). Written by hand against the current code; nothing was merged, cherry-picked or rebased.

Verdict: **FIXED** (the bug is real on this line and is fixed; regression RED before / GREEN after).

Caveat: both ratchet gates exit 1 on this tree. Every failing entry I could check is in code this commit does not change
(see "Gates"). They are pre-existing, not introduced here, and I did not re-anchor or edit any baseline.

## Finding

`track.set_arm_many` declared `tracks` as an untyped array and coerced each entry with `toString()`. With
`tracks: ["trk-0", 7]` the first entry was armed, then `7` became `""`, failed as not found, was reported as refused,
and the call still returned success.

## Evidence (current tree)

- Bug is live before the fix: `src/core/ControlCommandsArrangementState.cpp:350-353` (`armEach`) called `id.toString()`
  with no type check, and the pre-fix schema was `control::arrayProperty()` with no `items`.
- Schema validator had no array-item check: `src/core/ControlSchema.cpp` `validateValue` (pre-fix) ended at
  `checkObjectMembers`. The argument check runs before the handler at `src/core/ControlRegistry.cpp:251`.

## Fix

- `src/core/ControlSchema.cpp:115` `checkArrayItems` validates each element against the declared `items` schema;
  called from `validateValue` at `:174`. Header comment at `:27` lists `items`.
- `src/core/ControlCommandsArrangementState.cpp:333` `trackIdsProperty()` declares `tracks` as an array of strings
  (used at `:422`).
- `src/core/ControlCommandsArrangementState.cpp:342` `allTrackIdsAreStrings`, checked in `setArmMany` at `:385-388`
  before `armEach` (`:390`) performs any arm.
- Both product files are already listed in `tests/fork-sources.txt` (lines 1305 and 1625), so no
  `tests/upstream-modifications.txt` entry is needed. No new files were added.

## Regression test

`tests/src/core/ControlEditCommandsTest.cpp:665` `trackSetArmManyRejectsNonStringIdsBeforeArming`. It redirects the
working dir to a temp folder (as the port source did), sends `tracks: [<valid track>, 7]` with `armed: true`, disarms
the valid track as cleanup, then asserts `InvalidArgs` and that the cleanup found the route not armed. Added include:
`ConfigManager.h`.

## Environment notes (not product changes)

- Default ccache temp dir is under `/run/user/1000`, which the sandbox denies. All builds used
  `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p8-309`. The first build attempt (log `01-red-build.log`, EXIT=2) failed for this
  reason only.
- The write-jail denies `/home/kruzzzzy/Documents/Zene Studio`, the default take folder. The pre-existing test
  `ControlEditCommandsTest::trackSetArmManyReportsOnlyTheTracksItChanged` (`ControlEditCommandsTest.cpp:624`) arms
  without redirecting the working dir, so its first arm fails with "take file ... could not be opened for writing".
  That failure happens before any changed code runs and is identical with and without the fix. It is not caused by this
  commit. I did not change that test.
- With `HOME=/tmp/zene-home-new-p8-309` and `/tmp/zene-home-new-p8-309/Documents/Zene Studio` created, the same
  unmodified binary passes the whole target (log `11-green-test-tmphome2.log`, EXIT=0).

## Verification

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-309-port.r1/logs`

| Step | Command (abbreviated; full form in the list below) | Exit | Result |
|---|---|---:|---|
| RED build | `cmake --build build --target zene ControlEditCommandsTest` (default ccache) | 2 | ccache temp-dir permission error; not a compile error (`01-red-build.log`) |
| RED build, retried | same, with `CCACHE_TEMPDIR=/tmp/...` | 0 | `02-red-build.log` |
| **RED test** (before fix) | `ctest -R '^ControlEditCommandsTest$'` from `build/tests` | **8** | `FAIL! trackSetArmManyRejectsNonStringIdsBeforeArming() '!malformed.ok' returned FALSE. (malformed batch unexpectedly succeeded; valid route was armed before cleanup: yes)` (`03-red-test.log`) |
| GREEN build | same build, fix applied | 0 | `04-green-build.log` |
| GREEN test, default HOME | `ctest -R '^ControlEditCommandsTest$'` | 8 | new test PASS; only the pre-existing take-folder failure at `:624` (`05-green-test.log`) |
| **Reversal: fix removed** | `git diff -- <2 product files> > fix.patch && git apply -R fix.patch` | 0 | `fix.patch` saved |
| reversed build | build | 0 | `06-reverse-build.log` |
| reversed test | ctest | 8 | new test FAIL again with the same message (`07-reverse-test.log`) |
| **Reversal: fix re-applied** | `git apply fix.patch` | 0 | `git status`: 3 files modified |
| restored build | build | 0 | `08-restored-build.log` |
| restored test | ctest | 8 | new test PASS; only `:624` fails (`09-restored-test.log`) |
| HOME redirect, no take folder | `HOME=/tmp/zene-home-new-p8-309 ctest ...` | 8 | take folder missing (`10-green-test-tmphome.log`) |
| HOME redirect, folder created | `HOME=... ctest -R '^ControlEditCommandsTest$'` | **0** | `100% tests passed out of 1` (`11-green-test-tmphome2.log`) |
| Family build | `cmake --build build --target zene ControlRegistryTest ControlResultSchemaTest ControlRegistryNestingTest ControlBrowserCommandsTest BrowserCatalogTest BrowserCatalogQueryTest UndoHonestyTest ControlEditCommandsTest -j2` | 0 | `12-family-build.log` |
| Family test | `HOME=/tmp/zene-home-new-p8-309 ctest -R '^(ControlRegistryTest\|ControlResultSchemaTest\|ControlRegistryNestingTest\|ControlBrowserCommandsTest\|BrowserCatalogTest\|BrowserCatalogQueryTest\|UndoHonestyTest\|UndoHonestyStateTest\|ControlEditCommandsTest\|ControlCommandsSnapshot)$'` | 8 | 9 of 10 Passed; `UndoHonestyStateTest` was "Not Run" because its binary was not built yet (`13-family-test.log`) |
| Build UndoHonestyStateTest | `cmake --build build --target UndoHonestyStateTest -j2` | 0 | `14-undostate-build.log` |
| UndoHonestyStateTest | `ctest -R '^UndoHonestyStateTest$'` | 0 | `100% tests passed out of 1` (`15-undostate-test.log`) |
| Complexity gate | `bash tests/complexity-gate.sh --check` | **1** | pre-existing, see Gates (`16-complexity-gate.log`) |
| File-length gate | `bash tests/file-length-gate.sh --check` | **1** | pre-existing, see Gates (`17-file-length-gate.log`) |

Affected family: 10 ctest entries. Of these, 9 Passed in run 13 and `UndoHonestyStateTest` Passed in run 15, so all 10
pass. `ControlEditCommandsTest` is one ctest entry with 18 QtTest slots (17 pass and 1 pre-existing sandbox failure in
the default HOME; all pass with the temp HOME). The browser tests are included because the new `items` check also runs on
browser result schemas in test mode (`ControlCommandsBrowser.cpp:238`, `ControlBrowserSupport.cpp:279-282`). They pass.

## Gates

Both gates exit 1. Neither failure comes from code this commit changes.

- Complexity (`16-complexity-gate.log:97-102`): six functions over CCN 10 that are not in the baseline:
  `dotCentreAlong` (`XyPadPairTest.cpp`), `parseNotes` (`DawProjectReadTracks.cpp`), `adjust`
  (`ControlCommandsMixerTools.cpp`), `setStrip` (`ControlCommandsMixerStrip.cpp`), `rotateProjectRevision`
  (`ProjectRevisions.cpp`), `importedOverrides` (`gui/ControlCommandsShortcuts.cpp`). None is in a file this commit touches.
  The new `checkArrayItems`, `allTrackIdsAreStrings` and the `setArmMany` check are not in the list.
- File-length (`17-file-length-gate.log:24-37`): 13 files over 500 code lines not in the baseline. One is
  `tests/src/core/ControlEditCommandsTest.cpp`. Measured with the gate's own rule, it was 511 code lines at HEAD
  (`git show HEAD:...` piped through the gate's awk), so it already failed before this commit. My test adds 23 code lines
  (511 → 534). The product files are 183 → 197 (`ControlSchema.cpp`) and 338 → 352 (`ControlCommandsArrangementState.cpp`),
  under the limit.

## Files committed

- `src/core/ControlSchema.cpp`
- `src/core/ControlCommandsArrangementState.cpp`
- `tests/src/core/ControlEditCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-309-port.md`

No control command was added; the `track.set_arm_many` schema change does not change the command set (the
`ControlCommandsSnapshot` test passed, run 13).
