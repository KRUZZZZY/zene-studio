# Cluster review-e2bbacec3 — Rack zone removal inverse changes first-match order

Verdict: **ALREADY-FIXED** (no code change needed; the fix and its regressions are on this
branch at HEAD)

## Finding

`src/core/ControlCommandsRackZones.cpp`: rack zone removal's advertised inverse used
`rack.zone_add`, which appends the zone and therefore re-orders overlapping zones after
an inverse replay. The finding asks to prefer the exact `control.undo` checkpoint that
preserves every field and the original list order, and to regress first/middle removal of
overlapping zones with a full state comparison after undo.

## Evidence

The fix is already in this branch's history:

- `e2bbacec3` "fix(rack): zone_remove advertised an inverse that could never run"
- `1aa803b2b` "fix(rework e2bbacec3): zone_remove advertised an append that loses zone order"

Current `registerZoneRemove` (`src/core/ControlCommandsRackZones.cpp:184-245`) already:

- records an action checkpoint whose undo re-inserts the captured zone at its own index
  with `RackZones::insertZone(index, zone)` (preserving all fields, including the decoded
  sample), and whose redo drops it again with `removeZone(index)`;
- advertises `control.undo` (not `rack.zone_add`) as its inverse, so an agent replaying
  the inverse restores the exact list order instead of appending.

The regressions named by the finding are present and unweakened:

- `tests/src/core/RackZonesTest.cpp:216-250`
  `zoneRemoveAdvertisesAReplayableInverse` — asserts the advertised op is `control.undo`
  and that undoing restores the dropped zone.
- `tests/src/core/RackZonesTest.cpp:252-283`
  `zoneRemoveOfAnOverlappingFirstZoneUndoesInOrder` — removes the first of two overlapping
  zones that carry samples, undoes, and checks the index, the sample and the order.
- `tests/src/core/InstrumentLayersCommandsTest.cpp:361-384`
  `layerZoneRemoveFirstOfOverlappingUndoKeepsOrder` — the same shape for layer zones.
- `tests/src/core/InstrumentLayersCommandsTest.cpp:424-447`
  `midiPartsRemoveFirstOfOverlappingUndoKeepsOrder` — the same shape for MIDI parts.

## Proof the existing regressions catch the bug (fix reverted, then restored)

Only the product hunk of `1aa803b2b` was reversed (tests untouched):

```
git show 1aa803b2b --format= -- src/core/ControlCommandsRackZones.cpp > logs/fix.patch
git apply -R logs/fix.patch
cmake --build build --target RackZonesTest                       # EXIT=0
cd build/tests && ctest -R RackZonesTest --output-on-failure      # EXIT=8
```

Failing assertion without the fix (logs/test-red.log):

```
FAIL!  : RackZonesTest::zoneRemoveAdvertisesAReplayableInverse()
   Actual   (inverse.value(QStringLiteral("op")).toString()): "rack.zone_add"
   Expected (QStringLiteral("control.undo"))                : "control.undo"
   Loc: [../../tests/src/core/RackZonesTest.cpp(239)]
```

That is the finding's exact claim measured: the advertised inverse reverts to the append
shape that loses zone order. Restored with `git apply logs/fix.patch`, rebuilt, all cases
pass. So HEAD is green and the pre-fix code was red.

## Commands run (copied verbatim, EXIT codes as seen)

```
cmake --build build --target RackZonesTest > logs/build-red.log 2>&1            # EXIT=0
cd build/tests && ctest -R RackZonesTest --output-on-failure \
    > logs/test-red.log 2>&1                                                   # EXIT=8  (1 failed)
git apply logs/fix.patch
cmake --build build --target RackZonesTest > logs/build-green.log 2>&1          # EXIT=0
cd build/tests && ctest -R RackZonesTest --output-on-failure \
    > logs/test-green.log 2>&1                                                 # EXIT=0  (100% tests passed out of 1)
bash tests/complexity-gate.sh --check > logs/gate-complexity.log 2>&1           # EXIT=1
bash tests/file-length-gate.sh --check > logs/gate-filelength.log 2>&1         # EXIT=1
```

Affected family: `RackZonesTest` — 1 test binary executed, 100% passed after restore
(25 test cases).

## Gates: honest note (PRE-EXISTING failures, not caused by this review)

Both ratchet gates report EXIT=1 on this branch's untouched HEAD (`git status --porcelain`
empty; no source file of mine is in either report):

- complexity: 6 `REGRESSION: new function over target` lines for functions in
  `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`,
  `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`,
  `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`
  (logs/gate-complexity.log).
- file length: 14 `REGRESSION: new file over 500 lines` lines, all pre-existing Zene test
  files (logs/gate-filelength.log).

No gate limit, baseline or assertion was touched. These regressions belong to other commits
on the branch and are outside this single-bug review.

## Files committed

`docs/qwen-fixes-2026-10/cluster-review-e2bbacec3.md` (this report) only. No product or
test file changed: the behaviour the finding describes is already correct at HEAD
(`1aa803b2b`), and its regressions (`RackZonesTest`, `InstrumentLayersCommandsTest`)
already cover it.
