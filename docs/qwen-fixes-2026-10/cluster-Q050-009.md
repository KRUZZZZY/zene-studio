IMPLEMENTED

# cluster Q050-009 - Preset-based range and section export in the GUI

Unit: P0, size L, area L12. Branch `feat/050-Q050-009-oc`, base `050/m0-train-5` (`de5a85a96`).

Three acceptance criteria, each met and proven by a test I ran. Logs:
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-009.r2/logs`.

## Rework r2 (from the round-1 review)

The round-1 review asked for two changes, both done:

- **Save Preset is one undoable action.** `onSavePresetClicked` no longer selects the preset it
  just saved (`src/gui/modals/ExportProjectDialog.cpp:468`): it adds the name to the picker without
  changing the current index, so no `export.preset_apply` fires and the click leaves ONE checkpoint
  (the `export.preset_add` store write). The applied selection and the settings rows are preserved.
  New regression `ExportProjectDialogPresetTest::aSavedPresetIsOneUndoStep`: save from the dialog,
  assert the preset is stored and the applied selection did not move, then ONE `control.undo`
  (`undone_command == export.preset_add`) removes the document and still leaves the selection
  unchanged. On the pre-fix code the top checkpoint was the apply, so one undo left the document
  behind and this test fails.
- **The dialog test selects from the reopened dialog.** It now clears the active preset
  (`export.preset_apply` with no name), reopens, asserts the picker opens on Default Settings,
  switches to the saved preset, asserts `applied_preset` CHANGED to it, and only then renders and
  reads the file header.

## Acceptance

| # | Criterion | Test | Evidence (command / exit) |
|---|---|---|---|
| 1 | Create a saved preset from the export dialog, reopen it, select it, and verify the rendered file format, rate, depth, and channel settings match | `ExportProjectDialogPresetTest::aPresetSavedFromTheDialogReopensSelectsAndRendersAtItsSettings` (+ `::aSavedPresetIsOneUndoStep`) | `ctest -R ExportProjectDialogPresetTest --output-on-failure` from `build/tests` -> `Passed` EXIT `0` (`test-1.log`; 4/4 slots in `test-1-verbose.log`). Saves 48000/24/stereo from the dialog; clears the applied selection; reopens and asserts the picker opens on Default Settings; switches to the saved preset; checks `exportSettings()` (48000 / 24-bit / stereo / stereo channels) and that `applied_preset` changed to the preset; renders through the dialog's own Start button and parses the WAV header: rate 48000, bits 24, channels 2, frames > 0. `aSavedPresetIsOneUndoStep` proves one `control.undo` removes the preset and leaves the selection unchanged |
| 2 | Invoke Render Selection from the range menu and verify `render.render` receives the exact `start_ticks`/`end_ticks` | `RenderRangeMenuTest::renderSelectionForwardsTheExactRangeTicks` (+ `renderSelectionSaysSoWhenNoRangeIsSelected`, `exportSectionsForwardsTheOutputDirectory`) | `ctest -R RenderRangeMenuTest --output-on-failure` -> `Passed` EXIT `0` (`test-2.log`). Sets the range to 384..1152, triggers the menu action through an injected invoker and compares the captured `render.render` args (start 384 / end 1152 / out); with no range no picker/no render. `ControlRenderPresets` (the existing socket proof) already measures the range in the rendered file -> `Passed` EXIT `0` (`test-2.log`) |
| 3 | Export multiple named sections and verify one correctly named, bounded file per section; verify stem export follows the selected export rate and bit depth | `ControlRenderSections` (`tests/control-render-sections.py`) | `ctest -R ControlRenderSections --output-on-failure` -> `Passed` EXIT `0` (`test-2.log`); the same suite ran `ControlStemExportVerb` and `ControlRenderPresets` EXIT `0`. Intro (0..768) -> `Intro.wav` 328960 frames; Verse (768..2304) -> `Verse.wav` 658176 frames (each within the render period of the ticks the tempo implies); both headers 48000 Hz / 24-bit; `render.stems` reports and writes 48000 / 24-bit |

## What was built (short)

- Export dialog render-preset picker (`include/ExportProjectDialog.h`,
  `src/gui/modals/ExportProjectDialog.cpp`): list/apply (`export.preset_apply`), Save Preset...
  (`export.preset_add`, one undo step, does not apply), rows moved to the applied preset, one
  `exportSettings()` accessor.
- Navigate ▸ Range ▸ Render Selection... / Export Sections...
  (`src/gui/RenderRangeMenu.cpp`, `MenuBarExtras.cpp:140`) - both declare their command in the
  `controlCommand` property (the `agent_surface` gate passes, EXIT `0`).
- `render.sections` (`src/core/ControlCommandsRenderSections.cpp`): one bounded file per named
  section, named after it, through `render.render`. New row: `not_mutating`.
- `render.stems` follows the applied preset's rate and bit depth
  (`ControlCommandsRenderStems.cpp:148`).

## Gates (exits seen)

| Gate | Exit |
|---|---|
| `bash tests/complexity-gate.sh --check` | `1` (pre-existing red; none of my files listed) |
| `bash tests/file-length-gate.sh --check` | `1` (pre-existing red; none of my files listed) |
| `bash tests/fork-sources-gate.sh` | `0` |
| `bash tests/all-sources-reproduce.sh` | `0` |
| `bash tests/no-upstream-regression-gate.sh` | `0` |
| `tests/scripted/check-namespace` | `0` |
| `bash tests/no-tautology-gate.sh` | `0` |
| `python3 tests/doc-figures-gate.py` | `0` |
| `ctest -R "ExportProjectDialogPresetTest\|RenderRangeMenuTest\|ControlRenderSections\|ReversibilityContractTest\|StemExportTest\|RenderPresetMenuTest\|ControlRegistryTest\|ControlStemExportVerb\|ControlRenderPresets\|ControlCommandsSnapshot\|agent_surface"` | `0` (11/11) |

The commands snapshot was regenerated from a live instance (829 ids / 76 groups) and the A16
histogram re-taken (838 rows, 357 not_mutating); `ReversibilityContractTest` and
`ControlCommandsSnapshot` both pass.
