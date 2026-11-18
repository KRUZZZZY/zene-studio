IMPLEMENTED

# Q050-294.3 — Melodic step-sequencer view, stage 3/3: seeded shape and random fills

`note.step_fill` (src/core/ControlCommandsNoteStepFill.cpp) re-pitches a MIDI clip's existing step columns in the scale (shape contour or seeded random, density-dropped), never adds notes, one MidiClip checkpoint per fill and none for a no-op; the Step Grid panel's Fill row dispatches the same registered command; branch feat/050-Q050-294.3-oc, tree clean at 99fa202d79 before this report was written.

## Acceptance

| # | Criterion | Test | What it proves | Command | Exit |
|---|-----------|------|----------------|---------|------|
| 1 | The same starting clip, settings and seed give identical note state | `StepGridFillTest::repeatableBySeed`, `StepGridFillTest::namedScaleOverridesTheProject`, `StepGridPanelTest::theFillRowFillsFromASeed`, `ControlFill` checks 1 and 3 | Two fills from the same clip and seed write the same positions, keys and lengths; a named scale overrides the project's; the grid's Fill row, given a seed, writes the same keys twice; over the socket the same call gives the same notes | `ctest -R '^StepGridFillTest$'` (log 012); `ctest -R '^(StepGridPanelTest\|StepGridFillTest)$'` (log 028); `ctest -R '^ControlFill$'` (log 021); run from build/tests | 0, 0, 0 |
| 2 | Generated notes appear in the piano roll | `StepGridPanelTest::aRandomFillEmptiesStepsAndThePianoRollShowsIt` (`NoteEditor::canvas()->noteRect` valid for every filled note); `ControlFill` (`roll.get_state`, `note.step_get_state` and `window.step_grid` agree by id, key and step) | The notes a fill writes are the clip's ordinary notes, so the piano roll paints each one; a random fill that empties steps shows exactly what is left | `ctest -R '^(StepGridPanelTest\|StepGridFillTest)$'` (log 028); `ctest -R '^ControlFill$'` (log 021) | 0, 0 |
| 3 | One Undo restores the pattern before the fill | `StepGridFillTest::oneUndoRestoresExactRows`; the GUI fill cases `StepGridPanelTest::theFillRowFillsFromASeed`, `aRandomFillEmptiesStepsAndThePianoRollShowsIt`, `theFillIsRefusedWithNoScale`; `ControlFill` check 1 (ids included) | One `control.undo` after a fill restores the exact note rows, ids included, and the undo depth goes up by exactly one per fill | `ctest -R '^StepGridFillTest$'` (log 012); `ctest -R '^(StepGridPanelTest\|StepGridFillTest)$'` (log 028); `ctest -R '^ControlFill$'` (log 021) | 0, 0, 0 |

Reverse run. The product hunks were reverted with the test files left alone and the binaries kept. The RED build (log 030) exits 0 and the RED ctest (log 031) exits 8: all three suites ran and failed. `StepGridPanelTest` reported three null-guard assertions (`contour`, `kind`, `fill` widgets missing); `StepGridFillTest` failed its cases; `ControlFill` marked 12 check lines FAILED (log 031, lines 102–134). The restored build (log 032) exits 0 and the restored ctest (log 033) exits 0 with 3/3 passed.

Neighbouring suites, all green on this tree: `ReversibilityContractTest|ControlCommandsSnapshot|agent_surface` (log 047, exit 0, 3/3); `StepGridFillTest|StepGridPanelTest|ControlFill|ControlCommandsSnapshot` (log 052, exit 0, 4/4); the gates (log 053, exit 0, RESULT: PASS); `feature_check.py` (log 054, exit 0, RESULT: PASS, 8/8 rows PASS).

## Commands run

