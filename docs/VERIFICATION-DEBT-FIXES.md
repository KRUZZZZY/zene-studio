# Verification-debt fixes — 2026-09-11

Branch `post-alpha/gate-debt`, base commit `0c23587d2` (branch base `post-alpha/v0.2`).
**Nothing was pushed**; every commit is local. Only test/CI scripts changed — no product source,
no build file, so the shipped `v0.1.0-alpha` behaviour is untouched by construction.

Three defects listed in `docs/STATUS.md` ("Verification debt — this list under-counted it")
and in `tests/QA-GATES.md`. Each is fixed in its own commit and proved to **bite** by
`tests/test-verification-debt.sh`, which runs the changed gate twice against the **same**
synthetic fixture: once from the pre-fix revision (`git show $BASE:tests/<script>`, default
`VERIFICATION_DEBT_BASE=0c23587d2`) and once from this tree. Every exit code was read **unpiped**
(`cmd > log 2>&1; echo EXIT=$?`).

| commit | unit |
|---|---|
| `deb349727` | defect 1 — `tests/coverage-gate.sh` + `tests/coverage-entry-floor-exempt.txt` |
| `879e251ef` | defect 3 — `tests/fork-sources-gate.sh` (Gate 9) + `quality-gates.yml` step |
| `9763a185c` | defect 2 — `tests/run-all-gates.sh`: a skipped gate is not a pass |
| `d96db9e4c` | Gate 9 invoked by `run-all-gates.sh` |
| `ddc2f11cc` | `tests/test-verification-debt.sh` — the red/green proof |
| `docs` | `tests/QA-GATES.md`, `docs/STATUS.md`, this report |

Diffstat (`git diff --stat 0c23587d2..HEAD`): 6 files changed, 575 insertions(+), 27
deletions(-) — plus the docs commit.

---

## Defect 1 — `coverage-gate.sh`: no entry floor, zero-line file banked as 100%

**Where.** Pre-fix `tests/coverage-gate.sh:96` was the whole defect:

```python
now[key] = 100.0 if total == 0 else round(100.0 * hit / total, 2)
```

and the ratchet rule above it read "new files enter the baseline at their current coverage".
A file with `LF:0` therefore entered the committed baseline at **100.00%** — a claim the
tracefile does not support, permanent because nothing ever lowers an entry — and a brand-new
file entered at whatever it measured, `0.00` included, with no floor.

**Fixed at.** `tests/coverage-gate.sh` lines 46 (`COVERAGE_ENTRY_FLOOR`, default 50.00),
116–125 (a `LF:0` record becomes the explicit `unmeasurable` status and `now[key] = None`),
138 (a baseline value of `n/a` parses back as unmeasured), 169–190 (the floor decides new
entries; an entry recorded `n/a` that later becomes measurable is treated as a new entry too),
211–213 and 254–263 (reporting and the failure path), 275–285 (the baseline is written with
`n/a` where the old format wrote `100.00`). New file `tests/coverage-entry-floor-exempt.txt`
documents the declaration form: `<path><TAB><reason>`, blank reason → exit 2, the same shape as
`tests/upstream-modifications.txt`.

**Red proof** (pre-fix gate, fixture `trace-bad.info` = stable file at 100%, `src/braindead.cpp`
with `LF:0`, `src/newlow.cpp` at 5%):

```
EXIT=0                                     # it does not bite
$ tail -3 coverage/base-old-bad.tsv
src/braindead.cpp	100.00                  # banked at 100% — the defect, in the baseline
src/newlow.cpp	5.00
src/stable.cpp	100.00
```

**Green proof** (fixed gate, same fixture): `EXIT=1` and

```
FAIL: 1 new file(s) entered below the 50.00% coverage entry floor:
  src/newlow.cpp: 5.00%
unmeasurable  src/braindead.cpp: 0 instrumented lines - no coverage measurable (recorded as n/a, never as 100%)
entry floor: 50.00% for files not yet in the baseline (COVERAGE_ENTRY_FLOOR; 0 declared exemption(s))
```

