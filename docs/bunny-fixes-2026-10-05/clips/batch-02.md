# Space Bunny — clips lane, batch 02 (2026-10-05)

Branch `bugs/luna-clips-2026-10-05`. Six findings. One already fixed earlier on this
branch, two duplicates of batch-01's Cluster 2752, one duplicate of a finding in this
batch, and two whose real gap needs rule-8 territory.

Totals: FIXED 0 · FALSE-POSITIVE 0 · ALREADY-FIXED 1 · DUPLICATE 3 · SKIPPED-SCOPE 2.
No product code changed on this batch; the tree carries only this report.

### Cluster 1029 — `clip.set_snap_point` checkpoints state but returns no transaction

Verdict: DUPLICATE

Evidence: the same defect as batch-01's Cluster 2752, which named all three
envelope mutators including this one and was recorded FALSE-POSITIVE there. The
handler at `src/core/ControlCommandsClipEnvelope.cpp:216-236` checkpoints at `:233`
and returns no `__transaction`; the registry then synthesizes an honest record —
`src/core/ControlRegistry.cpp:389-408` sets `reversible = journalled` with the
mechanism "the undo step this call left on the journal (control.undo reverses it)" —
and `clip.set_snap_point` is `RC::TrueInverse` with `journalled = true`
(`src/core/ControlReversibilityTableDevices.cpp:155`), so `journalled` holds. The undo
is already proven end to end at
`tests/src/core/ClipTransposeTest.cpp:156-166`.

Commit: none

