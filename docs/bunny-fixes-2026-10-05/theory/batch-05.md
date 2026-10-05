# Theory lane — batch 05

Six findings. Five were REAL and are fixed, one is FALSE-POSITIVE against an
explicit convention that this lane's own test suite depends on.

## Summary

| Cluster | Subject | Verdict |
| --- | --- | --- |
| 5675 | Relative tuning file paths accepted despite the absolute-path contract | FIXED |
| 5674 | Tuning undo restores the table but loses scale and keymap state | FIXED |
| 208 | `mts.set_tuning` accepts mixed table and note-shaped arguments | FIXED |
| 4883 | `scale.list` drops scales when chords are included | FIXED |
| 1225 | `scale.from_detected_key` advertises an inverse that reruns the forward operation | FIXED |
| 1226 | Setting the scale context to its current value consumes an undo step | FALSE-POSITIVE |

### Cluster 5675 — Relative tuning file paths accepted despite the absolute-path contract
Verdict: FIXED
Evidence: `loadScale()` and `loadKeymap()` in `src/core/ControlCommandsMts.cpp` validated the `path`
argument only with `QFileInfo::exists()`, which resolves a relative name against the process working
directory. Both handlers' own error strings promise "a path argument must be absolute", so a `.scl`
or `.kbm` sitting in the CWD was loaded anyway. Both now refuse a non-absolute path as
`ControlErrorKind::InvalidArgs` *before* the existence check (`src/core/ControlCommandsMts.cpp:178`
and `:213`), so the failure kind matches the documented contract instead of reporting `NotFound` for
an argument that was never legal.
Commit: e7ecb0fd8
Test: `SessionTuningTest::typedRefusalsWriteNothingAndPushNoUndoStep` (two new relative-path rows:
`mts.load_scale` / `mts.load_keymap` → `InvalidArgs`)
Red/green: build EXIT=0; with the product code reverted `cd build/tests && ctest -R
'^SessionTuningTest$' --output-on-failure` EXIT=8 (`FAIL! typedRefusalsWriteNothingAndPushNoUndoStep()
Compared values are not the same`); with the fix applied the same command is EXIT=8 with only the
pre-existing `loadScaleWhileTransportPlaysRetunesSoundingNotes` failure remaining.

### Cluster 5674 — Tuning undo restores the table but loses scale and keymap state
Verdict: FIXED
Evidence: `tableMutation()` in `src/core/ControlCommandsMts.cpp` built both its undo and redo closures
from `SessionTuning::stateJson()`, which carries only the 128 frequencies, the active flag and the
source — as the file's own comment states. Loading a `.scl` or `.kbm` also replaces `m_scale` /
`m_keymap`, so after `control.undo` the table reverted while `mts.get_state` kept reporting the scale
the undone load had installed. `tableMutation()` now additionally captures the `Scale`/`Keymap` the
table was built from and restores them through the new `SessionTuning::setBuiltFrom()`
(`src/core/SessionTuning.cpp:368`, declared `include/SessionTuning.h:146`), which touches only that
provenance. Only the two load verbs opt in, so `set_note` / `set_tuning` / `master_set` steps behave
exactly as before. `stateJson()` and the `mts.set_tuning` schema are deliberately untouched: adding
scale/keymap keys there would have broken the bounded snapshot contract and moved the schema canary.
Commit: 0ee05c0c3
Test: `SessionTuningTest::undoOfALoadScaleAlsoUndoesTheScaleItInstalled` (writes its own
`theory-undo.scl` into a `QTemporaryDir`, loads it, then compares `mts.get_state`'s `scale` before the
load and after `control.undo`)
Red/green: build EXIT=0; with the product code reverted EXIT=8
(`FAIL! undoOfALoadScaleAlsoUndoesTheScaleItInstalled() - control.undo restored the table but
mts.get_state still reported the freshly installed scale`); with the fix applied 7 passed / 1 failed,
the one failure being the pre-existing environmental case named under **Leftovers**.

### Cluster 208 — `mts.set_tuning` accepts mixed table and note-shaped arguments
Verdict: FIXED
Evidence: `setTuning()` computed `hasPair = note && frequency` and rejected only when
`hasFrequencies == hasPair`. `frequencies` plus just `note` (or just `frequency`) passed validation,
the whole 128-entry table was written, and the extra field was silently ignored. The guard at
`src/core/ControlCommandsMts.cpp:246` now rejects any note-shaped field alongside `frequencies`, and
requires *both* halves when there is no table, so neither alone slips through either.
Commit: 1cbee1476
Test: `SessionTuningTest::typedRefusalsWriteNothingAndPushNoUndoStep` (three new refusal-table rows:
`frequencies`+`note`, `frequencies`+`frequency`, and a lone `frequency` → `InvalidArgs`)
Red/green: build EXIT=0; reverted EXIT=8
(`FAIL! typedRefusalsWriteNothingAndPushNoUndoStep()`); fixed 6 passed / 1 failed (the pre-existing
case only). Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 4883 — `scale.list` drops scales when chords are included
Verdict: FIXED
Evidence: `scaleEntries()` filtered with `chord.isScale() != !includeChords`, which keeps exactly one
of the two kinds — without chords it kept the chords, and with chords it kept the scales. Either way
one side vanished, so the documented "include_chords ADDS the chords to the scales" produced a list
that replaced the scales instead of extending them. The filter at
`src/core/ControlCommandsScale.cpp:90` now skips an entry only when it is not a scale *and* chords were
not requested.
Commit: 68ad05a5d
Test: `ControlNoteScaleVerbsTest::theScaleVocabularyResolvesThroughTheEngine` (new
`include_chords:true` block counting `is_scale` entries against the rest)
Red/green: build EXIT=0; reverted EXIT=8
(`FAIL! ... 'include_chords:true returned only 0 scales ...'`); fixed EXIT=0, 13 passed / 0 failed.

