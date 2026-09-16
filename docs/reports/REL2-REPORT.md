# REL-2 — the release path refuses a red commit for real

**Lane** `zene-030/wrel2` · **branch** `030/rel2-release-job` · **base** `3fae5a5e1`
**Commits** `a37a7eb0b` (feat) · `7ae3ba617` (test) · `91a5ba6dd` (fix, found by replaying a
live CI capture) · **task** board #682 (REL-2)

> Read upstream's `.github/workflows/` first: the seven-platform matrix, `checks` and
> `static-gates` were already named there, and `build.yml` already had a `release-gate` job
> (REL-2, 2026-09-13). This lane's work is what makes that job *refuse* instead of *skip*,
> and gives it a **pre-tag** path, because a gate that only runs after the tag is cut cannot
> keep the tag off a red commit.

---

## 1. Branch and commit SHAs

| | |
|---|---|
| worktree | `…/projects/lmms-fl-research/zene-030/wrel2` |
| branch | `030/rel2-release-job` |
| base | `3fae5a5e1` (the merge-base of this worktree and `release/0.3.0`; 0 ahead / 50 behind at start) |
| `a37a7eb0b` | `feat(rel): the release job depends on the whole matrix, checks and static gates, and fails instead of skipping` |
| `7ae3ba617` | `test(rel): red/green self-test for the release ref-fitness oracle` |
| `91a5ba6dd` | `fix(rel): two defects the live CI capture exposed in the evidence gate` |

Nothing pushed. Four new files under `tests/`, one new workflow, one edited workflow.
`git diff --stat 3fae5a5e1..HEAD` = 7 files, +1351 / −23.

Two **local scratch refs** were created for the proof and are still present so the parent can
re-run it (delete with `git branch -D rel2/red-ref-proof rel2/green-ref-proof`):

| ref | what it is |
|---|---|
| `rel2/red-ref-proof` = `b776daa16` | parent `91a5ba6dd` + **one deliberately failing test** appended to `tools/mcp-zene-control/tests/test_units.py` |
| `rel2/green-ref-proof` = `e7afbbffb` | `f611c888b` + the whole REL-2 change set cherry-picked on top |

## 2. What was added, and where it is registered

| artefact | what it refuses |
|---|---|
| `.github/workflows/release.yml` (new, job `release-fitness`) | the release job. `on: workflow_run` on `[build, checks, quality-gates]` gives it workflow-level dependencies (`needs:` cannot cross files); **no `if:` at all**, so a red upstream FAILS it instead of being a green no-op; `workflow_dispatch{ref}` is the **pre-tag** path |
| `tests/release-ref-fitness.sh` (new) | the oracle: `--ref <ref>` judges the ref's **content** in a temporary detached worktree by re-running the build-free commands of those same three jobs, plus the staging-path policy. Add `--require-ci` for the API leg. Exit `0` fit · `1` red · `2` usage · `3` incomplete — **and 3 is a refusal**: an unmeasured ref is not a fit ref |
| `tests/release-ci-evidence-gate.sh` (new) | this commit's own push runs at **job** granularity — the 7 platform jobs + `checks.yml`'s 3 + `static gates (3, 4, 6, 7, 8, 9, 11)`. A workflow conclusion is not coverage |
| `tests/release-staging-path-gate.sh` (new) | the **shape**: A1 staging is tag-only · A2 `release-gate` `needs:` all six build jobs · A3 it carries `always()` · A4 the release workflow runs the oracle · A5 no `if:` reads an upstream conclusion · A6 staging `needs:` the verdict |
| `tests/release-mcp-bridge-leg.sh` (new) | mirrors `quality-gates.yml`'s `MCP bridge Python tests (no build)` job, including its *"0 tests is an error"* count check. The one leg that runs a **real test suite** without a build (measured: 45 + 14 tests, 0.8 s) |
| `tests/test-release-ref-fitness.sh` (new) | the house's red/green control shape, six controls |
| `build.yml` (edited, additive) | see §3 |

**No manifest entry is required**: Gate 9 scans `src/ include/ plugins/ tests/ tools/ modules/`
for **C/C++** sources by extension, and Gate 6's `mapfile` verdict for `.github/*` is
`CI config (allowed, non-runtime)`. Both measured green after the change (§4). No gate,
baseline, ratchet or manifest was weakened, disabled or re-anchored; the deliverable only
adds refusals.

### Acceptance contract (§3.1 of the charter)

