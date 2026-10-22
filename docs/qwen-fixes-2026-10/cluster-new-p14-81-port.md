FIXED

# Cluster new-p14-81-port: track removal can claim an inverse was recorded when none was

**Verdict: FIXED** (ported by hand from `bugs/qwen-new-p14-81`; read with `git show`, not merged,
not cherry-picked, not rebased).

The finding is REAL on this branch. `journalTrackRemoval()` (`src/core/ControlStructuralSupport.cpp:253`)
captured the track's XML, called `addStructuralUndoStep(...)` and then answered `true` whatever the
writer had done; its header (`include/ControlStructuralSupport.h:114-119`) promises "False when
nothing could be recorded (no journal, ...)".

This is also a PORT of the whole accepted branch, because the merge conflict that blocked it was
in `tests/src/core/ScriptDawRemovalUndoTest.cpp` - a file the same branch changed for new-p14-82.
The newest line (train-12, `07d6d02bd`) contains NONE of the three product fixes it carried
(`git log --all --oneline -- src/core/ControlStructuralSupport.cpp` shows them only on
`bugs/qwen-new-p14-81`; neither `deviceDocumentIsUsable` nor `wrapperHostKeyElement` existed in this
tree), so all three are re-applied here and each has its own RED/GREEN proof:

| Cluster on the old branch | Verdict here | One line |
| --- | --- | --- |
| new-p14-81 | FIXED | A removal claimed an inverse was recorded when the journal recorded nothing; the step writer now answers whether it pushed, and every helper passes that answer on. |
| new-p14-82 | FIXED | A removal recorded a document the restore cannot use (malformed, not a device document, another device's, no restorable body); the recorder now holds the document to the restore's own rules. |
| new-p16-50 | FIXED | The restore read the FIRST `<key>` in the document, so a plugin's own nested key was taken for the wrapper's host key; the wrapper's own key is now identified by where it is. |

`new-p14-81` is the finding; the other two are carried by the same accepted branch and the same
conflicted test file, and are recorded separately below so a reviewer can check each.

---

## new-p14-81 - a removal claimed an inverse that was never recorded

**Verdict: FIXED**

### Evidence (the defect was real)

* `src/core/ControlStructuralSupport.cpp:253-291` (before this change): `journalTrackRemoval()`
  captured the XML, called `addStructuralUndoStep(...)` and then `return true;` - whatever the
  writer had done.
* `src/core/ControlReversibility.cpp:165-171` (before): the writer was `void` and returned early
  only for "no journal or no undo action". `ProjectJournal::addJournalStructure()`
  (`src/core/ProjectJournalStructural.cpp:65`: `if( !isJournalling() || !undoAction ) { return; }`)
  refuses a step while the journal is not journalling, so there were TWO ways to record nothing and
  still get `true` back.
* The caller took that claim for its transaction:
  `src/core/ControlCommandsArrangement.cpp:322-338` (`captured = journalTrackRemoval(...)` ->
  `trackRemovalTransaction(snapshot, captured, lanes)` -> `trackSnapshot(..., reversible)`), so
  `track.remove` answered `reversible: true` over a deletion no undo could reverse - the next undo
  then takes back an OLDER edit.
* The same lie was in the siblings: `journalTrackMove` (`:236`), `journalTrackRemovalWithLanes`
  (`:344`) and `journalRemovalDocument` (`:704`), plus `journalChainRemoval`
  (`src/core/ControlDeviceMoves.cpp:156`, the QA round-3 sibling) and the registry's trust in a
  handler's `reversible: true` (`src/core/ControlRegistry.cpp:628-655`).

### Fix (at the cause)

* `include/ControlReversibility.h:511` / `src/core/ControlReversibility.cpp:170`: the writer now
  answers `bool` - TRUE only when the step was really pushed - and asks `journal->isJournalling()`
  BEFORE `addJournalStructure()`, the same condition that call applies, so the answer cannot
  disagree with what was recorded.
* `src/core/ControlStructuralSupport.cpp:243`, `:268`, `:361`, `:717`: the four helpers return that
  answer instead of `true` (same closures, same document, same one step).
* `src/core/ControlDeviceMoves.cpp:165`: `journalChainRemoval()` returns the writer's answer
  (`rack.remove_chain` then records `snapshot only` / `reversible:false`).
* `src/core/ControlRegistry.cpp:640-655`: a handler's `reversible: true` with a JOURNAL inverse and
  `step == 0` is downgraded, with the reason in the record's mechanism. The PAIRED COMMAND
  (`applies: command`) and the DEFERRED journal step (`applies: journal_later`) keep their claim.
* `include/ControlStructuralSupport.h:112-145`: the contracts now name the "a journal that is not
  journalling" case.

### Regression (deterministic, three cases)

`tests/src/core/UndoHonestyStateTest.cpp`:

* `:347` `aRemovalThatRecordedNoStepIsNotReportedAsReversible()` - the helper's own answer is false
  while `setJournalling(false)`, and the `track.remove` record is not reversible; the same removal
  with the journal recording again is. The seam is the engine's own journalling flag, the switch
  `JournalPause` uses - no clock, no sleep, no thread.
* `:395` `aRackRemovalThatRecordedNoStepIsNotReportedAsReversible()` - `journalChainRemoval()`'s own
  answer, and the `rack.remove_chain` record, must not claim a recorded undo step.
* `:450` `aJournalInverseWithoutARecordedStepIsNotReportedAsReversible()` - a registered test command
  that declares a journal inverse records nothing; the registry's record must be `reversible:false`
  with `step == 0`.

`tests/src/core/ControlRegistryTest.cpp:393-399`: the fixture whose inverse is the paired COMMAND now
says `applies: command` (what it means) and keeps its assertion.

### Commands and exit codes

All commands were run from the worktree root unless stated; ctest from `build/tests`. Logs are in
`/tmp/new-p14-81-port-r1-logs/` (mirror - see "Log folder" below).

| Step | Exact command | EXIT | Result |
| --- | --- | --- | --- |
| RED before the fix | `ctest -R '^UndoHonestyStateTest$' --output-on-failure` (from `build/tests`) | **8** | `FAIL! : ...aRemovalThatRecordedNoStepIsNotReportedAsReversible() '!directRecorded' returned FALSE` - `Totals: 7 passed, 1 failed, 2 skipped` |
| Build with the fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14 cmake --build build --target zene UndoHonestyStateTest -j8` | 0 | built |
| GREEN | `ctest -R '^UndoHonestyStateTest$' --output-on-failure` | **0** | 1/1 passed; direct run `Totals: 8 passed, 0 failed, 2 skipped` |
| Build all affected | `... --target zene UndoHonestyStateTest ReversibilityUndoDeviceTest ControlRegistryTest ScriptDawRemovalUndoTest DeviceMovesUndoTest -j8` | 0 | built |
| GREEN family | `ctest -R '^(UndoHonestyStateTest\|ReversibilityUndoDeviceTest\|ControlRegistryTest\|ScriptDawRemovalUndoTest\|DeviceMovesUndoTest)$' --output-on-failure` | **0** | 5/5 CTest targets passed, 5 tests run |
| Proof: save and reverse the product diff | `git diff -- <the 7 product files> > .../logs/fix-final.patch && git apply -R .../logs/fix-final.patch` | 0 | fix removed (product only; tests untouched) |
| Proof: rebuild reverted | `... --target zene UndoHonestyStateTest -j8` | 0 | built |
| Proof: 81 regressions on reverted | `ctest -R '^UndoHonestyStateTest$' --output-on-failure` | **8** | THREE failures, quoted above: `aRemovalThat...`, `aRackRemovalThat...`, `aJournalInverseWithout...`; `Totals: 7 passed, 3 failed, 2 skipped` |
| Proof: re-apply | `git apply .../logs/fix-final.patch` | 0 | fix restored |
| Proof: rebuild restored | `... --target zene UndoHonestyStateTest ReversibilityUndoDeviceTest ControlRegistryTest ScriptDawRemovalUndoTest DeviceMovesUndoTest -j8` | 0 | built |
| Proof: GREEN restored | `ctest -R '^(UndoHonestyStateTest\|ReversibilityUndoDeviceTest\|ControlRegistryTest\|ScriptDawRemovalUndoTest\|DeviceMovesUndoTest)$'` | **0** | 5/5 passed |

## new-p14-82 - a removal recorded a document the restore cannot use

**Verdict: FIXED**

* Evidence: `journalRemovalDocument()` checked only non-empty / size cap / test seam, then recorded.
  A caller that passed malformed XML - or a device document naming another plugin, lacking the
  device's own state element, or carrying another hosted identity - got `true` back, and the undo
  had nothing it could rebuild. The restore refuses exactly those documents:
  `instantiateRecordedDevice` (`src/core/ControlStructuralSupport.cpp:415-445`) and
  `controlRestoreEffectState` (`src/core/ControlDeviceSupport.cpp:487-566`).
* Fix: `deviceDocumentIsUsable()` (`src/core/ControlStructuralSupport.cpp:171`) is the restore's own
  rules moved to the moment the inverse is recorded; `removalDocumentRecordable()` (`:680`) applies
  it, and `journalRemovalDocument()` (`:704`) records only through that predicate.
* Regression: `tests/src/core/ReversibilityUndoDeviceTest.cpp:402`
  `aRemovalWithAnUnusableDocumentIsNotRecorded()` - six unusable documents refused with no step
  left behind, the device's own document still recorded.
* RED/GREEN: the 82/50 product hunks were reverted alone (`/tmp/.../tmp-revert-82-50.patch`,
  `git apply -R` EXIT=0), rebuilt (`... --target zene ReversibilityUndoDeviceTest -j8` EXIT=0), and
  `ctest -R '^ReversibilityUndoDeviceTest$'` **EXIT=8**:
  `FAIL! : ...aRemovalWithAnUnusableDocumentIsNotRecorded() ... "a document that does not parse was
  recorded as an inverse (new-p14-82)"` (`Totals: 4 passed, 2 failed, 2 skipped`). Restored,
  rebuilt, same ctest **EXIT=0**.

## new-p16-50 - the restore read a nested `<key>` as the host key

**Verdict: FIXED**

* Evidence: `deviceKeyFromState()` used `root.elementsByTagName(KeyElement)` and took the FIRST
  `<key>`, so a plugin whose own state carries a `<key>` had it read as the host key; the undo then
  rebuilt the wrong sub-plugin variant or refused the document. The writer appends the wrapper's key
  LAST, as a direct child of the device's state element (`src/core/ControlDeviceSupport.cpp:483-489`).
* Fix: `wrapperHostKeyElement()` (`src/core/ControlStructuralSupport.cpp:377`) reads the last direct
  `<key>` of the state element; `deviceKeyFromState()` (`:391`) uses it.
* Regression: `tests/src/core/ReversibilityUndoDeviceTest.cpp:503`
  `theRestoreReadsTheWrappersHostKeyAndNotThePluginsNestedOne()` - a live device's document with a
  decoy key nested in the plugin's state and the wrapper's own key appended last.
* RED: same 82/50 revert run **EXIT=8** (`FAIL! : ...theRestoreReadsTheWrappersHostKey... 'restored
  != nullptr' returned FALSE`); GREEN restored **EXIT=0**.

---

## The merge conflict: `tests/src/core/ScriptDawRemovalUndoTest.cpp`

The accepted branch's new-p14-82 commit changed `aRedoAfterAFailedUndoRemovesNothing()` to create its
deliberately invalid journal entry through the lower-level writer, because `journalRemovalDocument()`
now refuses that document. On the newest line the same case still called
`QVERIFY(control::journalRemovalDocument(chain, first, 0, document.toString()))`
(`tests/src/core/ScriptDawRemovalUndoTest.cpp:485` before this port), which no longer compiles once
the writer returns `bool`. The case is re-applied by hand here (`:484-514`): it records the
unusable document through `control::addStructuralUndoStep`, pausing journalling around the closures
exactly as the accepted commit did, so the case still starts from an already-recorded step and still
proves the redo removes nothing. `QScopeGuard` and `<memory>` are already/also included.

## Callers and siblings checked

* The four structural helpers and `journalChainRemoval` (above).
* `track.remove` / `track.remove` with lanes (`src/core/ControlCommandsArrangement.cpp:305-338`),
  `track.move` (`src/core/ControlCommandsStructure.cpp:128`), the GUI track delete
  (`src/gui/tracks/TrackOperationsWidget.cpp:331-332`), `plugin.unload` and the script's
  `removeEffect` (`src/core/ControlStructuralSupport.cpp:678` -> `journalEffectRemoval`).
* `rack.remove_chain` reads `journalChainRemoval()`'s answer into its transaction
  (`src/core/ControlCommandsRack.cpp:224-245`).
* The OTHER writer callers (`ControlCommandsDawProject.cpp:546`, `ControlCommandsPluginPreset.cpp:372`,
  `ControlCommandsRenderBounce.cpp:256`, `ControlCommandsTrackFlatten.cpp:109`,
  `SessionCheckpoint.cpp:72`, `ControlDeviceMoves.cpp:69`, `:145`) are `void`-context callers: a
  real step or none, no transaction claims anything from the answer, so ignoring the return is
  behaviour-preserving. They now simply know the step may have been refused, as before.
* `control.undo`'s inverse dispatch (`src/core/ControlCommandsControl.cpp:383-405`,
  `:432-444`) already honours `applies: command` and `applies: journal_later`, which is why the
  registry downgrade keeps those two.

