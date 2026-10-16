IMPLEMENTED
Q050-015 Articulation map editor: feature ported onto train 5 (base de5a85a96), matching feat/050-Q050-015-articulation-maps.

Acceptance:
1. Editor creates, reorders, renames, removes entries and saves with project. Evidence: tests/src/gui/ArticulationMapEditorTest (QTEST_MAIN), cmd `ctest -R '^ArticulationMapEditorTest$' --output-on-failure`, EXIT 0 (2/2 tests; included suite). New files: src/gui/noteeditor/ArticulationMapEditor.{h,cpp}; UI entry Articulations... in src/gui/shell/ShellCommandMenus.cpp and Edit Articulations... in src/gui/noteeditor/NoteEditorMenus.cpp.
2. Playback test verifies each selected articulation sends keyswitch/program/controller before note. Evidence: tests/src/core/ArticulationPlaybackTest (QTEST_GUILESS_MAIN, 12 slots) with logging plugin tests/src/plugins/MpeTestConsumer.cpp; cmd `ctest -R '^ArticulationPlaybackTest$' --output-on-failure`, EXIT 0. Runtime env QT_QPA_PLATFORM=offscreen.
3. Headless command and undo produce the same map as UI. Evidence: ArticulationMapEditorTest::theEditorAndTheCommandLeaveTheSameMap and ArticulationPlaybackTest remap/undo tests; NotePlaybackTest passes with run `ctest -R '^NotePlaybackTest$' --output-on-failure`, EXIT 0.

Modified registered commands: track.articulations_set gains optional `from` and replies `notes_remapped`; snapshot regenerated (828 ids / 76 groups).

Gates: complexity/file-length show only the same pre-existing train 5 regressions as ../zene-cachyos; fork-sources-gate, all-sources-reproduce, no-upstream-regression-gate, check-namespace, no-tautology-gate, doc-figures-gate all EXIT 0.
