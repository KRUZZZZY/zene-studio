# Bunny import lane — batch-02 (2026-10-05)

Lane: DAWproject import (mixer snapshots in import undo, undo byte accounting, typed
refusal preservation). Branch `bugs/luna-import-2026-10-05`.

Six findings: four were REAL and are fixed by four new commits (one of them covering two
clusters), one is a duplicate of another in this batch, one is a false positive. The false
positive and the duplicate are argued from source rather than changed.

All six were read in the current tree first: every line number below is the tree as of
`011851e4d`, not the revision named in the report.

### Cluster 4125 — Marker-only tracks are silently treated as instruments

Verdict: FIXED

Evidence: `dawProjectTypeForContentType()` fell through to `instrument` for a track whose
`contentType` names only content this module cannot carry, and never set `lost`
(`src/core/DawProjectModel.cpp:139-165` before the fix). `markers` is the vocabulary's own
value for exactly such a track, and `parseLanes()` skips the `<markers>` lane without
reading its content (`src/core/DawProjectReadTracks.cpp:216-222`), so the track arrived as
an empty, apparently-supported instrument while `unmapped_track_types` stayed 0
(`DawProjectReadTracks.cpp:312`). The fix sets the existing `lostContentType` /
`unmapped_track_types` mechanism; a track that declares nothing stays lossless, because
the writer emits an empty `contentType` for an event or video track (LOSSY #5) and
flagging that would misreport every such export.

Commit: ed3944726

Test: DawProjectInterchangeRoundTripTest::markerOnlyTracksReportTheirContentAsLost

Red/green: red `cd build/tests && ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` -> EXIT=8
(`'model.tracks[0].lostContentType' returned FALSE`); green after the fix -> 15 passed / 1 failed, the
remaining failure being cluster 4124. `bash tests/complexity-gate.sh --check` -> EXIT=0,
`bash tests/file-length-gate.sh --check` -> EXIT=0.

Note: adding the loss flag took `dawProjectTypeForContentType` to CCN 11 and failed the complexity
ratchet, so the four-value mapping table was split into `engineTypeForContentValue()`. The ratchet was
not re-anchored.

### Cluster 4124 — Mixed track content can make imported notes disappear

Verdict: FIXED

Evidence: the audio/automation/video/tracks table was checked before the notes case, so a
`contentType="notes audio"` track returned `sample` and never reached notes
(`src/core/DawProjectModel.cpp:149-160` before the fix). The reader still carried the
`<Notes>` clip, but `applyDawProjectModel` adds notes only inside
`dynamic_cast<MidiClip*>` (`src/core/DawProjectSession.cpp:511-527`) and a sample track has no
MIDI clip, so a read that succeeded imported zero notes and reported nothing lost. `notes` is now
checked first; a single-value `audio` track still becomes a sample track, and the audio half of
the mixed track was already counted as LOSSY #1 by `parseClips()`
(`DawProjectReadTracks.cpp:156-163`).

Commit: 7d6e4a162

Test: DawProjectInterchangeRoundTripTest::mixedNotesAndAudioTracksKeepTheirNotes

Red/green: red ctest -> EXIT=8 (`'read.tracks[0].typeName == QStringLiteral("instrument")' returned FALSE`);
green after the fix -> EXIT=0, all 16 cases in the executable pass. Both gates EXIT=0.

### Cluster 5072 — The model digest omits values that model equality compares

Verdict: FIXED

Evidence: the mixer digest line ended at `mute=` with no `audioChannels` and the clip line at
`name=` with no `color` (`src/core/DawProjectModel.cpp:228-231` and `:249-251` before the fix),
while `operator==` compares mixer `audioChannels` (`include/DawProjectInterchange.h:236`) and
clip `color` (`include/DawProjectInterchange.h:209`), and the writer/reader carry both. Two
models differing only in either therefore hashed identically. Both fields are now in the digest.

Commit: cc881c708

Test: DawProjectInterchangeRoundTripTest::theDigestSeparatesEveryEqualityRelevantField

Red/green: red ctest -> EXIT=8 (`'dawProjectModelDigest(recolored) != base' returned FALSE`); green after
the fix -> the digest case passes, 13 passed / 3 failed at that commit. Both gates EXIT=0.

### Cluster 4136 — Model digest misses clip color changes

Verdict: DUPLICATE

Evidence: identical defect and identical fix to cluster 5072 above, which the report also filed
under "digest omits mixer audioChannels and clip color". This cluster is the clip-color half of
the same single omission, at the same revision, and was fixed by the same change rather than a
second one. The regression test `theDigestSeparatesEveryEqualityRelevantField` asserts both
halves (clip color and mixer `audioChannels`) so neither can regress alone again.

Commit: cc881c708 (shared with 5072)

Test: DawProjectInterchangeRoundTripTest::theDigestSeparatesEveryEqualityRelevantField

Red/green: same red/green as cluster 5072.

### Cluster 5070 — The JSON model view drops note data and clip colors

Verdict: FIXED

Evidence: `dawProjectModelJson()` built each clip object from time, duration, play_start, name
and a note COUNT (`src/core/DawProjectModel.cpp:360-370` before the fix). Clip `color` and every
note field were absent, although the `dawproject.read` description promises "every track with its
clips and notes" (`src/core/ControlCommandsDawProject.cpp:382-383`) and docs/DAWPROJECT-INTERCHANGE.md
line 283 lists `model` as the whole model as JSON. The clip object now carries `color` and a
`notes` array with time, duration, channel, key, vel and rel, via a `clipJson()` helper so the
enclosing function did not grow. Adding keys inside the `model` object does not move the
ControlCommandsSnapshot canary: that result field is declared `objectProperty()`
(`ControlCommandsDawProject.cpp:406`), so the declared schema is unchanged, and
`tests/control-commands-snapshot.py` has no `dawproject` entry.

Commit: 0992c7962

Test: DawProjectInterchangeRoundTripTest::theJsonModelViewCarriesNotesAndClipColors

Red/green: red ctest -> EXIT=8 (`'clip.contains(QStringLiteral("color"))' returned FALSE`); green after the
fix -> 14 passed / 2 failed at that commit. Both gates EXIT=0.

### Cluster 427 — DAWproject reader accepts a nested `<Project>` as the root

Verdict: FALSE-POSITIVE

Evidence: `sawProject` is set by the top-level loop in `parseDocument`, but the branch is only
reachable for a start element the loop itself reads. Any other element falls to the final
`else { skipElement(reader); }` (`src/core/DawProjectRead.cpp:220`), and `skipElement`
consumes through that element's matching end tag (`src/core/DawProjectReadShared.h:130-133`).
So for an unrelated outer element containing a valid-looking `<Project>`, the outer element is
one start element the loop reads, matches no branch, and the whole subtree — including the
nested `<Project>` — is skipped in one go. The `Project` branch never sees it, `sawProject`
stays false, and the read is refused at `DawProjectRead.cpp:222-226`. The nesting depth the
finding describes is unreachable: `<Project>` can only be entered from the document element
itself, so there is no path on which a nested one is parsed as the root.

Commit: none

Test: none

Red/green: none. No product code was changed, so no red/green cycle was run. The existing refusal
path is covered by `DawProjectInterchangeRoundTripTest::refusalsAreTyped`, which was re-run green
as part of every cycle in this batch.

Leftovers: `dawProjectModelFromXml` has no explicit check that the document declares an
`<Application>` element, only that `<Application name=...>` is present when one is found
(`src/core/DawProjectRead.cpp:139-151`); a document with no `<Application>` at all is read with
an empty `applicationName`. This is NOT one of the six findings and no test or doc claim was
found that it contradicts, so it is recorded here rather than fixed in this lane.