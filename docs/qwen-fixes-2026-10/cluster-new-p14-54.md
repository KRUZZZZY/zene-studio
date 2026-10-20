# Cluster new-p14-54: undo/redo of an audio take landing and the modified flag

**Verdict: FALSE-POSITIVE**

The finding says the undo and redo callbacks of the take landing (`src/core/ControlCommandsSessionAudioRecord.cpp:239-240`)
must call `setModified()`, or a save followed by undo leaves the song marked clean. They do not need to. Every undo and
every redo the journal applies already marks the song modified, whatever the step's own callbacks do. The callbacks
follow the same pattern as their siblings, and a new test case proves the behaviour: it passes on the unchanged code, and
it fails with the exact symptom of the finding when the journal's own mark is removed.

## Evidence (file:line)

- Claim site: `src/core/ControlCommandsSessionAudioRecord.cpp:238-241`. `applyLanded` runs, then `addUndoStep` at :239-240
  takes two lambdas that restore the `<session>` block or re-apply the cell, with no `setModified()`. :241 marks the song
  modified only for the initial landing.
- The undo marks the song: `src/core/ProjectJournal.cpp:112-151`, `ProjectJournal::undo()`. An action-only step (what
  `addJournalAction` records, `:252-264`) runs its undo action, so `did` is true (:143), and then
  `Engine::getSong()->setModified();` at **:148** runs.
- The redo marks the song: `src/core/ProjectJournal.cpp:155-188`, `ProjectJournal::redo()`, `setModified()` at **:185**.
- The flag is real: `src/core/Song.cpp:888-895` (`setModified(bool)` sets `m_modified` and emits `modified`).
- Every undo and redo goes through that journal: `control.undo` → `undoThroughJournal` → `journal->undo()`
  (`src/core/ControlCommandsControl.cpp:331-335`); `control.redo` → `journal->redo()` (`:546-548`); the GUI Ctrl+Z and
  redo call `Engine::projectJournal()->undo()/redo()` (`src/gui/MainWindow.cpp:1640-1651`).
- Sibling convention, which the finding would break: `src/core/ControlCommandsAlternatives.cpp:136-137` and
  `src/core/ControlCommandsCueMix.cpp:193-194` pass undo/redo lambdas that do not mark the song; the command marks it
  once. Undo marking is the journal's job, not each lambda's.
- The agent path refuses the scenario outright: `control.undo` after `project.save` returns a refusal (`cannot undo
  'project.save' (class 'snapshot')`, built by `irreversibleUndoMessage`, `src/core/ControlCommandsControl.cpp:258-269`),
  because the last recorded transaction is the save, which is not reversible. This was observed in the test run (see
  `test-2-diag.log`). The save is not a journal step: after the save, `journal->undo()` reverted the landing (test
  line 186, passed).

## Test

`tests/src/core/SessionAudioRecordTest.cpp:164-197`: new case `undoAndRedoOfALandedTakeMarkTheSongModified` (the file is
already registered in `tests/CMakeLists.txt`, so nothing new is registered). It records one bar, lands it, saves, undoes and
redoes through `ProjectJournal` (the path the GUI uses), and asserts the song is marked modified after the undo (:187) and
after the redo (:196), and clean after each save. Include added: `#include "ProjectJournal.h"` (:53).

This is a characterisation test, not a RED/GREEN regression: there is no defect to fix, so it passes on the original code.
Its sensitivity was checked instead (below): it fails when the mechanism the verdict relies on is removed.

## Commands and exit codes (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-54.r1/logs`)

