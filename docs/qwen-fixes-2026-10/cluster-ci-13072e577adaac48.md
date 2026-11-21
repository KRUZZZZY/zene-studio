ALREADY-FIXED

# cluster ci-13072e577adaac48 — Gate 7 file-length

The finding is that `tests/src/core/InstrumentLayersCommandsTest.cpp` exceeded the
500-code-line limit (507 on the `4ca909f` input). On this branch the file is already at
497 code lines, under the limit and with no baseline or exempt entry, so no product or
test fix is needed.

## Evidence

Logs 16-20 ran at `e0632552809fd91773c48e94f419c6e2f7845476` (a report-only commit); the test file is
unchanged since `b3f18c7c4e5d64b7f9c169d232c8d6e96e5a62f8`. `4ca909f` is not an ancestor
of HEAD: the ancestry check exits 1 (log 16). The shorter file is
already on this branch: log 24 shows the only difference between `4ca909f` and HEAD
across the test file, the baseline and the exempt list is 12 deletions in
`tests/src/core/InstrumentLayersCommandsTest.cpp` (1 file changed, 0 insertions), and
log 18 counts 497 code lines at HEAD against 507 code lines on the `4ca909f` blob.

`$LOGS` below is
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-13072e577adaac48.r2/logs`. The round-1
logs and their manifest are no longer reachable; every row below was regenerated in r2
and the commands in the table are the ones that produced the r2 logs.

| # | Command | EXIT / result | Log |
| --- | --- | --- | --- |
| 16 | `git merge-base --is-ancestor 4ca909f58a1cf7e78fc1e66430ba21e190241067 HEAD > $LOGS/16-ancestry.log 2>&1; echo EXIT=$?` | EXIT=1 — `4ca909f` is not an ancestor of HEAD | `$LOGS/16-ancestry.log` |
| 17 | `bash tests/file-length-gate.sh --check > $LOGS/17-gate-head.log 2>&1; echo EXIT=$?` | EXIT=0 — `PASS (check mode: no regressions; baseline not written)`; 2746 sources measured, 4 over 500; this test file is not listed | `$LOGS/17-gate-head.log` |
| 18 | `sed -n '110,129p' tests/file-length-gate.sh > $LOGS/code_lines.awk; echo 'END { print n + 0 }' >> $LOGS/code_lines.awk; { printf 'HEAD=%s\n' "$(git rev-parse HEAD)"; printf 'HEAD file code lines: '; awk -f $LOGS/code_lines.awk tests/src/core/InstrumentLayersCommandsTest.cpp; printf '4ca909f blob code lines: '; git show 4ca909f58a1cf7e78fc1e66430ba21e190241067:tests/src/core/InstrumentLayersCommandsTest.cpp \| awk -f $LOGS/code_lines.awk; } > $LOGS/18-code-lines.log 2>&1; echo EXIT=$?` | EXIT=0 — HEAD `e0632552809fd91773c48e94f419c6e2f7845476` at 497 code lines; `4ca909f` blob at 507 code lines | `$LOGS/18-code-lines.log` |
| 19 | `git show 4ca909f58a1cf7e78fc1e66430ba21e190241067:tests/src/core/InstrumentLayersCommandsTest.cpp > tests/src/core/InstrumentLayersCommandsTest.cpp; bash tests/file-length-gate.sh --check > $LOGS/19-gate-4ca909f-blob.log 2>&1; echo EXIT=$?` | EXIT=1 — 507 listed; `REGRESSION: new file over 500 lines: tests/src/core/InstrumentLayersCommandsTest.cpp (507)`; `FAIL: file-length ratchet regressed` | `$LOGS/19-gate-4ca909f-blob.log` |
| 20 | `git show HEAD:tests/src/core/InstrumentLayersCommandsTest.cpp > tests/src/core/InstrumentLayersCommandsTest.cpp; bash tests/file-length-gate.sh --check > $LOGS/20-gate-head-restored.log 2>&1; echo EXIT=$?` | EXIT=0 — `PASS (check mode: no regressions; baseline not written)` | `$LOGS/20-gate-head-restored.log` |
| 21 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-13072e577adaac48 > $LOGS/21-feature-check.log 2>&1; echo EXIT=$?` | EXIT=0 — `RESULT: PASS`; all eight rows PASS, including `file-length  no new item in your files` | `$LOGS/21-feature-check.log` |
| 22 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-13072e577adaac48 ci-13072e577adaac48 > $LOGS/22-submit-check.log 2>&1; echo EXIT=$?` | EXIT=0 — `RESULT: PASS - submit` | `$LOGS/22-submit-check.log` |
| 24 | `git diff --stat 4ca909f58a1cf7e78fc1e66430ba21e190241067 HEAD -- tests/src/core/InstrumentLayersCommandsTest.cpp tests/file-length-baseline.tsv tests/file-length-exempt.txt > $LOGS/24-diff-stat-vs-4ca909f.log 2>&1; echo EXIT=$?` | EXIT=0 — `tests/src/core/InstrumentLayersCommandsTest.cpp \| 12 ------------`; `1 file changed, 12 deletions(-)`; baseline and exempt list show no diff | `$LOGS/24-diff-stat-vs-4ca909f.log` |

The gate's limit is 500; the new-file rule is `tests/file-length-gate.sh:227-228`. The
same input (`4ca909f`) is RED (EXIT=1, 507), while HEAD is GREEN (EXIT=0, 497). No
product or test code changed, nothing was re-anchored, and no exemption was added. The
baseline and exempt lists do not name this file.

No regression test was added because there is no fix: the gate itself is the
regression-test artifact, and it is shown RED on the `4ca909f` input (log 19) and GREEN
on HEAD (logs 17 and 20).

## Round 2

The round-1 review's verdict reads exactly:

> proof fits the claim: no artifact shows `code-reference-or-test` (kind code-reference (path and line that show the behaviour) or a regression-test)

The round-1 manifest is not reachable from this sandbox, so its rejection could not be
inspected. The r2 manifest carries both artifact kinds: a code-reference (path and line
that show the behaviour) and a regression-test. Round 2 regenerates the evidence under
the r2 `$LOGS` folder, and this report cites only r2 logs.

- **Code-reference artifact (path:line):** the count rule is the `code_lines` function
  at `tests/file-length-gate.sh:106-136`; the new-file rule is at
  `tests/file-length-gate.sh:227-228`. At HEAD the artifact under review is
  `tests/src/core/InstrumentLayersCommandsTest.cpp` at 497 code lines (log 18), under
  the 500 limit.
- **Regression-test artifact:** the file-length gate itself. RED on the `4ca909f` copy
  is EXIT=1 (log 19, 507 lines, new-file regression); GREEN at HEAD is EXIT=0 (log 20,
  restored HEAD, 497 lines; also log 17). No product code changed, so there is no
  revert step beyond restoring the `4ca909f` copy.
- **Manifest and schema:** the r2 manifest carries the code-reference artifacts as
  path+line pairs and the regression-test artifact described above. Its field names
  follow the brief and are not checked against the schema: `schemas/proof.json` and the
  checker source are not reachable from this sandbox (logs 01-04 and 14).

## Self-check

- **Regression test FAILS reverted / PASSES with the change (exit codes):** the gate is
  the regression-test artifact. Over the `4ca909f` copy it is RED, EXIT=1 at 507 lines
  (log 19); over HEAD it is GREEN, EXIT=0 at 497 lines (logs 17 and 20). There is no
  product change to revert, and no test file was added or edited.
- **Test determinism (no wall clock, sleeps or races):** the check is the gate's
  `code_lines` awk at `tests/file-length-gate.sh:106-136`, a pure count of committed
  text. It uses no clock, sleep, network, filesystem ordering or random input; the
  497/507 counts are reproducible (log 18).
- **Fix at the cause and complete (callers and siblings):** no product or test code
  changed, so no caller or command sibling can drift. The cause is the branch state:
  `4ca909f` is not an ancestor of HEAD (log 16, EXIT=1) and HEAD already carries the
  12-line smaller file, which the gate accepts (logs 17 and 20).
- **No collateral damage:** this packet adds only this report. The gate, baseline,
  exempt list and test file are untouched. The gate still measures 2746 sources with 4
  over 500 and passes with its baseline (logs 17 and 20), so no other legitimate caller
  is newly limited. Old projects and saved data are unaffected because there is no code
  delta from this packet. The `4ca909f` lineage at 507 would exceed the limit if merged
  here (see Other findings), but this branch's 497-line file passes.
- **ALREADY-FIXED evidence (code/file:line and contract):** HEAD's
  `tests/src/core/InstrumentLayersCommandsTest.cpp` is 497 code lines by the gate's
  `code_lines` (`tests/file-length-gate.sh:106-136`), under the 500 limit; the new-file
  rule at `tests/file-length-gate.sh:227-228` fails only above 500; neither
  `tests/file-length-baseline.tsv` nor `tests/file-length-exempt.txt` names the file;
  and the gate cannot flag the file at HEAD (logs 17 and 20). The identical input at
  `4ca909f` is 507 and fails (log 19).
- **Sibling commands in the changed command files (`app.about`, `app.ask`, `app.help`,
  `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`,
  `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, `app.version`,
  `app.whats_new`, `arrangement.fit_tracks`, `arrangement.get_state`):** none is
  affected. That list comes from branch history against the fix base, not from this
  packet. No command, registry, product or test code changed here, so every sibling
  keeps its existing behaviour and reversibility class.

## Other findings

- Log 18 shows 507 code lines on the `4ca909f` copy against 497 at HEAD (10 code lines
  more). Log 24 shows the physical-line diff: 12 deletions in
  `tests/src/core/InstrumentLayersCommandsTest.cpp`, 1 file changed, 0 insertions. If
  that lineage is merged here, the file becomes 507 code lines, over the 500 limit, so
  the merge would need a split to keep Gate 7 green.
- `src/core/ScriptEngine.cpp` measures 657 code lines here versus 650 in CI. It is
  grandfathered in the baseline (listed among the 4 files over 500 without a
  regression), and the gate still passes (logs 17 and 20). Its baseline entry is 662
  (`tests/file-length-baseline.tsv:4`).
