# Cluster review-a84e5d2fe — Rack layer zone removal inverse changes first-match order

Verdict: **ALREADY-FIXED** on this branch (`bugs/qwen-review-a84e5d2fe`, HEAD `06c5dcf48`).
No product change was needed or made; the working tree is byte-identical to HEAD.

## Finding recap

`rack.layer_zone_remove` advertised `rack.layer_zone_add` as its inverse. That command
APPENDS a zone, so removing a zone that was not the last one and replaying the advertised
inverse moved the zone to the end of the list and changed the first-match order of
overlapping zones. The finding prescribed `control.undo` (the exact recorded checkpoint) or
an established exact ordered inverse, and a test that removes the first of two overlapping
zones and compares ordering/selection after the undo. Commit `daf753dcf`
("fix(rework a84e5d2fe): layer_zone_remove advertised an append that loses zone order") did
exactly that; `git merge-base --is-ancestor daf753dcf HEAD` returns 0, so it is on this branch.

## Code evidence (current tree, HEAD `06c5dcf48`)

- `src/core/ControlCommandsRackLayers.cpp:354-358` — the recorded undo step re-inserts the
  captured zone at its OWN index (`current->zones().insertZone(index, zone)`); the redo
  removes it at that same index.
- `src/core/ControlCommandsRackLayers.cpp:361-365` — the advertised inverse is
  `control.undo` with an empty args object, with the comment stating that a
  `rack.layer_zone_add` replay would append the zone and change the first-match order of
  overlapping zones.
- `src/core/ControlReversibilityTableClapInstrument.cpp:120-122` — the contract row for
  `rack.layer_zone_remove` is `TrueInverse`, its checkpoint text matches the code
  ("the recorded undo step re-inserts the captured zone at its own index").
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` — the `rack.layer_zone_remove`
  entry carries only `args_schema/description/group/id/mutating/requires/result_schema`, no
  advertised inverse (the `__transaction` payload is built at runtime by the fixed handler),
  so nothing stale still advertises an add.

## Test evidence (the finding's exact scenario)

`tests/src/core/InstrumentLayersCommandsTest.cpp`:

- `layerZoneRemoveAdvertisesAReplayableInverse` (line 334): adds a layer-0 zone, removes it,
  then asserts the recorded transaction's inverse op is `control.undo` (line 352) and replays
  it through `REV_UNDO_OR_FAIL()` (which is exactly `control.undo` with no args, see
  `tests/src/core/ReversibilityTestSupport.h:49-56`), asserting `basePlays(90, 100)` is true
  again and `basePlays(65, 100)` stayed false.
- `layerZoneRemoveFirstOfOverlappingUndoKeepsOrder` (line 362): the finding's scenario — two
  overlapping layer-0 zones (60..100 and 70..80), remove the FIRST, undo, and compare order:
  `zone(zonesBefore)->lowKey == 60` (the removed zone is first again) and
  `zone(zonesBefore + 1)->lowKey == 70`.

### Skip caveat resolved

The first ctest run "passed" only because those cases SKIPPED: this build tree had no plugin
`.so` at all (`find build/plugins -name '*.so'` → 0), so `revtest::firstLoadable("instrument")`
was empty and every instrument case reported "this build exposes no loadable instrument".
Building `tripleoscillator` (the device `firstLoadableInstrument()` prefers,
`tests/src/core/ReversibilityTestSupport.h:131-148`) made the zone cases actually run.

## RED/GREEN proof that the tests catch the defect

The advertised-inverse hunk of `daf753dcf` was temporarily reverted (the whole `git diff` of
`src/core/ControlCommandsRackLayers.cpp` saved as `fix.patch` and reverse-applied) and then
restored. LOGS = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-a84e5d2fe.r1/logs`.

1. Baseline with the fix in place (uninstrumented build tree):

   ```
   cmake --build build --target InstrumentLayersCommandsTest > LOGS/build-1.log 2>&1   BUILD_EXIT=0
   cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R InstrumentLayersCommandsTest \
       --output-on-failure > LOGS/test-1.log 2>&1                                      EXIT=0
   cmake --build build --target tripleoscillator > LOGS/build-plugin-1.log 2>&1        EXIT=0
   ```

   `direct-1.log`: `Totals: 4 passed, 0 failed, 10 skipped` — the zone cases skipped, see above.

2. RED (fix hunk reversed, so the handler advertises `rack.layer_zone_add` again):

   ```
   git diff -- src/core/ControlCommandsRackLayers.cpp > LOGS/fix.patch                  DIFF_EXIT=0
   cmake --build build --target InstrumentLayersCommandsTest > LOGS/build-2-red.log 2>&1 BUILD_EXIT=0
   cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R InstrumentLayersCommandsTest \
       --output-on-failure > LOGS/test-3-red-real.log 2>&1                             EXIT=8
   ```

   `test-3-red-real.log`:

   ```
   FAIL!  : InstrumentLayersCommandsTest::layerZoneRemoveAdvertisesAReplayableInverse() Compared values are not the same
      Actual   (inverse.value(QStringLiteral("op")).toString()): "rack.layer_zone_add"
      Expected (QStringLiteral("control.undo"))                : "control.undo"
      Loc: [../../tests/src/core/InstrumentLayersCommandsTest.cpp(352)]
   Totals: 12 passed, 1 failed, 1 skipped, 0 blacklisted
   ```

