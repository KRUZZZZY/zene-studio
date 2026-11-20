FIXED

# Cluster new-p11-69: crash report count wording

The bug: `crashReportStatusText` in `src/gui/CrashReportMenu.cpp` formatted every
nonzero `report_count` as "one report", so a count of two or more was displayed
with singular wording ("Armed, one report not yet offered").

The fix (round 1, commit `9886cb604a`, on this branch and an ancestor of HEAD
`a70a1a5466`) formats the status from `report_count` with plural wording:
`src/gui/CrashReportMenu.cpp:103-120` now has three cases — 0 → "%1, no report",
1 → "%1, one report (not yet offered | already offered)", and >1 →
"%1, %2 reports (not yet offered | already offered)". The zero and one-report
wordings are byte-identical to before; only the >1 branch is new.

Round 10 (this session) re-verified everything on the current tree. The round-8
escalation ("tree build failed twice") was an unrelated tree-wide breakage —
Qt6 `findChild` static asserts about missing Q_OBJECT macros in
`AccessibleAnnouncements.cpp`, `AccessibleCanvas.cpp` and
`ControlCues.cpp` (per the r8 build log) — since repaired by merge-train
merges. On today's tree `cmake --build build --target zene` exits 0
(build-1.log).

## Evidence (all from this session, round 10; logs in
/home/kruzzzzy/.config/kads/zene/state/runs/new-p11-69.r10/logs)

