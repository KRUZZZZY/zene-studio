BLOCKED

# Q050-188.1 — evidence and self-check: retained run choices (packet p6)

Verdict: BLOCKED. Gate 1 (ctest) and Gate 2 (coverage) cannot run in this lane
(bunny-guard exit 97), so the full gate run is not green. Several ASK items are
not proven by a run. Everything not listed as proven below is stated as not proven.

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-188.1.r2/logs/`
(cited as `NNN-label.log`). The quiet runner records the exit code in its stdout
line, which is reproduced below where the log exists.

## Acceptance table

| Criterion | Command (worktree root unless noted) | Exit | Log |
|---|---|---|---|
| ControlMasteringChoices | `ctest -R '^ControlMasteringChoices$' --output-on-failure` in `build/tests` | 0 | 036 |
| MasteringChoicesGuiTest | `ctest -R '^MasteringChoicesGuiTest$'` in `build/tests` | 0 | 037 |
| MasteringTest | `ctest -R '^MasteringTest$'` in `build/tests` | 0 | 038 |
| ControlMasteringCommands | `ctest -R '^ControlMasteringCommands$'` in `build/tests` | 0 | 039 |
| zene target build | `CCACHE_DISABLE=1 cmake --build build --target zene -j4` | 0 | 033 (032 was exit 2, ccache permission denied) |
| Gate 1 ctest | `bash tests/run-all-gates.sh --with-coverage` | FAIL: "build FAILED (exit 97)", a full build is refused by bunny-guard | 034 lines 4-5 |
| Gate 2 coverage | same run | FAIL: configuring refused by bunny-guard (exit 97) | 034 lines 8-9 |
| Gate 4 complexity | same run | no unit file appears in the CCN>10 list (034 lines 30-127); summary row not re-read | 034 |
| Gate 6 upstream divergence | same run | PASS line present (034 line 9035) | 034 |
| Gate 7 file-length | same run | FAIL, 25 REGRESSION lines, identical to the base commit (see below) | 034, 035 |
| Gate 8 duplication | same run | PASS | 034 |
| Gate 9 fork-sources | same run | PASS (MasteringChoicesGuiTest.cpp registered in all-sources.txt) | 034 |
| Gate 10 unregistered-tests | same run | PASS | 034 |
| Gate 11 evidence | same run | main evidence check PASS (line 9156); self-test 9 FAIL (see Gate 11 below) | 034 |
| Gate 12 rt-safety | same run | PASS | 034 |
| Gate 13 scripted-checks | same run | FAIL, `git submodule add` refused by bunny-guard | 034 |
| Gate 14 release-fitness | same run | FAIL, `git worktree add` refused by bunny-guard | 034 |
| Gate 15 verification-debt | same run | FAIL, `git config` / `git add -A` refused by bunny-guard | 034 |
| submit_check (before this report) | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> Q050-188.1` | 1, RESULT FAIL | 040 |
| submit_check (after this report) | same | see final section | see final section |

Base-commit comparison for Gate 7 (`effbba8a57`, extracted with `git archive` to a
temp directory, file-length gate in check mode): exit 1, log 035. The 25
REGRESSION lines are identical to those of the unit tree (log 034). `sort -u` of
each set, then `comm`: no line only in the unit, no line only in the base. The
regressions are pre-existing and this unit adds none. The ShellCommandMenus.cpp
regression that this unit did cause is resolved (see Notes).

Gate 11 self-test 9 (log 034, lines 9144-9148): a tracked 2 MB file in a throwaway
repository. The fixture calls `git add -A`, which bunny-guard refuses (reproduced
in a probe directory: `git add -A` exit 97). The index stays empty, so the gate
exits 0 instead of 1. The fixture's stderr goes to /dev/null, so the guard line
does not show in the gate log. This is an environment cause, not a product cause.
Not run against the base commit.

Gate 13 (log 034, lines 9178-9197): `tests/scripted/verify` calls `git submodule add`,
which bunny-guard refuses. Traceback ends in `CalledProcessError`. Environment cause.

Gate 14 control 3/4 (log 034, line 9226-9237): `error: cannot create the scratch
worktree`. `git worktree add` at tests/test-release-ref-fitness.sh:125 is refused
(reproduced: exit 97, no worktree created). Environment cause.

Gate 15 defects 2-3 (log 034, lines 9277-9299): `git config`, `git add -A` in the
forksrc fixture refused by bunny-guard. Environment cause.

## Self-check

ASK items quoted verbatim from submit_check (log 040).

**1. "Each new test FAILS with your product change reverted (reverse the non-test hunks, rebuild, run: exit code?), then PASSES restored."**

Partially proven. A controlled RED is shown for two of four tests (MasteringTest; ControlMasteringChoices, whose reversed-run exit code is not recorded). Not shown for MasteringChoicesGuiTest or ControlMasteringCommands.
- ControlMasteringChoices: reversed-hunk run, log 004: `1/1 Test #849: ControlMasteringChoices ***Failed`. Restored run, log 006: `100% tests passed out of 1`; re-run today, log 036, exit 0. The exit code of the reversed run is NOT recorded in the log, and the log does not show "11 failed checks"; those figures from the earlier notes are not verified here.
- MasteringTest: guard-disabled run, log 010: `Totals: 16 passed, 2 failed`. Restored, log 012: `100% tests passed out of 2`; re-run today, log 038, exit 0.
- MasteringChoicesGuiTest: failures in logs 015, 017, 019 are development-time failures, not a controlled reversal of the product hunks. Green in log 021 (3/3). Controlled RED not run: NOT PROVEN.
- ControlMasteringCommands: green in log 002 (4/4), re-run today log 039 exit 0. No reversed run: NOT PROVEN.

