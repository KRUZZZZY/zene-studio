FIXED

# cluster new-p17-14 - direct note edits left the project marked clean

The finding is real. The line it names (`src/core/ControlCommandsNoteProbability.cpp:147`,
`note.probability_set`) had already been fixed on this branch by cluster new-p23-12, but the
same defect was still live in eight handlers of the same family: `note.add`, `note.remove`,
`note.edit`, `note.slide_set`, `note.slide_clear`, `note.transpose`, `note.velocity_offset`
and `note.velocity_scale`. Each one checkpointed the clip and emitted `dataChanged()` (so the
editors repainted) and none called `Song::setModified()`, so a project edited through them and
then closed or replaced was announced clean. The fix is a shared edit finalizer,
`control::markProjectEdited()`, and a call to it on every one of those paths.

## Decision, path by path, with the code that shows it

`MidiClip::addNote` / `MidiClip::removeNote` never mark the song modified (`src/tracks/MidiClip.cpp`;
no `setModified` in that file), and the registry does not mark it either: `ControlRegistry.cpp:251-300`
runs the handler inside an `EditBracket` and validates the reply, and nothing there touches the flag.
`Song::setModified` is a plain flag setter (`src/core/Song.cpp:1016-1023`), so a handler that never
calls it leaves `isModified()` as it was.

| verb | status before this commit | evidence |
| --- | --- | --- |
| `note.add` | not fixed | `src/core/ControlCommandsNotes.cpp:139` `clip->dataChanged();` with no mark |
| `note.remove` | not fixed | `src/core/ControlCommandsNotes.cpp:200` same |
| `note.edit` | not fixed | `src/core/ControlCommandsNoteEdit.cpp:460` `clip->dataChanged();`, no mark |
| `note.slide_set` | not fixed | `src/core/ControlCommandsNoteSlide.cpp:86` via `publishSlideEdit()` (`:79-84`), no mark |
| `note.slide_clear` | not fixed | same `publishSlideEdit()` at `src/core/ControlCommandsNoteSlide.cpp:194` |
| `note.transpose` | not fixed | `src/core/ControlCommandsNoteTransform.cpp:244` `clip->dataChanged();`, no mark |
| `note.velocity_offset` | not fixed | `src/core/ControlCommandsNoteTransform.cpp:291` same |
| `note.velocity_scale` | not fixed | `src/core/ControlCommandsNoteTransform.cpp:332` same |
| `note.probability_set` | ALREADY-FIXED by cluster new-p23-12 | `git show b86ce3a9f6 -- src/core/ControlCommandsNoteProbability.cpp` adds `if (changed) { Engine::getSong()->setModified(); }`; the defect was live when the finding was written |
| `note.move`, `note.resize`, `note.velocity_set` | ALREADY-FIXED by cluster new-p23-12 | `git show b86ce3a9f6 -- src/core/ControlCommandsNotes.cpp` |
| `note.randomize` | ALREADY-FIXED | `src/core/ControlCommandsNoteRandom.cpp:385` `if (changed) { song->setModified(); }` |
| `note.curves_set` | already correct | `src/core/ControlCommandsNoteCurves.cpp:163` marks unconditionally (a curve write always changes state) |
| `note.expression_set/_clear/_stream_set` | already correct | `src/core/ControlCommandsNoteExpression.cpp:342`, `:401`, `:504` |
| `note.invert`, clipboard verbs, `note.scale`, `note.ratchet`, `note.add_many`, `note.step_*`, `note.transform_where` | already correct | they finish through `notes::finish()` (`src/core/ControlCommandsNoteShared.h:67-80`), which marks the project |
| `note.select` / `note.transform_where`'s selection half | FALSE-POSITIVE, by design | `src/core/ControlCommandsNoteSelect.cpp:97` `cmd.mutating = false;` - selection is view state, it takes no checkpoint and no dirty flag (same rule documented at `src/core/ControlCommandsNoteShape.cpp:470`) |

The sibling family check the pre-submit script asks for (`note.*` group) is in the table: every
note verb in the changed files is either fixed here, already fixed, or marks through `finish()`.

## The fix

`include/ControlNoteShared.h:114-130` declares the finalizer next to `checkpointNoteEdit`, so the
undo rule and the dirty rule are read together; `src/core/ControlNoteShared.cpp:74-85` defines it:

```cpp
void markProjectEdited(bool changed)
{
	if (!changed) { return; }
	if (Song* song = Engine::getSong()) { song->setModified(); }
}
```

Call sites, all of them on paths that already computed "did this edit change anything?" for the
journal checkpoint:

- `src/core/ControlCommandsNotes.cpp:143` (add), `:205` (remove), `:291` (move),
  `:376` (resize), `:444` (velocity_set)
- `src/core/ControlCommandsNoteEdit.cpp:464`
- `src/core/ControlCommandsNoteProbability.cpp:155` (now through the finalizer, unchanged behaviour)
- `src/core/ControlCommandsNoteSlide.cpp:91` inside `publishSlideEdit()`, which only a real change reaches
- `src/core/ControlCommandsNoteTransform.cpp:250`, `:294`, `:336`
- `src/core/ControlCommandsNoteShared.h:77` `notes::finish()` uses it instead of its inline call