with nothing written: the failed run leaves the baseline without a `src/braindead.cpp` line.
Control run on the good fixture (`src/newgood.cpp` at 95%): `EXIT=0`, and the baseline gains
`src/newgood.cpp	95.00` — the ratchet still ratchets.

**Real-tree run.**

```
$ bash tests/coverage-gate.sh /tmp/gatedebt-coverage-synth.info tests/coverage-baseline.tsv --check
EXIT=0        # the committed 47-file baseline passes unchanged under the new rules
$ bash tests/coverage-gate.sh /tmp/gatedebt-coverage-synth-newfile.info tests/coverage-baseline.tsv --check
EXIT=1        # same tracefile + one new file at 20% -> refused by the floor
```

`gatedebt-coverage-synth.info` is **synthetic input built from the real committed baseline**
(one lcov record per baseline entry, `LF:200`, `LH=round(pct*2)`), so the gate ran its real
code path against the real baseline file — it is *not* a measurement of the tree. The real
instrumented run is listed under "Not run" below, with the reason.

**Residual risk.** The floor is one flat number (50%): it cannot tell 50%-earned-by-tests from
50%-reached-by-luck, exactly as Gates 4 and 7 accept for their own ratchets. `COVERAGE_ENTRY_FLOOR`
is environment-overridable, so a caller can run the gate with a weaker floor and get a green exit
code — the number used is printed on every run, so it cannot be weakened silently. An unmeasurable
file stays legal indefinitely; the status is explicit (`n/a` / `unmeasurable`) rather than silent,
which is the distinction the defect was about. Files already in the baseline are untouched, so a
file that entered at `0.00` before this fix keeps its `0.00` — the ratchet still only bites drops.

## Defect 2 — `run-all-gates.sh`: SKIP laundered into PASS

**Where.** Pre-fix `tests/run-all-gates.sh:46-51`: `record()` set `fail=1` only for `FAIL`, and
the script ended with `echo "RESULT: PASS — every executed gate passed"` and exit 0. A run in
which gates were skipped therefore exited 0 — and the two gates it could launder are the two
most expensive ones: gate 1 (`ctest`, needs `build/`, skips itself when it is absent) and gate 2
(coverage, opt-in behind `--with-coverage`).

**Fixed at.** `tests/run-all-gates.sh` lines 18–23 (exit-code block in the header),
47–66 (`SKIPPED` collection in `record()`, `skip_hint()`), 152–170 (summary counts the skips,
names each one and says how to run it, then the distinct exit code).

**Chosen: a summary line *and* a distinct exit code 3.** Not 1 (a skip is not a failure;
conflating them trains readers to ignore the code) and not "warn but exit 0" (a warning that
leaves the exit code at 0 is the same laundering in a different font, and this script's exit
code is what a human or a lane reads as the gate state).

**Red proof** (pre-fix runner, fixture with every gate stubbed green, no `build/`, no
`--with-coverage`): `EXIT=0`, summary `RESULT: PASS — every executed gate passed`, while gates 1
and 2 were skipped.

**Green proof** (fixed runner, same fixture): `EXIT=3` —

```
skipped: 2 of 9 gates did not run
  gate 1 (ctest): no configured build/ directory — to run it: configure build/ (...)
  gate 2 (coverage): --with-coverage was not passed — to run it: pass --with-coverage
RESULT: PASS-WITH-SKIPS (exit 3) — 2 of 9 gates did not run; this run is INCOMPLETE, not green.
```

Control: with every gate actually running (fixture `build/` configured, `--with-coverage`):
`EXIT=0`, `RESULT: PASS — every executed gate passed (9/9 ran)`.

**Real-tree run.**

```
$ bash tests/run-all-gates.sh
EXIT=3        # gates 1, 3, 4, 5, 6, 7, 8, 9 all ran and passed; gate 2 (coverage) skipped
```

That 3 is the honest answer: the default invocation does not run the coverage ratchet, and the
run now says so instead of printing PASS and exiting 0 (`tests/QA-GATES.md` records it).

