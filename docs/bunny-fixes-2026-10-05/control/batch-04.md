# Control lane — batch 04 (2026-10-05)

Six findings, one fixed. Two were already false on the current tree (one of them
fixed by a commit that is already on this branch), two are design-level and out of
scope, and one was refused for a reason worth recording.

### Cluster 4067 — The supposedly bounded child-error read loads the entire file

Verdict: ALREADY-FIXED

Evidence: `src/core/ControlMasteringSupport.cpp:291-296` — `childErrorText()` already
does the bounded read the finding asks for: it seeks to
`max(0, size() - ChildErrorKeep)` and reads exactly `ChildErrorKeep` bytes, with the
comment "Only the tail is read: the bound is on memory too, not just on the quote." The
quoted `readAll().right(ChildErrorKeep)` shape is from revision e5a603132; the bounded
version landed in 57980beef, which is on this branch.

Commit: 57980beef (pre-existing, not mine)

Test: none

Red/green: none

### Cluster 4972 — Failed mastering runs leave partial or overwritten WAV files behind

Verdict: SKIPPED-SCOPE

Evidence: `src/core/ControlMasteringSupport.cpp:451-460` — the failure branch does
publish `failed` and does discard `run->before` without restoring it, exactly as
reported. The described remedy ("compare the directory with the captured state and
remove new files and restore captured files") is not a defect fix but a new
behaviour: the run's undo half currently exists only on the success path, and adding a
rollback to the failure path changes what a failed `mastering.run` guarantees. That is
a design decision about the verb's contract, not the smallest correct fix for a stated
bug, and it belongs to the lane owner. It is also not deterministically testable
headlessly here: the failure path needs a child that writes a candidate and then exits
non-zero, and the child is launched by `MasteringJob` rather than injected.

Commit: none

Test: none

Red/green: none

### Cluster 3052 — Async mastering inserts its undo step out of transaction order

Verdict: SKIPPED-SCOPE

Evidence: `src/core/ControlCommandsControl.cpp:414-416` — the async ordering is a
deliberate, documented part of the verb's shape, not an oversight. The ACK carries
`applies: "journal_later"` (`src/core/ControlMasteringSupport.cpp:403`) precisely so a
record whose step has not arrived yet is still undoable, and `control.undo` honours it
by skipping the `irreversible` refusal for such a record. Making the transaction record
track the late step would mean reordering or re-recording a transaction after the fact,
i.e. changing the transaction/journal contract the reversibility table and the
provenance journal are built on — beyond this lane's remit and closer to rule 9's
"known-by-design" list.

Commit: none

Test: none

Red/green: none

### Cluster 4976 — Mixer routing undo steps retain channel positions instead of stable IDs

Verdict: FIXED

Evidence: `src/core/ControlMixerSupport.cpp:256-299` (pre-fix) — `recordMixerRouteStep`
and `recordMixerSidechainStep` captured `fromIndex`/`toIndex` and their undo and redo
closures resolved routes through those captured POSITIONS, while the command surface
itself resolves `"ch-<n>"` by persistent id via `resolveMixerChannel`
(`src/core/ControlMixerSupport.cpp:45`, SPEC-stable-ids.md slice 2). `Mixer::deleteChannel`
(`src/core/Mixer.cpp:1105`) renumbers every later channel, so a step retained across a
removal acted on the channels that took the recorded positions' places. The steps now
capture the two persistent channel ids and resolve them to live positions through
`resolveMixerChannel` when each closure runs; a channel removed in the meantime makes
the step a no-op rather than a write through a stale position. Header and callers were
changed together.

Commit: fa22ad21f

Test: MixingRoutingTest::aRouteUndoStepFollowsItsChannelsThroughARenumbering

Red/green:
- RED (fix reverted with `git checkout HEAD --` on the three product files, new test kept):
  `cmake --build build --target zene MixingRoutingTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
  `./MixingRoutingTest aRouteUndoStepFollowsItsChannelsThroughARenumbering` -> EXIT=1
  `FAIL! 'control::findMixerRoute(1, 2) == nullptr' returned FALSE.`
- GREEN:
  `cmake --build build --target zene MixingRoutingTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
  `cd build/tests && ctest -R '^MixingRoutingTest$' --output-on-failure > .../bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=0
  `cd build/tests && ctest -R '^(MixingRoutingTest|MixerModesTest|MixerSendsOnFadersTest|DevicePanelTest|ProjectStateTest|MixerToolsTest|NoteEditingCommandsTest)$' --output-on-failure > .../bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=0 (7/7 passed)
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0

### Cluster 363 — Modulation support definitions are in the wrong namespace

Verdict: FALSE-POSITIVE

Evidence: `include/ControlModulationSupport.h:37-76` — the header opens
`namespace lmms` (line 37) and closes it at line 76; there is no `lmms::control` block
anywhere in it, so `songModulationLayer`, `modulationRouteJson` and the rest are declared
in exactly the namespace the source defines them in. `src/core/ControlModulationSupport.cpp:43`
opens `namespace lmms` and defines them there, with `using namespace control;` at line 46
only to reach `channelIdOf()`/`idToIndex()` from ControlVocabulary.h — which is what that
line says it is for. Header and source agree; there is no link error to fix.

Commit: none

Test: none

Red/green: none

### Cluster 4054 — An empty scope silently edits the whole clip

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlNoteShared.cpp:36-45` — `readScope()` does treat an empty
string like an omitted one (`if (scope.isEmpty() || scope == "clip")`), so the finding
reads correctly at that line. But no caller can reach it with an empty value: all four
callers declare `scope` as `enumProperty({clip, selection})` in their args schema
(`ControlCommandsScaleEdit.cpp:156`, `ControlCommandsNoteRandom.cpp:452`,
`ControlCommandsNoteSlide.cpp:260`, and `scopeArgument()` at
`ControlCommandsNoteTransform.cpp:328-331`), and the registry validates the enum before
the handler runs. I confirmed this by writing the regression case the finding asks for —
`note.scale` with `{"scope": ""}` returns `InvalidArgs` and the clip is untouched, on the
UNMODIFIED product code, so it passed without any fix. The header comment at
`include/ControlNoteShared.h:72-75` states the contract ("Anything else is refused typed
rather than defaulted") and the schema layer enforces it; `readScope`'s `isEmpty()` is
belt-and-braces for a call that never arrives.

Commit: none

Test: none (case written, run, observed GREEN against unmodified code, then removed)

Red/green: none — deliberately not a red/green pair. The trial case was added to
`tests/src/core/NoteEditingCommandsTest.cpp`, run (EXIT=0 against unmodified product
code), and then removed because it would have pushed that file from 495 to 511 lines and
failed `tests/file-length-gate.sh` (EXIT=1, "new file over 500 lines"). Since the
finding is a false positive there is no defect to pin, so the case was dropped rather
than splitting an unrelated test file to house a non-regression.

Leftovers: nothing half-done. Two things are worth an owner's call but are not mine to
decide: whether a failed `mastering.run` should roll its output directory back (4972),
and whether `mastering.run`'s async transaction record should track its late journal step
(3052). Both are contract changes to the verb, not defect fixes.
