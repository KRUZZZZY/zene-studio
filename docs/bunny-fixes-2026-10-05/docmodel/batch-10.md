# Bunny fixes — docmodel batch 10 (2026-10-05)

Lane: DataFile, DocumentIndex, provenance, automation, track folders. Branch
`bugs/bunny-docmodel-2026-10-05`. Five findings, all in `src/tracks/TrackFolder.cpp`.

### Cluster 4539 — Rejoining a routing folder can restore a stale channel assignment

Verdict: FIXED

Evidence: `routeChild()` appended a new `ChannelBinding` with `push_back` for a
child that had already left the folder and rejoined, while `savedChannelFor()`
(`src/tracks/TrackFolder.cpp:264`) returns the FIRST match for a child id — so the
release undid the older assignment. `childUnlinked()` deliberately keeps the old
binding (`src/tracks/TrackFolder.cpp:241-248`), which is what let the list hold two
entries for one child. A binding is now REPLACED in place by the new
`TrackFolder::recordBinding()` (`src/tracks/TrackFolder.cpp:312`), so a release
restores the channel the child was on when it last joined.

Commit: 4029b8edb

Test: TrackFolderTest::rejoiningARoutingFolderKeepsTheNewestSavedChannel

Red/green: red `cmake --build build --target zene TrackFolderTest` → EXIT=0,
`cd build/tests && ctest -R '^TrackFolderTest$' --output-on-failure` → EXIT=8
(18 passed, 1 failed: `'model->value() == whileOut' returned FALSE`). Green: same
ctest → EXIT=0; the suites covering the changed file
(`ctest -R '^(TrackFolderMenuTest|SessionPerformTest|UnclaimedTrackTypeTest|AudioAnalysisTest)$'`)
→ EXIT=0. Gates: `bash tests/complexity-gate.sh --check` → EXIT=0,
`bash tests/file-length-gate.sh --check` → EXIT=0.

### Cluster 4536 — Reassigning a child's mixer channel breaks folder routing

Verdict: SKIPPED-SCOPE

Evidence: the reported state is real — the only observers of a track's
mixer-channel model are the track's own `updateMixerChannel()` slot
(`src/tracks/InstrumentTrack.cpp:113`, `src/tracks/SampleTrack.cpp:63`), and no
bounded search finds any connection from a child's model back to its folder, so a
child moved out of the folder channel by hand stops feeding a folder that still
reports routing enabled (`src/tracks/TrackFolder.cpp:366`). Both remedies the
finding offers are owner decisions, not repairs: re-pointing the child whenever
the user picks another channel makes the folder fight the mixer menu and needs a
connection lifetime owned by the folder, while refusing the reassignment denies a
legitimate user action and changes what the channel command answers. Neither is
behaviour-preserving, so no code was changed.

Commit: none

Test: none

Red/green: none

### Cluster 4537 — Releasing routing leaves children with an invalid channel range

Verdict: FIXED

Evidence: `routeChild()` widens the child's model so it can hold the folder's
channel (`src/tracks/TrackFolder.cpp:294-297`), but `releaseRouting()` only
restored the VALUE via `unrouteChild()`; the deleted index stayed selectable, and
the mixer has no channel there. The release now calls the new
`TrackFolder::shrinkChildRange()` (`src/tracks/TrackFolder.cpp:397,406`) after the
delete, putting each child's range back to `numChannels()-1` — the same thing
`MixerView::updateMaxChannelSelector()` (`src/gui/MixerView.cpp:385`) does for
every track when the channel list changes.

Commit: 82ac99553

Test: TrackFolderTest::releasingRoutingTakesTheChildRangeBackOffTheDeletedChannel

Red/green: red `cmake --build build --target zene TrackFolderTest` → EXIT=0,
`cd build/tests && ctest -R '^TrackFolderTest$' --output-on-failure` → EXIT=8
(19 passed, 1 failed on the `maxValue` comparison). Green: same ctest → EXIT=0;
`ctest -R '^(TrackFolderMenuTest|SessionPerformTest|UnclaimedTrackTypeTest|AudioAnalysisTest)$'`
→ EXIT=0. Gates: `complexity-gate.sh --check` → EXIT=0,
`file-length-gate.sh --check` → EXIT=0.

### Cluster 4540 — Renaming a routing folder leaves its mixer channel label stale

Verdict: FIXED

Evidence: the name was copied onto the channel exactly once, in `enableRouting()`
(`src/tracks/TrackFolder.cpp:362`), and nothing carried a later rename across —
even though that name is what the channel's caption, tooltip and LCD read
(`src/gui/MixerChannelView.cpp:114,295,409,543`) and what the project saves for it
(`src/core/Mixer.cpp:2244`). `TrackFolder::setName()` (`src/tracks/TrackFolder.cpp:422`)
now forwards the new name to the channel while the folder is routing; it overrides
`Track::setName` (`include/Track.h:409`), so both the GUI and the control layer get it.

Commit: 578b70aa2

Test: TrackFolderTest::renamingARoutingFolderRenamesItsMixerChannel

Red/green: red `cmake --build build --target zene TrackFolderTest` → EXIT=0,
`cd build/tests && ctest -R '^TrackFolderTest$' --output-on-failure` → EXIT=8
(20 passed, 1 failed: `'mixerChannel->m_name == renamed' returned FALSE`). Green:
same ctest → EXIT=0; `ctest -R '^(TrackFolderMenuTest|SessionPerformTest|UnclaimedTrackTypeTest|AudioAnalysisTest)$'`
→ EXIT=0. Gates: `complexity-gate.sh --check` → EXIT=0,
`file-length-gate.sh --check` → EXIT=0.

### Cluster 4535 — Routing a parent folder does not route nested descendants

Verdict: SKIPPED-SCOPE

Evidence: the mechanics are as reported — `enableRouting()` visits only direct
children (`src/tracks/TrackFolder.cpp:366`) and a folder has no mixer-channel
model of its own (`Track::mixerChannelModel()` returns nullptr by default,
`include/Track.h:338`; `TrackFolder` declares no override), so `routeChild()`
returns at `src/tracks/TrackFolder.cpp:285` and a nested track is never reached.
Nesting is deliberately allowed (`canHold` refuses only self, another container
and a cycle, `src/tracks/TrackFolder.cpp:209`), so this is a routing SEMANTICS
decision, not a repair: routing descendants recursively would send the inner
track to the outer folder's channel and leave the inner folder summing nothing
while reporting routing enabled, and refusing a folder that contains folders
would remove an existing capability. The owner has to choose; no code was changed.

Commit: none

Test: none

Red/green: none

Leftovers: clusters 4536 and 4535 are left for the owner as a routing-policy
decision (enforce-or-refuse a child's manual channel change; and what nested
folders mean for a routed parent). All three fixes in this batch touch only
`src/tracks/TrackFolder.cpp`, `include/TrackFolder.h` and the existing
`tests/src/core/TrackFolderTest.cpp` — no new test file, no control-command
schema change, so the ControlCommandsSnapshot canary is untouched.