This is **release-path infrastructure, not a product feature**: it registers no control-surface
command group and ships no UI, so items 2–4 of the contract (command ids, A16 row, socket
proof, `KNOWN-LIMITATIONS.md` line) do not apply. Its own proof is a registered shell harness
plus a workflow, in the same class as the existing `tests/release-honesty-gate.sh`,
`tests/test-package-upload-guard.sh` and `tests/evidence-gate.sh`.

## 3. What changed in `build.yml`, and why neither change is a relaxation

**(a) `release-gate` gains `always()` and a dispatch trigger.**

```yaml
if: ${{ always() && (startsWith(github.ref, 'refs/tags/') || github.event_name == 'workflow_dispatch') }}
```

The old comment said *"Do NOT give it `if: always()` … running the gate on a red matrix is the
defect"*. The defect was the reverse: with `needs:` and no `always()`, GitHub **skips** the job
whenever a build job fails, so the gate recorded `skipped` and expressed no refusal. Its own
first step, which refuses a `failure`/`cancelled`/`skipped` entry in `${{ toJSON(needs) }}`,
was **unreachable in exactly the case it was written for**. `always()` cannot make the job
pass — it can only rename the refusal from `skipped` to `failure`, which is what makes it
readable by anything downstream. The tag-only condition was also wrong for the task's own
framing: it made the gate speak after the tag already existed.

**(b) the six package-upload steps lose the `workflow_dispatch` disjunct.**

They were `if: startsWith(github.ref, 'refs/tags/') || github.event_name == 'workflow_dispatch'`,
so a dispatch on **any branch** staged release packages with **no fitness verdict anywhere in
the run** — a second sanctioned staging path, and one the tag-only gate could never see. The
condition is now `if: startsWith(github.ref, 'refs/tags/')`. `tests/test-package-upload-guard.sh`
(which checks the `path:`/`if-no-files-found:` policy of every upload step) still passes
unchanged, EXIT=0.

**(c) `stage-artefacts`** (`needs: [release-fitness]`) is the only job that emits the release's
staging record, and it refuses a tag run whose artefact set is incomplete, expired or
digest-less — the same three things `~/.local/bin/zene-release` guards 4/5 re-check before it
prints `gh release create`. The release job is therefore the only sanctioned way to stage
artefacts because (i) packages upload only on a tag (A1), (ii) a tag is cut only from a ref the
release job judged FIT (A4–A6), and (iii) `release-tag-protection` already gates tag creation.

## 4. What was PROVED, with real output

All exit codes are consecutive unpiped measurements (`cmd > log 2>&1; echo EXIT=$?`) from
`logs/rel2/proof-transcript.log` (gitignored — `.gitignore` has `/logs/`; a `.log` under
`tests/` or `docs/` would be refused by Gate 11). Full transcript: `logs/rel2/proof-transcript.log`;
per-run logs in `logs/rel2/evidence/`.

### 4.1 The probe: which of the candidate legs are green, before choosing the leg set

`docs`/`logs` probe over 16 candidate legs — see `logs/rel2/probe-legs.sh` and `.log`.

**The oracle agrees with real CI.** At `f611c888b` — the commit where the product repo's own
`static gates (3, 4, 6, 7, 8, 9, 11)` job concluded `success` (run 34870198457) — all 13
pre-existing legs pass locally:

```
scripted-verify EXIT=0   package-upload-guard EXIT=0   yamllint EXIT=0
g3 EXIT=0  g4 EXIT=0  g4-tools EXIT=0  g6 EXIT=0  g7 EXIT=0  g7-tools EXIT=0
g8 EXIT=0  g8-tools EXIT=0  g9 EXIT=0  g11 EXIT=0
```

`tests/scripted/check-strings` is the one candidate leg that could not be measured here: it
fails with `CalledProcessError … git --git-dir plugins/CarlaBase/carla//.git ls-files` because
the `carla` submodule is not initialised in this worktree, and `checks.yml` runs
`git submodule update --init --recursive` first. It is **excluded by name** from the leg table
rather than included as a permanently-red leg, and that exclusion is stated here rather than
hidden. `shellcheck` is likewise excluded: CI pins `shellcheck-alpine:v0.9.0` while this host has
0.11.0, which reports style-only `SC2268` on `buildtools/update_locales` that 0.9.0 does not, so
a local run would red the leg for a version difference and not for the ref.

### 4.2 RED REF — the refusal, and its cause

```
$ bash tests/release-ref-fitness.sh --ref rel2/red-ref-proof
ref: rel2/red-ref-proof   commit: b776daa16b8a8265141e9cadca49b4f9f78f6f7f
EXIT=1
```

