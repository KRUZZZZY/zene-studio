IMPLEMENTED
Q050-015 Articulation map editor: feature ported onto train 5 (base de5a85a96), branch feat/050-Q050-015-oc.
Round 2 (r2) addresses the r1 review: the map and note-index changes are now one InstrumentTrack critical
section, and two regression tests pin the atomic edit transition.

Acceptance:
1. Editor creates, reorders, renames, removes entries and saves with project. Evidence: tests/src/gui/ArticulationMapEditorTest (QTEST_MAIN); cmd `ctest -R '^ArticulationMapEditorTest$' --output-on-failure`, EXIT 0. UI entry Articulations... in src/gui/shell/ShellCommandMenus.cpp and Edit Articulations... in src/gui/noteeditor/NoteEditorMenus.cpp; every edit is one track.articulations_set.
2. Playback test verifies each selected articulation sends keyswitch/program/controller before note. Evidence: tests/src/core/ArticulationPlaybackTest (QTEST_GUILESS_MAIN, 14 slots) with logging plugin tests/src/plugins/MpeTestConsumer.cpp; cmd `ctest -R '^ArticulationPlaybackTest$' --output-on-failure`, EXIT 0, env QT_QPA_PLATFORM=offscreen. Includes the r2 atomicity tests theEditNeverAnnouncesANewMapWithOldNoteIndices and theRemapAnnouncesItsClipsWithTheTrackUnlocked.
3. Headless command and undo produce the same map as UI. Evidence: ArticulationMapEditorTest::theEditorAndTheCommandLeaveTheSameMap, ::everyEditIsOneUndoStep and ::notesFollowTheirArticulation (the UI/command equivalence proof), plus the ArticulationPlaybackTest remap/undo slots; cmd `ctest -R '^ArticulationMapEditorTest$' --output-on-failure`, EXIT 0. (Corrected in round 4: this line used to cite NotePlaybackTest, which does not test the equivalence.)

r2 change: applyArticulationEdit (src/core/ArticulationRemap.cpp:86) replaces the map and remaps the notes under ONE InstrumentTrack::lock (initial edit, undo, redo); the touched clips' dataChanged is emitted after unlocking (announceArticulationEdits, src/core/ControlCommandsNotePlayback.cpp:174). Verified: the two regression tests fail against a deliberately re-split build (log 06-build-split2 / test run 07-test-split2: "Legato" vs "Staccato"; tryLock returned FALSE) and pass against the one-lock build.

Modified registered commands: track.articulations_set gains optional `from` (a schema change, captured in the regenerated commands snapshot) and replies `notes_remapped`; the snapshot's command count is unchanged at 828 ids / 76 groups; doc-figures-gate.py EXIT 0.

Gates (r2): complexity-gate --check EXIT 1 and file-length-gate --check EXIT 1, each with only the same train-5 regression listings as ../zene-cachyos (diff of REGRESSION lines EXIT 0); fork-sources-gate EXIT 0, all-sources-reproduce EXIT 0, no-upstream-regression-gate EXIT 0, scripted/check-namespace EXIT 0, no-tautology-gate EXIT 0, doc-figures-gate.py EXIT 0.

Also pass: ArticulationMapEditorTest, NotePlaybackTest, MpePlaybackTest, ControlRegistryTest, ControlSurfaceReferenceTest, NoteEditorTest, ShellTest, AgentSurfaceReportTest, ReversibilityContractTest.
