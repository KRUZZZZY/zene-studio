IMPLEMENTED

# Q050-294.2 - Melodic step-sequencer view, stage 2/3: melodic step-grid editor

Branch `feat/050-Q050-294.2-oc` (cut from `050/m0-train-26`, `af010bc3cc`). Feature record: `docs/050-features/Q050-294.2.md`.
Stage 3 (seeded shape and random fills) is not built; nothing here has to be undone for it (a fill can be one more
`note.step_set`-style command and one more button in the same panel).

Built: `StepGridPanel` / `StepGridCanvas` (src/gui/noteeditor/StepGridPanel.cpp, StepGridCanvas.cpp) - a grid of steps x
keys over one MIDI clip that reads only `note.step_get_state` and writes only `note.step_set` (the stage-1 commands);
`window.step_grid` (`not_mutating`, src/gui/ControlCommandsStepGrid.cpp) opens it and answers the grid's own cells; entry
points Edit > Step Grid... and Notes > Tools > Open in Step Grid, both dispatching `window.step_grid`.

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-294.2.r1/logs/` (file names below).

## Acceptance

| # | Criterion | Command / test | Exit |
|---|-----------|----------------|------|
| 1 | A musician can open a clip in the grid and edit scale-aware pitch, velocity, chance and ratchet | `ctest -R '^StepGridPanelTest$'` - canvas clicks (add snapped F#->F, select, right-click remove), keys typed into Velocity/Chance/Ratchet (150 / 0.25 / repeats 3 on the clip's note), Pitch Up arrow E->F->G in C major, out-of-scale C# keeps its key, Scale rows only, 1/8 regroup, Notes > Tools > Open in Step Grid opens the grid (`test-green-2.log`) | green 0; product reverted 8 (`test-red-2.log`: test cannot build, Not Run), wiring reverted 8 (`test-red-wiring.log`) |
| 1b | Chance and ratchet keep their played behaviour | `ctest -R '^NotePlaybackTest$'` (`stepSetRatchetAndChanceArePlayed`, stage 1's test, re-run here; `test-11.log`) | 0 (after building the `nes` module; before it the unrelated `nesNoteStartDoesNotAllocate` failed, 8, `test-10.log`) |
| 2 | Piano roll and socket readback show the same clip state | `ctest -R '^ControlStepGrid$'` (`check_open`, `check_edit_and_undo`, `check_view`: grid cells == `roll.get_state` == `note.step_get_state` by id/key/step/velocity/chance/ratchet, at open, after a batch, at 1/8) and `StepGridPanelTest::anAgentEditAndThePianoRollShowTheSameClip` (agent edit shown at once; note editor paints it) | green 0; product reverted 8 (`test-red-2.log`), wiring reverted 8 (`test-red-wiring.log`) |
| 3 | One undo restores the prior pattern | `ControlStepGrid` `check_edit_and_undo` (one `control.undo` -> `note.list` equal incl. ids, grid shows before-cells; opening the grid leaves `control.undo_depth` unchanged) and `StepGridPanelTest` (each click/field commit is one undo step, back to exact rows incl. ids) | green 0; reverted 8 |

Also green in one run (`test-green-2.log`, exit 0): `ControlRegistryTest`, `ReversibilityContractTest`, `NoteEditorTest`,
`NoteMelodyTest`, `ControlNoteEditor`, `ControlNoteStep`, `agent_surface`, `ControlCommandsSnapshot`.

## Self-check

- **Reverted -> fails, restored -> passes.** `git diff af010bc3cc HEAD` of the ten product files (`product.patch`) reversed with
  `git apply -R`; `zene` rebuilt `ZENE_BUILD_EXIT=0` (`build-red-zene.log`); `StepGridPanelTest` `TEST_BUILD_EXIT=2`
  (`build-red-test.log`, StepGridPanel.h gone). The first ctest run still found the stale pre-revert binary and passed it, so I
  removed that binary and re-ran: `RED_TEST_EXIT=8` (`test-red-2.log`: StepGridPanelTest Not Run, ControlStepGrid Failed).
  A runtime-only revert of the menu entry + `window.step_grid` registration (`wiring.patch`) built (`BUILD_EXIT=0`) and
  gave `RED_WIRING_TEST_EXIT=8` (`theNoteEditorMenuOpensTheGrid` fails; ControlStepGrid checks fail). Restored with
  `git checkout HEAD -- <files>`, rebuilt `BUILD_EXIT=0`, `GREEN_TEST_EXIT=0` (`test-green-2.log`, 10/10).
- **Every criterion proven by a test I ran:** the table above; every exit code was read from an unpiped `ctest` run.
- **One undo step, honest class:** each grid gesture is exactly one `note.step_set` call (one MidiClip checkpoint, stage 1's
  `true_inverse` row); a field commits once per Return/arrow (keyboard tracking off, the Remove button is not auto-default so
  Return never removes). `window.step_grid` only opens a window: `not_mutating`, row in ControlReversibilityTableShell.cpp,
  proven by `control.undo_depth` unchanged in `ControlStepGrid`. A16 histogram re-taken (934/459/42/25/408);
  `ReversibilityContractTest` exit 0.
- **Audio thread:** nothing added on it. All new code is GUI-thread (paint, mouse, spin boxes, registry calls); no engine,
  playback or play-handle code changed. Local TSan does not run on this box; by design the grid reaches the clip only through
  `note.step_set`, which edits it as `note.edit` does. The check's audio-path hint matches only
  `tests/src/gui/StepGridPanelTest.cpp`'s `#include "NotePlayHandle.h"` and `NotePlayHandleManager::init()` in `initTestCase`
  - test setup that creates the play-handle pool a test binary needs (as `NoteEditorTest` / `NoteMelodyTest` do), not product
  code and not on any audio thread.
- **Old projects / round-trip:** no format change and nothing saved by the grid (its resolution, scale choice and row filter are
  interface state). The notes it writes use the existing `prob` / `rep` attributes, whose defaults-omitted save and round-trip
  stage 1's `ControlNoteStep check_persistence` proves (green in `test-green-2.log`). I did not render audio; nothing in the
  render path changed.
- **Portability:** Qt API used exists in Qt 5.15 and 6.8 (`QOverload`, `QSpinBox::setKeyboardTracking`, `QJsonArray::first`,
  `QMouseEvent::pos` as neighbouring note-editor code uses it); no Qt-6-only API, no `small/near/far/interface/min/max`
  identifiers, no windows.h macro names, no `std::filesystem`, no POSIX calls; `<initializer_list>`/`<iterator>` included
  explicitly for MSVC.
- **Gates:** `feature_check.py` - all eight PASS (`featurecheck-3.log`). The installed script writes its logs beside the
  worktree, which this lane's write jail refuses, so I ran a copy in /tmp with only the log directory (and its own ROOT, so the
  cached baselines are the installed ones) changed. Snapshot regenerated live (925 ids / 78 groups, `snapshot-1.log`);
  `doc-figures-gate.py` exit 0 (`docfig-2.log`).
- **Environment notes:** `ControlNoteEditor` and `NotePlaybackTest` needed the `tripleoscillator` and `nes` plug-in modules
  built in this tree; I built them (exit 0). The ccache temp dir from the environment was not writable; builds used
  `CCACHE_TEMPDIR=/tmp/ccache-tmp-q2942`.