```
leg                      mirrors                            verdict
scripted-verify          checks.yml/scripted-checks         PASS
…
g9-fork-sources          quality-gates.yml/static-gates     PASS
g11-evidence             quality-gates.yml/static-gates     PASS
mcp-bridge-python-tests  quality-gates.yml/mcp-bridge-python-tests RED (exit 1)
staging-path             (this fork: REL-2 staging sanction) PASS

Failing legs (the cause is the first refusal line in each log):
  [RED] g4-complexity
        REGRESSION: new file over 500 lines: include/ControlRegistry.h (502)
  [RED] g7-file-length
        REGRESSION: plugins/ClapEffect/ClapHost.cpp grew 953 -> 1001 lines
  [RED] mcp-bridge-python-tests
        FAIL: test_deliberately_failing (tests.test_units.TestRel2RedRefProof.test_deliberately_failing)
        FAILED (failures=1)

RESULT: REFUSED — 3 leg(s) red on b776daa16. Do not cut a tag from this ref.
```

The **failing test** is the deliberate one: `FAILED (failures=1)` is a real
`unittest` assertion failure in `tests.test_units`, the module `quality-gates.yml`'s no-build
bridge job runs. The two gate reds are the **pre-existing** complexity/file-length regressions
this lane's base already carries (the lane brief names them; they are not this lane's work).

### 4.3 GREEN REF — the pass

`rel2/green-ref-proof` = `f611c888b` + the REL-2 change set, so that every leg exists and can be
measured on a ref whose static gates and checks really are green in CI.

```
$ bash tests/release-ref-fitness.sh --ref rel2/green-ref-proof
commit: e7afbbffba73205ea44511aee2c70839ecf1237e
EXIT=0
```

```
leg                      mirrors                            verdict
scripted-verify          checks.yml/scripted-checks         PASS
package-upload-guard     checks.yml/scripted-checks         PASS
yamllint                 checks.yml/yamllint                PASS
g3-no-tautology          quality-gates.yml/static-gates     PASS
g4-complexity            quality-gates.yml/static-gates     PASS
g4-complexity-tools      quality-gates.yml/static-gates     PASS
g6-upstream-regression   quality-gates.yml/static-gates     PASS
g7-file-length           quality-gates.yml/static-gates     PASS
g7-file-length-tools     quality-gates.yml/static-gates     PASS
g8-duplication           quality-gates.yml/static-gates     PASS
g8-duplication-tools     quality-gates.yml/static-gates     PASS
g9-fork-sources          quality-gates.yml/static-gates     PASS
g11-evidence             quality-gates.yml/static-gates     PASS
mcp-bridge-python-tests  quality-gates.yml/mcp-bridge-python-tests PASS
staging-path             (this fork: REL-2 staging sanction) PASS

RESULT: FIT — every required leg ran and passed on e7afbbffb (rel2/green-ref-proof).
```

### 4.4 THE CURRENT RELEASE TIP — refused, and it has no CI at all

```
$ bash tests/release-ref-fitness.sh --ref release/0.3.0
commit: 76d5a251ba34445a192eb81b096e3e3f1a06fc94
EXIT=1
```
```
g4-complexity            quality-gates.yml/static-gates     RED (exit 1)
g7-file-length           quality-gates.yml/static-gates     RED (exit 1)
RESULT: REFUSED — 2 leg(s) red on 76d5a251b. Do not cut a tag from this ref.
```

This is a **finding, not a lane failure**, and it is the reason the task says *before any tag*:
the release line is not releasable today. Three independent measurements say so.

1. **`release/0.3.0` has never had a green seven-platform matrix.** From the Actions API, the
   push runs on `release/0.3.0`: `build.yml` has **zero** successful runs
   (README-measured: 11 commits, `failure` or `cancelled` on every one), while `checks.yml`
   (10) and `quality-gates.yml` (11) have green runs. No commit is green on all three.
2. **The last CI'd commit is 50+ commits behind the tip.** The newest push runs for the branch
   are at `f611c888b` / `11f97ccfc`; the tip `76d5a251b` (and `823124529`, `c34f3a64b` as the
   train moved during this lane) has **no push run at all**, so the pre-tag evidence leg
   refuses it for *absence* as well as for reds.
3. **The static gates are red on the tip and were green at `f611c888b`.** Same legs, same host,
   same commands: all green at `f611c888b`, `g4`/`g6`/`g7` red at the tip. The trees after
   `f611c888b` grew files and functions without re-anchoring, and no CI run has happened since
   to say so. `tests/run-all-gates.sh` on this lane's base reports the same class of reds.

