# Theory lane — batch-09

Six findings (deduplicated v2) across session tuning / Scala / MTS. Verdicts are
decided from the CURRENT tree, not from the line numbers in the reports.

| Verdict | Count |
| --- | --- |
| FIXED | 1 |
| ALREADY-FIXED | 2 |
| FALSE-POSITIVE | 2 |
| REAL-UNTESTED | 1 |

---

### Cluster 3525 — A malformed tuning snapshot can unexpectedly activate tuning

Verdict: FALSE-POSITIVE

Evidence: `ControlRegistry::invoke` runs `validateArgs(cmd->argsSchema, args)` at
`src/core/ControlRegistry.cpp:251` BEFORE the handler is dispatched, so a
non-boolean `active` never reaches the tuning code on the wire path. The `active`
property is declared `booleanProperty()` for every tuning command
(`src/core/ControlCommandsMts.cpp:369`, `:400`, `:421`, `:447`;
`booleanProperty()` defined at `src/core/ControlVocabulary.cpp:75`), and
`typeMatches` requires `value.isBool()` for `type == "boolean"`
(`src/core/ControlSchema.cpp:54`). The remaining `toBool(true)` calls in
`setState` (`src/core/SessionTuning.cpp:345`) are only ever fed booleans this
process produced internally — the `fullState` path
(`src/core/ControlCommandsMts.cpp:92-106`) and the undo/redo closures that
capture `stateJson()` output. There is no path that delivers `null` or the
string `"false"`.

Commit: none

Test: none

Red/green: none — no code change was made, so no red/green exists to report.

---

### Cluster 4453 — Accepted tuning frequencies can become zero or infinity at render

Verdict: ALREADY-FIXED