## No collateral damage

No document format changed. `controlEffectStateXml()` is untouched, so saved device documents read
the same; the new-p16-50 reader reads exactly the element that writer appends last. The new-p14-82
check is the rule the restore already applied to the same documents, so it refuses nothing the undo
could have replayed - proven by "the device's own document is still recorded". The new-p14-81 check is
the journal's own `isJournalling()`, so a removal in a normal session records exactly as before. No
limit was added. Old `all-sources.txt` entries are not product data; see the gate section.

## Gates

| Gate | Exact command | EXIT | Result |
| --- | --- | --- | --- |
| Feature gates (worktree path) | `python3 .../feature_check.py <worktree>` | 1 | `PermissionError` creating `<worktree>-feature-check` beside the worktree - the sandbox denies writes outside the worktree. Not a gate result. |
| Feature gates (hard-link mirror) | `python3 .../feature_check.py /home/kruzzzzy/.cache/zene-gate-mirror/zene-qwen-new-p14-81-port` | **0** | `RESULT: PASS` - all eight rows green: fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0, complexity "no new item in your files", file-length "no new item in your files" |
| Gate 9 by hand (after the manifest repair) | `bash tests/fork-sources-gate.sh` | **0** | "PASS: every tracked source in scope is registered (1976 fork-NEW, 1214 inherited, 48 tooling)", 0 stale |
| Whole-tree recipe | `bash tests/all-sources-reproduce.sh` | **0** | "REPRODUCES: the entry list in all-sources.txt is the recipe's own output." |
| Ratchets by hand | `bash tests/complexity-gate.sh --check` / `bash tests/file-length-gate.sh --check` | 1 / 1 | The train's own red rows (7 and 19 entries). Cross-checked against the cached base listing for `07d6d02bd`: **zero** of them are in a file this branch changed |
| Completion check (worktree path) | `python3 .../submit_check.py <worktree> new-p14-81-port` | 1 | every row PASS except `gates (feature_check.py)` - the same sandbox `PermissionError` as the row above |
| Completion check (mirror) | `python3 .../submit_check.py /home/kruzzzzy/.cache/zene-gate-mirror/zene-qwen-new-p14-81-port new-p14-81-port` | **0** | `RESULT: PASS - submit`: commits on the branch, working tree clean, report, tests changed, gates - all PASS; the five ASK items are answered under Self-check |