### 4.5 The CI-evidence leg, on a REAL captured API response

A real capture of `f611c888b`'s own push runs (22 rows, `gh api --paginate`, saved verbatim to
`logs/rel2/evidence/ci-capture-ci-green.tsv`) and replayed:

```
$ bash tests/release-ci-evidence-gate.sh --sha f611c888b… --runs-tsv logs/rel2/evidence/ci-capture-ci-green.tsv
EXIT=1
```
```
  workflow           required job                   verdict
  build.yml          linux-x86_64                   RED (failure)
  build.yml          linux-arm64                    RED (failure)
  build.yml          macos-x86_64                   RED (failure)
  build.yml          macos-arm64                    RED (failure)
  build.yml          mingw64                        ok (success)
  build.yml          msvc-x64                       RED (failure)
  build.yml          windows-arm64                  ok (success)
  checks.yml         scripted-checks                ok (success)
  checks.yml         shellcheck                     ok (success)
  checks.yml         yamllint                       ok (success)
  quality-gates.yml  static gates (3, 4, 6, 7, 8, 9, 11) ok (success)
  build.yml          <whole run>                    RED (this commit has a completed build.yml push run concluding failure)

RESULT: REFUSED — 6 gap(s) in this commit's own CI coverage.
```

This is the **measured defect** the whole task turns on, in the product repo's own numbers
(build run 34870198514): 5 of the 7 platform jobs red, and
`release gate (green matrix required)` = **`skipped`**. Two of the seven really did pass
(`mingw64`, `windows-arm64`) and the gate reports them as passing — a run-level conclusion is
not a job-level result, in either direction.

The other two captures are empty and refuse on absence: `rel2/green-ref-proof` (a local commit,
0 rows → no push run exists) and the release tip (0 rows → the tip is CI-unverified).

### 4.6 The self-test, and the gates the change sits under

```
$ bash tests/test-release-ref-fitness.sh                                        EXIT=0
$ bash tests/test-package-upload-guard.sh                                       EXIT=0
$ bash tests/release-staging-path-gate.sh                                       EXIT=0
$ bash tests/fork-sources-gate.sh                                               EXIT=0   (Gate 9)
$ bash tests/evidence-gate.sh                                                   EXIT=0   (Gate 11)
$ bash tests/no-upstream-regression-gate.sh                                     EXIT=0   (Gate 6)
$ bash tests/no-tautology-gate.sh                                               EXIT=0   (Gate 3)
$ for i in $(git ls-files '*.yml'); do yamllint $i; done                        EXIT=0
```

Self-test controls, all PASS: the leg table names 15 legs; the required job set is 11 names and
includes `static gates (3, 4, 6, 7, 8, 9, 11)` **by name**; a deliberately red ref is REFUSED by
one leg and by the full set (EXIT=1) while its base PASSes that leg (EXIT=0); a leg whose tool
is absent does not become a pass (EXIT=1); the staging-path gate is seen RED against a workflow
**copy** with the `workflow_dispatch` staging path restored (EXIT=1), which is the same control
shape `tests/test-package-upload-guard.sh` uses.

`tests/run-all-gates.sh` on this branch (`logs/rel2/evidence/run-all-gates.log`):

```
gate   name                     result
1      ctest                    SKIP          3  no-tautology         PASS
2      coverage                 SKIP          4  complexity           FAIL
5      mutation                 FAIL          6  upstream-regression  PASS
7      file-length              FAIL          8  duplication          PASS
9      fork-sources             PASS         10  unregistered-tests   PASS
11     evidence                 PASS
RESULT: FAIL — see the failing gate above
EXIT=1
```

The lane brief's expectation is exit 3 (pass-with-skips); the measured result is exit 1, and
**the red set is identical to the one measured at this lane's base `3fae5a5e1`, before any of
this lane's work** (gates 4, 5, 7 — the inherited complexity/file-length reds and the mutation
gate). This lane's change added no red gate. Gates 3, 6, 8, 9, 10 and 11 are green both before
and after, which is what makes the "no manifest, no baseline, no ratchet moved" claim checkable
rather than asserted.

## 5. What does NOT work, or is not verified