**Residual risk.** Exit 3 is new, so automation that tests `== 0` now sees non-zero for the
default invocation. That is the intended change — that invocation *is* incomplete — and it is
stated in the script header and in `tests/QA-GATES.md`. CI is unaffected: the workflow calls
gates individually and does not use this script (`grep -rn 'run-all-gates' .github/` → no
matches). A gate can still skip for a reason the runner does not know (a gate's own SKIP path,
e.g. Gate 8 without `npx`); the runner reports it as a skip, which is correct even though its
`skip_hint` is generic.

## Defect 3 — nothing said "this new source is not in tests/fork-sources.txt"

**Where.** No such check existed anywhere: the scope lists were read by Gates 2, 4, 7 and 8, and
nothing verified that a tracked source was in one of them. The 2026-09-10 incident is the proof:
`include/LatencyCompensation.h` and `src/core/LatencyCompensation.cpp` were in no list, and the
only gate that reacted — Gate 6 — diagnosed them as an *undeclared change to upstream-inherited
code*, the wrong diagnosis for files upstream has never had.

**Fixed at.** New `tests/fork-sources-gate.sh` (Gate 9, 138 lines): every tracked source under
`src/`, `include/`, `plugins/` and `tests/` must be in `tests/fork-sources.txt` (fork-NEW, watched
by the fork-scoped ratchets) or `tests/all-sources.txt` (inherited, watched by the whole-tree
scope). Anything in neither is named with the verdict `NOT IN tests/fork-sources.txt`, followed
by what to do about it. Exclusions are stated in the script, not silently skipped: the vendored
third-party trees and `tests/reference/**` (frozen verbatim copies; provenance in
`tests/reference/ORIGIN.tsv`). Stale manifest entries are reported too. Wired into
`run-all-gates.sh` (gate 9) and into the `static-gates` job of `quality-gates.yml`, which runs on
every push and pull request.

**Red proof** (fixture repo where `src/core/LatencyCompensation.cpp` and
`include/LatencyCompensation.h` are committed in no scope list):

```
$ git cat-file -e 0c23587d2:tests/fork-sources-gate.sh     -> fails: no check existed
$ bash <fixture>/tests/no-upstream-regression-gate.sh <base>   # the pre-fix reaction
EXIT=1
include/LatencyCompensation.h   VIOLATION: undeclared change to upstream-inherited code
src/core/LatencyCompensation.cpp VIOLATION: undeclared change to upstream-inherited code
# grep -F "not in tests/fork-sources.txt" gate6.log -> no match: the plain statement never appeared
```

**Green proof** (fixed Gate 9, same commit):

```
EXIT=1
src/core/LatencyCompensation.cpp   NOT IN tests/fork-sources.txt
                                   (and not known upstream in tests/all-sources.txt)
include/LatencyCompensation.h      NOT IN tests/fork-sources.txt
...
FAIL: 2 tracked source file(s) are not in tests/fork-sources.txt
```

and after adding the two paths to the fixture's `tests/fork-sources.txt`: `EXIT=0`,
`PASS: every tracked source in scope is registered`.

**Real-tree run.**

```
$ bash tests/fork-sources-gate.sh
EXIT=0
scanned 1091 tracked source file(s) under src/, include/, plugins/, tests/;
  100 fork-sources entry(ies), 992 inherited upstream, 0 stale entry(ies).
PASS: every tracked source in scope is registered (100 fork-NEW, 992 inherited).
```

**Residual risk.** The rule is "registered in one of the two manifests": a fork-NEW file added to
`all-sources.txt` instead of `fork-sources.txt` is no longer invisible, but it is outside the
fork-scoped ratchets (the whole-tree scope still measures it). The gate reads *tracked* files, so
a work-in-progress file that is not yet committed is not flagged — deliberate, since the gate
judges commits, not working trees. Stale entries are reported, not fatal.

## The proof harness

```
$ bash tests/test-verification-debt.sh
EXIT=0        # 29 assertions, all OK
```