Test: ClipTransposeTest (pre-existing, covers this command's undo)

Red/green: none

### Cluster 1030 — `clip.set_formant` checkpoints state but returns no transaction

Verdict: DUPLICATE

Evidence: the third of batch-01 Cluster 2752's three mutators, same handler file, same
synthesized-record argument. `setFormant` (`src/core/ControlCommandsClipEnvelope.cpp:239-256`)
checkpoints at `:251` before writing the edits, and is `RC::TrueInverse` /
journalled at `src/core/ControlReversibilityTableDevices.cpp:156`. The additional
concern in the report — that the command "may change the stretch mode"
(`:253`, `SampleClip::setWarpStretchMode`) — is covered by the checkpoint as well,
because the checkpoint is taken before that write, and is the same residual gap
already logged as SKIPPED-SCOPE in batch-01 Cluster 1031.

Commit: none

Test: none

Red/green: none

### Cluster 128 — Link creation permits an audio clip as the note source

Verdict: ALREADY-FIXED

Evidence: fixed earlier on this branch by `28f72f897` ("fix(clip.link): a clip with no
note list was accepted as a link group's ANCHOR"). The current
`resolveNewMembers()` refuses a non-`MidiClip` anchor before any write, at
`src/core/ControlCommandsClipLink.cpp:108-112`, with the shared typed refusal
`noNoteList()` (`:85-92`). The regression case is in place and passing:
`tests/src/core/ClipLinkPersistenceTest.cpp:175-179` drives
`clip.link_create {clip: <sample>, clips: [<midi>]}` and asserts the `InvalidArgs`
refusal naming the note list.

Commit: none (pre-existing `28f72f897`)

Test: ClipLinkPersistenceTest (pre-existing case, name not split out at this revision)

Red/green: `cmake --build build --target ClipLinkTest ClipLinkPersistenceTest ClipTransposeTest zene` EXIT=0; `cd build/tests && ctest -R '^(ClipLinkTest|ClipLinkPersistenceTest|ClipTransposeTest)$' --output-on-failure` EXIT=0 (3/3 passed)

### Cluster 1035 — `clip.link_create` inverse leaves members linked

Verdict: SKIPPED-SCOPE

Evidence: the undo path is not broken; only the advertised inverse record is
incomplete, and making it correct needs rule-8 territory. The engine never replays
that record: `transactionPayload()` (`src/core/ControlEditSupport.cpp:481-493`) writes
only `before` / `inverse` / `reversible` / `mechanism` and no `applies` key, so
`undoByCommand()` (`src/core/ControlCommandsControl.cpp:331-333`) reads `applies` as
`"journal"`, returns `std::nullopt`, and `control.undo` unwinds the ProjectJournal
instead. That journal checkpoint is exactly the complete inverse the report asks for:
one checkpoint over the anchor and every newcomer at
`src/core/ControlCommandsClipLink.cpp:212-215`, and the merge folds the mirror's own
checkpoint into the same step. This is already covered end to end by
`ClipLinkTest::undoingAnAddedGroupMemberRestoresItsPriorNotesAndMembership`
(`tests/src/core/ClipLinkTest.cpp:381-422`): after linking A with newcomers C and D,
one `control.undo` leaves A/B linked, C and D unlinked, and C and D back to their own
prior notes.

A truly replayable inverse for the command would need a way to unlink N members and
restore their prior content without mirroring — i.e. new arguments or a new verb on
`clip.link_create` / `clip.link_remove`, which is rule 8 (ControlCommandsSnapshot canary,
the parent's call). Declined; no product code changed. The report's own fallback
("or direct clients to a complete journal undo") is what the mechanism string already
states: `ClauseClipLinkJournalled`, `include/ControlClipLinkSupport.h:84-89`.

Commit: none

Test: ClipLinkTest::undoingAnAddedGroupMemberRestoresItsPriorNotesAndMembership (pre-existing)

Red/green: `cd build/tests && ctest -R '^ClipLinkTest$' --output-on-failure` EXIT=0

### Cluster 2760 — Link-create's advertised inverse does not restore adopted content

Verdict: DUPLICATE

Evidence: same handler, same inverse line and same root cause as Cluster 1035 in this
batch — the `__transaction` recorded at `src/core/ControlCommandsClipLink.cpp:227-232`
names one `clip.link_remove` of the anchor, which cannot express "unlink every newcomer
and restore their adopted content". It is the content facet of 1035's membership facet,
with the identical fix and the identical blocked reason, so it is recorded once under
1035. Its premise is also already contradicted end to end by the same test as 1035
(`tests/src/core/ClipLinkTest.cpp:403-421`: C's 64 and D's 66 are both restored by one
`control.undo`).

Commit: none

Test: none

Red/green: none

### Cluster 1036 — `clip.link_remove` inverse can overwrite the removed clip's content

Verdict: SKIPPED-SCOPE

Evidence: the claim is true only of a by-hand replay of the recorded inverse, and that
replay is not the undo. As in Cluster 1035, `transactionPayload()` writes no `applies`
key, so `control.undo` reverses through the ProjectJournal, never through
`clip.link_create` and therefore never through `ClipLinks::mirrorContent()`. I measured
the journal path directly with a temporary case in `ClipLinkTest` (added, run, then
reverted — not committed, since no product defect was found): a member removed from a
three-member group and undone came back with its ORIGINAL group id and its anchor's
group membership intact, and a two-member group dissolved by `clip.link_remove` and
undone came back with the SAME group id on both clips — not a freshly allocated one —
which is the report's second, sharper claim. So no content is overwritten and no new
group id appears on the undo path.

A replayable inverse would have to restore the removed clip's membership, group id and
prior note list without mirroring, which again means new arguments or a new verb —
rule 8. No product code changed.

Commit: none

Test: temporary `ClipLinkTest::bunnyVerifyUnlinkUndoRestoresMembershipAndContent`
(measurement only, reverted; tree is clean)

Red/green: measurement `cmake --build build --target ClipLinkTest` EXIT=0,
`ctest -R '^ClipLinkTest$'` EXIT=0 after the case passed; the reverted tree was rebuilt
and re-run green as `ctest -R '^ClipLinkTest$'` EXIT=0.

## Leftovers

- Clusters 1035 and 1036 are open for the parent as a *provenance truthfulness* item,
  not an undo bug: `clip.link_create` / `clip.link_remove` record an inverse op that
  would be lossy if a client replayed it verbatim, and no in-command fix exists short
  of a verb or argument change (rule 8).
- Clusters 1029 / 1030 stay folded into batch-01's Cluster 2752 (consistency item,
  no defect demonstrated).
- Nothing half-done: no product file was modified on this batch, both gates are
  EXIT=0, and `git status --short` prints nothing.