IMPLEMENTED

# Q050-294.1 - Melodic step-sequencer view, stage 1/3: scriptable melodic step state

Branch `feat/050-Q050-294.1-oc` (cut from `050/m0-train-24`). Feature record: `docs/050-features/Q050-294.1.md`.
Stage 2 (the grid editor) and stage 3 (seeded fills) are not built; nothing here has to be undone for them.

Built: `note.step_get_state` (`not_mutating`) and `note.step_set` (`true_inverse`, one MidiClip checkpoint per batch) in
`src/core/ControlCommandsNoteStep.cpp`, plus `NoteTransform::snapKeyToScale`. The cells are ordinary clip notes; velocity,
chance and ratchet are the fields the engine already saved (`prob`, `rep`) and plays.

## Acceptance

| # | Criterion | Command / test | Exit |
|---|-----------|----------------|------|
| 1 | Socket read reports step and note state incl. pitch, velocity, chance, ratchet | `ctest -R ControlNoteStep` (`check_read`) | green 0; reversed product 8 |
| 1b | Chance and ratchet are played | `ctest -R NotePlaybackTest` (`stepSetRatchetAndChanceArePlayed`) | green 0; reversed product 8 |
| 2 | One `control.undo` restores the exact pattern | `ControlNoteStep` (`check_edit_and_undo`: `note.list` before == after undo, ids included) | green 0; reversed 8 |
| 3 | Old projects keep note defaults | `ControlNoteStep` (`check_persistence`: no `prob`/`rep` in the saved default project, reopens chance 1 / ratchet 1; values round-trip) | green 0; reversed 8 |

Also green: `ReversibilityContractTest`, `ControlRegistryTest`, `ControlCommandsSnapshot`, `ControlNoteEditor` (one ctest run, exit 0).

## Self-check

- **Reverted -> fails, restored -> passes.** `git apply -R` of the seven product files (`$LOGS/product.patch`), rebuild `zene`
  and `NotePlaybackTest`, `ctest -R "ControlNoteStep|NotePlaybackTest"` -> `RED_TEST_EXIT=8` (both failed; `test-red.log`). Re-applied
  (`git diff HEAD` empty, `registerNoteStepCommands` grep 1/1), rebuilt, same ctest -> `GREEN_TEST_EXIT=0` (`test-green.log`).
- **Every criterion proven by a test I ran:** see the table; each exit code was read from an unpiped `ctest` run.
- **One undo step, honest class:** `note.step_set` takes exactly one `addJournalCheckPoint` for the batch (`checkpointNoteEdit`), and none
  for a batch that changes nothing (`check_refusals` shows edited 0 and `control.undo_depth` unchanged). Class `true_inverse`
  (`finish()` records `control.undo` over the MidiClip checkpoint, the `note.ratchet` / `note.add_many` precedent); `note.step_get_state`
  `not_mutating`. A16 histogram re-taken to 889/435/42/25/387; `ReversibilityContractTest` passes.
- **Audio thread:** nothing added on it. Both handlers run on the control thread; no engine, playback or play-handle code changed
  (the ratchet/chance playback is the existing `InstrumentTrack::triggerNote` / `notePlaysNow`). Local TSan does not run on this box;
  by design reasoning the commands touch the clip the way `note.add_many` / `note.edit` do.
- **Old projects / round-trip:** no format change. `check_persistence` saves a default-notes project, asserts no ` prob=` / ` rep=`
  in the file, reopens it at chance 1 / ratchet 1, then round-trips chance 0.5, ratchet 4, velocity 120. I did not render audio; nothing
  in the render path changed.
- **Portability:** only `QJson*`, `QString::arg`, `QStringList`, `std::optional/map/set` in C++20; no Qt-6-only API, no
  `small/near/far/interface/min/max` identifiers, no `std::filesystem`, no POSIX calls.
- **Gates:** `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-feat-q050-294.1` exits 0:
  fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology and doc-figures pass; complexity
  and file-length list no new item in this unit's files.

## Round 2 (pre-review check)

- **Complexity:** `check_read` (CCN 26) and `check_edit_and_undo` (CCN 33) in `tests/control-note-step.py` were over the target of 10.
  Their compound conditions are now small named predicates and `check_edit_and_undo` is split into `check_cells` / `check_applied`; every
  assertion is the same. `bash tests/complexity-gate.sh --check` lists no item in `tests/control-note-step.py`,
  `ControlCommandsNoteStep.cpp` or `NoteTransform.*`.
- **Red/green re-proved on the rewritten driver:** with the one registration line (`registerNoteStepCommands(registry);` in
  `ControlRegistryRegistrations.cpp`) removed, `zene` rebuilt, `ctest -R ControlNoteStep` -> `RED_EXIT=8`; line restored
  (`git checkout HEAD --`, grep count 1), rebuilt, `ctest -R "ControlNoteStep|NotePlaybackTest"` -> `GREEN_EXIT=0` (both Passed).
  The full seven-file reversal of round 1 (exit 8 / 0) still stands for `NotePlaybackTest`, whose test is unchanged.

## Honest limits / not done

- **No GUI in this stage** (the submit check WARNs on it): the unit is the scriptable state; the grid editor is stage 2 and is meant to
  drive these same two registered commands. A GUI test is therefore not part of this stage.
- `ControlReversibilityTranscript` fails in this build because the `ladspaeffect` host module is not built here (not caused by this unit);
  `ReversibilityUndoDeviceTest` was not built and not run.
- Not run: the whole ctest suite and `run-all-gates.sh` (targeted runs only, per the brief). The scale context from `scale.set` is
  process state, so `note.step_set` also takes `root` / `scale` for a fixed scale.
