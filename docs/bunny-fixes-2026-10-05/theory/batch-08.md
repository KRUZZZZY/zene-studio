# Theory lane — batch-08 report

Three of the six findings were real and are fixed; two were already fixed in the
current tree; one is a false positive by construction. Every finding cites a
revision older than the tip, so each was re-read at the current tree before being
classified. Line numbers below are from the tree at this report.

## Summary

| Cluster | Verdict | Commit |
| --- | --- | --- |
| 3364 Loading more than 512 markers silently discards the rest | FIXED | b17c98e11 |
| 4449 Invalid Scala ratios are silently changed | FIXED | 4c586833d |
| 771 Accepted tuning values can become zero or non-finite audio frequencies | ALREADY-FIXED | none |
| 5317 Loaded tunings can produce infinite render frequencies | FIXED | 02a8dad92 |
| 5314 The tuning source can disagree with the published table | FALSE-POSITIVE | none |
| 4454 Undo restores tuning values but leaves stale scale and keymap objects | ALREADY-FIXED | none |

### Cluster 3364 — Loading more than 512 markers silently discards the rest

Verdict: FIXED

Evidence: `ProjectMarkers::load()` walked `<marker>` children under a condition
that stopped at `MaxMarkers` (512, include/ProjectMarkers.h:60) and said nothing
about the rest, so a project written with 513 markers loaded as 512 and the next
save wrote the smaller list (src/core/ProjectMarkers.cpp:185-199 before the fix).
`load()` now counts the elements the file offers and warns, naming the cap and
the number left out, whenever the file holds more than the project can carry. The
cap itself is unchanged: an over-long list is still repaired, not refused.

`qWarning` was chosen over a new error channel because `ProjectSection::load`
returns void, and it matches the existing product style (the `feedScale` warning
at src/core/SessionTuning.cpp:363, the PDC capacity warning).

Commit: b17c98e11

Test: ProjectMarkersTest::aMarkerListOverTheCapIsReportedNotSilentlyTruncated

Red/green:
- Red: `cd build/tests && ctest -R '^ProjectMarkersTest$' --output-on-failure` EXIT=8
  (`'!said.isEmpty()' returned FALSE. (a marker past the cap was dropped with nothing said about it)`)
- Green: same command EXIT=0 (10 cases)
- Related: `ctest -R '^(SessionPerformTest|ArrangementModesTest|ArrangerTimelineTest|AudioEditingTest|AafImportTest)$'` EXIT=0
- Gates: `tests/complexity-gate.sh --check` EXIT=0, `tests/file-length-gate.sh --check` EXIT=0

The test asserts on a `qInstallMessageHandler` recorder rather than
`QTest::ignoreMessage`, because the latter only suppresses a warning and cannot
prove one was emitted (`tests/src/core/DataFileSaveIntegrityTest.cpp` records the
same reasoning).

### Cluster 4449 — Invalid Scala ratios are silently changed

Verdict: FIXED

Evidence: `intervalFromLine()` parsed the denominator without checking its own
`ok` flag and then clamped BOTH halves up to 1, so `3/nope` became `3/1`, `3/0`
became `3/1` and `0/2` became `1/2` — the table held an interval the file never
asked for (src/core/SessionTuningScala.cpp:71-83 before the fix). Ratio parsing
now lives in `ratioFromLine()`, which checks the denominator's parse result and
refuses a nonpositive numerator or denominator rather than rewriting it
(src/core/SessionTuningScala.cpp:56-74). The cents branch and every valid ratio
are unchanged. The helper is file-local because inlining it pushed
`intervalFromLine` past the complexity budget.

Commit: 4c586833d

Test: SessionTuningTest::anInvalidRatioLineIsRefusedInsteadOfRewrittenToADifferentOne

Red/green:
- Red: `cd build/tests && ctest -R '^SessionTuningTest$' --output-on-failure` EXIT=8
  (`'3/nope' was accepted: the bad ratio line became a different, valid interval`)
- Green: the new case passes; every other SessionTuningTest case is unchanged
- Related: `ctest -R '^(SessionTuningMenuTest|LaneReadoutTest)$'` EXIT=0
- Gates: `tests/complexity-gate.sh --check` EXIT=0, `tests/file-length-gate.sh --check` EXIT=0

The test loads the good JI fixture first, then four bad scales, asserting each is
refused as `ControlErrorKind::Refused` and that the standing table is bit-for-bit
the same afterwards (`sameTable`).

### Cluster 771 — Accepted tuning values can become zero or non-finite audio frequencies

Verdict: ALREADY-FIXED

Evidence: the snapshot half of this claim does not hold. `entryIsFrequency()`
already requires `std::isfinite(frequency) && frequency >= 0.0 && frequency <=
MaxTableFrequency` (src/core/SessionTuning.cpp:106-111, with `MaxTableFrequency`
= `std::numeric_limits<float>::max()` at :101), and `setNoteFrequency()` applies
the same rule (src/core/SessionTuning.cpp:350-354). A snapshot frequency of
`1e300` is therefore refused before it can be committed, so the render path never
sees it.