### The two gate rows were red at HEAD, and this branch repairs them

`bash tests/all-sources-reproduce.sh` fails on the untouched HEAD too (proven: the HEAD copy of the
manifest, run through the same script, EXIT=1). Train-12's `07d6d02bd` re-added 1148 vendored
`plugins/SfzPlayer/sfizz/...` entries without touching the recipe in the manifest header, undoing the
repair an earlier accepted port (`new-p14-199-port`) had made. The repair here follows the gate's own
instruction ("run the 'Regenerate with' command ... and write its output over the entry list"):

* `tests/all-sources.txt`: 1148 deletions, exactly the sfizz entries (`git diff -U0 ...
  | grep -c '^-plugins/SfzPlayer/sfizz/'` -> 1148), plus a dated header note. The remaining entries
  equal the recipe's output byte for byte (`bash tests/all-sources-reproduce.sh` -> REPRODUCES).
* No ratchet baseline, limit or recipe was changed, and no path the recipe derives was dropped.

## Log folder

`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-81-port.r1/logs` is **not writable in
this session** even though it is owned by the running user: every write there returns
`Permission denied` under the session's sandbox (Landlock; `~/.local/share/...` is denied while the
worktree is writable). The numbered build/test/gate logs for this run are therefore in
`/tmp/new-p14-81-port-r1-logs/` (permission probe: `echo x > .../logs/probe` -> `Permission denied`).
`feature_check.py` was additionally run against a hard-link mirror of the worktree in
`/home/kruzzzzy/.cache/zene-gate-mirror/` so its own `<worktree>-feature-check` log folder could be
created, with all eight rows passing.