| Step | Command (run from the worktree / `build/tests`) | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target SessionAudioRecordTest -j4` | 2 | `build-1-baseline.log` (ccache could not write to `/run/user/1000`; environment, not code) |
| Build, with ccache temp in /tmp | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-54 cmake --build build --target SessionAudioRecordTest -j4` | 0 | `build-2-baseline.log` |
| First test run (test used `control.undo`) | `ctest -R SessionAudioRecord --output-on-failure` | 8 | `test-1-baseline.log`: my test called `control.undo` after the save and was refused. Fixed by using the journal (see above). |
| Diagnostic run (refusal reason) | `ctest -R "^SessionAudioRecordTest$" --output-on-failure` | 8 | `test-2-diag.log`: refusal text, `cannot undo 'project.save' (class 'snapshot')` |
| Baseline (unchanged product code), after fix to test | `CCACHE_TEMPDIR=... cmake --build build --target SessionAudioRecordTest SessionAudioRecorderTest -j4` | 0 | `build-4-baseline.log` |
| Baseline family run | `ctest -R SessionAudioRecord --output-on-failure` (in `build/tests`) | 0 | `test-3-baseline.log`: 100% passed, 2 of 2 (`SessionAudioRecorderTest`, `SessionAudioRecordTest`) |
| New case alone | `./SessionAudioRecordTest undoAndRedoOfALandedTakeMarkTheSongModified -v2` | 0 | `test-4-newcase-baseline.log`: `PASS undoAndRedoOfALandedTakeMarkTheSongModified()`, Totals 3 passed |
| Sensitivity (mutation): remove the journal's undo mark | `sed -i '148{/Engine::getSong()->setModified();/d}' src/core/ProjectJournal.cpp`, then the build, then the run | build 0 / test **1** | `build-5-mutation.log`, `test-5-mutation.log`: `FAIL! ... 'Engine::getSong()->isModified()' returned FALSE. (the undo changed the cell but left the song marked clean)` at `SessionAudioRecordTest.cpp:187` |
| Restore | `git apply -R …/logs/mutation-journal-undo.patch` | 0 | `git status` then shows only the test file |
| Rebuild after restore and run | build, then `./SessionAudioRecordTest undoAndRedoOfALandedTakeMarkTheSongModified` | build 0 / test 0 | `build-6-restored.log`, `test-6-restored.log`: Totals 3 passed |
| Gates, as written | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `gates-1.log`: `PermissionError` creating `zene-qwen-new-p14-54-feature-check` beside the worktree (outside the write-jail). Gates did not run. |
| Gates, log folder redirected | `python3 /tmp/gate-p14-54/feature_check.py <worktree>` (copy of the same script; only `ROOT` pointed at the qwen folder, and the log folder moved to `/tmp/gate-logs-p14-54`) | **0** | `gates-2.log`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures PASS; complexity and file-length "no new item in your files"; `RESULT: PASS` |

Tests executed: the `SessionAudioRecord` family has 2 ctest entries (both ran). The `SessionAudioRecordTest` binary runs 4
test functions (3 existing + the new case) between `initTestCase` and `cleanupTestCase`.

## Product files changed

None. The verdict is FALSE-POSITIVE, so no product file is changed. The only file committed is
`tests/src/core/SessionAudioRecordTest.cpp` (the characterisation case and the `ProjectJournal.h` include). The sensitivity
mutation was reverted and the file is clean (`git status` shows only the test file, and it is restored byte-for-byte to
the committed version).

## Self-check

- Each new test FAILS with the product change reverted: no product change to revert. The sensitivity check is the
  equivalent: removing the journal's undo mark (the mechanism the verdict relies on) makes the test fail with the
  finding's message (`test-5-mutation.log`, exit 1), and restoring it passes (`test-6-restored.log`, exit 0).
- The regression test is deterministic: no wall clock, sleeps or races. The take is recorded with the test's own
  period driver (`recordUntilDone`, as the neighbouring cases do); the save and undo/redo are synchronous calls.
- The fix is at the cause: not applicable (no defect). The claim's cause is the one at :148 and :185 in the journal, which
  the finding did not name.
- No collateral damage: no product code changed; no save format, command or limit touched.
- Verdict evidence: the code citations above (file:line) plus the runtime check.
- Environment deviations, stated plainly: (1) ccache needed `CCACHE_TEMPDIR` under `/tmp` (the default `/run/user/1000`
  was not writable). (2) The gate script could not run as written (its log folder is outside the write-jail); the copy in
  `/tmp` changes only the two path lines. Gate logic, gate limits and baselines are unchanged; the baseline JSON for base
  `2da2ed356` was already cached and was only read.
