ALREADY-FIXED

# cluster ci-13072e577adaac48 — Gate 7 file-length

The finding is that `tests/src/core/InstrumentLayersCommandsTest.cpp` exceeded the
500-code-line limit (507 on the `4ca909f` input). On this branch the file is already at
497 code lines, under the limit and with no baseline or exempt entry, so no product or
test fix is needed.

## Evidence

HEAD is `b3f18c7c4e5d64b7f9c169d232c8d6e96e5a62f8`. `4ca909f` is not an ancestor of HEAD:
`git merge-base --is-ancestor 4ca909f HEAD` exits 1. The shorter file is already on this
branch; the 12-line difference is the opposite direction (HEAD has 12 fewer lines).

`$LOGS` below is `/home/kruzzzzy/.config/kads/zene/state/runs/ci-13072e577adaac48.r1/logs`.

| # | Command | EXIT / result | Log |
| --- | --- | --- | --- |
| ancestry | `git merge-base --is-ancestor 4ca909f HEAD` | EXIT=1 — `4ca909f` is not an ancestor of HEAD | this run |
| 01 | `git diff --stat 4ca909f58a1cf7e78fc1e66430ba21e190241067 HEAD -- tests/src/core/InstrumentLayersCommandsTest.cpp tests/file-length-baseline.tsv tests/file-length-exempt.txt > $LOGS/01-diff-vs-4ca909f.log 2>&1; echo EXIT=$?` | EXIT=0 — 1 file changed, 12 deletions; baseline and exempt list unchanged | `$LOGS/01-diff-vs-4ca909f.log` |
| 02 | `bash tests/file-length-gate.sh --check > $LOGS/02-gate-head.log 2>&1; echo EXIT=$?` | EXIT=0 — `PASS (check mode: no regressions; baseline not written)`; 2746 sources measured, 4 over 500; this test file is not listed | `$LOGS/02-gate-head.log` |
| 03 | `sed -n '110,129p' tests/file-length-gate.sh > $LOGS/code_lines.awk; echo 'END { print n + 0 }' >> $LOGS/code_lines.awk; { printf 'HEAD=%s\n' "$(git rev-parse HEAD)"; printf 'HEAD file code lines: '; awk -f $LOGS/code_lines.awk tests/src/core/InstrumentLayersCommandsTest.cpp; printf '4ca909f blob code lines: '; git show 4ca909f58a1cf7e78fc1e66430ba21e190241067:tests/src/core/InstrumentLayersCommandsTest.cpp \| awk -f $LOGS/code_lines.awk; } > $LOGS/10-code-lines-rerun.log 2>&1; echo EXIT=$?` | EXIT=0 — HEAD `1647fb4870577e52496dcdc52dea607f2292e7f5` (report commit; test file identical to `b3f18c7`) 497 code lines; `4ca909f` blob 507 code lines, which would fail as a new file | `$LOGS/10-code-lines-rerun.log` |
| 04 | `git show 4ca909f58a1cf7e78fc1e66430ba21e190241067:tests/src/core/InstrumentLayersCommandsTest.cpp > tests/src/core/InstrumentLayersCommandsTest.cpp; bash tests/file-length-gate.sh --check > $LOGS/04-gate-4ca909f-blob.log 2>&1; echo EXIT=$?` | EXIT=1 — 507 listed; `REGRESSION: new file over 500 lines: tests/src/core/InstrumentLayersCommandsTest.cpp (507)`; `FAIL: file-length ratchet regressed` | `$LOGS/04-gate-4ca909f-blob.log` |
| 05 | `git show HEAD:tests/src/core/InstrumentLayersCommandsTest.cpp > tests/src/core/InstrumentLayersCommandsTest.cpp; bash tests/file-length-gate.sh --check > $LOGS/05-gate-head-restored.log 2>&1; echo EXIT=$?` | EXIT=0 — `PASS (check mode: no regressions; baseline not written)` | `$LOGS/05-gate-head-restored.log` |
| 06 | `git diff HEAD 4ca909f58a1cf7e78fc1e66430ba21e190241067 -- tests/src/core/InstrumentLayersCommandsTest.cpp > $LOGS/06-blob-diff.log 2>&1; echo EXIT=$?` | EXIT=0 — 12 added lines (used for Other findings) | `$LOGS/06-blob-diff.log` |
| 07 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-13072e577adaac48 > $LOGS/07-feature-check.log 2>&1; echo EXIT=$?` | EXIT=0 — `RESULT: PASS`; all eight rows PASS, including `file-length  no new item in your files` | `$LOGS/07-feature-check.log` |
| 08 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-13072e577adaac48 ci-13072e577adaac48 > $LOGS/08-submit-check.log 2>&1; echo EXIT=$?` | EXIT=0 — `RESULT: PASS - submit` | `$LOGS/08-submit-check.log` |

