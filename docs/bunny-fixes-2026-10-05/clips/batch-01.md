# Space Bunny — clips lane, batch 01 (2026-10-05)

Branch `bugs/luna-clips-2026-10-05`. Six findings, one committed fix, one already
fixed earlier on this branch, one duplicate, three not defects.

Totals: FIXED 1 · FALSE-POSITIVE 2 · ALREADY-FIXED 1 · DUPLICATE 1 · SKIPPED-SCOPE 1.

### Cluster 4726 — Failed clip-list parsing leaves partial output behind

Verdict: FIXED

Evidence: `include/ControlClipLinkSupport.h:205` (old) appended each id to `*out`
as it parsed, so the duplicate and non-string branches at lines 199-202 / 191-196
returned false with a half-read list already in the caller's vector — directly
against the guarantee in the function's own doc comment ("leaves `out` untouched
otherwise"). The list is now read into a local `parsed` and assigned to `*out`
only after every entry passes (`include/ControlClipLinkSupport.h:175-215`, the
hand-over at `:211`).

Impact was contained in practice: the single caller
(`src/core/ControlCommandsClipLink.cpp:204-205`) passes a fresh local and returns
the error immediately, so nothing user-visible was corrupted. This is a contract
repair, not a crash fix.

Commit: 7a43cbbc1

Test: ClipLinkTest::aRefusedClipListWritesNothingIntoTheCallersVector

Red/green:
- Red: `cmake --build build --target ClipLinkTest zene` EXIT=0; `cd build/tests && ctest -R '^ClipLinkTest$'` EXIT=8
  — `FAIL! aRefusedClipListWritesNothingIntoTheCallersVector 'out == kept' returned FALSE. (a refused list wrote 2 of 2 ids into the caller's vector)`
- Green: `cmake --build build --target ClipLinkTest zene` EXIT=0; `ctest -R '^ClipLinkTest$'` EXIT=0
- Other tests over the changed header: `ctest -R '^(ClipLinkTest|ClipLinkPersistenceTest|ControlSuccessPathsTest|ControlResultSchemaTest)$'` EXIT=0 (4/4)
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0

### Cluster 2716 — Mirroring notes can change another linked clip's length

Verdict: FALSE-POSITIVE

Evidence: the reported behaviour is the documented contract of record, not a
defect. `docs/LINKED-CLIPS.md:190-193`: "**A member with auto-resize on re-sizes
to the propagated content**, because that is the engine's own rule for a clip
whose notes changed (`MidiClip::updateLength()`); a member that was manually
resized (`autoresize=0`) keeps its length." The mirror deliberately goes through
the member's own note API for exactly this reason, and says so at
`src/core/ClipLinks.cpp:185-188`. `MidiClip::addNote` → `updateLength()`
(`src/tracks/MidiClip.cpp:197`, `:129-153`) is the intended path.

The finding's premise — that the contract says length never changes — holds only
for an *explicit* placement edit: `clip.move` / `clip.resize` / `clip.trim` /
`clip.slip` do not propagate (`docs/LINKED-CLIPS.md:84`). A note-content mirror
resizing an auto-resize member is separate and specified. Applying the proposed
fix ("preserve the peer's length and resize setting") would silently flip a
documented decision and contradict `docs/LINKED-CLIPS.md` §4 point 4.

Commit: none

Test: none

Red/green: none

### Cluster 130 — Malformed gain-envelope points silently become valid points

Verdict: ALREADY-FIXED

