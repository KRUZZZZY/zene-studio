# Theory lane — batch 03

Six findings, all dispositions decided against the current tree. No product code
was changed: five are FALSE-POSITIVE at the wire surface the report is written
against, and the sixth is SKIPPED-SCOPE because its only compliant fix is a
result-schema change.

## Summary

| Cluster | Subject | Verdict |
| --- | --- | --- |
| 3800 | `append` overwrites a chord at an occupied tick | SKIPPED-SCOPE |
| 4719 | `chord.track_write` can erase a clip writing no notes | FALSE-POSITIVE |
| 5665 | Unrecognised quantize `scope` edits the whole clip | FALSE-POSITIVE |
| 5666 | Invalid `start`/`end` can quantize the whole clip | FALSE-POSITIVE |
| 5667 | `groove.apply` truncates fractional integer args | FALSE-POSITIVE |
| 5668 | `groove.quantize` truncates a fractional seed | FALSE-POSITIVE |

### Cluster 3800 — `append` can silently overwrite an existing chord
Verdict: SKIPPED-SCOPE
Evidence: `src/core/ControlCommandsChordEdit.cpp:249` calls `track->set(event, nullptr)` and
`src/core/ChordTrack.cpp:302-308` replaces the event at the same `pos` in place — but that is the
track's DOCUMENTED contract, not an accident: `include/ChordTrack.h:194-200` states a chord track
"stores `event` under its POSITION: replaces the event at that position in place ... `replaced`
receives whether an existing event was overwritten", and a chord track cannot hold two chords at
one tick. The report's own half of the fix ("report replacements explicitly") needs a new result
key, which rule 8 forbids; the other half ("skip collisions") contradicts the engine rule that
`tests/src/core/ControlChordCommandsTest.cpp:385-394` deliberately asserts ("a chord track cannot
hold two chords at one tick. That is the engine's rule, asserted here rather than assumed"), so
changing it would mean weakening an existing test. Non-written slices are already reported, not
silently dropped (`src/core/ControlCommandsChordEdit.cpp:282-283` counts them into `skipped`, and
the command description at line 395-397 says so).
Commit: none
Test: none
Red/green: none

### Cluster 4719 — Chord track writing can erase a clip without writing notes
Verdict: FALSE-POSITIVE
Evidence: the report's premise — "the chord track contains only unresolvable events" — is a state
the engine cannot hold. Every event enters the track through `ChordTrack::set`, which refuses
anything `isWritable` rejects (`src/core/ChordTrack.cpp:300`), and `isWritable` includes
`nameResolves` (`src/core/ChordTrack.cpp:271-282`), so a stored event's chord name always resolves
in the vocabulary; the project-file loader goes through the same `set` and skips what it cannot
resolve (`src/core/ChordTrack.cpp:443`). A non-empty track therefore always yields at least one
resolvable event, `eventKeys` (`src/core/ControlChordSupport.cpp:463-467`) returns its keys, and
`specs` is non-empty — and an EMPTY track is already refused up front with a typed `Refused`
(`src/core/ControlCommandsChordWrite.cpp:130-135`), before `beginClipWrite` runs. The clear at
`src/core/ControlCommandsChordWrite.cpp:154` therefore cannot be followed by a zero-note write.
Commit: none
Test: none
Red/green: none

### Cluster 5665 — An unrecognised quantize scope edits the whole clip
Verdict: FALSE-POSITIVE
Evidence: `scope` is declared as an enum of exactly `clip`/`selection`
(`src/core/ControlCommandsGrooveEdit.cpp:441`, `enumProperty` at
`src/core/ControlVocabulary.cpp:126-132`), and `ControlRegistry::invoke` validates the args schema
BEFORE it runs the handler (`src/core/ControlRegistry.cpp:251`), where `checkEnum`
(`src/core/ControlSchema.cpp:102-110`) rejects any value outside the set with a typed
`InvalidArgs`. A misspelling such as `"selction"` never reaches
`quantizeTargets` (`src/core/ControlCommandsGrooveEdit.cpp:280`), whose `== "selection"` test is
therefore a default for the two legal values, not an unvalidated branch.
Commit: none
Test: none
Red/green: none

### Cluster 5666 — Invalid quantize range fields can quantize the whole clip
Verdict: FALSE-POSITIVE
Evidence: `start` and `end` are `tickProperty()` (`src/core/ControlCommandsGrooveEdit.cpp:442-443`),
i.e. type `integer` bounded 0..0x7fffffff (`src/core/ControlVocabulary.cpp:119-124`). A non-number
such as `start: "bad"` fails the schema type check in `typeMatches`
(`src/core/ControlSchema.cpp:50-53`) and is refused by `ControlRegistry::invoke`
(`src/core/ControlRegistry.cpp:251`) before `quantizeTargets`' `toInt(0)` default
(`src/core/ControlCommandsGrooveEdit.cpp:281-282`) can be reached — the `toInt` defaults are the
"field absent" path, which is what the finding needs them to be confused with.
Commit: none
Test: none
Red/green: none

### Cluster 5667 — Groove apply truncates fractional integer arguments
Verdict: FALSE-POSITIVE
Evidence: `random_velocity` and `seed` are declared `integerProperty(0, 200)` and
`integerProperty(0, kMaxSchemaInteger)` (`src/core/ControlCommandsGrooveEdit.cpp:400-401`), and an
`integer` type requires a whole value (`src/core/ControlSchema.cpp:50-53`), so `random_velocity:1.9`
and `seed:2.7` are refused with `InvalidArgs` at the schema gate
(`src/core/ControlRegistry.cpp:251`). The casts in `readGrooveRandom`
(`src/core/ControlCommandsGrooveEdit.cpp:194, 206`) are reached only by a value the schema already
proved integral.
Commit: none
Test: none
Red/green: none

### Cluster 5668 — Groove quantize truncates a fractional seed
Verdict: FALSE-POSITIVE
Evidence: `seed` is declared `integerProperty(0, kMaxSchemaInteger)`
(`src/core/ControlCommandsGrooveEdit.cpp:447`), so `seed:3.8` fails the integer type check
(`src/core/ControlSchema.cpp:50-53`) and is refused before `readHumanise`
(`src/core/ControlCommandsGrooveEdit.cpp:149-159`) runs. The handler's range-and-type check is a
second line of defence for direct C++ callers, which the wire surface never is.
Commit: none
Test: none
Red/green: none

Leftovers: a REVERSED but well-typed quantize range (`start` > `end`, both integers in
0..0x7fffffff) passes the schema and selects no notes at all in
`src/core/ControlCommandsGrooveEdit.cpp:287` (`pos >= start && pos < end`), so it is a harmless
no-op rather than the whole-clip quantize the finding describes; closing it would need an
explicit `start < end` refusal, which is a behaviour change and out of scope here. No other
cluster work was left unfinished.

## Evidence run

- `cmake --build build --target zene ControlChordCommandsTest ControlChordWriteTest ControlGrooveCommandsTest` → EXIT=0
- `cd build/tests && ctest -R '^(ControlChordCommandsTest|ControlChordWriteTest|ControlGrooveCommandsTest)$' --output-on-failure` → EXIT=0 (3/3 passed)
- `bash tests/complexity-gate.sh --check` → EXIT=0
- `bash tests/file-length-gate.sh --check` → EXIT=0