## Files committed

Product: `include/ControlDeviceMoves.h`, `include/ControlReversibility.h`,
`include/ControlStructuralSupport.h`, `src/core/ControlDeviceMoves.cpp`, `src/core/ControlRegistry.cpp`,
`src/core/ControlReversibility.cpp`, `src/core/ControlStructuralSupport.cpp`.

Tests: `tests/src/core/ControlRegistryTest.cpp`, `tests/src/core/ReversibilityUndoDeviceTest.cpp`,
`tests/src/core/ScriptDawRemovalUndoTest.cpp`, `tests/src/core/UndoHonestyStateTest.cpp`.

Ledger: `tests/all-sources.txt` (the pre-existing red gate repair above).

No new product file, so nothing new for `tests/fork-sources.txt`; no upstream file changed, so nothing
for `tests/upstream-modifications.txt`. No control command was added or changed, so the
`ControlCommandsSnapshot` canary is untouched.

## Self-check

**The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Yes,
and the evidence is above: `git apply -R .../fix-final.patch` EXIT=0, rebuild EXIT=0,
`ctest -R '^UndoHonestyStateTest$'` **EXIT=8** with all three new-p14-81 cases named in the output,
`git apply .../fix-final.patch` EXIT=0, rebuild EXIT=0, the five-target family **EXIT=0**. The same
was proven separately for new-p14-82 and new-p16-50 (EXIT=8 with their hunks reverted, EXIT=0
restored).

