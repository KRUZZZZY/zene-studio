IMPLEMENTED

# Q050-291.1 - Convert MIDI CC lanes and device automation, stage 1/2: reversible commands

Stage 1 of the scoping brief's split: the agent commands. The note-editor lane chooser and the GUI prompt are stage 2
(the brief's second split entry) and are not in this unit; both will call these same commands. Full design, limits and
revert proof: `docs/050-features/Q050-291.1.md`.

**What changed.** `clip.controllers_to_automation` and `clip.controllers_from_automation`
(`src/core/ControlCommandsControllerConvert.cpp`), both `true_inverse`. And one defect fixed on the way that the
save/load criterion depends on: `DataFile::cleanMetaNodes` deleted every `metadata="1"` child on project save, which
included a MIDI clip's `<controllers metadata="1">` lanes, so CC lanes were never written to a project file
(`src/core/DataFile.cpp`). The element keeps its flag, so older builds still skip it.

## Acceptance

All commands from `build/tests`, `QT_QPA_PLATFORM=offscreen`, `LMMS_PLUGIN_DIR` unset. Logs: `Q050-291.1.r1/logs/`.

| # | Criterion | Test | Exit |
| --- | --- | --- | --- |
| 1 | Schema-backed commands convert both ways, require an explicit target when ambiguous, keep point timing and value correspondence, refuse unsupported mappings without partial edits | `ControllerConversionTest` (8 cases: to / from automation, clip position + start offset + moved automation clip, refusals, non-linear clip, round trip); `ControlNoteEditor` (`check_controller_conversion`, real socket); `ControlCommandsSnapshot` (schemas) | `ctest -R ^ControllerConversionTest$` 0 [test-2]; final batch of 12 tests 0 [test-12-final] |
| 2 | Existing CC and automation reads expose the converted points; one undo restores the prior state; save/load keeps the data and the parameter link | same test: `aCcLane...OneUndoTakesItBack` (undo depth +1, `control.undo` once removes the track), `aNonLinearClip...UndoPutsItBack`, `automationBecomesALane...` (undo restores the old lane), `convertedDataAndTheParameterLinkSurviveASaveAndLoad` (file text holds both lanes, `clearProject` + `loadProject`, lanes + points + `existingAutomationClip` link back) | 0 [test-12-final] |

Other runs, all in `test-12-final` (exit 0, 12/12): `ClipSerialisationTest`, `DataFileFormatTest`,
`DataFileSaveIntegrityTest`, `UndoHistoryPersistenceTest`, `MidiClipControllersLoadTest`, `AutomationLanesTest`,
`NoteEditingCommandsTest`, `ReversibilityContractTest` (A16 line now 889 / 436 true_inverse), `DocFiguresGate`
(snapshot regenerated live: 880 ids / 77 groups).
Gates: `feature_check` PASS on all 8 rows [feature-check/, gates-3.log] - run from a copy of the script that writes its
logs to the run folder, because the original makes `<worktree>-feature-check/` beside the worktree, which is outside the
write area (`PermissionError`). The gate commands themselves are unchanged.

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored.** Registration call removed + `DataFile`
  hunk neutralised: `ControllerConversionTest` 6 of 6 cases fail, `ctest` exit 8 (build exit 0) [build-9-reverted,
  test-7-reverted]. Only the `DataFile` hunk neutralised: 7 pass, `convertedDataAndTheParameterLinkSurviveASaveAndLoad`
  fails ("the project file does not carry the clip's CC lanes"), exit 8 [build-10-datafile-reverted,
  test-8-datafile-reverted]. Restored: all pass, exit 0 [build-11-restored, test-9-restored; after a refactor to small
  functions, test-12-final, exit 0]. `ControlNoteEditor` was not run reverted; its new checks need the new commands, so
  it fails without them (not measured).
- **Every criterion proven by a test I ran:** table above, exit codes as shown.
- **One undo step per edit; honest class.** Both commands are `true_inverse` (`ControlReversibilityTableArranger.cpp`,
  `ReversibilityContractTest` exit 0). The tests assert `control.undo_depth` rises by exactly one per call and that
  one `control.undo` restores the state; refusals record no step (asserted).
- **Audio thread.** Both commands run on the control thread. `setControllerPoints` takes the instrument-track lock as
  `clip.controllers_set` already does; automation edits use `putValue` / `removeNodes` under the clip's own mutex as
  `automation.add_point` does. Nothing allocates, locks or emits on the audio thread. No TSan run (does not run on this
  box); race reasoning only: the writes use the same protocols as the existing commands.
- **Old projects / round trip.** Old files have no `<controllers>` in a clip and load as before
  (`MidiClipControllersLoadTest`, `DataFileFormatTest`, `DataFileSaveIntegrityTest`, `ClipSerialisationTest` exit 0). New
  data round-trips (`convertedDataAndTheParameterLinkSurviveASaveAndLoad`). One behaviour change: a project file now
  contains the clip's `<controllers metadata="1">` element when a clip has lanes (it used to be dropped); older builds skip
  it by its `metadata` flag in the note loop.
- **Portability.** Only Qt 5.15-compatible API (`QJsonObject`, `QList`, `QStringList::join`, `QString::arg`), no
  `small/near/far/interface/min/max` identifiers (`std::min`/`std::max` with explicit types are calls, not names), no
  `std::filesystem`, no POSIX-only calls. Not compiled on Qt 5.15 / MSVC / macOS here (this box builds Qt 6.12/GCC).
- **GUI path (submit_check WARN).** No GUI in this unit by design: the brief's split puts the note-editor lane chooser,
  the target prompt and the visible refusal in stage 2, which calls these commands. Until then the conversions are reachable
  through the control socket, MCP, Lua `zene.command` and the command palette (commands list).

## Not verified / open

- The `BoolModel` / `ComboBoxModel` branch of the "switch or list of choices" refusal is not exercised by a test
  (tripleoscillator exposes none); the same refusal path is proven with a collapsed range.
- `ControlNoteEditor` was not run reverted.
- Decisions to confirm: copy rather than move (source stays); linear mapping across `min..max` in the parameter's unit;
  to-automation makes the target clip linear (refused if that would change other nodes of a non-linear clip).
