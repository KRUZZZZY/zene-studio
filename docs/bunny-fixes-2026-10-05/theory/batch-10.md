# Theory lane — batch-10

Three findings (deduplicated v2), all three in the `.scl` / `.kbm` parse half of
session tuning. Verdicts are decided from the CURRENT tree, not from the line
numbers in the reports.

| Verdict | Count |
| --- | --- |
| FIXED | 3 |
| FALSE-POSITIVE | 0 |
| ALREADY-FIXED | 0 |
| DUPLICATE | 0 |
| REAL-UNTESTED | 0 |
| SKIPPED-SCOPE | 0 |

All three landed in `src/core/SessionTuningScala.cpp`, one commit per defect.

Pre-existing failure, not introduced here and not in scope:
`SessionTuningTest::loadScaleWhileTransportPlaysRetunesSoundingNotes()` fails in
this environment with "two instrument tracks with a real instrument could not be
created" (the log shows `plugin-scan: 0 file(s) scanned`). It failed identically
in the very first RED run, before any product code was touched, and it is in a
test slot none of these three commits modify.

---

### Cluster 5318 — Truncated Scala scales are accepted

Verdict: FIXED

Evidence: `readScaleLines()` took the second non-comment line as a loop limit
only and then validated just `lines->isEmpty()`
(`src/core/SessionTuningScala.cpp:112` loop, `:129` the old emptiness check). The
interval count was never compared with the declared degree count, so a `.scl`
declaring 5 degrees and ending after 1 interval passed, and `loadScaleFile()`
built a Scale from the 1/1 plus that single line — a table from a scale the file
never described. The check is now `lines->size() != limit`
(`src/core/SessionTuningScala.cpp:129-137`), which also subsumes the old
emptiness refusal, since `limit` is positive by then.

Commit: `ac9bfeaf3`

Test: `SessionTuningTest::aScaleFileShorterThanItsDeclaredDegreeCountIsRefused`

Red/green: `cd build/tests && ctest -R '^SessionTuningTest$' --output-on-failure`
Red EXIT=8 — `FAIL! ...aScaleFileShorterThanItsDeclaredDegreeCountIsRefused()
'!loaded.ok' returned FALSE. (a .scl with fewer interval lines than its declared
degree count was accepted)`. Green after the fix: the case PASSES, 11 passed,
1 failed (the pre-existing slot above).

One supporting change inside the same commit: the four pre-existing bad-ratio
fixtures in `anInvalidRatioLineIsRefusedInsteadOfRewrittenToADifferentOne()`
declared 3 degrees but carried only 2 interval lines. They passed for the wrong
reason once truncation became an error, so each body now carries 3 interval
lines. Their assertions are untouched — they still test the ratio validation.

---

### Cluster 5320 — The KBM declared map size is ignored

Verdict: FIXED

Evidence: `keymapOf()` validated the size field only for positivity
(`src/core/SessionTuningScala.cpp`, the `!sizeOk || size <= 0` branch) and then
handed the `Keymap` whatever `mappingOf()` actually collected, checking only
`map.empty()`. `Keymap::getDegree()` computes
`keyOffset % m_map.size()` (`src/core/Keymap.cpp:74-78`), so a `.kbm` declaring
a 12-entry map and carrying one mapping line produced a map with a one-entry
period — every key on the keyboard answered with that one degree. The comparison
`map.size() != static_cast<std::size_t>(size)` now runs before the Keymap is
built (`src/core/SessionTuningScala.cpp`, in `keymapOf()`), and it subsumes the
old `map.empty()` refusal, since an empty map cannot match a positive size.

Commit: `944321ce9`

Test: `SessionTuningTest::aKeymapWhoseMappingLinesDoNotMatchItsDeclaredSizeIsRefused`

Red/green: `cd build/tests && ctest -R '^SessionTuningTest$' --output-on-failure`
Red EXIT=8 — `FAIL! ...aKeymapWhoseMappingLinesDoNotMatchItsDeclaredSizeIsRefused()
'!loaded.ok' returned FALSE. ('.../long.kbm' was accepted: its mapping does not
have the period it declares)`. Green after the fix: the case PASSES, 12 passed,
1 failed (the pre-existing slot above).

Both directions are covered: declares 12 and carries 1, and declares 3 and
carries 12.

---

### Cluster 5321 — Malformed KBM key fields silently become key 0

Verdict: FIXED

Evidence: the four key bounds were read with
`std::clamp(fields.at(1).toInt(), 0, NumKeys - 1)` and three more like it, with
no conversion result checked. `QString::toInt()` returns 0 when the text is not a
number, so a `.kbm` whose middle key said `oops` became middle key 0, survived
the clamp as if it were real, and built a table from a keymap nobody wrote. Each
field now reads through `keyFieldOf()`, which checks the conversion and leaves
out-of-range whole numbers clamped exactly as the dialog's spin boxes clamp them
(`src/core/SessionTuningScala.cpp`, `keyFieldOf()` / `keyBoundsOf()`).

Commit: `073c118a2`

Test: `SessionTuningTest::aKeymapWithAMalformedKeyFieldIsRefused`

Red/green: `cd build/tests && ctest -R '^SessionTuningTest$' --output-on-failure`
Red EXIT=8 — `FAIL! ...aKeymapWithAMalformedKeyFieldIsRefused() '!loaded.ok'
returned FALSE. (a .kbm whose first key field is malformed was accepted: it
loaded as a real key)`. Green after the fix: the case PASSES, 13 passed, 1 failed
(the pre-existing slot above).

All four key positions are covered, plus a fractional key (`0.5`), which
`toInt()` also rejects and used to truncate into a plausible key.

The header fields moved out of `keymapOf()` into `keyFieldOf()`,
`baseFreqOf()` and `keyBoundsOf()`: the extra decisions would otherwise have
pushed that function past the CCN 10 ratchet. Base-frequency validation
behaviour is unchanged — same `toDouble` check, same message, now returning
early from `baseFreqOf()`.

---

### Checks run on the final state

- `bash tests/complexity-gate.sh --check` — EXIT=0 (PASS, no regressions)
- `bash tests/file-length-gate.sh --check` — EXIT=0 (PASS, no regressions)
- `ctest -R '^(ControlSuccessPathsTest|SessionTuningMenuTest)$' --output-on-failure`
  — EXIT=0, 2/2 passed. `ControlSuccessPathsTest` is the other test that feeds
  this parser a real `.kbm` (`tests/src/core/ControlSuccessPathsTest.cpp:88-97`),
  and its 12-size / 12-line keymap still loads.
- No `.scl` or `.kbm` files are bundled in the tree outside `build/`, so the
  stricter parse cannot break a shipped data file.

Leftovers: `SessionTuningTest::loadScaleWhileTransportPlaysRetunesSoundingNotes()`
still fails in this environment (plugin scan finds 0 files, so no instrument
tracks can be created). Pre-existing, unrelated to these three defects, left
alone. The GUI dialog parser (`src/gui/MicrotunerConfig.cpp:545-566`) still
ignores both counts the way it always did; the fixes are in the core parser
only, so dialog-fed and socket-fed files now diverge on malformed input. Moving
the dialog onto the core parser is a larger design change and is out of this
lane's scope.