**The test is deterministic: no wall-clock timing, sleeps or races.** Yes. new-p14-81 uses
`ProjectJournal::setJournalling(false)` as its seam - the engine's own flag, the switch `JournalPause`
uses - and reads the recorded transaction; no clock, sleep, thread or timing assertion. The rack case
uses the same flag. The registry case registers a test command and reads `lastTransaction()`. The
device cases build their documents in memory with `QDomDocument`; no device is timed.

**The fix is at the cause and complete: every caller and sibling of what you changed was checked and
stays consistent.** Yes: the writer is the one place that knows whether a step was pushed; its answer
is propagated through all four track/device helpers and `journalChainRemoval`; the registry closes the
handler-claim path; every caller is listed above with what it does with the answer.

**No collateral damage: old projects and saved data still load as before, and any limit or check you
added does not hit other callers that legitimately exceed it.** Yes. No format, field or bound changed.
The new-p14-82 check is the restore's existing rule applied earlier; the regression proves the
device's own document is still recorded. The new-p14-81 check is the journal's own condition, so the
normal session path is unchanged. The `all-sources.txt` repair removes only the vendored tree the file
itself excludes; no product path was dropped.

**If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract
that shows it.** Not applicable: all three verdicts are FIXED, and the pre-fix code and contract are
cited in each section (`src/core/ControlStructuralSupport.cpp:253-291` and
`src/core/ControlReversibility.cpp:165-171` for new-p14-81; `:641-674` for new-p14-82; `:342-349` for
new-p16-50).
