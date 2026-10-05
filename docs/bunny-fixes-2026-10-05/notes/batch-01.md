# Space Bunny fixer lane `notes` — batch-01

Worktree `zene-bunny-notes`, branch `bugs/bunny-notes-2026-10-05`. Six findings, all in
`src/core/ControlCommandsCompGroup.cpp`. Four fixed, two false positives.

### Cluster 5605 — `comp.move_edge` treats muted takes as playable

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlCommandsCompGroup.cpp:93-129` — `takeReaches()` answers "does a take of
that lane REACH this range", which is exactly the command's declared contract ("Refused where no
take of that lane reaches over the range it would grow into", `src/core/ControlCommandsCompGroup.cpp:295`).
Audibility is a different, documented axis: `docs/COMPING.md:22` — "per-lane audibility is the take
clip's own `muted` … which already exists, is already serialised and is already what the engine
reads". The engine's own lookup agrees with the command and ignores mute: `TakeLaneModel::takeAt()`
(`src/core/TakeLane.cpp:337-352`) matches on lane and range only, so `comp.get_state` still reports
a muted take as `status: "bound"` — excluding mute from the command's check would make `comp.move_edge`
refuse a move that `comp.get_state` calls resolved. And `comp.select`, the direct sibling, applies no
coverage check at all, so a composite naming a muted lane is already reachable by design; muting a take
is the user's way to silence one, and refusing edits that touch it would make a temporary mute a
barrier to arranging. No product code changed.

Commit: none

Test: none

Red/green: none

### Cluster 5604 — `comp.move_edge` can accept a gap in the take it grows

Verdict: FIXED

Evidence: `src/core/ControlCommandsCompGroup.cpp:93-106` before the fix — `takeCovers()` was satisfied
by ANY clip of the lane (`std::any_of` over `track.getClips()`), so the two endpoints of the grown
range could come from two different takes with a gap between them, which the two-endpoint check in
`takeReaches()` could not see. The move was accepted, and the composite then named a lane with no take
over the gap — `TakeLaneModel::takeAt()` returns nullptr there and `comp.get_state` reports it
`unresolved`. `takeReaches()` now walks every tick: `takeSpans()` builds one span per take (its own
range cut short by the segment's slip, since at timeline tick `t` the handle reads `t + sourceOffset`),
and the range is accepted only if the sorted spans close it without a gap.

Commit: 7e6fe2f72

Test: RecordToolsTest::anEdgeMoveRefusesAcrossAGapInTheGrowingLane

Red/green:
  cmake --build build --target zene RecordToolsTest   BUILD=0
  ctest -R '^RecordToolsTest$'                        EXIT=8 (red — move accepted, expected Refused)
  ctest -R '^RecordToolsTest$'                        EXIT=0 (green)
  ctest -R '^TakeLaneCompTest$'                       EXIT=0
  bash tests/complexity-gate.sh --check               EXIT=0
  bash tests/file-length-gate.sh --check              EXIT=0

### Cluster 1039 — `comp.move_edge` checkpoints but returns no transaction

Verdict: FIXED (the reported "lacks a reversible record" is not what was wrong)

Evidence: `src/core/ControlCommandsCompGroup.cpp:108-134` before the fix — the handler took the Track
checkpoint and rewrote the composite but returned no `__transaction`, so the record the registry built
had an EMPTY before-state and no inverse descriptor (`ControlRegistry::recordTransactionOf`,
`src/core/ControlRegistry.cpp:389-404` builds an "honest" record in that case). The command was
nevertheless already reversible: its contract row is true_inverse
(`src/core/ControlReversibilityTableDevices.cpp:176`)
and the journal step reversed it, which the pre-existing `aCompEdgeMoves` already proved. What was
missing is the A16 §3 description, which every sibling in the group carries (`comp.select`
`ControlCommandsCompEdits.cpp:181`, `comp.rebuild`, `comp.lane_add`, `comp.assign`). The handler now
returns the composite before the move plus the `control.undo` inverse; the no-op move (`to` equals the
boundary) still returns no payload, since it changes nothing and the registry records nothing for a
call that did.

Commit: 1bcf1cd16

Test: RecordToolsTest::aMovedEdgeRecordsItsBeforeStateAndInverse

Red/green:
  cmake --build build --target zene RecordToolsTest     BUILD=0
  ctest -R '^RecordToolsTest$'                         EXIT=8 (red — before.segments[0].end read 0)
  ctest -R '^RecordToolsTest$|^TakeLaneCompTest$'       EXIT=0 (green, 2 tests run)
  ctest -R '^(UndoHonestyTest|ControlRegistryTest|ControlResultSchemaTest)$' EXIT=0 (3 tests)
  bash tests/complexity-gate.sh --check                 EXIT=0
  bash tests/file-length-gate.sh --check                EXIT=0

### Cluster 2763 — Group comp selection creates separate undo checkpoints

Verdict: FALSE-POSITIVE

Evidence: `selectGroup()` does take one `Track::addJournalCheckPoint()` per track
(`src/core/ControlCommandsCompGroup.cpp:196-202`), but `ControlRegistry::runHandler` records the
journal depth before the handler and folds everything it pushed back into ONE step afterwards
(`src/core/ControlRegistry.cpp:324-337` → `ProjectJournal::mergeCheckpointsFrom`,
`src/core/ProjectJournal.cpp:290-336`, which keeps the EARLIEST capture per object — the state before
the command). The contract row says exactly this: `comp.select_group … "Track checkpoints, merged
into one step"` (`src/core/ControlReversibilityTableDevices.cpp:178`). Measured, not assumed: the
pre-existing `oneCompChoiceOnSeveralTracks` selects a range on two tracks and then calls `control.undo`
ONCE, asserting both composites are back to two segments — it passes
(`./RecordToolsTest oneCompChoiceOnSeveralTracks` → `PASS`). No product code changed.

