# Takeover report — Space Bunny, lane `clips` (branch `bugs/luna-clips-2026-10-05`)

Taken over from a Luna in-progress state (`23a1027e9`, tests only, UNVERIFIED). Baseline
build + test run over the WIP: `zene` and the four lane test targets built EXIT=0; of the
new cases, three were red for the reasons below and one area was already correct.

Luna cluster numbering follows the STATUS.md lane scopes:
1 = link anchor validation, 2 = gain-envelope point validation, 3 = truthful undo for clip edits.

---

### Cluster 1 — clip.link_* anchor validation

Verdict: FIXED

Evidence: `src/core/ControlCommandsClipLink.cpp:90` (`resolveNewMembers`) type-checked every
id in `clips` with `dynamic_cast<MidiClip*>` but never the anchor, while
`src/core/ClipLinks.cpp:170` (`collectStaleMembers`) returns false on a null `sourceMidi` and
`clip.link_create` ignores `MirrorReport::ok` — so an audio clip could be accepted as the
group's content source, the call answered success, nothing was mirrored, and the audio clip
was left a persisted `link` member of a note-list group.

Commit: `28f72f897`

Test: `ClipLinkPersistenceTest::refusalsAreTypedAndNothingIsHalfWritten` (the
`audioAnchor` half, added by the WIP), plus the new malformed-membership cases in the same
function.

Red/green:
- `cmake --build build --target zene ClipLinkTest ClipLinkPersistenceTest ClipGainEnvelopeTest ClipTransposeTest` → EXIT=0
- Red: `cd build/tests && ctest -R '^ClipLinkPersistenceTest$' --output-on-failure` → EXIT=8 (`'!audioAnchor.ok' returned FALSE`, ClipLinkPersistenceTest.cpp:177)
- Green: `cd build/tests && ctest -R '^ClipLinkPersistenceTest$' --output-on-failure` → EXIT=0

The WIP's second block in that test (empty `clips` array, duplicate member, anchor named in
`clips`) was already correct on arrival — `cliplink::readClipList`,
`include/ControlClipLinkSupport.h:172`, rejects all three before any checkpoint. Left as-is.

---

### Cluster 2 — gain-envelope point validation

Verdict: FIXED

Evidence: `src/core/ControlCommandsClipEnvelope.cpp:74` (`readPoints`) declared `points` as
`arrayProperty()`, which does not validate array elements, and read each one with
`toObject()`/`toInt()`/`toDouble()` — every one answers a default for a wrong shape. A
non-object element, a null, a string, a missing `tick` or `gain_db`, a string where a number
belongs, or a fractional tick each became a valid breakpoint at tick 0 / 0 dB and were
written into the clip behind a journal checkpoint.

Commit: `a6457f398`

Test: `ClipGainEnvelopeTest::theCommandValidatesAndUndoes` (the malformed-shape loop added
by the WIP).

Red/green:
- Red: `cd build/tests && ctest -R '^ClipGainEnvelopeTest$' --output-on-failure` → EXIT=8 (`'!rejected.ok' returned FALSE`, ClipGainEnvelopeTest.cpp:175)
- Green: `cd build/tests && ctest -R '^ClipGainEnvelopeTest$' --output-on-failure` → EXIT=0

The WIP's range-failure block (tick past the clip end, dB past ±, valid point preceding an
invalid one) was already correct on arrival — `after` is a local `ClipEdits` and
`setClipEdits` is only reached on success. Left as-is. The reader's own shape guard is shared
with cluster 3's `exactInt()`.

---

### Cluster 3 — truthful undo for clip edits

Two sub-findings: one real, three ALREADY-correct.

#### 3a — clip.set_snap_point coerced an unrepresentable 'ticks' to 0

Verdict: FIXED

Evidence: `src/core/ControlCommandsClipEnvelope.cpp:221` read the argument with
`QJsonValue::toInt()`, which answers its default `0` for anything that is not exactly an int
(2147483648, a value below INT_MIN, a fraction). 0 is inside every clip, so the range check
that follows passed: the command answered success and wrote `snapTicks = 0` plus a journal
checkpoint for an argument the caller never sent.

