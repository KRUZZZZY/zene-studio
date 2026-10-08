# Space Bunny fixer lane `cmds1` — batch 04

Branch `bugs/bunny-cmds1-2026-10-05`, six findings, 1 fixed / 3 false-positive / 2 skipped-scope.
All findings in `src/core/ControlCommandsClip*.cpp`.

### Cluster 3802 — Replay inverses lose a clip's auto-resize setting
Verdict: SKIPPED-SCOPE
Evidence: `src/core/ControlCommandsClip.cpp:243-257`, `src/core/ControlCommandsClipTrim.cpp:170-183` —
the inverses of `clip.resize` / `clip.trim` / `clip.slip` indeed carry only length, edges and offset,
so a hand-reissued inverse leaves the clip with auto-resize false. The gap is real but unfixable
without a schema change: restoring the flag requires adding an `auto_resize` param to those three
commands' inverse payloads, which is a params-schema change reserved for the parent lane (rule 8).
`tests/src/core/ControlVerbInverseTest.cpp` already proves the journal-backed path does restore the
flag on undo, which is why `control.undo` is unaffected.
Commit: none
Test: none
Red/green: none

### Cluster 1023 — `clip.delete` declares nullable `note_count` as required numeric
Verdict: SKIPPED-SCOPE
Evidence: `src/core/ControlEditSupport.cpp:303-308` — `clipState()` sets `note_count` to
`QJsonValue::Null` for any non-MIDI clip, while `src/core/ControlCommandsClip.cpp:363` declares
`clip.delete`'s `note_count` with the bare `control::numberProperty()` instead of the
`control::nullable(control::numberProperty())` form `clip.add` uses at `:114`. So deleting or
dry-running an audio clip emits a null against a non-nullable schema. The fix is a one-word change to
the result schema, i.e. a `ControlCommandsSnapshot` canary move, so it is out of scope here (rule 8);
`tests/result-schema-known-violations.txt` is empty, so there is no grandfathering either.
Commit: none
Test: none
Red/green: none

### Cluster 5624 — Repeating a long clip can overflow tick arithmetic
Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsClipArrange.cpp:56` caps `MaxRepeats = 64`, and
`include/Song.h:77` / `include/TimePos.h:38` give `MaxSongLength = 9999 * DefaultTicksPerBar` with
`DefaultTicksPerBar = 192`, i.e. 1,919,808 ticks. So `count * length` at `:74` tops out at
64 × 1,919,808 = 122,867,712, and `tick_t` is `std::int32_t` (INT32_MAX = 2,147,483,647) — the
worst reachable product is ~5.7% of the range, with no wrap and no invalid position.
Commit: none
Test: none
Red/green: none

### Cluster 2744 — Repeat ignores all-track ripple mode
Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsClipArrange.cpp:74` calls
`arrangement::rippleShift(ref.track, next, count * length)`, and that helper is already mode-aware:
it returns every song track when the current ripple mode is `Ripple::All`, and the handler's
`checkpointRipple` checkpoints every track it reports as moved. Repeats therefore already move the
following clips on all tracks when all-track ripple is active.
Commit: none
Test: none
Red/green: none

### Cluster 5625 — Time scaling can overflow note and clip ticks
Verdict: FALSE-POSITIVE
Evidence: the factor is capped at 8, and `scaleNotes()` only ever touches positions and lengths that
are themselves bounded by `MaxSongLength` = 1,919,808 ticks (`include/Song.h:77`,
`include/TimePos.h:38`). The maximum scaled product is therefore 8 × 1,919,808 = 15,358,464, about
0.7% of INT32_MAX, so the `tick_t` casts in `src/core/ControlCommandsClipArrange.cpp:90-98` cannot
overflow. Reaching a value near the tick limit would first require a clip that cannot be constructed.
Commit: none
Test: none
Red/green: none

### Cluster 3803 — `clip.time_scale` bypasses linked-clip note mirroring
Verdict: FIXED
Evidence: `src/core/ControlCommandsClipArrange.cpp:90-98` — `scaleNotes()` wrote note positions and
lengths with bare `note->setPos()` / `note->setLength()`, bypassing the note entry points, so
time-scaling one member of a linked MIDI group left the other members stale. The handler now calls
`ClipLinks::mirrorContent(midi)` after `scaleNotes(midi, factor)` when `midi->linkId() > 0`, matching
the pattern already used by sibling verbs (`src/core/ControlCommandsNoteTransform.cpp`,
`src/core/ControlCommandsNoteRandom.cpp:348`).
Commit: 1a9d6eaed
Test: ClipLinkTest::aTimeScaleIsSeenByTheOtherMember (added to the existing
`tests/src/core/ClipLinkTest.cpp`, no new target)
Red/green: red — `cd build/tests && ctest -R '^ClipLinkTest$' --output-on-failure` EXIT=8, failing
with `'notePositionAt(b, 0) == 96 && noteLengthAt(b, 0) == 48' returned FALSE. (clip.time_scale
wrote the source clip only: the other member still holds position 48 / length 24, not 96 / 48)`.
green — `cmake --build build --target zene ClipLinkTest` EXIT=0, then
`cd build/tests && ctest -R '^ClipLinkTest$' --output-on-failure` EXIT=0. Regression sweep:
`cmake --build build --target zene ClipJoinRippleTest RangeEditsTest ClipLinkPersistenceTest
ControlLinkCommandsTest ClipLinkTest` EXIT=0 and
`cd build/tests && ctest -R '^(ClipJoinRippleTest|RangeEditsTest|ClipLinkPersistenceTest|ControlLinkCommandsTest|ClipLinkTest)$' --output-on-failure`
EXIT=0 (5/5 passed). Gates: `bash tests/complexity-gate.sh --check` EXIT=0,
`bash tests/file-length-gate.sh --check` EXIT=0.

Leftovers: clusters 1023 and 3802 are genuine but both need a command schema change
(`note_count` nullability in `clip.delete`'s result; an `auto_resize` param on the
`clip.resize`/`clip.trim`/`clip.slip` inverses), which rule 8 reserves for the parent lane — they are
ready to apply once the snapshot canary is moved deliberately. Nothing else is half-done.
