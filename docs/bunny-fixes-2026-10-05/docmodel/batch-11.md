# Bunny fixes — docmodel batch 11 (2026-10-05)

Lane: DataFile, DocumentIndex, provenance, automation, track folders. Branch
`bugs/bunny-docmodel-2026-10-05`. This batch is the rework the parent reviewer
asked for: commit `197a8955c` ("deleting a folder never gave its mixer channel
back") was REJECTED because it traded a leak for stale indices.

### Cluster 5421 — Mixer::deleteChannel renumbered tracks but not TrackFolders

Verdict: FIXED

Evidence: `Mixer::deleteChannel` renumbers what it deletes around: the VCA
groups (`src/core/Mixer.cpp:1116`) and the instrument and sample channel models
(`src/core/Mixer.cpp:1138-1170`). A routing folder's own `m_mixerChannel`
(`include/TrackFolder.h:238`) is an index into the very same
`Mixer::m_mixerChannels` vector and was in no such walk, so with two routing
folders on channels 3 and 4, deleting the first erased channel 3 while the second
kept recording 4 — an index that by then named the strip above its own. Measured:
`Mixer::deleteChannel` indexes `m_mixerChannels[index]` unchecked, so the second
folder's later deletion removes a strip it does not own. `Mixer::deleteChannel`
now tells every folder in the song's and the pattern store's track lists about
the deletion, BEFORE the track walk (`src/core/Mixer.cpp:1127-1136`), and the new
`TrackFolder::channelDeleted()` (`src/tracks/TrackFolder.cpp:424`) applies the
same rule — equal → −1, above → minus one.

Commit: fd74af9d8

Test: TrackFolderTest::deletingOneRoutingFolderRenumbersTheOther

Red/green: red — the source fix reverted, the new tests kept —
`cmake --build build --target zene TrackFolderTest` → EXIT=0,
`cd build/tests && ctest -R '^TrackFolderTest$' --output-on-failure` → EXIT=8
(21 passed, 2 failed: `'upper->mixerChannel() == upperChannel - 1' returned
FALSE`). Green: same ctest → EXIT=0 (24 passed);
`ctest -R '^(TrackFolderMenuTest|UnclaimedTrackTypeTest|SessionPerformTest|AudioAnalysisTest)$'`
→ EXIT=0. Gates: `bash tests/complexity-gate.sh --check` → EXIT=0,
`bash tests/file-length-gate.sh --check` → EXIT=0.

### Cluster 5422 — Undo of a routing folder's delete adopted a channel that no longer exists

Verdict: FIXED

Evidence: the journal restores a deleted track from its own XML through
`control::restoreTrackFromXml` → `Track::create`
(`src/core/ControlStructuralSupport.cpp:231`), and `TrackFolder`'s
`loadTrackSpecificSettings` adopts `mixch` unchecked as an index
(`src/tracks/TrackFolder.cpp:568-569`) — it is an index READ FROM A FILE. The
delete this restore reverses gave that channel back to the mixer (the channel
197a8955c made ~TrackFolder release), so the restored folder named a strip it does
not own and routed its children into somebody else's channel. Measured red: the
restored folder recorded `mixerChannel() == numChannels()`, an index one past the
end. The restore step now asks a restored routing folder to take a channel that
really exists (`src/core/ControlStructuralSupport.cpp:241`), and the new
`TrackFolder::ensureRoutingChannel()` (`src/tracks/TrackFolder.cpp:457`) creates
one and names it after the folder rather than adopting the stale index.

Commit: fd74af9d8

Test: TrackFolderTest::undoingARoutingFolderDeleteRestoresItsChannel

Red/green: red — source fix reverted, tests kept —
`cd build/tests && ctest -R '^TrackFolderTest$' --output-on-failure` → EXIT=8
(`'typed->mixerChannel() < static_cast<int>(Engine::mixer()->numChannels())'
returned FALSE`). Green: same ctest → EXIT=0;
`ctest -R '^ControlUndoStructuralTranscript$'` → EXIT=0 (the end-to-end
track add/remove/undo transcript, which is the suite that owns the journal restore
this changed); `ctest -R '^(ReversibilityUndoTest|ControlEditCommandsTest)$'` →
EXIT=0. Gates EXIT=0 both.

### Cluster 5423 — TrackFolder::releaseRouting() had the same latent problem

Verdict: DUPLICATE

Evidence: `releaseRouting()` (`src/tracks/TrackFolder.cpp:383`) is the other
caller that hands a folder's channel back to the mixer, so it reached the same
un-renumbered `Mixer::deleteChannel` walk. It is not a second defect: the folder
that owns the deleted index is the one deleting it, so its own index is the
correct one, and the fix for cluster 5421 renumbers the OTHER folders it damaged.
Fixed by the same commit and covered by the same regression test, which deletes a
routing folder (the ~TrackFolder path) and then deletes the survivor.

Commit: fd74af9d8

Test: TrackFolderTest::deletingOneRoutingFolderRenumbersTheOther

Red/green: none separately — see cluster 5421 (same ctest run, EXIT=8 red /
EXIT=0 green).

Leftovers: undoing a routing folder's delete restores the FOLDER and its channel,
but not its MEMBERSHIP — the children were never deleted, so no child checkpoint
is replayed and each one's `folder` attribute is never re-applied; they come back
at the container root. That is a separate defect in the journal's structural
restore (it is derived state, so the folder's own XML cannot carry it) and is
untouched here. `ensureRoutingChannel()` routes whatever children ARE present, and
`childLinked()` routes the ones that arrive later, so the routing itself is
correct once membership is fixed. Also untouched: a project LOAD whose `mixch`
names a channel the mixer's own saved list no longer has — the same adoption, on
the loader's path rather than the journal's.