Commit: `ff6cbdf59`

Test: `ClipTransposeTest::aClipKeepsItsSnapPoint` (the 2147483648.0 case added by the WIP).

Red/green:
- Red: `cd build/tests && ctest -R '^ClipTransposeTest$' --output-on-failure` → EXIT=8 (errorKind 0, expected `ControlErrorKind::InvalidArgs`)
- Green: `cd build/tests && ctest -R '^ClipTransposeTest$' --output-on-failure` → EXIT=0

The same `toInt()`-with-a-default pattern is present in `setLoop`, `setFormant` and
`setTranspose` in that file, but each of those commands declares a RANGE in its
`argsSchema`, so an out-of-range value is refused by schema validation before the handler
runs. Tightening them was out of scope for the defect and is left alone.

#### 3b — undoing an added link-group member

Verdict: ALREADY-FIXED

Evidence: `src/core/ControlCommandsClipLink.cpp:132` (`linkCheckpointList`) checkpoints the
anchor (when it was unlinked) plus every newcomer in ONE `addJournalCheckPoint` call before
the first write, and `ClipLinks::mirrorContent` takes its own checkpoint over the members it
rewrites. The WIP's `ClipLinkTest::undoingAnAddedGroupMemberRestoresItsPriorNotesAndMembership`
PASSED on the very first run of the takeover, before any of my edits — the two merged
checkpoints do restore C's and D's prior note content and their unlinked membership while
the pre-existing A/B pair survives.

Test: `ClipLinkTest::undoingAnAddedGroupMemberRestoresItsPriorNotesAndMembership`
Red/green: none — `ctest -R '^ClipLinkTest$'` was EXIT=0 both before and after the fixes.

#### 3c — undo of clip.gain_envelope_set

Verdict: ALREADY-FIXED

Evidence: `src/core/ControlCommandsClipEnvelope.cpp:109` takes the clip's journal checkpoint
before `setClipEdits`, and the recorded transaction inverse replays the previous `points`.
The WIP's strengthened assertion (`clip->clipEdits() == before` rather than
`!hasEnvelope()`) passed on the first run.

#### 3d — undo of clip.set_transpose restores the warp stretch mode

Verdict: ALREADY-FIXED

Evidence: `src/core/ControlCommandsClipEnvelope.cpp:143` takes ONE checkpoint covering both
the edits and the warp element, and the WIP's new undo assertions (mode back to
`PreservePitch`, `transpose` back to 0) passed on the first run.

---

## No schema touched

No `argsSchema` / `resultSchema` was added, removed or re-typed by this lane, and no control
command was added, removed or renamed — so the `ControlCommandsSnapshot` canary does not
move. The guards are all in the handlers, and the `exactInt()` rejection of an out-of-int
value is deliberately in the handler rather than as a range on `integerProperty()`.

## Gates

`bash tests/complexity-gate.sh --check` and `bash tests/file-length-gate.sh --check` were
each run before all three commits and returned EXIT=0 every time. No ratchet was moved.

## Final state

`cd build/tests && ctest -R '^(ClipLinkTest|ClipLinkPersistenceTest|ClipGainEnvelopeTest|ClipTransposeTest|ControlSuccessPathsTest)$' --output-on-failure` → EXIT=0 (5/5 passed).
`ControlSuccessPathsTest` is the one test outside the lane's own files that drives
`clip.link_create`; it is green.

## Leftovers

- No half-done work: all three WIP areas were either repaired and committed or confirmed
  already correct, and every WIP test case now passes unmodified. No WIP test premise needed
  correcting — all three red failures were real product defects, not wrong assertions.
- Not attempted (out of lane scope, not a defect of this lane): the `toInt()`-with-a-default
  pattern in `setLoop` / `setFormant` / `setTranspose` (3a note above).
- The full 400-feature validation suite was not run here; the program runs it at batch and
  integration points.
