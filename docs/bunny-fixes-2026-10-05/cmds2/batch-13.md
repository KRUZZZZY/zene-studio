# Space Bunny fixer lane `cmds2` — batch 13

Worktree `zene-bunny-cmds2`, branch `bugs/bunny-cmds2-2026-10-05`, base commit
`49d615bd8 docs(bunny): cmds2 batch-12 report`.

Six findings adjudicated: 2 FIXED, 3 FALSE-POSITIVE, 1 SKIPPED-SCOPE.

### Cluster 283 — Sample crop, uniqueness and speed edits omit their transaction reports

Verdict: FALSE-POSITIVE

Evidence: The report assumes a payload-less `__transaction` means "no reversibility
recorded", but `ControlRegistry.cpp:389-408` synthesizes an honest transaction
object whenever the handler supplies none, and all three commands are declared
`TrueInverse` in `src/core/ControlReversibilityTableDevices.cpp:154,157,158`, each
calling `addJournalCheckPoint()` on success — so `reversible:true` is recorded.
`__transaction` is also stripped from the wire payload for every command, so adding
it explicitly would be cosmetic and would not change what a client observes. An
existing case already proves the undo path works for these edits:
`tests/src/core/AudioAutomationToolsTest.cpp:100-101`.

Commit: none
Test: none
Red/green: none

### Cluster 3993 — MusicXML export rejects uppercase file extensions

Verdict: FIXED

Evidence: `src/core/ControlCommandsScoreExport.cpp:55` matched the target path's
suffix with `QString::endsWith` using the default case-sensitive comparison, so
`score.MUSICXML` was rejected with "'path' is a .musicxml or .xml file" even though
it is a valid MusicXML target. Both suffix checks now use `Qt::CaseInsensitive`.

Commit: `9535ef4a8`
Test: `ArrangeToolsTest::theScoreSuffixIsCaseInsensitive`
Red/green:

- `cmake --build build --target zene ArrangeToolsTest` → EXIT=0
- red: `cd build/tests && ctest -R '^ArrangeToolsTest$' --output-on-failure` → EXIT=8
  (`'written.ok' returned FALSE. "score.MUSICXML: 'path' is a .musicxml or .xml file"`)
- green: same command → EXIT=0 (100% tests passed, 0 failed out of 1)

### Cluster 4884 — Unreadable scripts get the hash of an empty file

Verdict: FIXED

Evidence: `scriptEntryJson` in `src/core/ControlCommandsScript.cpp` cleared the
byte buffer when `controlReadFileBytes` failed but still hashed the cleared buffer,
so an unreadable script was listed with a non-zero `size` paired with
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` (the SHA-256 of
zero bytes). The entry now carries an empty `sha256` when the read fails.

Commit: `c49b9ef39`
Test: `ControlAutomationScriptTest::anUnreadableScriptIsListedWithAnEmptyHash`
Red/green:

- `cmake --build build --target zene ControlAutomationScriptTest` → EXIT=0
- red: `cd build/tests && ctest -R '^ControlAutomationScriptTest$' --output-on-failure` → EXIT=8
  (the reported hash was `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`)
- green: the new case passes; the executable still shows 2 pre-existing failures that
  reproduce on this branch without these changes —
  `automationModeSetSucceedsOnABareParameter` and `automationRecordModeSetRequiresAClip`,
  both `'!trackId.isEmpty()' ... (no track with parameters in the fresh song)`.
- related suite: `cmake --build build --target zene ScriptBindingsTest ScriptCommandBridgeTest
  ScriptDawBindingTest ScriptEngineTest ScriptClockTest ScriptMemoryBudgetTest
  ScriptStabilisationTest ScriptBudgetActionTest ControlRegistryTest` → EXIT=0;
  `cd build/tests && ctest -R '^(ScriptEngineTest|ScriptMemoryBudgetTest|ScriptCommandBridgeTest|ScriptBindingsTest|ScriptClockTest|ScriptDawBindingTest|ScriptStabilisationTest|ScriptBudgetActionTest|ControlRegistryTest)$' --output-on-failure`
  → EXIT=0 (100% passed, 0 failed out of 9). An earlier run of this same suite
  reported 8 × "Not Run" — those executables had simply never been built in this
  worktree, not a regression; after building them all pass.

### Cluster 286 — Concurrent `script.run` calls can overwrite each other's budget and logs

Verdict: FALSE-POSITIVE

Evidence: Every control handler is marshalled to the UI thread before it runs
(`src/core/ControlRegistry.cpp:223-234` and `:270-275`, via `runOnUiThread` /
`BlockingQueuedConnection`), so the override / run / read-logs / restore sequence in
`script.run` cannot interleave with another `script.run` — the wire reader thread never
executes handler bodies concurrently with the pump. The script pump loop itself is
bounded and non-reentrant (`src/core/ScriptEngine.cpp:413-420`, `tryAcquire(1, 2)`
with no event processing inside the loop), so there is no second interleaving source.
Adding a lock would be unreachable-at-runtime code.

Commit: none
Test: none
Red/green: none

### Cluster 2975 — A failed Lua script can leave edits with no transaction record in its reply

Verdict: SKIPPED-SCOPE

Evidence: `ControlResult::failure` carries no result object
(`include/ControlRegistry.h:72-78`), so a typed error has no channel through which to
report a transaction or a partial-change record. Attaching one would change the
closed wire error contract (rule 8); rolling script edits back would need per-command
compensation that does not exist. Out of scope for this lane.

Commit: none
Test: none
Red/green: none

### Cluster 4885 — Script memory-budget changes claim undo without recording it

Verdict: FALSE-POSITIVE

Evidence: `script.set_memory_budget` sets `inverse.op = "script.set_memory_budget"`
with `applies: "command"` (`src/core/ControlCommandsScript.cpp:318-332`), and
`undoByCommand` dispatches that inverse command when the op is registered
(`src/core/ControlRegistry.cpp:330-346`, invoked at `:412`). `control.undo` therefore
restores the previous cap by re-issuing the command, with no journal step required.

Commit: none
Test: none
Red/green: none

## Gates

Both run before each fix commit, both EXIT=0:

- `bash tests/complexity-gate.sh --check` → EXIT=0 (`build/bunny-cx.log`)
- `bash tests/file-length-gate.sh --check` → EXIT=0 (`build/bunny-fl.log`)

## Leftovers

- `ControlAutomationScriptTest::automationModeSetSucceedsOnABareParameter` and
  `::automationRecordModeSetRequiresAClip` fail on this branch before any of my
  changes (`'!trackId.isEmpty()' ... no track with parameters in the fresh song`);
  `automationModeSetSucceedsOnABareParameter` reproduces in isolation via
  `./ControlAutomationScriptTest automationModeSetSucceedsOnABareParameter`
  (`tests/src/core/ControlAutomationScriptTest.cpp:268`). Not touched — they belong to
  a different lane.
- Cluster 2975 (transaction on a typed failure) and cluster 286 (a lock that is
  unreachable at runtime) are left as adjudicated above, not as pending work.