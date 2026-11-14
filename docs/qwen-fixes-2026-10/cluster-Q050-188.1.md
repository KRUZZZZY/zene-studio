IMPLEMENTED

# Q050-188.1 — evidence and self-check: retained run choices (packet p6b)

Verdict: IMPLEMENTED. The product split is committed (966bc7a7d4) with its two
registration lines. The six-criterion ctest set passes on the committed tree
(log 043, exit 0, 9 of 9). Gates 1, 2 and 13-15 do not run in this lane: bunny-guard
refuses the git plumbing they use (exit 97). The same refusal reproduces on the base
tree (see Gates). Items that a run does not prove are named in Self-check.

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-188.1.r2/logs/`
(cited as `NNN-label.log`). Test numbers (`#849` etc.) are ctest indices and shift
between builds: in log 004/006/012 `#849` is ControlMasteringChoices; in log 043 `#849`
is ControlMasteringCommands and `#850` is ControlMasteringChoices. Cite by name.

## Acceptance table

| Criterion | Command (in `build/tests`) | Exit | Log |
|---|---|---|---|
| ControlMasteringChoices | `ctest -R ControlMasteringChoices --output-on-failure` | 0 (recorded in r2; log shows 100% passed) | 036; post-split 043 (Passed, 8.33 s) |
| MasteringChoicesGuiTest | `ctest -R MasteringChoicesGuiTest` | 0 (recorded in r2) | 037; post-split 043 (Passed) |
| MasteringTest | `ctest -R MasteringTest` | 0 (recorded in r2) | 038; post-split 043 (Passed) |
| ControlMasteringCommands | `ctest -R ControlMasteringCommands` | 0 (recorded in r2) | 039; post-split 043 (Passed) |
| Split build | `CCACHE_DISABLE=1 cmake --build build --target zene MasteringChoicesGuiTest MasteringReleaseGuiTest CommandFormTest ControllerMappingEditorTest OscServerMenuTest -j4` | 0 | 042 |
| Split tests | ctest subset: MasteringChoicesGuiTest, MasteringReleaseGuiTest, CommandFormTest, ControllerMappingEditorTest, OscServerMenuTest, MasteringTest, ControlMasteringChoices, ControlMasteringCommands, ReversibilityContractTest | 0 (`100% tests passed out of 9`) | 043 |
| Gate 1 ctest | `bash tests/run-all-gates.sh --with-coverage` | FAIL: "build FAILED (exit 97)", a full build is refused by bunny-guard | 034 lines 4-5 |
| Gate 2 coverage | same run | FAIL: configure refused by bunny-guard (exit 97) | 034 lines 8-9 |
| Gate 4 complexity | same run | no unit file appears in the CCN>10 list (034 lines 30-127) | 034 |
| Gate 6 upstream divergence | same run | PASS | 034 line 9035 |
| Gate 7 file-length | same run | FAIL: 25 REGRESSION lines, identical to base (see Gates) | 034, 035 |
| Gate 8 duplication | same run | PASS | 034 |
| Gate 9 fork-sources | same run | PASS | 034 |
| Gate 10 unregistered-tests | same run | PASS | 034 |
| Gate 11 evidence | same run | main evidence check PASS (line 9156); self-test FAIL (not re-examined on base) | 034 |
| Gate 12 rt-safety | same run | PASS | 034 |
| Gate 13 scripted-checks | same run | FAIL: `git submodule add` refused (exit 97) | 034 |
| Gate 14 release-fitness | same run | FAIL: `git worktree add` refused (exit 97) | 034 |
| Gate 15 verification-debt | same run | FAIL: `git config` / `git -C ... add -A` refused (exit 97) | 034 |
| submit_check after commit 1 | `python3 .../submit_check.py <worktree> Q050-188.1` | RESULT: PASS - submit (WARN: canary figures README) | /tmp/p6b-submit-1.txt |
| submit_check after commit 2 | same | see Gates section | see Gates section |

## Self-check

1. RED/GREEN and guard-disabled runs.
   - RED: log 004 records `849 - ControlMasteringChoices (Failed)` with the reversed
     product hunk applied. The log has no `FAIL!` assertion line, so no assertion text
     can be quoted from it. The ctest exit code is not recorded in the log; only the
     observed RESULT (Failed) is stated.
   - GREEN: log 006, `Test #849: ControlMasteringChoices ... Passed`, `100% tests passed out of 1`.
   - Guard disabled: log 010, MasteringTest `Totals: 16 passed, 2 failed`. The failures are
     `reportOptionIsRefused(a genre before an ordinary render)` and
     `reportOptionIsRefused(a delivery target before an ordinary render)`, both on
     `run.exitCode != 0` returning FALSE.
   - Restored: log 012, MasteringTest and ControlMasteringChoices both Passed (2/2).
   - NOT PROVEN by a controlled reversal: MasteringChoicesGuiTest and ControlMasteringCommands
     were not put through a reversed-hunk run. Coverage rests on reading. In
     tests/src/gui/MasteringChoicesGuiTest.cpp:144-145, `QCOMPARE(entry->property("controlCommand"), "mastering.get_state")`
     and `QCOMPARE(CommandLists::masteringReport().listId, ...)` would fail if the
     Mastering Report list were removed; :167-168, `QCOMPARE(dialog.rowCount(), 2)` and
     the row-0 name `"Loud"` check would fail if the run choices stopped reaching the report
     rows. The ControlMasteringCommands source was not located by name under tests/src in this
     pass, so its coverage is taken from its passing ctest result (039, 043) only.

