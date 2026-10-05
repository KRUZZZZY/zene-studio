# Theory lane — batch 04

Six findings. Four were REAL and are fixed, one is FALSE-POSITIVE on arithmetic
grounds, and one is SKIPPED-SCOPE because its only compliant fix would exceed the
transaction payload contract.

## Summary

| Cluster | Subject | Verdict |
| --- | --- | --- |
| 4774 | Quantize rounding can overflow `tick_t` | FALSE-POSITIVE |
| 3866 | Swing can delay notes past the halfway point | FIXED |
| 4768 | Rename response reports the old groove name | FIXED |
| 1106 | Marker transaction records only the previous count | SKIPPED-SCOPE |
| 4847 | Marker actions can retain arguments from a different command | FIXED |
| 4848 | Marker actions accept arguments that will fail when fired | FIXED |

### Cluster 4774 — Quantize rounding can overflow `tick_t`
Verdict: FALSE-POSITIVE
Evidence: the ceiling and nearest calculations at `src/core/ControlCommandsGrooveEdit.cpp:304-306`
do add `grid` to a signed 32-bit `pos`, but the operands are bounded by the song length, which is
two orders of magnitude below `INT32_MAX`. `include/Song.h:77` sets `MaxSongLength = 9999 *
DefaultTicksPerBar`, and `include/TimePos.h:38` sets `DefaultTicksPerBar = 192`, giving 1,919,808
ticks. Even doubling the largest position by the largest legal grid stays around 3.84M, so
`pos + grid - 1` cannot overflow, and the swing addition that follows (`applySwing`, same file,
line 312) adds at most `grid / 2` to an already-clamped position. The reported overflow cannot be
reached through any command input.
Commit: none
Test: none
Red/green: none

### Cluster 3866 — Swing can delay notes past the halfway point
Verdict: FIXED
Evidence: `applySwing` in `src/core/ControlCommandsGrooveEdit.cpp:312-324` delayed a note by
`lround(swing * grid / 2.0)` with no upper bound. For an odd grid such as 3, full swing produced a
2-tick delay, placing the note past the 1.5-tick midpoint and closer to the next grid step than to
its own. The delay is now clamped with `std::min(..., grid / 2)`. Note that the report's suggested
cap of `(grid - 1) / 2` was rejected: it truncates even grids and would have delayed a note on grid
12 by 5 ticks instead of 6. `grid / 2` is the largest integer delay that cannot pass the midpoint on
any grid.
Commit: 27a7b7acd
Test: ControlGrooveCommandsTest::swingStopsShortOfTheNextGridStep
Red/green:
- RED: `cd build/tests && ctest -R '^ControlGrooveCommandsTest$' --output-on-failure` — EXIT=8
  (grid 3, note quantized to tick 3 landed at 5, expected 4)
- GREEN: `cd build/tests && ctest -R '^ControlGrooveCommandsTest$' --output-on-failure` — EXIT=0

### Cluster 4768 — Rename response reports the old groove name
Verdict: FIXED
Evidence: `grooveRename` in `src/core/ControlCommandsGroovePool.cpp:247-302` copied the groove into
a local `renamed` before calling `pool->rename`, then returned that stale copy as the touched
groove. The copy's name is produced by `grooveJson` (`src/core/ControlGrooveSupport.cpp:114-117`),
so the response carried `renamed_to` = new name next to `groove.name` = old name. The result is now
built from `pool->find(to)` after the rename.
Commit: 95bfbd6f3
Test: ControlGrooveCommandsTest::renameReportsTheGrooveUnderItsNewName
Red/green:
- RED: `cd build/tests && ctest -R '^ControlGrooveCommandsTest$' --output-on-failure` — EXIT=8
  (`groove.name` was "before", expected "after")
- GREEN: `cd build/tests && ctest -R '^ControlGrooveCommandsTest$' --output-on-failure` — EXIT=0