Falsifiability check: with one expected exit code tampered to the wrong value the same harness
prints `FAIL ... expected exit 1, got 0` and exits 1 — it is not a script that can only say PASS.
Set `KEEP_FIXTURES=1` to keep the temp trees; `VERIFICATION_DEBT_BASE=<rev>` to prove against a
different pre-fix revision.

## Real-tree runs, all unpiped

| command | exit | note |
|---|---|---|
| `bash tests/test-verification-debt.sh` | **0** | 29/29 assertions |
| `bash tests/fork-sources-gate.sh` | **0** | 1091 scanned, 0 unregistered, 0 stale |
| `bash tests/coverage-gate.sh <synth> tests/coverage-baseline.tsv --check` | **0** | synthetic tracefile from the real baseline |
| `bash tests/coverage-gate.sh <synth+new20%> tests/coverage-baseline.tsv --check` | **1** | same, plus one new file at 20% → floor refuses |
| `bash tests/run-all-gates.sh` | **3** | gates 1,3,4,5,6,7,8,9 pass; gate 2 skipped by design. The real run inside it: `100% tests passed, 0 tests failed out of 25` and `kill score 26/29 = 89.7% (threshold 80%)` |
| `bash tests/no-tautology-gate.sh` | **0** | unchanged gate, run for the record |
| `bash tests/complexity-gate.sh --check` | **0** | unchanged gate, run for the record |
| `bash tests/no-upstream-regression-gate.sh` | **0** | unchanged gate, run for the record |
| `bash tests/file-length-gate.sh --check` | **0** | unchanged gate, run for the record |
| `bash tests/duplication-gate.sh` | **0** | unchanged gate (1.08% vs 5% budget) |

## Not run, and why

- **A real instrumented coverage run** (`bash tests/run-coverage.sh build-coverage` then
  `coverage-gate.sh build-coverage/coverage/coverage-fork.info --check`). It needs a
  `-DWANT_COVERAGE=ON` build plus the full ctest suite plus lcov — 15–22.4 min measured in CI by
  the repo's own numbers, and the slowest gate in the suite, which the task brief puts outside
  the ~15-minute budget for a real run. Substituted: the fixture red/green proof, plus the gate
  run against the real committed baseline with a synthetic tracefile (both above). **Not proven:
  that the fixed gate parses a real lcov tracefile produced by this tree identically to the old
  one.** The parsing code is unchanged except for the `LF:0` branch, and the fixture feeds the
  script real lcov-format input, but a real tracefile was not seen.
- **Mutation-suite numbers beyond what Gate 5 reports in the run above**: gate 5 ran as part of
  `run-all-gates.sh` and passed; no separate analysis was made, as it was not touched.
- **Baseline re-anchor: none, deliberately.** No `*baseline*` file is modified in this branch
  (`git diff --name-only 0c23587d2..HEAD` → no baseline paths). The entry floor is an *entry*
  rule, so the committed 2026-09-09 coverage baseline still passes unchanged — verified by the
  real-tree run above. The only expected baseline change is on the next write-mode coverage run,
  where a zero-instrumented file previously stored as `100.00` is rewritten as `n/a`; that is a
  format correction, not a re-anchor, and it lowers no gate.
- **Non-Linux**: these are POSIX shell scripts with `bash`, `git`, `python3` and (for Gate 8)
  `npx`; they were exercised on Linux only, as the rest of this suite is.

## Notes for the next reader

- `tests/QA-GATES.md` carries the new rules, dated, in Gate 2 (entry floor, `unmeasurable`/`n/a`,
  exemption file), Gate 9 (new section) and "Running all gates" (exit codes 0/1/3), and the CI
  section now lists Gates 3, 4, 6, 7, 8 and 9 for `static-gates`.
- `docs/STATUS.md` marks the three defects closed in its own verification-debt list and points
  here; the two remaining items in that list (NeuralAmp/RnnoiseDenoiser at 0.00% with no
  registered test file; Gate 6's window vs the base..HEAD history) are untouched.
