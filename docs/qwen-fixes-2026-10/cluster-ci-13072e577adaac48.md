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

| # | Command | EXIT / result | Log |
| --- | --- | --- | --- |
| ancestry | `git merge-base --is-ancestor 4ca909f HEAD` | EXIT=1 — `4ca909f` is not an ancestor of HEAD | this run |
| 01 | `git diff --stat 4ca909f..HEAD -- tests/src/core/InstrumentLayersCommandsTest.cpp` | EXIT=0 — 1 file changed, 12 deletions | `logs/01-diff-vs-4ca909f.log` |
| 02 | `bash tests/file-length-gate.sh --check` at HEAD | EXIT=0 — `PASS (check mode: no regressions; baseline not written)`; 2746 sources measured, 4 over 500; this test file is not listed | `logs/02-gate-head.log` |
| 03 | file-length audit at HEAD using the gate's `code_lines` (`tests/file-length-gate.sh:106-136`) | EXIT=0 — HEAD `b3f18c7c4e5d64b7f9c169d232c8d6e96e5a62f8`; 497 code lines (632 physical); 0 baseline entries, 0 exempt entries; at `4ca909f` 507 code lines (644 physical), which would fail as a new file | `logs/03-code-lines-head.log` |
| 04 | gate `--check` with the `4ca909f` blob written to the file | EXIT=1 — 507 listed; `REGRESSION: new file over 500 lines: tests/src/core/InstrumentLayersCommandsTest.cpp (507)`; `FAIL: file-length ratchet regressed` | `logs/04-gate-4ca909f-blob.log` |
| 05 | gate `--check` after restoring the HEAD blob | EXIT=0 — `PASS (check mode: no regressions; baseline not written)` | `logs/05-gate-head-restored.log` |
| 06 | blob diff, HEAD blob vs `4ca909f` blob | EXIT=0 — 12 added lines (used for Other findings) | `logs/06-blob-diff.log` |
| 07 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-13072e577adaac48` | EXIT=0 — `RESULT: PASS`; all eight rows PASS, including `file-length  no new item in your files` | `logs/07-feature-check.log` |
| 08 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-13072e577adaac48 ci-13072e577adaac48` | EXIT=0 — `RESULT: PASS - submit` | `logs/08-submit-check.log` |

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
  regression), and the gate still passes (log 02).