A no-op still leaves the project clean: `note.slide_set` to the value the note already carries never
reaches `publishSlideEdit`, and every other site passes its own `changed`. That is the collateral
control in the new test.

No control command was added, no undo step was added, no audio-thread code was touched (all of this
is command-handler code on the UI thread), and no save/load path changed: the flag is in memory only.

## Test

`tests/src/core/ControlNoteNoOpUndoTest.cpp` (existing file for this area, already registered in
`tests/CMakeLists.txt:1759`), two new slots:

- `directNoteEditsMarkProjectDirty` - clears the flag with `Song::noteProjectSaved`, runs each of the
  eight verbs, and asserts `Song::isModified()` afterwards (accumulating every failure, so one run
  names all eight).
- `noOpSlideSetLeavesProjectClean` - the control: the same call with the value the note already holds
  must leave the project clean.

No clock, sleep or thread in either: the seam is `Song::noteProjectSaved()` / `Song::isModified()` on
state that is already there when the command returns.

## Commands and exit codes (this session)

Logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p17-14.r1/logs`.

| step | command | exit | log |
| --- | --- | --- | --- |
| RED build | `cmake --build build --target ControlNoteNoOpUndoTest -j4` | 0 | build-1.log |
| RED run | `cd build/tests && ctest -R ControlNoteNoOpUndoTest --output-on-failure` | 8 | test-1.log |
| first fix build (compile error, `notes` namespace not declared) | `cmake --build build --target zene ControlNoteNoOpUndoTest -j4` | 2 | build-2.log |
| fix build | `cmake --build build --target zene ControlNoteNoOpUndoTest -j4` | 0 | build-4.log |
| GREEN run | `cd build/tests && ctest -R ControlNoteNoOpUndoTest --output-on-failure` | 0 | test-2.log |
| family build (16 targets) | `cmake --build build -j4 --target ControlNoteNoOpUndoTest NoteEditingCommandsTest NoteEditRangeTest NoteNoOpEditUndoStepTest NoteTransformTest NoteToolsTest NoteToolsValidationTest NoteRandomTest NoteCurvesAcrossTest ClipLinkTest SlideNotesTest NoteShapeValidationTest NoteSelectionIdentityTest NoteCommandTransactionsTest ControlNoteExpressionCommandsTest ControlNoteScaleVerbsTest` | 0 | build-5-family.log |
| family run | `cd build/tests && ctest -R 'ControlNoteNoOpUndoTest\|NoteEditingCommandsTest\|NoteEditRangeTest\|NoteNoOpEditUndoStepTest\|NoteTransformTest\|NoteToolsTest\|NoteToolsValidationTest\|NoteRandomTest\|NoteCurvesAcrossTest\|ClipLinkTest\|SlideNotesTest\|NoteShapeValidationTest\|NoteSelectionIdentityTest\|NoteCommandTransactionsTest\|ControlNoteExpressionCommandsTest\|ControlNoteScaleVerbsTest' --output-on-failure` | 0 (16/16) | test-3-family.log |
| 4b patch out | `git diff -- <8 product files> > logs/fix.patch` | 0 | fix.patch |
| 4b reverse | `git apply -R logs/fix.patch` | 0 | - |
| 4b RED build | `cmake --build build --target zene ControlNoteNoOpUndoTest -j4` | 0 | build-6-red.log |
| 4b RED run | `cd build/tests && ctest -R ControlNoteNoOpUndoTest --output-on-failure` | 8 | test-4-red.log |
| 4b reapply | `git apply logs/fix.patch` | 0 | - |
| 4b GREEN build | `cmake --build build --target zene ControlNoteNoOpUndoTest -j4` | 0 | build-7-green.log |
| 4b GREEN run | `cd build/tests && ctest -R ControlNoteNoOpUndoTest --output-on-failure` | 0 | test-5-green.log |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | gate-1.log |

The RED run, without the product change, failed with:

```
FAIL!  : ControlNoteNoOpUndoTest::directNoteEditsMarkProjectDirty() 'failures.isEmpty()' returned FALSE. (note.transpose left the project clean
note.velocity_offset left the project clean
note.velocity_scale left the project clean
note.edit left the project clean
note.slide_set left the project clean
note.slide_clear left the project clean
note.add left the project clean
note.remove left the project clean)
```

Gate table (`gate-1.log`), unpiped exit code 0:

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

**Does the regression test fail with the product change reverted and pass with it?**
Yes, both directions were run in this session. With `git apply -R logs/fix.patch` applied and the
targets rebuilt: `build-6-red.log` build exit 0, `test-4-red.log` ctest exit **8**, all eight verbs
listed as leaving the project clean. With `git apply logs/fix.patch` back: `build-7-green.log` exit 0,
`test-5-green.log` exit **0**. The same test also failed before any product change existed
(`test-1.log`, exit 8).

**Is the test deterministic?** Yes. It uses no clock, no sleep and no second thread: the seam is
`Song::noteProjectSaved()` (set the flag clean) and `Song::isModified()` (read it) around a synchronous
`ControlRegistry::invoke` through `revtest::run`. The verbs and their argument values are fixed
literals; `note.randomize` is not used here (it takes a seed and is already covered elsewhere).

**Is the fix at the cause and complete?** The cause is that the "did this edit change anything?"
answer drove the journal checkpoint but not the dirty flag. The file-by-file table above walks every
note verb of the family - the eight that were broken, the ones new-p23-12 already fixed, the ones
that mark through `notes::finish()`, and the two whose answer is "selection is view state". Where the
same code path served two verbs (`publishSlideEdit`, `finish`, the three transforms) the mark went
into the shared function, not into one caller.

**No collateral damage?** The only behaviour added is `Song::setModified()` on a call that already
took a journal checkpoint and already repainted the clip. Old projects are untouched: the flag is
in-memory state, no serialization, schema, loader or file format was changed. No limit or check was
added, so nothing else can start being refused. A call that changes nothing still leaves the project
clean, which `noOpSlideSetLeavesProjectClean` (new) plus the pre-existing `noOpResizeDoesNotAddCheckpoint`,
`noOpVelocitySetDoesNotAddCheckpoint` and `zeroJitterRandomizeDoesNotDirtyProject` all assert.

**Undo, commands, realtime.** No control command was added or changed (the ControlCommandsSnapshot
canary is untouched). No undo step was added: every site marks *after* the checkpoint decision that
already existed, so one edit is still one step. Nothing here runs on the audio thread - these are
UI-thread command handlers - and no allocation, lock, wait or log call was added.

**ALREADY-FIXED / FALSE-POSITIVE claims.** Cited with `file:line` and with `git show b86ce3a9f6`
for the two groups new-p23-12 closed; the two FALSE-POSITIVE rows cite `ControlCommandsNoteSelect.cpp:97`
(`cmd.mutating = false`) and `ControlCommandsNoteShape.cpp:470`, which state that selection is view state.

**Sibling commands.** The pre-submit script names `app.*` and `arrangement.*` ids, but those come from
earlier commits on this branch: its sibling search walks every command file changed between the merge
base and HEAD, and this lane has 516 changed files, while my commit `b663f21b0c` touches only
`include/ControlNoteShared.h`, `src/core/ControlNoteShared.cpp`, `src/core/ControlCommandsNoteShared.h`
and five `src/core/ControlCommandsNote*.cpp` files. It registers, renames or removes no command, so the
snapshot canary is untouched (935 ids before and after).

The `note.*` group is the sibling set that matters here, and each id was checked against the file that
registers it (`tools/mcp-zene-control/zene_control/commands_snapshot.json`):

- fixed here: `note.add`, `note.remove`, `note.edit`, `note.slide_set`, `note.slide_clear`,
  `note.transpose`, `note.velocity_offset`, `note.velocity_scale`;
- already marking, now through the same finalizer: `note.move`, `note.resize`, `note.velocity_set`,
  `note.probability_set`;
- already marking in its own handler: `note.randomize`, `note.random_seed_set`,
  `note.pitch_curve_set`, `note.expression_set`, `note.expression_clear`, `note.expression_stream_set`;
- marking through `notes::finish()`: `note.invert`, `note.add_many`, `note.arpeggiate`, `note.cut`,
  `note.paste`, `note.duplicate`, `note.remove_many`, `note.slice`, `note.scale`, `note.ratchet`,
  `note.restore_timing`, `note.transform_where`, `note.flam`, `note.transpose_in_key`, `note.repeat`,
  `note.group`, `note.ungroup`;
- inherits a fixed handler: `note.across` delegates to the requested `note.*` handler
  (`src/core/ControlCommandsNoteAcross.cpp:129` `command->handler(call)`), so it marks through it;
- not a project-state mutation: `note.list`, `note.select`, `note.select_where` (view state),
  `note.copy` (clipboard only), `note.expression_get`, `note.expression_stream_get`,
  `note.pitch_curve_get`, `note.random_seed_get`, `note.step_get_state`.

The check behind that: every `dataChanged()` call site in the note command translation units, and every
`setModified` / `finish()` site, was walked (`build-5-family.log` and `test-3-family.log` are the build
and run of the 16 tests that cover them; `successfulNoteMutationsMarkProjectDirty` covers the group that
was already correct).

## Other findings

- `note.articulations_set` (`src/core/ControlCommandsNotePlayback.cpp:193-236`) swaps the track's
  articulation map and remaps notes, and does not call `Song::setModified()` (the only mark in that
  file, `:269`, belongs to `note.groove_set`). Same class of defect, different command family
  (track state, not a note edit) - not touched here, listed for its own cluster.
- The build tree needed one extra compile to settle after the header move
  (`ControlCommandsNoteShared.h` now includes `ControlNoteShared.h`); that is a normal include-order
  cost, not a defect.