The gate's limit is 500; the new-file rule is `tests/file-length-gate.sh:227-228`. The
same input (`4ca909f`) is RED (EXIT=1, 507), while HEAD is GREEN (EXIT=0, 497). No
product or test code changed, nothing was re-anchored, and no exemption was added.

No regression test was added because there is no fix: the gate itself is the check, and
it is shown RED on the `4ca909f` input (log 04) and GREEN on HEAD (logs 02 and 05).

## Self-check

- **Regression test FAILS reverted / PASSES with the change (exit codes):** not
  applicable and not needed. There is no product change to revert and no test was added.
  The check is the file-length gate itself: EXIT=1 over the `4ca909f` blob at 507
  (log 04), EXIT=0 over HEAD at 497 (logs 02 and 05).
- **Test determinism (no wall clock, sleeps or races):** the check is the gate's
  `code_lines` awk at `tests/file-length-gate.sh:106-136`, a pure count of committed
  text. It uses no clock, sleep, network, filesystem ordering or random input; the
  497/507 counts are reproducible.
- **Fix at the cause and complete (callers and siblings):** no product or test code
  changed, so no caller or command sibling can drift. The cause is the branch state:
  `4ca909f` is not an ancestor of HEAD (EXIT=1) and HEAD already carries the 12-line
  smaller file, which the gate accepts.
- **No collateral damage:** this packet adds only this report. The gate, baseline,
  exempt list and test file are untouched. Old projects and saved data are unaffected
  (no code delta from this packet). The `4ca909f` lineage at 507 would exceed the limit
  if merged here (see Other findings), but this branch's 497-line file passes.
- **ALREADY-FIXED evidence (code/file:line and contract):** HEAD's
  `tests/src/core/InstrumentLayersCommandsTest.cpp` is 497 code lines by the gate's
  `code_lines` (`tests/file-length-gate.sh:106-136`), under the 500 limit; the new-file
  rule at `tests/file-length-gate.sh:227-228` fails only above 500; the audit at log 03
  shows 0 baseline and 0 exempt entries; and the gate cannot flag the file at HEAD
  (log 02). The identical input at `4ca909f` is 507 and fails (log 04).
- **Sibling commands in the changed command files (`app.about`, `app.ask`, `app.help`,
  `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`,
  `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, `app.version`,
  `app.whats_new`, `arrangement.fit_tracks`, `arrangement.get_state`):** none is
  affected. That list comes from branch history against the fix base, not from this
  packet. No command, registry, product or test code changed here, so every sibling
  keeps its existing behaviour and reversibility class.

## Other findings

- `06-blob-diff.log` shows 12 extra lines on the `4ca909f` lineage. If that lineage is
  merged here, the file becomes 507 code lines, over the 500 limit, so the merge would
  need a split to keep Gate 7 green.
- `src/core/ScriptEngine.cpp` measures 657 code lines here versus 650 in CI. It is
  grandfathered in the baseline (listed among the 4 files over 500 without a
  regression), and the gate still passes (log 02). Its baseline entry is 662
  (`tests/file-length-baseline.tsv:4`).