| Label | Log | Exit | Shows |
|-------|-----|------|-------|
| build-p1b | 002 | 0 | `StepGridFillTest` linked |
| red-p1b | 008 | 8 | first RED, before the first commit: `StepGridFillTest` failed (`no command 'note.step_fill'`) |
| test-p2 | 012 | 0 | `StepGridFillTest` 1/1 passed |
| gates-p2 | 013 | 0 | fork-sources PASS (2528 fork-NEW, 1213 inherited, 49 tooling); all-sources REPRODUCES |
| test-p3 | 015 | 0 | `StepGridPanelTest` and `StepGridFillTest` 2/2 passed |
| test-p4 | 019 | 0 | `ControlNoteStep`, `ControlStepGrid`, `ControlFill` 3/3 passed |
| test-p5 | 021 | 0 | `ControlFill` 1/1 passed (non-empty random repeat) |
| test-p7 | 028 | 0 | `StepGridPanelTest` and `StepGridFillTest` 2/2 passed after the Fill row |
| reverse run GREEN | 029 | 0 | `StepGridPanelTest`, `StepGridFillTest`, `ControlFill` 3/3 passed |
| reverse run RED build | 030 | 0 | product hunks reverted; `StepGridPanelTest` rebuilt |
| reverse run RED ctest | 031 | 8 | all three suites ran and failed (see Acceptance) |
| reverse run restored build | 032 | 0 | product restored, rebuilt |
| reverse run restored ctest | 033 | 0 | 3/3 passed |
| snapshot-p9 | 035 | 0 | `932 id(s) across 78 group(s)` from the live binary |
| docfigures | 037 | 0 | `snapshot measures 932 ids / 78 groups`, RESULT: PASS |
| test-p10 | 047 | 0 | `ReversibilityContractTest`, `agent_surface`, `ControlCommandsSnapshot` 3/3 passed |
| docfigures | 048 | 0 | RESULT: PASS |
| build-p12 | 051 | 0 | `StepGridFillTest` built |
| test-p12 | 052 | 0 | `StepGridFillTest`, `ControlFill`, `ControlCommandsSnapshot`, `StepGridPanelTest` 4/4 passed |
| gates-p12 | 053 | 0 | fork-sources and all-sources REPRODUCE; RESULT: PASS |
| feature-p12 | 054 | 0 | feature_check RESULT: PASS, 8 rows PASS |

## Self-check

1. **Reverse run.** `ctest` log 031 exit 8 (RED), then log 033 exit 0 (restored); the RED build (030) and restored build (032) both exit 0. Hunks reverted, per the reverse-run record: `src/core/ControlCommandsNoteStepFill.cpp` removed; `src/core/CMakeLists.txt`, `src/core/ControlRegistryRegistrations.cpp` and `include/ControlRegistryGroups.h` taken from `68b20065f2^`; `include/StepGridPanel.h` and `src/gui/noteeditor/StepGridPanel.cpp` taken from `53582e6163^`. Test files were left untouched. `68b20065f2` also touches `src/core/ControlReversibilityTableArranger.cpp` and `tests/CMakeLists.txt`; those two were not in the revert list above, so they stayed in place during the reverse run. The reversibility row and the test registration stay because the tests must still exist, so the RED comes only from the absent command, the reverted registration, CMake and group header, and the reverted panel. Verified against `git show --stat 68b20065f2` (command file, registration, CMake, group header) and `git log` for the panel files (`53582e6163`, the Fill-row commit).

2. **Per-criterion test and exit code.** Criterion 1: `repeatableBySeed`, `namedScaleOverridesTheProject`, `theFillRowFillsFromASeed`, `ControlFill` checks 1 and 3; exit 0 in logs 012, 028, 021. Criterion 2: `aRandomFillEmptiesStepsAndThePianoRollShowsIt`, `ControlFill` roll/state agreement; exit 0 in logs 028, 021. Criterion 3: `oneUndoRestoresExactRows`, the three GUI fill cases, `ControlFill` check 1; exit 0 in logs 012, 028, 021. All test names are present in `tests/src/gui/StepGridFillTest.cpp` and `tests/src/gui/StepGridPanelTest.cpp`.

3. **One undo and reversibility.** Each fill takes one MidiClip checkpoint (`control::checkpointNoteEdit`, one call at `src/core/ControlCommandsNoteStepFill.cpp:345`, guarded by the `changes` flag (the same rule `note.step_set` uses; its other two mentions in the file are comments)), so the undo depth rises by exactly one per fill. A fill that changes nothing takes no checkpoint. The `note.step_fill` row is `RC::TrueInverse, true` at `src/core/ControlReversibilityTableArranger.cpp:146`. Asserted by `oneUndoRestoresExactRows` and `densityAndNoChangeTakeNoStep` (log 012, exit 0).

