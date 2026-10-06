# Control lane — batch 11 (2026-10-05)

Branch `bugs/bunny-control-2026-10-05`. Four findings, one fixed. Every line
reference below was read in the current tree; the line numbers in the incoming
reports had moved.

### Cluster 4104 — Transaction step IDs lose precision in JSON

Verdict: FALSE-POSITIVE

Evidence: the cast is real — `src/core/ControlTransactions.cpp:140` inserts
`static_cast<double>(tx.step)`, and `step` is a `quint64`
(`include/ControlRegistry.h:134`). The report's premise is not: it says "Journal
IDs carry the high bit". They do not. Every serial in the journal comes from one
monotonic counter, incremented by one per pushed step —
`src/core/ProjectJournal.cpp:173`, `:204`, `:232`, `:249` are all
`serial = ++m_stepSerial`, and the only other assignment (`:328`, a coalesce) copies
an existing serial. The counter starts at 0 and is never rewound, not even by
`clearJournal()` (`src/core/ProjectJournal.cpp:348-350` explicitly keeps it
advancing), so no code path can set a high bit. Loss of precision needs a serial
above 2^53, i.e. roughly 9.0e15 undo steps in one journal's lifetime — the
double conversion is exact for every value the counter can actually reach.

The proposed remedy would also be out of scope on its own: emitting `step` as a
string changes the wire type of a field in the published `control.transactions`
result (`src/core/ControlCommandsControl.cpp:215-223` declares `transactions` only
as an untyped array, so nothing would catch the change), which is a command
result-schema change.

Commit: none

Test: none

Red/green: none

### Cluster 5033 — Malformed IDs can alias real objects

Verdict: FIXED

Evidence: REAL as reported. `control::idToIndex()` parsed the suffix with
`QString::toInt()` alone (`src/core/ControlVocabulary.cpp:193-199`), and `toInt()`
accepts a sign and surrounding whitespace, so the documented decimal grammar was
never enforced: `clip--0` converted its `-0` suffix to 0 and resolved to the real
clip 0, letting a mutating command act on it through a malformed id. The suffix is
now required to be ASCII decimal digits before it is converted; the empty suffix
and a non-numeric one keep returning -1 as before.

Commit: `6ee232560`

Test: `ControlRegistryTest::malformedIdSuffixesDoNotAliasRealObjects`

Red/green:

```
cmake --build build --target zene ControlRegistryTest   # EXIT=0
cd build/tests && ctest -R '^ControlRegistryTest$' --output-on-failure
  EXIT=8  FAIL: idToIndex("clip--0","clip-") actual 0, expected -1
           Totals: 24 passed, 1 failed
  (fix applied to src/core/ControlVocabulary.cpp)
cmake --build build --target zene ControlRegistryTest   # EXIT=0
cd build/tests && ctest -R '^ControlRegistryTest$' --output-on-failure   # EXIT=0
cd build/tests && ctest -R '^ProjectRevIdsTest$' --output-on-failure    # EXIT=0
bash tests/complexity-gate.sh --check    # EXIT=0
bash tests/file-length-gate.sh --check    # EXIT=0
```

### Cluster 4987 — Controller templates can resolve distinct targets to the same model

Verdict: SKIPPED-SCOPE

Evidence: the mechanism is real and acknowledged in the code — a template records
`target->fullDisplayName()` (`src/core/ControllerSurface.cpp:255`) and resolves it
by returning the first breadth-first match
(`src/core/ControllerSurface.cpp:281`), so two identically named models are
indistinguishable in a saved template. Fixing it means changing the template
file's target identity format, `currentBindings()` and `resolveTarget()`, and the
`controller.template_save` description that documents the stored identity
(`src/core/ControlCommandsControllerTemplates.cpp:105`) — a design change well
beyond a behaviour-preserving defect fix, and one that changes the wire shape of
what existing saved templates contain. Left alone.

Commit: none

Test: none

Red/green: none

### Cluster 4988 — Template names can escape the template directory

Verdict: FALSE-POSITIVE

Evidence: `templatePathFor()` is indeed unchecked
(`src/core/ControllerSurface.cpp:110-112`), but that is its documented contract,
not a hole — `include/ControllerSurface.h:82-86` states the name-to-file rule
"has to be checkable from outside this class", i.e. the rule lives in the caller,
and every entry point enforces it before touching the filesystem:
`isUsableTemplateName()` (`src/core/ControlCommandsControllerTemplates.cpp:58-76`)
rejects an empty name, a `/` or `\`, a `..` and a leading dot, and it is called by
`controller.template_save` (`:122`), `controller.template_apply` (`:230`) and
`controller.template_delete` (`:290`). So the reported `../../other` never reaches
`saveTemplate()` or `removeTemplate()`. A bounded search confirms there is no other
production caller: the only users of these functions are
`ControllerSurface.cpp` itself, `ControlCommandsControllerTemplates.cpp`, and the
test's own fixed name. Defensive duplication inside `templatePathFor()` would
be a change without a defect behind it.

Commit: none

Test: none

Red/green: none

Leftovers: none. Cluster 4987 is deliberately unfixed (design change, above);
clusters 4104 and 4988 were not touched. Build/test logs from this batch remain
under `build/` and are not tracked.