Commit: none

Test: RecordToolsTest::oneCompChoiceOnSeveralTracks (pre-existing, passing)

Red/green: none

### Cluster 1040 — `comp.select_group` checkpoints tracks but returns no transaction

Verdict: FIXED

Evidence: `src/core/ControlCommandsCompGroup.cpp:174-184` before the fix — the handler mutated every
track after taking its checkpoint and returned `{tracks, lane, begin, end}` with no `__transaction`, so
the A16 record held an empty before-state and no inverse and named neither the tracks it wrote nor
their prior composites. The handler now returns the composite of every track before the edit, keyed by
the same ids the result's `tracks` array carries, with the `control.undo` inverse and a mechanism that
says the per-track checkpoints are folded into one step. Same narrow scope as cluster 1039: the record
already existed and the edit was already reversible.

Commit: b2611d880

Test: RecordToolsTest::aGroupedChoiceRecordsEveryTrackItWrote

Red/green:
  cmake --build build --target RecordToolsTest     BUILD=0
  ctest -R '^RecordToolsTest$'                     EXIT=8 (red — before.size() 0)
  ctest -R '^RecordToolsTest$|^TakeLaneCompTest$'   EXIT=0 (green, 2 tests run)
  ctest -R '^(UndoHonestyTest|ControlRegistryTest|ControlResultSchemaTest)$' EXIT=0 (3 tests)
  bash tests/complexity-gate.sh --check            EXIT=0
  bash tests/file-length-gate.sh --check           EXIT=0

### Cluster 1937 — Pass splitting accepts clips that comping does not support

Verdict: FIXED

Evidence: `splitPasses()` resolved its clip through the general `resolveClip()`
(`src/core/ControlCommandsCompGroup.cpp:187-191`), which walks the song container
(`ControlEditSupport.cpp:107-129`) and therefore reaches an AUTOMATION clip — a pattern track's clip
is not in the song container and never was. The cuts then went through, each piece was moved to the
loop's start and tagged with a take lane by `assignTakeLane()`, which writes the tag unconditionally
(`src/core/ControlCommandsComp.cpp:356-359`), while `comp.assign` routes the same clip through
`resolveTakeClip()` (`src/core/ControlCompSupport.cpp:44-60`) and refuses anything that is not an audio
or MIDI take. The result was take lanes whose composite can never resolve. `splitPasses()` now
resolves through the same validator.

Commit: 67d093904

Test: RecordToolsTest::splittingPassesRefusesAClipCompingCannotHold

Red/green:
  cmake --build build --target RecordToolsTest     BUILD=0
  ctest -R '^RecordToolsTest$'                     EXIT=8 (red — errorKind 0, expected Refused)
  ctest -R '^RecordToolsTest$|^TakeLaneCompTest$'   EXIT=0 (green, 2 tests run)
  bash tests/complexity-gate.sh --check            EXIT=0
  bash tests/file-length-gate.sh --check           EXIT=0

Leftovers: none. The report's cluster-5605 wording assumed a `PatternClip` is reachable through
`comp.split_passes`; only an automation clip is (pattern tracks are not in the song container
`enumerateClips()` walks), which is the case the new regression covers.