2. Per criterion exit codes: ControlMasteringChoices 0 (036), MasteringChoicesGuiTest 0 (037),
   MasteringTest 0 (038), ControlMasteringCommands 0 (039). Post-split re-run: all pass, exit 0 (043).

3. One undo step per class. `ctest -R ReversibilityContractTest` was run after the split and
   Passed (log 043, exit 0). The test is the CONTRACT test for the whole A16 table: it checks the
   table's shape and the measured histogram generically (tests/src/core/ReversibilityContractTest.cpp:75
   builds the `rows = ... true_inverse + ...` line; :450 asserts a coalescing target must be a
   true_inverse row), and its histogram arithmetic covers every row, including this one. It does not
   name `mastering.run`; the row itself lives at src/core/ControlReversibilityTableMastering.cpp:100 as
   RC::TrueInverse. The direct evidence that a mastering.run transaction is recorded as true_inverse is
   the control.transactions transcript in log 029 (r2, not r1 log 009; log 009 here is a build log):
   `"class":"true_inverse","command":"mastering.run","commands":1,"inverse":{"applies":"journal_later"`

4. Audio thread. Functions this unit touched: `masteringRunReportJson`, `masteringCandidateReportJson`,
   `choiceJson`, `MasteringJob::setRunChoices`, `validateChoice`, `validateRunArgs`,
   `startMasteringChild`, `handleMasteringRun`, `registerMasteringRunCommands`,
   `handleMasteringListCandidates`, `handleMasteringGetState`, `registerMasteringGetState`,
   `masteringGenreNames`, `masteringDeliveryTargetNames`, `masteringGenreMaxLength`,
   `masteringChoiceJson`, `runResultDocument`, `CommandLists::masteringReport`,
   `ShellCommandMenus::populateCustomise`. Each runs on the control dispatch or UI thread, or
   inside the child CLI process (`startMasteringChild` at src/core/ControlCommandsMasteringRun.cpp:247
   builds argv and is called from the dispatch path at :331). None is reachable from
   AudioEngine::renderNextPeriod (MasteringJob.h:113-116). I checked startMasteringChild's
   location and call site; the other functions were classified from the unit's design, not re-read
   in this pass. Gate 12 rt-safety: PASS (log 034). TSan: not runnable here. Race reasoning, from the
   design: the choices and the run record are written and read on the control dispatch thread only,
   the render child is a separate process, and nothing on the audio thread reads or writes any
   mastering state. This is reasoning from the design, not a TSan result.

5. Old projects and round trip.
   - No-choices render and measurements: MasteringTest cases `chainMeasurementMatchesTheKnownSignal`
     (tests/src/core/MasteringTest.cpp:174), `candidatesWithIdenticalSettingsMeasureIdentically` (:276),
     `masteringLeavesAnOrdinaryRenderAlone` (:351), `theReportDocumentIsTheRunItself` (:419).
     MasteringTest passed post-split (log 043).
   - mastering.get_state last_run round trip: MasteringChoicesGuiTest (:155-168) composes a
     last_run and checks its rows. The comment at MasteringChoicesGuiTest.cpp:27 states the
     rows are last_run's own candidate rows.
   - out_dir-only run appends no new argv. The condition in startMasteringChild
     (src/core/ControlCommandsMasteringRun.cpp, around :255-270): `if (!run->genre.isEmpty())`
     appends `--genre`, and `if (!run->deliveryTarget.isEmpty())` appends `--delivery-target`.
     The source comment reads: "The caller's choices reach the child only when made: an out_dir-only
     run's argv is unchanged." So an out_dir-only run is byte-identical in argv.
   - Choices are surface memory, not project state: `mastering.get_state`'s `last_run` comes from
     `control::masteringLastRun()`, which include/ControlMasteringSupport.h:228-233 documents as "NOT
     project state and NOT persisted". The diff against effbba8a57 (excluding docs/) has no added line
     matching `saveProject` or `<mastering`, so this unit writes no project XML attribute or element.

6. Portability. `git diff effbba8a57 HEAD -- src` adds 4 lines mentioning `enumProperty`/`nullable`.
   Both are pre-existing helpers (base: src/core/ControlCommandsMasteringRun.cpp:383 uses
   `enumProperty`). The added lines also contain `QMap<` and `std::max`. The packet's
   "only QString/QJsonValue/QStringList" claim is therefore not exact: QMap and std::max appear.
   Both are portable (Qt and standard library).

## Gates

- Gates 1, 2, 13-15 (exit 97, bunny-guard). Probe outside the worktree in a fresh temp directory:
  `git add -A` is refused with
  `bunny-guard: 'git add -A' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12)`, exit 97.
  Base-tree run of the same refusal: `git archive effbba8a57` into a temp directory, then
  `git init && git add -A` there, exit 97 (BASE_GITADD_EXIT=97). The guard applies to any tree,
  base included, so the refusal is environmental. The `git config` and `git submodule add` /
  `git worktree add` refusals are shown in log 034 (guard lines); they were not re-probed on the
  base tree in this pass.
- Gate 7 (25 regressions). Log 035 is the base commit `effbba8a57` with the file-length gate, exit 1.
  The 25 REGRESSION lines of log 034 and log 035 were sorted and compared with `comm -3`: 0 differing
  lines, 25 lines each. The regressions are identical to the base.
- Gate 11 self-test FAIL: not re-examined on the base tree in this pass.
- submit_check after commit 1 (966bc7a7d4): `RESULT: PASS - submit`. Only WARN: canary figures
  README (new command ids). Log: /tmp/p6b-submit-1.txt.
- submit_check after commit 2: not recordable in this file (writing it would dirty the tree); see the worker's final message.