The ctest working directory was
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p11-69/build/tests`.

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Tree builds (escalation point) | `cmake --build build --target zene > .../logs/build-1.log 2>&1; echo EXIT=$?` | 0 | build-1.log, no errors. |
| Build test target | `cmake --build build --target CrashReportMenuTest > .../logs/build-2.log 2>&1; echo EXIT=$?` | 0 | build-2.log. |
| GREEN, fix present | `ctest -R '^CrashReportMenuTest$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | 0 | test-1.log: 1/1 Passed. |
| Capture product-only patch (fix is already committed, so it is taken from the fix commit, product file only, tests excluded) | `git diff 9886cb604a~1 9886cb604a -- src/gui/CrashReportMenu.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch; echo EXIT=$?` | 0 | Working tree back at the pre-fix singular-only wording (verified by rg: lines 111-112 show the old one-branch text). |
| Rebuild without fix | `cmake --build build --target CrashReportMenuTest > .../logs/build-3.log 2>&1; echo EXIT=$?` | 0 | build-3.log. |
| RED, fix reverted | `ctest -R '^CrashReportMenuTest$' --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` | 8 | test-2.log: `theLineNamesTheState` FAILED — actual "Armed, one report not yet offered", expected "Armed, 2 reports not yet offered" for `report_count` 2. Exactly the reported bug. |
| Restore fix | `git apply .../logs/fix.patch; echo EXIT=$?` | 0 | `git status --short` empty afterwards: the applied patch reproduces HEAD exactly. |
| Rebuild with fix | `cmake --build build --target CrashReportMenuTest > .../logs/build-4.log 2>&1; echo EXIT=$?` | 0 | build-4.log. |
| GREEN, fix restored | `ctest -R '^CrashReportMenuTest$' --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?` | 0 | test-3.log: 1/1 Passed. |
| Build the rest of the Crash family (executables were not built in this tree: ctest had reported them "Not Run", a pre-existing build-tree state) | `cmake --build build --target CrashReporterArmTest CrashReporterTest RecordingCrashLossTest ControlCrashAcknowledgeTest > .../logs/build-5.log 2>&1; echo EXIT=$?` | 0 | build-5.log. |
| Whole ctest family | `ctest -R 'Crash' --output-on-failure > .../logs/test-5.log 2>&1; echo EXIT=$?` | 0 | test-5.log: 7/7 Passed (CrashReportMenuTest, CrashReporterArmTest, CrashReporterTest, RecordingCrashLossTest, ControlCrashAcknowledgeTest, CrashReporterAppleSignalCheck, ControlCrashReporter). |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p11-69 > .../logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | gate-1.log: all rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity "no new item in your files", file-length "no new item in your files"). |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p11-69 new-p11-69 > .../logs/submit-1.log 2>&1; echo EXIT=$?` | 1 → 0 after this report | submit-1.log: only FAIL was the missing Self-check section, added below; re-run exits 0 (submit-2.log). |

The regression test is `tests/src/gui/CrashMenuTest`'s `theLineNamesTheState`
(`tests/src/gui/CrashReportMenuTest.cpp:92-104`), which added the count-of-two
rows (lines 100-103) checking both pending states.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reverted: `git apply -R fix.patch` (EXIT=0), rebuild (EXIT=0), then
  `ctest -R '^CrashReportMenuTest$'` → EXIT=8 with the exact bug symptom
  (actual "Armed, one report not yet offered" vs expected "Armed, 2 reports not
  yet offered", test-2.log). Restored: `git apply fix.patch` (EXIT=0), rebuild
  (EXIT=0), same ctest → EXIT=0, 1/1 Passed (test-3.log).
- **The test is deterministic: no wall-clock timing, sleeps or races.**
  `theLineNamesTheState` calls `crashReportStatusText` directly with literal
  `QJsonObject` states (injected values: `report_count` 0/1/2, `pending`
  true/false) — a pure function over injected state, no clock, no sleeps, no
  waiting. The menu-path test (`theReportIsKeptOrDeletedFromTheMenu`) writes
  report files explicitly into a `QTemporaryDir` and drives menu actions
  synchronously; its confirm/opener seams are injected lambdas
  (`setCrashReportDeleteConfirm` / `setCrashReportFolderOpener`), so no dialogs
  and no timing.
- **The fix is at the cause and complete: every caller and sibling checked.**
  The cause is the single formatting point, `crashReportStatusText`
  (`src/gui/CrashReportMenu.cpp:103-120`). Its only product caller is
  `showState()` (CrashReportMenu.cpp:90), which passes the `crash.list_reports`
  result straight through, so the menu line, `theLineNamesTheState` and the
  state agree. `report_count` product consumers were enumerated with rg:
  `CrashReportMenu.cpp:85` (a boolean `hasReport > 0` — unaffected by wording)
  and the producer `ControlCommandsCrash.cpp:244` (`reports.size()`). No other
  product code formats the count (rg for "one report"/"reports not yet" finds
  no other formatter). The core module writes ONE fixed-name report file
  (`ControlCommandsCrash.cpp:276`: "the reporter writes ONE bounded report
  file (crash-reports/zene-crash-report.txt)"), so the live count is 0 or 1
  today; the formatter is nevertheless honest for any count, which is the
  contract the finding asks for ("Format the status from `report_count`,
  including plural wording"). Zero and one wording unchanged.
- **No collateral damage.**
  The change touches only a GUI status-line string; no file format, project
  loading, save data, limit or check is involved. Old projects and saved data
  load exactly as before (nothing outside the status text changed; the whole
  Crash test family still passes 7/7). No caller can be rejected by anything
  added here.
- **Realtime, undo, commands.**
  The formatting runs on the GUI thread (`showState` from `aboutToShow` and
  menu action handlers); nothing on the audio thread allocates, locks, waits
  or logs. Undo: a menu status label is not undoable state; nothing added.
  No control command was added or changed: `crash.list_reports` was already
  declared (CrashReportMenu.cpp:135) and the fix adds no registry entry
  (ControlCommandsCrash.cpp untouched).
- **Registration.**
  `src/gui/CrashReportMenu.cpp` and `include/CrashReportMenu.h` are fork-added
  files already listed in `tests/fork-sources.txt` (lines 2332 and 1133); the
  fork-sources gate passes (gate-1.log). No upstream-inherited file was
  changed.
- **Verdict provenance.**
  The verdict is FIXED on this branch: the product fix and the regression test
  are both present (commit `9886cb604a`, ancestor of HEAD `a70a1a5466`) and
  were fully re-verified this session with the RED/GREEN runs above. No product
  code needed to change in round 10; the escalated build failure was unrelated
  (Qt6 Q_OBJECT static asserts in a11y files, per the r8 build log) and the
  current tree builds clean (build-1.log, EXIT=0).

## Other findings

- The build tree had unbuilt Crash-family test executables (ctest reported
  CrashReporterArmTest, CrashReporterTest, RecordingCrashLossTest and
  ControlCrashAcknowledgeTest as "Not Run" before I built them). That is a
  build-tree state, not a repository defect; after building, all 7 pass.
- The round-8 tree-wide build failure (Qt6 `findChild` static asserts about
  missing Q_OBJECT macros in `AccessibleAnnouncements.cpp`,
  `AccessibleCanvas.cpp`, `ControlCues.cpp`) is already repaired on the current
  tree by merge-train merges; no action needed here.