**2. "Every acceptance criterion is proven by a test you ran yourself (name the test and its exit code per criterion)."**

Partially proven. Exit codes observed on this tree today:
- ControlMasteringChoices: 0 (log 036)
- MasteringChoicesGuiTest: 0 (log 037)
- MasteringTest: 0 (log 038)
- ControlMasteringCommands: 0 (log 039)
Gate 1 (full ctest) and Gate 2 (coverage) did not run (guard exit 97). Not met for the full gate set.

**3. "Each edit is ONE undo step, and every new or changed command has an honest reversibility class."**

Partially proven, by reading only.
- `mastering.run` is classed `RC::TrueInverse` (src/core/ControlReversibilityTableMastering.cpp:100). Its note cites the reversibility contract (src/core/ControlReversibilityTableMastering.cpp:95-119, and the anti-drift test in tests/src/core/ReversibilityContractTest.cpp). That test was NOT run in this packet.
- `control.transactions` exists (src/core/ControlCommandsControl.cpp:231) and holds before-states (note at line 282). It is the record the undo-step claim relies on.
- "One undo step per edit" is not proven by a test run in this packet.

**4. "Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal."**

Reading-based, with one gate. Gate 12 rt-safety sweep: PASS (log 034, line 9176).
- Threading note, include/MasteringJob.h:113-116: "run() is blocking and executes on the calling (CLI or test) thread ... Nothing here touches the audio callback, and no part of it is reachable from AudioEngine::renderNextPeriod()."
- Files touched by this unit (none is reachable from AudioEngine::renderNextPeriod, per the note above and the review of the diff): src/core/main.cpp, include/MasteringJob.h, src/core/MasteringJob.cpp, include/MasteringReport.h, src/core/MasteringReport.cpp, include/ControlMasteringSupport.h, src/core/ControlMasteringSupport.cpp, src/core/ControlCommandsMastering.cpp, src/core/ControlCommandsMasteringRun.cpp, src/core/ControlReversibilityTableMastering.cpp, src/gui/CommandLists.cpp, include/CommandLists.h, src/gui/shell/ShellCommandMenus.cpp, src/gui/shell/ShellCommandMenusCustomise.cpp (new), tests/src/core/MasteringTest.cpp, tests/src/gui/MasteringChoicesGuiTest.cpp.
- TSan: NOT runnable on this box, so no sanitizer evidence. Race reasoning: the choices are copied into the run struct on the dispatch thread and read only there, in completeMasteringRun. No audio-path change.

**5. "Old projects still load and render exactly as before; new data round-trips through save and load."**

NOT PROVEN in this packet. No save/load or render run was executed today. The MasteringTest and MasteringChoicesGuiTest runs (logs 038, 037) pass, but I did not verify that they cover old-project load or the save/load round trip.

**6. "Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro names, no std::filesystem outside _WIN32."**

Checked by grep on the touched files (include/MasteringJob.h, include/MasteringReport.h, include/ControlMasteringSupport.h, src/core/MasteringJob.cpp, src/core/MasteringReport.cpp, src/core/ControlMasteringSupport.cpp, src/core/ControlCommandsMastering*.cpp, src/core/ControlReversibilityTableMastering.cpp, src/gui/CommandLists.cpp, src/gui/shell/ShellCommandMenus*.cpp, tests/src/core/MasteringTest.cpp, tests/src/gui/MasteringChoicesGuiTest.cpp) for `windows\.h`, `std::filesystem`, `Qt6`, `QT_VERSION`: no matches. The grep covers only the named patterns, not every Qt 6 API.

## Snapshot note

tools/mcp-zene-control/zene_control/commands_snapshot.json (committed in 2d12e735a6)
names this worktree as its lane path (`.../zene-feat-q050-188.1/tools/zene-pa-agentctl`)
and reports version `0.4.0-alpha.6727+a6861ca`, in the `instance` block (captured_at
2026-11-13T15:29:31Z). That is normal capture churn from live regeneration.

## Notes

- Gate 7 ShellCommandMenus.cpp: this unit's one-line addition took it to 501 code lines.
  The fix was to move `populateCustomise` out, to src/gui/shell/ShellCommandMenusCustomise.cpp
  (new, 76 lines). ShellCommandMenus.cpp is no longer in the regression list. No re-anchor and
  no trimming.
- Gate 9: tests/all-sources.txt regenerated with the header recipe. The diff adds exactly
  `src/gui/shell/ShellCommandMenusCustomise.cpp` and `tests/src/gui/MasteringChoicesGuiTest.cpp`.
- src/core/main.cpp: not re-diffed in this packet. Its unit-level diff is listed above.
- Working tree: the code changes (ShellCommandMenus.cpp split, ShellCommandMenusCustomise.cpp,
  src/gui/CMakeLists.txt, tests/fork-sources.txt, tests/all-sources.txt) are NOT committed.
  The packet says to commit only the report, so they stay in the tree. This is why submit_check
  reports "working tree clean" as FAIL.
- Bunny-guard refused `git worktree add`, `git worktree list`, and `git worktree remove`. No
  worktree was created or removed; the base-tree proof used `git archive` instead.