The claim's Scala half ("values are not checked after exponentiation") was the
one real gap, and it is the same defect as cluster 5317 below: it is fixed there
by `buildTable()` validating every built entry against the same range. It is
counted once, under 5317, rather than double-counted here.

Commit: none

Test: none

Red/green: none

### Cluster 5317 — Loaded tunings can produce infinite render frequencies

Verdict: FIXED

Evidence: `buildTable()` committed whatever it computed, with no range check,
and `noteToFreq()` narrows the double to a float
(include/SessionTuning.h:114-123). `keymapOf()` accepts any finite positive base
frequency (src/core/SessionTuningScala.cpp:216-223), so a `.kbm` base frequency
of `1e100` parsed, built and was published — reaching the render path as
infinity. `buildTable()` now checks every built entry against
`MaxTableFrequency` (0 still allowed: it is the unmapped marker) and refuses the
whole scale/keymap when one is out of range, naming the offending key
(src/core/SessionTuning.cpp:202-216). Because the refusal happens before
`commit()`, the table the session already had stays standing — nothing is
half-installed.

The per-key computation moved into a new file-local `frequencyForKey()`
(src/core/SessionTuning.cpp:114-129). The arithmetic is byte-identical; the
extraction was required because adding the validation loop in place pushed
`buildTable` to CCN 14 and the complexity ratchet rejected it.

Commit: 02a8dad92

Test: SessionTuningTest::aKeymapThatWouldOverflowFloatIsRefusedWithTheTableLeftStanding

Red/green:
- Red: `cd build/tests && ctest -R '^SessionTuningTest$' --output-on-failure` EXIT=8
  (`a keymap whose frequencies overflow float was accepted: the render path would read them as infinity`)
- Green: the new case passes; 9 of 10 SessionTuningTest cases pass
- Related: `ctest -R '^(SessionTuningMenuTest|LaneReadoutTest|SessionPlaybackRenderTest)$'` EXIT=0
- Gates: `tests/complexity-gate.sh --check` EXIT=0, `tests/file-length-gate.sh --check` EXIT=0

### Cluster 5314 — The tuning source can disagree with the published table

Verdict: FALSE-POSITIVE

Evidence: `commit()` publishes the new ring index with a CAS and only then
assigns `m_source` (src/core/SessionTuning.cpp:257-259 before this batch), so the
ordering the report describes is real in the source text. It cannot be observed,
because no caller can read the pair concurrently: `commit()` and `stateJson()`
(src/core/SessionTuning.cpp:404-419) run only on the control/UI thread. Every
control command is dispatched there by `ControlRegistry::runOnUiThread()`
(src/core/ControlRegistry.cpp:223-234, used from `invoke` at :270), and
`mts.*` is a control command surface. The audio thread reads only `isActive()`
and `noteToFreq()`, never `m_source`.

Making the pair atomic anyway would mean a lock or a second publish step on the
control path — which rule 6 (no locking on realtime-adjacent paths) and rule 7
(behaviour-preserving minimal diff) both counsel against, for a race no caller
can currently enter. Left alone deliberately.

Commit: none

Test: none

Red/green: none

### Cluster 4454 — Undo restores tuning values but leaves stale scale and keymap objects

Verdict: ALREADY-FIXED

Evidence: the undo step recorded by `tableMutation()` captures the scale and
keymap on both sides of the mutation and restores them on undo and redo
(src/core/ControlCommandsMts.cpp:127-157, with `setBuiltFrom()` called at :145
on undo and :151 on redo). Both load paths opt in:
`loadScale()` (:196) and `loadKeymap()` (:231) pass
`restoreBuiltFrom=true`. `setBuiltFrom()` carries the documented half-state
contract at include/SessionTuning.h:146.

The exact scenario in the report — load scale A, load scale B, undo, then load a
keymap — is already covered and green:
SessionTuningTest::undoOfALoadScaleAlsoUndoesTheScaleItInstalled
(tests/src/core/SessionTuningTest.cpp, which passes on this tree).

Commit: none

Test: SessionTuningTest::undoOfALoadScaleAlsoUndoesTheScaleItInstalled (pre-existing)

Red/green: none (already green before this batch)

## Leftovers

`SessionTuningTest::loadScaleWhileTransportPlaysRetunesSoundingNotes` fails in
this environment both before and after every change in this batch: it fails in
`revtest::addInstrumentTrack` ("two instrument tracks with a real instrument could
not be created"), before any scale is parsed, so no load in this report is
covered by it. It needs a loadable instrument plugin, which this build does not
expose. Not touched — it is an environment gap, not a defect, and the rules
forbid loosening a test to make it pass.