Evidence: fixed on this branch by `02a8dad92` ("fix(tuning): a table built outside
float's range reached the render path as infinity"). `MaxTableFrequency` is
`std::numeric_limits<float>::max()` and every entry is screened by
`entryIsFrequency` (`src/core/SessionTuning.cpp:106`+) and
`tableFrequenciesAreRenderable` (`src/core/SessionTuning.cpp:139`+);
`setNoteFrequency` refuses a frequency that is not finite, positive and within
that bound (`src/core/SessionTuning.cpp:357`), as does `setState`. So `1e100`
and `1e-100` are rejected instead of being narrowed to infinity/zero.

Commit: 02a8dad92 (pre-existing on this branch)

Test: SessionTuningTest::aKeymapThatWouldOverflowFloatIsRefusedWithTheTableLeftStanding
(`tests/src/core/SessionTuningTest.cpp:650`)

Red/green: none in this batch — the case was already present and already green
when the baseline build ran.

---

### Cluster 5319 — Invalid Scala ratios are silently changed

Verdict: ALREADY-FIXED

Evidence: fixed on this branch by `4c586833d` ("fix(scala): an invalid .scl ratio
line is rewritten into a different valid one"). `ratioFromLine` now rejects a
non-positive denominator and a non-positive numerator
(`src/core/SessionTuningScala.cpp:69` and `:71`) instead of clamping them to 1,
so an interval line `0/2` is a parse error rather than a silently different `1/2`.

Commit: 4c586833d (pre-existing on this branch)

Test: SessionTuningTest::anInvalidRatioLineIsRefusedInsteadOfRewrittenToADifferentOne
(`tests/src/core/SessionTuningTest.cpp:582`)

Red/green: none in this batch — the case was already present and already green.

---

### Cluster 3526 — Session tuning reads reusable table slots without pinning them

Verdict: REAL-UNTESTED

Evidence: the unpinned reads are real. `setNoteFrequency` reads
`m_tables[m_front.load(std::memory_order_relaxed)]` (`src/core/SessionTuning.cpp:365`),
`stateJson` reads `m_tables[m_front.load(std::memory_order_acquire)]`
(`src/core/SessionTuning.cpp:414`), and `publishToMtsEsp` reads the front slot
(`src/core/SessionTuningMtsEsp.cpp:127`) — none of them acquire a reader slot,
while `commit` can recycle and overwrite the back slot. `acquireReadSlot` /
`releaseReadSlot` exist (`include/SessionTuning.h:214-236`) but are not used on
these paths.

No code was changed. This is a control-thread race between a commit and a reader,
and there is no deterministic headless way to make it fail: it needs real
thread interleaving, not a test that drives the API in sequence. Per the batch
rule "if you cannot make a deterministic failing test headlessly, do NOT change
product code", the finding is recorded and left untouched rather than fixed on
speculation. Note also that pinning would sit next to the audio-thread read path,
so any future fix must be checked against the realtime rule (no locking on the
render path) before it is applied.

Commit: none

Test: none

Red/green: none.

---

### Cluster 1696 — Undoing MTS master changes cannot restore registration

Verdict: FALSE-POSITIVE

Evidence: the inverse is captured and replayed explicitly, not through
`setState`. `mts.master_set` captures `wasEnabled` BEFORE the write
(`src/core/ControlCommandsMts.cpp:322`, `:341`) and records undo/redo closures
that call `SessionTuning::instance()->setMtsMaster(wasEnabled, &ignored)` and
`setMtsMaster(enabled, &ignored)` respectively
(`src/core/ControlCommandsMts.cpp:332-339`). `SessionTuning::setMtsMaster` itself
registers/deregisters the MTS master as a side effect of the flag
(`src/core/SessionTuningMtsEsp.cpp:105` and `:110`), so replaying the flag
restores registration. In the reversibility table `mts.master_set` is a
`RC::TrueInverse` action checkpoint (`src/core/ControlReversibilityTableMts.cpp:111`),
which is exactly the mechanism that captures a before-state per command — it is
never routed through the snapshot `setState` path the finding describes.

Commit: none

Test: none

Red/green: none — no code change was made.

---

### Cluster 3530 — Oversized tuning sources are silently truncated

Verdict: FIXED

Evidence: `commit` clamps with `m_source = source.left(MaxSourceLength)`
(`src/core/SessionTuning.cpp:273`), so any mutating entry point that skipped the
cap check reported success while quietly dropping part of the source. `setState`
checked it; `setNoteFrequency`, `loadScaleFile` and `loadKeymapFile` did not, so
an `mts.set_note` source over 256 characters and a `.scl`/`.kbm` path over 256
characters both landed truncated state. The cap is now checked once in the
namespace-scope helper `sourceFitsCap` (`src/core/SessionTuning.cpp:116`) and
consulted by all three entry points (`src/core/SessionTuning.cpp:343`,
`:364`; `src/core/SessionTuningScala.cpp:254`, `:311`), each returning a typed
refusal that leaves the standing table alone. The boundary stays inclusive —
exactly 256 characters still succeeds, because the check is `>` the cap, not
`>=`. The helper keeps `setNoteFrequency` at CCN 9 so the complexity ratchet was
not re-anchored.

Commit: 181c04356

Test: SessionTuningTest::aSourceOverTheCapIsRefusedRatherThanSilentlyTruncated
(`tests/src/core/SessionTuningTest.cpp:470`)

Red/green:
- RED: `cmake --build build --target zene SessionTuningTest > build/bunny-b.log 2>&1; echo EXIT=0` (EXIT=0), then `cd build/tests && ctest -R '^SessionTuningTest$' --output-on-failure > /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-theory/build/bunny-t.log 2>&1; echo EXIT=8` — `FAIL! ... '!loaded.ok' returned FALSE. (a scale whose path is over the source cap loaded successfully: the recorded source was silently truncated)` at `tests/src/core/SessionTuningTest.cpp:500`.
- GREEN: same build (EXIT=0) and same ctest command (EXIT=8) — `PASS! aSourceOverTheCapIsRefusedRatherThanSilentlyTruncated()`; `Totals: 10 passed, 1 failed, 0 skipped`.
- The one remaining failure is `loadScaleWhileTransportPlaysRetunesSoundingNotes` (`tests/src/core/SessionTuningTest.cpp:352`), which is PRE-EXISTING and unrelated: it cannot create two instrument tracks because the plugin scan finds 0 files in this environment. It fails identically before this change and is not in a slot the fix touches, so the executable's exit code cannot reach 0 here.
- `cmake --build build --target SessionTuningMenuTest > build/bunny-b2.log 2>&1; echo EXIT=0`; `cd build/tests && ctest -R '^SessionTuningMenuTest$' --output-on-failure > /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-theory/build/bunny-t2.log 2>&1; echo EXIT=0` (the other test covering the changed files, fully green).
- Gates: `bash tests/complexity-gate.sh --check > build/bunny-cx.log 2>&1; echo EXIT=0`; `bash tests/file-length-gate.sh --check > build/bunny-fl.log 2>&1; echo EXIT=0`.

Leftovers: cluster 3526 (unpinned table reads) is REAL but has no deterministic
headless failing test, so the product code is deliberately unchanged and it needs
a concurrency-instrumented test — or an owner decision on whether the control
thread is in fact multi-threaded here — before it can be fixed. The void GUI
paths `feedScale` (`src/core/SessionTuning.cpp:376`) and `feedKeymap`
(`src/core/SessionTuning.cpp:392`) still pass an over-cap source to `commit` and
will truncate it; they return nothing, so they cannot report a typed refusal and
were left out of scope to keep the fix minimal. Separately,
`loadScaleWhileTransportPlaysRetunesSoundingNotes` fails in this environment for
lack of instrument plugins; that is not a batch-09 defect.