4. **Realtime.** `submit_check` flagged `NotePlayHandle` because `tests/src/gui/StepGridFillTest.cpp` includes `NotePlayHandle.h` (line 49) and calls `NotePlayHandleManager::init()` (line 172). Both are test-harness setup only. `git diff --name-only 68b20065f2^..HEAD` contains no `AudioEngine`, `NotePlayHandle` or `InstrumentTrack` file. The command edits Note keys and removals on a MidiClip on the control (GUI) thread and emits `dataChanged` from `finish()`, the same contract as `note.step_set`; the feature record states that nothing runs on the audio thread (`docs/050-features/Q050-294.3.md`, Known limits).

5. **Old projects and new data.** A fill changes only Note key state and removes Note entries, all already in the clip's serialized note list. The feature record adds no saved attribute and no command argument that is written to a project file, so old projects load and render as before and there is nothing new to round-trip. Stage 1 and 2 save/load behaviour is not touched by this branch's diff.

6. **Portability.** A grep of `src/core/ControlCommandsNoteStepFill.cpp`, `include/StepGridPanel.h` and `src/gui/noteeditor/StepGridPanel.cpp` finds no `std::filesystem`, no `windows.h`, no absolute paths, no mutex or file I/O. `std::uniform_int_distribution` appears only in a comment (line 36). Seeded draws use `std::mt19937` mapped by a 64-bit multiply (6 matches for `mt19937`). The grep does not check the Qt API surface; the Qt-6-only claim is not verified by it.

7. **Completeness through the real paths.** (a) GUI path: a musician opens the clip in the Step Grid (Edit > Step Grid..., or the "Open in Step Grid" menu item, which runs `window.step_grid`), sets Shape, Contour, Seed, Spread and Density, and clicks Fill. The test does exactly that, with `QTest::mouseClick` on `stepGridFill` (`StepGridPanelTest::theFillRowFillsFromASeed`, `theFillIsRefusedWithNoScale`, `aRandomFillEmptiesStepsAndThePianoRollShowsIt`; log 028, exit 0). (b) Agent path: the same `note.step_fill` over the socket on the real binary (`ControlFill`, log 021, exit 0) and in process (`StepGridFillTest`, log 012, exit 0). (c) Edge cases tried: same seed twice; a different seed; a named scale overriding the project; No Scale (Refused, no change, no undo step); an unknown clip; seed -1, 1.5 and 2^31 (InvalidArgs); a kind typo (InvalidArgs); low 60 / high 61 with fewer than two scale keys (InvalidArgs); density 1 and 100; and a fill that changes nothing (no undo step). The command also refuses an empty clip as `NotFound` at `src/core/ControlCommandsNoteStepFill.cpp:335-337`, but that empty-clip case is not directly exercised by these tests. Cited tests: `StepGridFillTest::refusalsChangeNothing`, `namedScaleOverridesTheProject` and `densityAndNoChangeTakeNoStep` (log 012, exit 0).

8. **The GUI test drives the visible control and the same command.** The GUI cases click the real `stepGridFill` QPushButton, not a handler (`StepGridPanelTest` lines 359, 370, 392 and 425). The six Fill widgets (kind, contour, seed, spread, density and the button) declare `controlCommand = note.step_fill` in one loop in `StepGridPanel::buildFills`, and `theFillRowFillsFromASeed` asserts it on the button (line 374). `StepGridPanel::fillSteps` dispatches it with `run("note.step_fill", ...)`, so the click reaches the one registered command. The effect is measured on the clip's notes, and one `control.undo` restores the exact rows, ids included (logs 028 and 021, exit 0). No panel code writes notes directly.

## Known deviations

- Commit trailers are mixed. `68b20065f2`, `f005ca75ea`, `e2a0d22b36` and `99fa202d79` carry `Co-Authored-By: Claude Haiku 5.5`. `53582e6163`, `06419dae58`, `e4ee3eb32d` and `92600744c3` carry `duet:deepseek-haiku`. The brief forbids amending, so these were left as they are.
- `tools/dawproject-proof.sh` part 2 cannot run on this tree: `tools/dawproject-a16-histogram.cpp` does not link (stub signature drift at :87 vs `include/ControlReversibility.h:511`, and a missing controller-scripts translation unit). This is pre-existing. The A16 block was re-derived from `ReversibilityContractTest`'s measured line and validated by log 047.
- The `complexity` ratchet flagged three functions this branch added; they were split into helpers with behaviour unchanged, and feature_check log 054 passes.