1. **The workflow files have never run on GitHub.** `release.yml` is new and `build.yml` is
   edited; nothing is pushed this pass, so no Actions run has executed either. Locally
   verified instead: YAML parses (`yaml.safe_load` → jobs `release-fitness`, `stage-artefacts`),
   `yamllint` clean, and every property the job depends on is asserted mechanically by
   `tests/release-staging-path-gate.sh`. **The CI-only legs are: (a) GitHub actually skipping a
   `needs`-dependent job on a red matrix, (b) `workflow_run` firing on all three upstream
   workflows, (c) the `toJSON(needs)`/`GITHUB_STEP_SUMMARY` expressions evaluating.** (a) is
   already *measured* in the product repo's own history — the `skipped` in §4.5 is exactly it —
   so the defect it fixes is observed; that `always()` removes it is inferred from the documented
   semantics and is the one claim a dispatch of `build.yml` would settle.
2. **The API leg's in-flight path is untested against a live in-flight matrix.** `--runs-tsv`
   replay covers completed/absent/red; the `IN FLIGHT (in_progress)` branch is reachable only
   from a live API call during a running matrix.
3. **`release-tag-protection` was not re-measured.** §3(c) cites it from
   `quality-gates.yml`'s own comment ("none of them a required check"), not from a fresh
   `gh api repos/…/rulesets` in this lane.
4. **`check-strings`, `check-namespace` and `shellcheck` are not legs** — the first two need the
   `carla` submodule, the third a version pin, both stated with their measurements in §4.1.
5. **`tests/run-all-gates.sh` is mode 644 in this worktree** and must be invoked as
   `bash tests/run-all-gates.sh`; the brief's bare `tests/run-all-gates.sh` gives
   `Permission denied` (exit 126). Not this lane's file; named as a finding.
6. **`on: workflow_run` only fires from the default branch.** GitHub registers a `workflow_run`
   trigger only for a workflow file that exists on the repository's default branch, so until
   `release.yml` is merged there, the **dispatch** path is the one that works — and
   `workflow_dispatch` needs the file reachable on the ref being dispatched. Consequence for
   the next pass: run `gh workflow run release.yml -f ref=<candidate>` (or the UI) against the
   merged branch to rehearse the pre-tag decision *before* the first tag; the
   `workflow_run` trigger takes over by itself once the file is on the default branch. This is
   a GitHub behaviour this lane could not measure without a push, so it is stated as a
   precondition rather than as a verified fact.

## 6. Hotspots / collisions

- `hotspot: .github/workflows/build.yml` — 63 KB, six near-identical upload steps. Any lane
  editing CI touches the same file; this lane's change is confined to the `if:` line of those six
  steps and to the `release-gate` block's `if:` plus its comment. If another lane is editing
  `build.yml` in the same train, expect a trivial conflict there.
- `hotspot: docs/reports/` — this lane's report follows the `docs/reports/<LANE>-REPORT.md`
  convention; the raw evidence deliberately lives in the gitignored `logs/rel2/` (a committed
  `.log` is refused by Gate 11).
- `hotspot: release/0.3.0 is moving under this lane` — measured at `823124529`, then
  `76d5a251b`, then `c34f3a64b` within the same window, and the readset differs each time
  (3 red legs, then 2). Any re-measurement must name the tip SHA it measured.
- Pre-existing reds named, not touched: `g4-complexity` (regressions in
  `tests/stem_commands_lib.py`, `plugins/ClapEffect/ClapHost.cpp`, `plugins/Vst3Effect/Vst3Host.cpp`),
  `g5-mutation`, `g7-file-length` (`src/core/ControlCommands{Notes,Project,WarpEdit}.cpp`,
  `include/ControlRegistryGroups.h`, `include/ControlRegistry.h`), and Gate 9's inherited
  `tools/dawproject-{a16-histogram,roundtrip-proof}.cpp` namespace-comment errors at `3fae5a5e1`.

## 7. The single next action

**Wire `tests/release-staging-path-gate.sh` into `quality-gates.yml`'s `static-gates` job as an
added step** (`run: bash tests/release-staging-path-gate.sh`) — deliberately **not** done in this
lane: assertions A4–A6 need `.github/workflows/release.yml` to exist, so adding the step before
this branch is merged would turn every sibling lane's push red for a file that is not on their
branch. Once `release.yml` has landed on `release/0.3.0`, that one step makes the invariant run
on every push, and the release path can no longer be edited back into a skipping shape silently.

Then: dispatch `build.yml` once with the release path merged and confirm the `release gate (green
matrix required)` job reports **`failure`** (not `skipped`) on a red matrix, which is the one
claim in §5.1 that only CI can settle.