3. GREEN (fix restored, tree identical to HEAD):

   ```
   git apply -R LOGS/fix.patch                                                           APPLY_R_EXIT=0
   cmake --build build --target InstrumentLayersCommandsTest > LOGS/build-4-green.log 2>&1 BUILD_EXIT=0
   cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R InstrumentLayersCommandsTest \
       --output-on-failure > LOGS/test-4-green.log 2>&1                                 EXIT=0
   QT_QPA_PLATFORM=offscreen ./InstrumentLayersCommandsTest > LOGS/direct-2-green.log 2>&1 DIRECT_EXIT=0
   ```

   `test-4-green.log`: `1/1 Test #159: InstrumentLayersCommandsTest ..... Passed 2.65 sec`,
   `100% tests passed out of 1` (1 ctest entry executed; a zero-test run never occurred).
   `direct-2-green.log`: `Totals: 13 passed, 0 failed, 1 skipped, 0 blacklisted` — the one
   skip is `multiOutputBusesRouteCreateUndo`, the pre-existing "this build has no
   clapinstrument module" environment skip.

4. Surrounding families (unchanged HEAD, no product edit):

   ```
   cmake --build build --target InstrumentLayerZonePublishTest RackZonesTest \
       > LOGS/build-5-neighbours.log 2>&1                                               BUILD_EXIT=0
   cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "InstrumentLayerZonePublishTest|RackZonesTest" \
       --output-on-failure > LOGS/test-8-neighbours.log 2>&1                             EXIT=0   (2/2 Passed)
   cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "ControlReversibilityTranscript" \
       --output-on-failure > LOGS/test-7-transcript.log 2>&1                             EXIT=0   (1/1 Passed)
   ```

   `ctest -R "Rack|InstrumentLayer|Reversibility"` (`LOGS/test-5-family.log`, EXIT=8) lists 13
   "Not Run" entries and one transcript failure: those binaries were never built and the
   transcript aborted on missing plugins (`ladspaeffect`, then `amplifier`). Building
   `ladspaeffect` (`build-plugin-2.log`, EXIT=0) and `amplifier` (`build-plugin-3.log`, EXIT=0)
   cleared the transcript — a build-completeness artefact of this worktree, unrelated to the
   cluster, and proof that the reversibility transcript/snapshot canary is intact with the
   fix in place.

`git status --short` after restoration: empty — the working tree is exactly HEAD.

## Gates

Both ratchets fail on the UNMODIFIED HEAD `06c5dcf48` (the tree was clean when they ran; this
cluster commits no product code and no new or changed source file, so it cannot move either):

- `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-1.log 2>&1` — **EXIT=1**, 6
  pre-existing "new function over target" entries, none of them in this cluster's files:
  `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp` (CCN 16),
  `parseNotes@src/core/DawProjectReadTracks.cpp` (13),
  `adjust@src/core/ControlCommandsMixerTools.cpp` (12),
  `setStrip@src/core/ControlCommandsMixerStrip.cpp` (11),
  `rotateProjectRevision@src/core/ProjectRevisions.cpp` (11),
  `importedOverrides@src/gui/ControlCommandsShortcuts.cpp` (11).
- `bash tests/file-length-gate.sh --check > LOGS/gate-filelength-1.log 2>&1` — **EXIT=1**, 15
  pre-existing "new file over 500 lines" entries (e.g. `tests/src/core/DeviceDspTest.cpp` 752,
  `tests/src/gui/ArrangerTimelineTest.cpp` 689, `tests/src/core/VideoTrackTest.cpp` 651);
  none of them in this cluster's files.

The identical six complexity regressions are reported in the sibling lane's report for its own
HEAD (`docs/qwen-fixes-2026-10/cluster-review-427cc1f30.md`, same 6 functions), so this is
inherited branch drift from earlier lane merges. Re-anchoring a gate is forbidden by the brief,
and splitting 6 unrelated functions / 15 unrelated files is outside "change only the buggy
behaviour". The gate exits are recorded as observed, not adjusted.

## Action taken

No code change: the defect is already fixed on this branch (commit `daf753dcf`, ancestor of
HEAD), the advertised inverse is `control.undo` with an index-exact recorded undo step, and
the advertised-op regression test fails for real (EXIT=8) with the fix reverted and passes
(EXIT=0) with it in place. Only this report was committed.