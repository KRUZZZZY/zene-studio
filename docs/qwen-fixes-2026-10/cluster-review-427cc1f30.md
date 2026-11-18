# Cluster review-427cc1f30 — MIDI part removal inverse changes first-match order

Verdict: **ALREADY-FIXED** on this branch (`bugs/qwen-review-427cc1f30`, HEAD `de5a85a96`).
No product change was needed or made.

## Finding recap

`plugin.midi_part_remove` advertised `plugin.midi_part_add` as its inverse. A replay of
that command APPENDS the part, so removing a part that was not the last one and replaying
the inverse changed the first-match order of overlapping parts. The finding prescribed
using the exact recorded checkpoint `control.undo` if index-preserving replay could not
hold the semantics, and testing a first/middle deletion with an overlapping configuration.
That is exactly what commit `b209aaaaa` (rework of the original `427cc1f30` fix, merged
via `566587c66`, both ancestors of HEAD) did.

## Code evidence (current tree)

- `src/core/ControlCommandsPluginParts.cpp:228-232` — the recorded undo step re-inserts
  the captured zone at its own index (`RackZones::insertZone(index, zone)`); the redo
  removes it at that index.
- `src/core/ControlCommandsPluginParts.cpp:235-240` — the advertised inverse is now
  `control.undo` with an empty args object, with the comment stating why a
  `plugin.midi_part_add` replay would append and lose the first-match position.
- `src/core/ControlReversibilityTableClapInstrument.cpp:94-98` — the contract row for
  `plugin.midi_part_remove` is `TrueInverse` with the checkpoint text matching the code.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` carries no advertised
  inverse (the `__transaction` payload is built at runtime by the fixed handler), so
  nothing stale advertises an add.

## Test evidence (the finding's exact scenario)

`tests/src/core/InstrumentLayersCommandsTest.cpp`:

- `midiPartsAddRemoveUndoReload` (line 386): asserts the recorded transaction's inverse
  op is `control.undo`, then replays it and asserts the part comes back.
- `midiPartsRemoveFirstOfOverlappingUndoKeepsOrder` (line 426): two parts on the same
  key 60 (channel 3 then channel 5), removes the FIRST (`part-0`), undoes, and compares
  the whole list: `zone(0)->chain == 2` (channel 3 first-match wins again) and
  `zone(1)->chain == 4`.

### Skip caveat resolved

The first ctest run "passed" only because the cases SKIPPED: a targeted
`--target zene InstrumentLayersCommandsTest` build never builds the instrument plugins,
so `revtest::addInstrumentTrack()` found no loadable device ("this build exposes no
loadable instrument"). Building `tripleoscillator` (the device `firstLoadableInstrument()`
prefers; tests/CMakeLists.txt gates the fixture on
`LMMS_TEST_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins`) made the cases actually run.

## RED/GREEN proof that the tests catch the defect

The product state was temporarily reverted by reverse-applying the fix hunks of
`b209aaaaa` (the only later touch of the file is the merge `566587c66`), then restored.
LOGS = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-427cc1f30.r1/logs`.

1. RED (fix hunk reverted):

   ```
   git show b209aaaaa -- src/core/ControlCommandsPluginParts.cpp > LOGS/fix-product.patch
   git apply -R LOGS/fix-product.patch                                    (applied, rc 0)
   cmake --build build --target zene InstrumentLayersCommandsTest -j8 \
       > LOGS/build-3-red.log 2>&1                                        BUILD_EXIT=0
   cd build/tests && ./InstrumentLayersCommandsTest \
       > LOGS/test-4-red.log 2>&1                                         TEST_EXIT=1
   ```

   `test-4-red.log`:
   `FAIL! : InstrumentLayersCommandsTest::midiPartsAddRemoveUndoReload()`
   `Actual (inverse.value("op").toString()): "plugin.midi_part_add"`,
   `Expected ("control.undo")`. All other cases unchanged (the one skip is
   `multiOutputBusesRouteCreateUndo`, a pre-existing clap-module-environment skip).

2. GREEN (fix restored, tree identical to HEAD):

   ```
   git apply LOGS/fix-product.patch                                       APPLIED=0
   cmake --build build --target zene InstrumentLayersCommandsTest -j8 \
       > LOGS/build-5-green.log 2>&1                                      BUILD_EXIT=0
   cd build/tests && ./InstrumentLayersCommandsTest \
       > LOGS/test-7-green.log 2>&1                                       BINARY_EXIT=0
   ```

   `test-7-green.log`: both `midiPartsAddRemoveUndoReload()` and
   `midiPartsRemoveFirstOfOverlappingUndoKeepsOrder()` PASS (14 Qt cases: 13 pass,
   1 environment skip).

3. ctest family, required form:

   ```
   cd build/tests && ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure \
       > LOGS/test-8-green.log 2>&1                                       CTEST_EXIT=0
   ```

   `1/1 Test #156: InstrumentLayersCommandsTest ..... Passed`,
   `100% tests passed out of 1` (1 test entry executed; zero-run never observed).

`git status --short` after restoration: empty — the working tree is exactly HEAD.

## Gates

Both ratchets fail on the UNMODIFIED HEAD `de5a85a96` (run before any product edit of
this session; logs `gate-complexity.log`, `gate-filelength.log`):

- `bash tests/complexity-gate.sh --check` — EXIT=1: 6 pre-existing
  "new function over target" entries (`dotCentreAlong@tests/src/gui/XyPadPairTest.cpp`
  CCN 16, `lmms::interchange::readdetail::parseNotes@src/core/DawProjectReadTracks.cpp`
  13, `lmms::adjust@src/core/ControlCommandsMixerTools.cpp` 12,
  `lmms::setStrip@src/core/ControlCommandsMixerStrip.cpp` 11,
  `lmms::control::rotateProjectRevision@src/core/ProjectRevisions.cpp` 11,
  `importedOverrides@src/gui/ControlCommandsShortcuts.cpp` 11).
- `bash tests/file-length-gate.sh --check` — EXIT=1: 14 pre-existing "new file over 500
  lines" entries (e.g. `src/core/SmfInterchangeReader.cpp` 504,
  `tests/src/core/DeviceDspTest.cpp` 752).

The identical six complexity regressions appear in the concurrently-running sibling
lane's log for its own HEAD
(`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-43592d093.r1/logs/gate-complexity.log`),
so this is inherited branch drift from earlier lane merges, not something this cluster
introduces. This fix commits no product code and no new or changed source files, so it
cannot move either ratchet; re-anchoring is forbidden by the brief, and splitting 6
unrelated functions / 14 unrelated files is outside "change only the buggy behaviour".
Gate exits after this report: EXIT=1 and EXIT=1, with REGRESSION lists byte-identical to
the pre-commit runs (see `gate-complexity-2.log`, `gate-filelength-2.log`).

## Action taken

No code change: the defect is already fixed on this branch (commits `427cc1f30` + rework
`b209aaaaa`), the advertised inverse is `control.undo`, and the first-of-two-overlapping
undo-order regression test passes for real and fails with the fix reverted. Only this
report was committed.