### Cluster 1225 — `scale.from_detected_key` advertises an inverse that reruns the forward operation
Verdict: FIXED
Evidence: `contextTransaction()` advertises the forward command as its own inverse op. For
`scale.from_detected_key` that is unusable: the handler ignores the recorded root/scale entirely and
re-reads the project key (`src/core/ControlCommandsScale.cpp:269`), so replaying the advertised
inverse re-applied the detected key rather than restoring the replaced context. The verb now
advertises `control.undo`, which unwinds the recorded step. Because `control.undo` takes no arguments
— replaying it *with* root/scale is refused with `unexpected property 'root'` —
`contextTransaction()` omits `inverseArgs` for that op (`src/core/ControlCommandsScale.cpp:184-190`);
the other callers are untouched and keep their `root` + `scale` args. No command or schema was added.
Commit: ae589bfa5
Test: `ControlNoteScaleVerbsTest::theDetectedKeyVerbAdvertisesAnInverseThatRestoresTheContext`
(sets E Minor, points the project key at D Major, runs the verb, reads the recorded inverse via
`stateOf(...)` and replays it, expecting E Minor back)
Red/green: build EXIT=0; reverted EXIT=8
(`FAIL! theDetectedKeyVerbAdvertisesAnInverseThatRestoresTheContext() 'the advertised inverse
'scale.from_detected_key' refused: unexpected property 'root''`); fixed EXIT=0, 14 passed / 0 failed.

### Cluster 1226 — Setting the scale context to its current value consumes an undo step
Verdict: FALSE-POSITIVE
Evidence: `rootSet()` and `scaleSet()` do call `contextTransaction()` unconditionally, but "a no-op
edit still records one step" is this group's documented convention, not an accident.
`tests/src/core/ControlNoteScaleVerbsTest.cpp:600-602` states it outright — *"Two undos: the second
snap is its own step (a no-op edit still records one, and undoing it leaves the first snap's
result)"* — and the surrounding case `theScaleContextIsReversibleAndSnapRefusesWithoutOne` (W8)
depends on that exact step count: it re-runs a snap that moves nothing and then requires a second
`REV_UNDO_OR_FAIL()` to reach the first snap's real edit. Suppressing the step for no-op writes would
break that case, and the alternative (coalescing) is explicitly listed as known-by-design. A no-op
edit recording a step is also the normal, predictable behaviour of an undo stack: one user action, one
step.
Commit: none
Test: none
Red/green: none

## Verification

- `cd build/tests && ctest -R '^(SessionTuningTest|ControlNoteScaleVerbsTest)$' --output-on-failure`
  → EXIT=8. `ControlNoteScaleVerbsTest` is fully green (14 cases);
  `SessionTuningTest` is 7 passed / 1 failed, the single failure being the pre-existing
  environmental case below.
- Every RED above was produced by reverting *only* the product code
  (`git checkout -- include/SessionTuning.h src/core/SessionTuning.cpp
  src/core/ControlCommandsMts.cpp src/core/ControlCommandsScale.cpp`), rebuilding, and re-running, so
  each failure is attributable to the fix and not to stale binaries.
- `bash tests/complexity-gate.sh --check` → EXIT=0 and
  `bash tests/file-length-gate.sh --check` → EXIT=0 before every commit. No ratchet re-anchored.
- `fff` MCP was unavailable in this environment; findings were located with bounded `rg`/`sed`
  instead (no unbounded searches, no `build/` or `src/3rdparty/`).

Leftovers: `SessionTuningTest::loadScaleWhileTransportPlaysRetunesSoundingNotes` still fails in this
environment and was **not** fixed here. `Engine::addInstrumentTrack` returns an empty id for both
tracks (`'!trackAId.isEmpty() && !trackBId.isEmpty()' returned FALSE`) because no instrument plugin
is available — the run log shows `plugin-scan: 0 file(s) ... 0 plugin(s)`. It reproduces identically
with all of this batch's product code reverted, so it is unrelated to these five fixes, and it is
left for whoever can run it with a populated plugin path.