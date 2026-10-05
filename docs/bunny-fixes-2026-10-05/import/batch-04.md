# Bunny import batch-04 — DAWproject import

Six deduplicated findings (v2), each verified against the tree at `d44b6b431` before anything
was touched. Five were changed, one was pinned with a guard test instead of a fix.

### Cluster 3100 — Duplicate track IDs can attach clips to the wrong track
Verdict: FIXED
Evidence: `src/core/DawProjectReadTracks.cpp:317` inserted every non-empty `<Track>` id into
`state.trackIndexById` (`IdMap = QMap<QString,int>`, `DawProjectReadShared.h:97`) with no
duplicate check, so a second track of the same id silently won the lookup and a
`<Lanes track="...">` naming it resolved to the later track. The refusal now goes through
`insertTrackIndex()`, which records `state.error` naming the id — the reader already fails any
recorded error.
Commit: ee1e3ceac
Test: DawProjectInterchangeRoundTripTest::duplicateTrackIdsAreRefused
Red/green: `ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` → EXIT=8 (1 failed:
`'!dawProjectModelFromXml(duplicate, ...)'` returned FALSE) → same command EXIT=0 (21 passed).
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 2216 — Note channel and release velocity are silently discarded
Verdict: FIXED (release velocity); the note CHANNEL is left as an accepted loss
Evidence: the release-velocity half was real. `DawProjectSession.cpp:283` set
`noteEntry.rel = noteEntry.vel` on export and the apply loop ignored `rel` entirely, so a file's
note-off velocity was replaced by its note-on velocity on the first import — while the engine
does carry one (`Note::releaseVelocity` / `setReleaseVelocity`, `include/Note.h:187,411`, saved as
`relvel` in `src/core/Note.cpp:377`). Apply now sets it when `rel` differs from `vel`; export
writes the note's own value, falling back to `vel` for a note that carries none.
The channel half is NOT fixable here: no engine `Note` has a per-note MIDI channel, and the only
way to make the loss visible would be a new `DawProjectLossReport` counter, which adds a key to
the `loss` object of a command result — a result-schema change (rule 8), the parent's canary.
Verdict for that half is therefore left unchanged and stated here rather than coded.
Commit: 5e2905ea7
Test: DawProjectInterchangeRoundTripTest::releaseVelocitySurvivesAnImportAndExport
Red/green: same ctest → EXIT=8 (1 failed: "the release velocity came back as 0.8, not 0.2") →
EXIT=0. Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 4134 — Folder loss count counts folders instead of lost children
Verdict: FIXED
Evidence: `DawProjectSession.cpp:295` incremented `folderChildrenLost` once per folder track, so
`folder_children_lost` counted FOLDERS while the reader half of the same counter
(`DawProjectReadTracks.cpp:331`, `if (depth > 0)`) increments once per nested track — a count of
lost parent-child relations. An empty folder reported one lost child; a folder of five reported
one. `lostFolderChildren()` now returns `TrackFolder::childCount()`, which derives membership
from each track's `parentFolder()`.
Commit: cf45503d1
Test: DawProjectInterchangeRoundTripTest::theFolderLossCountIsTheNumberOfLostChildren
Red/green: same ctest → EXIT=8 (1 failed: Compared values are not the same, 2 != 3) → EXIT=0
(22 passed). Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 5069 — Failed imports can leave the session partially replaced
Verdict: FIXED (the residual gap; the tempo-map half was already fixed)
Evidence: the tempo/metre arm is already built before the song is touched
(`DawProjectSession.cpp:469-473`, with the comment that a map-capacity refusal leaves tracks and
global settings intact), so that half of the finding was already handled. The remaining hazard
was real: `createTrackOfType` can return nullptr for the types `Track::create`'s switch leaves
falling through to `default: break` (`src/core/Track.cpp:361-377`, `Event` and `Video`), and that
was discovered AFTER `song->clearAllTracks()` (`DawProjectSession.cpp:477`) and after the tempo,
metre and mixer were rewritten, so the refusal returned with the tracks gone.
`allTrackTypesCreatable()` now asks about every track before the first mutation.
Commit: 3c1db0916
Test: DawProjectInterchangeRoundTripTest::aTrackTypeTheEngineCannotCreateIsRefusedBeforeAnyMutation
Red/green: same ctest → EXIT=8 (1 failed: `song->getTempo()` 90, expected 137) → EXIT=0.
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 5068 — Import leaves mixer channels from the previous project
Verdict: FIXED
Evidence: `DawProjectSession.cpp:481-489` created and updated the strips the model names and never
removed the ones it did not, so a four-strip session importing a one-strip file kept three strips
of the previous project — names, faders, mute, solo — as though the document described them. The
undo path already deleted them (`ControlCommandsDawProject.cpp:218-221`, the restoreSession loop),
so applying was the odd one out. `removeMixerChannelsPast()` deletes them top-down after the update
loop, keeping the master at index 0.
Commit: 9ae83d742
Test: DawProjectInterchangeRoundTripTest::importRemovesMixerChannelsTheModelDoesNotName
Red/green: same ctest → EXIT=8 (1 failed: Compared values are not the same, 4 != 1) → EXIT=0
(23 passed); `ctest -R '^(MixerChannelPanTest|TrackFolderTest)$'` → EXIT=0.
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 3137 — Large timeline positions overflow LMMS ticks on import
Verdict: ALREADY-FIXED (pinned with a guard test; no product change)
Evidence: the claim is that a beat converts to `qint64` and is narrowed to `tick_t` without a
range check (`DawProjectSession.cpp:573,575,583,585`), so 50,000,000 beats would overflow. It
cannot: every beat in a file passes through `ticksFromBeats()` (`DawProjectReadShared.h:189-197`),
which clamps into ±INT32_MAX and counts the value in `rounded_times`, and `pointTime`
(`DawProjectRead.cpp:110-123`) runs through it too — so what reaches the `static_cast<tick_t>` is
already inside the tick_t range. This is the same conclusion batch-03 reached for cluster 4127
(citing `fb9b053d4`), and the existing cases `beatConversionRejectsValuesThatAreNotBeats` and
`importRefusesAnUnrepresentableBeat` cover the conversion side. The new test pins the clamp so a
future simplification of the reader cannot remove it silently.
Commit: b25620f73
Test: DawProjectInterchangeRoundTripTest::hugeBeatPositionsCannotEnterTheModelOutOfTickRange
Red/green: none — the behaviour pinned already held.
`ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` → EXIT=0 (26 passed).
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

Leftovers: the note-CHANNEL half of cluster 2216 is still uncounted in the loss report — it needs a
new `DawProjectLossReport` counter, which changes the `loss` object inside a `dawproject.*` result
and so is the parent's canary, not this lane's. Nothing else from this batch is half-done; the
tree was clean before and after every commit above.