### Cluster 1106 — Marker transaction records only the previous count
Verdict: SKIPPED-SCOPE
Evidence: `recorded()` in `src/core/ControlCommandsMarkers.cpp:91-104` does put only the marker count
into the `__transaction` `before` state, while the undo closure captures the complete list. Writing
the full prior list into the payload would not be safe: marker names and action args are unbounded
strings and `MaxMarkers` is 512 (`include/Song.h`), so a full serialization can exceed
`control::MaxTransactionBytes` (262144). Exceeding it makes `ControlRegistry::trimTransactions`
(`src/core/ControlTransactions.cpp:59-71`) evict older transaction records, which trades a cosmetic
detail for losing undo history — the opposite of an improvement. The recorded undo step already
restores the complete marker list, so no marker is ever unrecoverable; this is a transaction
payload-contract decision, not a behaviour defect, and changing the contract is outside this lane.
Commit: none
Test: none
Red/green: none

### Cluster 4847 — Marker actions can retain arguments from a different command
Verdict: FIXED
Evidence: `readMarkerAction` in `src/core/ControlCommandsMarkers.cpp:137+` stored any supplied
`args` over the marker's existing ones, and stored nothing at all when `args` was omitted. Changing
a marker from `transport.set_tempo` with `{"bpm":120}` to `transport.play` without `args`
therefore left `bpm` attached to `transport.play`. When the action is supplied and differs from the
current action and no `args` are given, the stored args are now cleared.
Commit: 69ce477e9
Test: ProjectMarkersTest::changingAnActionDoesNotKeepThePreviousArguments
Red/green:
- RED: `cd build/tests && ctest -R '^ProjectMarkersTest$' --output-on-failure` — EXIT=8 (marker
  retained `{"bpm":120}` under the new action)
- GREEN: `cd build/tests && ctest -R '^ProjectMarkersTest$' --output-on-failure` — EXIT=0

### Cluster 4848 — Marker actions accept arguments that will fail when fired
Verdict: FIXED
Evidence: the action path of `setMarker` in `src/core/ControlCommandsMarkers.cpp` only checked
that the action names a registered command; the stored `args` were never validated against that
command's schema. A marker could be saved with `transport.set_tempo` and no `bpm`, so every
crossing invoked an invalid command. A new helper `actionArgsAreRunnable()` resolves the action
through the registry and runs `ControlRegistry::validateArgs` (declared
`src/core/ControlSchema.cpp:161`) — the same check `invoke()` performs — and a refused pair is now
reported as `ControlErrorKind::InvalidArgs` instead of being persisted.
Commit: a1305e44b
Test: ProjectMarkersTest::aMarkerActionCannotCarryArgumentsItsCommandWouldRefuse
Red/green:
- RED (product file reverted, test kept):
  `cmake --build build --target zene ProjectMarkersTest` — EXIT=0, then
  `cd build/tests && ctest -R '^ProjectMarkersTest$' --output-on-failure` — EXIT=8
- GREEN: `cmake --build build --target zene ProjectMarkersTest ControlGrooveCommandsTest SessionPerformTest`
  — EXIT=0, then
  `cd build/tests && ctest -R '^(ProjectMarkersTest|ControlGrooveCommandsTest|SessionPerformTest)$' --output-on-failure`
  — EXIT=0, 100% passed (3/3)

## Verification

Both ratchets were run before every commit in this batch and passed each time:
`bash tests/complexity-gate.sh --check` (EXIT=0) and `bash tests/file-length-gate.sh --check`
(EXIT=0). `readMarkerAction` initially reached CCN 13 after the 4847 change, so the validation was
extracted into `actionArgsAreRunnable()` rather than growing the function further.

Leftovers: none — all four fixes are committed, both gates pass, and the full
ProjectMarkersTest / ControlGrooveCommandsTest / SessionPerformTest run is green. Cluster 1106 is
deliberately left as-is and may deserve a separate transaction-payload design decision by the
parent lane.