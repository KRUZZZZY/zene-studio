IMPLEMENTED

# cluster Q050-019 - Chord progression panel

Unit: P0, M, L07. Goal: browse named progressions, see suggestions for the current scale, preview
them, and write a chosen progression from the interface. Every acceptance criterion is met and
proven by a test run on this worktree; the exact commands and exit codes are below.

## What was built

- The read that feeds the panel: `chord.progression_list` gained optional `scale`/`root`/`octave`
  arguments and reports `suggestions[]` - the catalogue spelt in the key, each entry's own chords
  named by the same vocabulary the generator writes with
  (`src/core/ControlCommandsChord.cpp:121`, `:218`). No new command id; still `NotMutating`.
- The panel: `include/ChordProgressionPanel.h`, `src/gui/ChordProgressionPanel.cpp` -
  `gui::ChordProgressionPanel`, a view over `chord.progression_list` (suggestions) and
  `chord.progression_generate` (Write).
- The command and its menu entries: `window.progression`
  (`src/gui/ControlCommandsShell.cpp:313`), the Edit menu action
  (`src/gui/MainWindow.cpp:404`) and the footer Tools entry
  (`src/gui/shell/ShellToolsMenu.cpp:78`).
- The A16 row (`src/core/ControlReversibilityTableShell.cpp:112`, `NotMutating`) and the re-taken
  histogram block in `docs/RELEASE-NOTES-v0.3.0-alpha.md` (`rows=838 ... not_mutating=357`).
- Tests: `tests/src/gui/ChordProgressionPanelTest.cpp`,
  `tests/control-chord-progression.py` (registered `ControlChordProgression`), and a new
  `suggestionsSpellTheCatalogueInTheSelectedScale` slot in `tests/src/core/ControlChordWriteTest.cpp`.

## Acceptance

| Criterion | Proof | Exact command | Exit |
| --- | --- | --- | --- |
| The Edit view opens a progression panel | the panel is opened by the registered `window.progression` command (Edit menu `MainWindow.cpp:404`); its `agent_surface` sweep reports `window.progression = {"status":"ok"}` | `ctest -R '^agent_surface$' --output-on-failure` | 0 |
| ... and shows suggestions for the selected scale | `ChordProgressionPanelTest::thePanelShowsSuggestionsForTheSelectedScale` (`setScale("Major",0)` -> >=8 rows; `chordsFor("I-V-vi-IV") == "Major, Major, minor, Major"`), and `ControlChordWriteTest::suggestionsSpellTheCatalogueInTheSelectedScale` (the wire `suggestions[]`) | `ctest -R '^(ChordProgressionPanelTest|ControlChordWriteTest)$' --output-on-failure` | 0 |
| A control-socket test selects a progression, writes its notes, and verifies one undo restores the clip | `ControlChordProgression`: over `--control-socket`, spells the catalogue, writes 12 notes (`notes_replaced 1`, first step exactly {60,64,67}), reads them back through `roll.get_state`, and one `control.undo` restores the clip to its one note; the A16 record is `true_inverse` on the clip checkpoint | `ctest -R '^ControlChordProgression$' --output-on-failure` | 0 |
| The UI calls the same registered command as the headless workflow | `ChordProgressionPanelTest::theWriteButtonRunsTheRegisteredCommand` (button property `chord.progression_generate`) and `writingThroughThePanelIsOneUndoStep` (write + ONE undo) | `ctest -R '^ChordProgressionPanelTest$' --output-on-failure` | 0 |
| Documents the remaining bounds | `docs/CHORD-TRACK.md` §6b, `docs/KNOWN-LIMITATIONS.md` (text preview not an audition; target is the shell's selected clip; the panel leaves the chord track untouched; pattern/velocity/seed/variation not exposed) | `git grep -n "progression panel" docs/CHORD-TRACK.md` (review) | - |
| No regression in the chord group or the A16 table | `ctest -R 'Chord\|ReversibilityUndo'` (9/9) | `ctest -R 'Chord\|ReversibilityUndo' --output-on-failure` | 0 |
| Snapshot and histogram stay true | `ControlCommandsSnapshot` (829 ids / 76 groups, no `wasm.*`) and `ReversibilityContractTest` | `ctest -R '^(ReversibilityContractTest\|ControlCommandsSnapshot)$' --output-on-failure` | 0 |

## Commands run and exit codes (all on this worktree)

```
cmake --build build --target zene ControlChordWriteTest -- -j4            EXIT=0   (build-2.log)
cmake --build build --target ChordProgressionPanelTest ControlChordWriteTest -- -j4  EXIT=0 (build-4.log)
ctest -R '^(ChordProgressionPanelTest|ControlChordWriteTest)$'            EXIT=0   (test-2.log)
ctest -R '^ControlChordProgression$'                                      EXIT=0   (test-4.log)
ctest -R '^(ReversibilityContractTest|ControlCommandsSnapshot)$'          EXIT=0   (test-5.log)
ctest -R 'Chord|ReversibilityUndo'                                        EXIT=0   (test-7.log)
ctest -R '^agent_surface$'                                                EXIT=0   (test-agentsurface.log)
python3 tests/doc-figures-gate.py                                         EXIT=0
bash tests/fork-sources-gate.sh                                           EXIT=0
bash tests/all-sources-reproduce.sh                                       EXIT=0
bash tests/no-upstream-regression-gate.sh                                 EXIT=0
tests/scripted/check-namespace                                            EXIT=0
bash tests/no-tautology-gate.sh                                           EXIT=0
bash tests/complexity-gate.sh --check                                     EXIT=1   (train-5 red; no file of this lane newly listed)
bash tests/file-length-gate.sh --check                                    EXIT=1   (train-5 red; no file of this lane newly listed)
```

The two red gates are red on train 5 by design (gate 4: `XyPadPairTest`, `DawProjectReadTracks`,
`ControlCommandsMixerTools`, `ControlCommandsMixerStrip`, `ProjectRevisions`,
`ControlCommandsShortcuts`; gate 7: `SmfInterchangeReader`, `TakeLane` and 12 tests). Grepping the
gate logs for `ChordProgression`/`control-chord` finds nothing: none of this lane's files is newly
listed in either.