Evidence: fixed earlier on this branch by `a6457f398` ("fix(clip.gain_envelope_set):
a malformed envelope point was written as a real breakpoint"). The current
`readPoint()` at `src/core/ControlCommandsClipEnvelope.cpp:99-125` refuses a
non-object, a non-whole `tick` and a non-numeric `gain_db`, and `readPoints()`
(line 128) stages into a caller-local `ClipEdits` so a refusal keeps the old
envelope. The reported inputs (`points: [{}]`, `points: [null]`) are both covered
by the existing case list at `tests/src/core/ClipGainEnvelopeTest.cpp:158-176`.

The report's line number (84) points at `exactInt()`, which is that fix's helper.

Commit: none (pre-existing `a6457f398`)

Test: ClipGainEnvelopeTest::theCommandValidatesAndUndos (pre-existing)

Red/green: none

### Cluster 1031 — Replaying `clip.set_transpose` does not restore the prior stretch mode

Verdict: SKIPPED-SCOPE

Evidence: the residual gap is real but the undo is not broken, and the fix needs
rule-8 territory. The transaction's own mechanism string names the journal, not the
replayed command: "Clip journal checkpoint: undo restores the clip's previous
edits and stretch mode" (`src/core/ControlCommandsClipEnvelope.cpp:197`), and the
journal does cover the warp element — the checkpoint is taken before both writes
(`:189-192`). The recorded `inverse` op is never replayed by the engine:
`transactionPayload` (`src/core/ControlEditSupport.cpp`) writes only
`op`/`args` and no `applies` key, so `undoByCommand`
(`src/core/ControlCommandsControl.cpp:332-334`) defaults `applies` to `"journal"`,
takes the journal branch and returns `std::nullopt`. Replaying
`clip.set_transpose {semitones: 0}` by hand does leave Rubber Band, because the
handler only writes the mode when `semitones != 0`
(`src/core/ControlCommandsClipEnvelope.cpp:192`).

Restoring the mode through a replayed inverse would require a new argument on
`clip.set_transpose` — a command params/result change, reserved to the parent
because it moves the ControlCommandsSnapshot canary. Changing what
`semitones: 0` means instead would alter a command's documented behaviour
("0 resets", `:300-301`). No product code changed.

Commit: none

Test: none

Red/green: none

### Cluster 1028 — `clip.set_loop` checkpoints state but returns no transaction

Verdict: DUPLICATE

Evidence: the same defect as Cluster 2752, of which it is a strict subset — this
report names `clip.set_loop` alone (handler at
`src/core/ControlCommandsClipEnvelope.cpp:202-213`), Cluster 2752 names all three
mutators including this one. Recorded once under 2752. Its stated impact is in any
case contradicted by the evidence given there.

Commit: none

Test: none

Red/green: none

### Cluster 2752 — Three clip mutators omit their transaction record

Verdict: FALSE-POSITIVE

Evidence: the claimed impact — an edit that "can be undone" but is recorded as
non-reversible, or that `control.undo` may fail to target — does not occur. When a
handler returns no `__transaction`, the registry synthesizes an honest record:
`src/core/ControlRegistry.cpp:389-408` stamps the contract class, sets
`reversible = journalled` and states the mechanism as "the undo step this call
left on the journal (control.undo reverses it)". All three mutators do take a real
checkpoint before writing (`src/core/ControlCommandsClipEnvelope.cpp:210-211`,
`:233-234`, `:251-252`) and all three are `RC::TrueInverse` with mechanism "Clip
journal checkpoint" (`src/core/ControlReversibilityTableDevices.cpp:152,155,156`),
so `journalled` is true and the record is reversible and honest.

This is also already demonstrated end to end: `clip.set_snap_point` returns no
payload today and `control.undo` still reverses it —
`tests/src/core/ClipTransposeTest.cpp:156-166` asserts the edit, then the undo,
then the reset-to-absence in the saved element. `clip.set_loop` and
`clip.set_formant` are the same shape.

The only real difference is that these three leave the registry to synthesise the
record instead of hand-writing a `before` payload. That is a consistency
preference, not a defect, and adding the payload would change the recorded
provenance input for no behavioural gain — declined under the
behaviour-preserving rule. Note for the record that the payload would have been
schema-safe: `__transaction` is a private side channel that
`finishResult` drops before the reply
(`tests/src/core/ControlResultSchemaTest.cpp:116-123`), so no canary movement —
the reason this was worth measuring rather than assuming.

Commit: none

Test: none

Red/green: none

## Leftovers

- Cluster 1031 stays open for the parent: a real `clip.set_transpose` inverse that
  restores the prior stretch mode needs a new command argument, which is a
  ControlCommandsSnapshot canary decision, not a fixer-lane change.
- Clusters 1028/2752 are re-raised as a possible *consistency* item (handlers
  hand-writing the transaction the registry currently synthesises); no defect
  demonstrated, so nothing changed.
- Nothing half-done in the committed fix: `include/ControlClipLinkSupport.h` and
  its test are committed together, gates are EXIT=0, and the tree is clean.
