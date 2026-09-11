# Pipeline hardening — three failures the lanes surfaced

Worktree `projects/lmms-fl-research/zene-pa-hardening`, branch `post-alpha/pipeline-hardening`,
based on `post-alpha/integration`. Everything below is verification/deployment pipeline work:
**no product behaviour changed**, no existing gate was weakened, nothing was pushed and no
PR/issue was touched.

The three items are the ones four independent lanes surfaced: a package upload that can match
nothing and still go green, no correct gate scope for fork-authored `tools/` code, and a
documented regeneration command that uses a stale base ref. Two **pre-existing** reds on the
integration branch had to be repaired to make the pipeline actually green — they are recorded in
their own section below, not hidden here.

---

## Item 1 — a package upload could match nothing and still go green

### What was wrong

`.github/workflows/build.yml` uploads each platform's package by glob. What **this tree** says
(measured, not assumed):

| job | step | glob |
|---|---|---|
| `linux-x86_64` | Upload artifacts (L100) | `build/lmms-*.AppImage` |
| `linux-arm64` | Upload artifacts (L189) | `build/lmms-*.AppImage` |
| `macos` (both arches) | Upload artifacts (L289) | `build/lmms-*.dmg` |
| `mingw64` | Upload artifacts (L376) | `build/lmms-*.exe` |
| `msvc-x64` | Upload artifacts (L505) | `build\lmms-*.exe` |
| `windows-arm64` | Upload artifacts (L575) | `build\lmms-*.exe` |
| `msvc-x64` | Upload the ctest log on failure | `build/tests/Testing/Temporary/LastTest.log` |

The globs are still `lmms-*` **in this tree**. The rename to `zene-*` lives on branch
`post-alpha/wave-r-rename` (commit `018d2041f`, "feat(naming): wave R — rename the product to
Zene Studio in build, packaging and UI") and is **not** an ancestor of this branch: it is the only
branch in the clone whose `build.yml` contains a `zene-` path. The merged `docs/zene-studio-rename`
branch renamed the repo/docs, not the artefacts. So on a merge of `wave-r-rename`, the packaging
rename and this guard land together — which is the point of fixing the guard first.

None of the six package steps set `if-no-files-found`, so all six took the action's default,
`warn`: a job that packages nothing warns and **succeeds**, publishing zero assets for that
platform. Nobody learns until a user finds nothing to download, which is exactly what a rename
that misses one packaging path produces.

### What changed

`if-no-files-found: error` added to all six package-upload steps
(`.github/workflows/build.yml` L113-L114, L207-L208, L311-L312, L399-L400, L528-L529, L600-L601),
with the rationale written beside the first one.

The one **intentionally package-less** upload is exempt, explicitly rather than by weakening the
check: `Upload the ctest log on failure` (L496-L520) runs on `failure()` and ctest may not have
written `LastTest.log`, so it keeps `if-no-files-found: warn` — with that reason written into the
step (L517-L519) and into `tests/QA-GATES.md`. Failing there would replace the real failure with a
missing-log error.

### The pinned action supports the flag (verified, not assumed)

`actions/upload-artifact@v7` — tag ref `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a`:

- `action.yml` at that ref declares the input:
  `if-no-files-found: … warn | error: Fail the action with an error message | ignore … default: 'warn'`.
- `src/upload/upload-artifact.ts` at that ref fails the step for `if-no-files-found: error` with
  `core.setFailed("No files were found with the provided path: ${inputs.searchPath}. No artifacts
  will be uploaded.")` — `searchPath` is the step's `path:`, so the failure **names the glob and
  the directory it searched**, and it fails **in the job that produced no package**.

`tests/qa-gate`-style probing was unnecessary: the flag is available at the pinned version, so it
was preferred over a custom script.

### Proof

`tests/test-package-upload-guard.sh` (new; run on every push by the `checks` workflow).

Part 1 exercises the decision both ways against real directories — `bash tests/test-package-upload-guard.sh`:

```
=== Part 1: the glob decision, both ways ===
  matched: pkg-1.0-linux-x86_64.AppImage
  PASS a matching package is uploaded (exit 0)                                  0
  No files were found with the provided path: pkg-*.AppImage. No artifacts will be uploaded.
  PASS no matching package fails the job under 'error' (exit 1)                 1
  No files were found with the provided path: pkg-*.AppImage. No artifacts will be uploaded.
  PASS no matching package only warns under 'warn' (exit 0) - the silent pass   0
```

Part 2 asserts every upload step in the workflow carries the right policy, and proves the checker
can go red by running it against a copy with the flag stripped
(`sed '/if-no-files-found: error/d'`): the committed workflow → exit 0; the weakened copy → exit 1
(green→red in the run above, `RESULT: PASS`, harness exit **0**).

### What I did NOT do

- No custom per-platform script: the pinned action's own flag is sufficient and fails in the
  producing job with the glob named.
- Did not change the globs (`lmms-*` → `zene-*`): that is `post-alpha/wave-r-rename`'s change, and
  duplicating it here would conflict on merge.
- Did not touch the tag/dispatch gate on the upload steps (the artifact-storage-quota policy).
- **Residual gap, stated:** the upload steps are skipped on a normal push
  (`if: startsWith(github.ref, 'refs/tags/') || workflow_dispatch`), so a *push* run that packages
  nothing is still silent — nothing is published on a push either, so no user is affected. The
  guard fires on the release path (tag/dispatch), which is where an empty package reaches a user.
- Noticed, not changed: the package upload steps carry no `name:` input and therefore share the
  action default artifact name.

---

## Item 2 — the gates' scope had no home for `tools/`

### What each scope list is FOR (established before deciding)

| list | what it is for | consumers |
|---|---|---|
| `tests/fork-sources.txt` | the fork's own **product** sources; the fork-scoped ratchets and Gate 2's per-file coverage baseline | Gates 2, 4, 7, 8; `run-coverage.sh` lcov extraction |
| `tests/all-sources.txt` | whole-tree first-party **C/C++** scope ("every first-party C/C++ source in this repo", its own header) — upstream code plus the fork's C/C++ tests and probes | Gates 4, 7, 8 with `--scope all` |
| `tests/upstream-modifications.txt` | the upstream-**divergence ledger**: one `<path><TAB><reason>` per inherited file this repo deliberately changed. Reason mandatory | Gate 6 |

`tools/` does not exist upstream — `git ls-tree -r --name-only 4e677cb6c6ab -- tools` is empty (and
stays empty on upstream master) — so every file under it is fork-authored by construction. Both
pre-existing homes misdescribe such a file, and the lane's workaround (declaring tooling in the
**divergence ledger**) is a false statement about it: a file upstream has never had cannot be a
divergence of inherited code.

The measured cost of the other home: `tools/mmpz-git/mmpz_git.py` is **818 lines** and its
functions reach **CCN 83**; across the tooling there are 105 functions with 8 over CCN 10 and one
file over the 500-line target. Registering that in `tests/fork-sources.txt` fires Gates 4 and 7 as
*new* violations — i.e. it would have forced a product-ratchet re-anchor to accommodate a Python
tool no product build ever compiles.

### The decision

**Option (a): a `tools` scope list with its own baselines.** Option (b), an explicit documented
exclusion, was rejected: it would leave the fork's own tooling measured by nothing and would solve
the problem by removing a signal rather than by giving the file a home. The pipeline's whole point
is that a file in no scope list is **reported**; an exclusion is honest only about what it does
not do, and Gate 9's remedy ("give the file a home") is better served by a third scope.

### Implementation

- **`tests/tools-sources.txt`** (new) — the home: 8 tracked source files under `tools/` (the `.py`
  and `.sh` set), with a header carrying the reasoning above, the regeneration command, and the
  scope limits.
- **`tests/complexity-gate.sh`** — `--scope tools` arm (`tools-sources.txt` +
  `tests/complexity-baseline-tools.tsv`), scope-aware baseline header and count line.
- **`tests/file-length-gate.sh`** — `--scope tools` arm with `tests/file-length-baseline-tools.tsv`.
- **`tests/duplication-gate.sh`** — `--scope tools` arm; the tools scope selects
  `--format python --format cpp` (jscpd has no shell format — stated limit) via a per-scope
  `FORMAT_ARGS` array.
- **`tests/fork-sources-gate.sh`** (Gate 9) — now scans `tools/` as well, with a wider extension
  set for `tools/` (`.py`/`.sh` beside the C/C++ ones), a third registration target, and a
  tooling-aware remedy line. A tools file in **no** list is still reported
  (`NOT IN tests/fork-sources.txt` + the two other lists), never silently skipped; `tools-sources.txt`
  is optional for synthetic fixtures that have no `tools/`, so the red/green fixture harness keeps
  working.
- **`tests/run-all-gates.sh`** — Gates 4, 7 and 8 now run the tools scope in the **same gate row**
  (a red tools ratchet is a red gate 4/7/8); the row count stays 9 so the runner's own fixture
  assertions (`tests/test-verification-debt.sh`) are untouched.
- **`.github/workflows/quality-gates.yml`** — three tools-scope steps in `static-gates`.
- **`tests/QA-GATES.md`** — the Tooling-scope paragraph, the Gate 9 rule, and the Gate 4/7/8 command
  blocks.

Baselines: `tests/complexity-baseline-tools.tsv` (8 functions grandfathered) and
`tests/file-length-baseline-tools.tsv` (1 file) were captured with the gates' own recorded
mechanism (`--reanchor "reason"`), the same grandfathering policy the product ratchets use — not a
raised threshold and not a disabled gate.

### Proof (unpiped)

```
bash tests/fork-sources-gate.sh                          -> GATE9 EXIT=0
   scanned 1118 tracked source file(s) under src/, include/, plugins/, tests/, tools/
   110 fork-sources, 1000 all-sources (whole-tree), 8 tools-sources, 0 stale
bash tests/complexity-gate.sh --check --scope tools      -> EXIT=0
bash tests/file-length-gate.sh --check --scope tools     -> EXIT=0
bash tests/duplication-gate.sh --scope tools             -> EXIT=0  (0.00% duplicated lines)
```

The gate earned its keep during this work: scanning `tools/` for the first time immediately named
`tools/mmpz-git/scratch/proto.py`, which had been in no list — reported, then registered.

### Limits

- `.sh` entries are measured by Gates 4 and 7 only (jscpd in the version wired here has no shell
  format).
- Tooling is outside Gate 2 (lcov instruments C/C++) and Gate 5 (the mutation harness targets one
  C++ TU) by construction — not by exemption.
- The five C/C++ files under `tools/` stay in `tests/all-sources.txt` (the whole-tree C/C++ scope);
  one file, one home, and moving them would shrink the whole-tree scope.

---

## Item 3 — the documented regeneration command used a stale base ref

### What was wrong

`tests/fork-sources.txt`'s header told a reader to regenerate with

```sh
git diff --name-only --diff-filter=A origin/master HEAD -- src include plugins ...
```

In this clone `origin` is upstream **LMMS/lmms** and its `master` is a moving upstream ref that is
**not an ancestor of the fork point** (`git merge-base --is-ancestor origin/master 4e677cb6c6ab`
exits 1; `origin/master` was `518a7e8ef`, 2026-09-10, while the fork is based on `4e677cb6c6ab`,
2026-08-30). Following it literally therefore reclassifies **7 pre-existing upstream files** as
fork-new:

```
include/AudioOss.h                 include/AudioSoundIo.h        include/MidiOss.h
plugins/Sf2Player/fluidsynthshims.h
src/core/audio/AudioOss.cpp        src/core/audio/AudioSoundIo.cpp
src/core/midi/MidiOss.cpp
```

Every one of them exists at `4e677cb6c6ab` (`git cat-file -e 4e677cb6c6ab:<path>` → present), i.e.
they are upstream files this fork never authored.

### What changed

The header now names the base explicitly — `4e677cb6c6ab`, the upstream commit this fork is based
on — uses it in the command, and says why `origin/master` is wrong and what the wrong command
costs (7 files, 116 instead of 109). It also notes the equivalent
`git merge-base origin/master HEAD` (which returns the same commit **only while origin is
fetched**; the literal commit cannot drift) and the verification step
`git cat-file -e 4e677cb6c6ab^{commit}`.

### Proof

Ran the command **exactly as the header documents it** (extracted from the header text, not
retyped):

```
before (old command, origin/master):  116 lines  -> 7 false positives
after  (documented command, 4e677cb6c6ab): 109 lines -> 0 false positives
```

The 109 is not just plausible: sorted, it equals the manifest exactly — the only difference from
`tests/fork-sources.txt`'s 110 entries is `tools/local-ci.sh`, which is outside the command's
pathspec (hand-added; now documented in both manifests).

---

## Pre-existing reds on the integration branch, repaired

`post-alpha/integration` did **not** have a green gate suite when this lane started. Both reds are
declaration omissions by merged lanes, not product defects; neither fix changes product behaviour.

1. **Gate 9, exit 1 — three fork-authored test files in no scope list**:
   `tests/src/core/LufsMeterTest.cpp` (`d124b2a86`), `tests/src/core/MidiLearnTest.cpp`
   (`22617a93c`), `tests/src/core/SessionModelTest.cpp` (`ca5c19376`). Registered in
   `tests/all-sources.txt`, which is where **every** test file in this repo lives — 43 of the 51
   `tests/src/**` entries there are fork-authored (absent at the fork point; the other 8 are
   upstream's own tests). The fork ratchets and Gate 2's per-file baseline deliberately measure
   product sources, so this is the convention, not a workaround: the alternative (fork-sources.txt)
   would put test harnesses under the per-file coverage entry floor.
2. **Gate 6, exit 1 — four undeclared changes to inherited code**, all from
   `5d6ccdf1f` ("feat(midi): global one-shot MIDI-learn mode"): `include/MainWindow.h`,
   `include/MidiController.h`, `src/core/midi/MidiAlsaSeq.cpp`, `src/core/midi/MidiClient.cpp`.
   Declared in `tests/upstream-modifications.txt` with real reasons citing the lane's commit — the
   LEDGER's own rule ("ship a regression test in the same change") is already satisfied by
   `tests/src/core/MidiLearnTest.cpp`. Gate 6 now reports 38 ledger files, 0 violations, exit 0.

---

## Verification — every exit code measured unpiped (`cmd > log 2>&1; echo EXIT=$?`)

| command | exit | meaning |
|---|---|---|
| `bash tests/test-package-upload-guard.sh` | **0** | glob guard: 0/1/0 both ways; workflow passes; weakened copy rejected |
| `bash tests/fork-sources-gate.sh` | **0** | 1118 sources scanned, 0 unregistered, 0 stale |
| `bash tests/no-upstream-regression-gate.sh` | **0** | 38 ledger files, 0 violations |
| `bash tests/complexity-gate.sh --check` (fork) | **0** | no ratchet regression |
| `bash tests/complexity-gate.sh --check --scope tools` | **0** | tools baseline holds |
| `bash tests/file-length-gate.sh --check` (fork) | **0** | no ratchet regression |
| `bash tests/file-length-gate.sh --check --scope tools` | **0** | tools baseline holds |
| `bash tests/duplication-gate.sh` / `--scope tools` | **0** / **0** | both under the 5% budget |
| `bash tests/no-tautology-gate.sh` | **0** | passes |
| `bash tests/test-verification-debt.sh` | **0** | Gate 9's red/green fixture harness still holds |
| `bash tools/local-ci.sh --build-dir build --jobs 4` | **0** | configure + build + **ctest 27/27 passed** |
| `bash tests/run-all-gates.sh` | **3** | PASS-WITH-SKIPS (gate 2 not requested) — gates 1,3,4,5,6,7,8,9 PASS; mutation kill score 23/26 = 88.5% (threshold 80%); **not** a green run, see below |

`run-all-gates.sh` is **not** a green run at exit 3 (by design: a skipped gate is not a pass):

```
1 ctest PASS   2 coverage SKIP   3 no-tautology PASS   4 complexity PASS   5 mutation PASS
6 upstream-regression PASS   7 file-length PASS   8 duplication PASS   9 fork-sources PASS
skipped: 1 of 9 gates did not run -> gate 2 (coverage): pass --with-coverage
RESULT: PASS-WITH-SKIPS (exit 3)
```

**Not run, and why:** Gate 2 (coverage) needs a full instrumented rebuild
(`tests/run-coverage.sh`, `-DWANT_COVERAGE=ON`, ~15-25 min) and this lane was given exactly one
build. No coverage baseline was written or re-anchored, so the coverage ratchet is unchanged by
this work — but the three test files newly registered in `tests/all-sources.txt` are in the
whole-tree scope, whose coverage figures in `docs/CONVENTIONS.md` predate this change (they were
already stale: `tests/all-sources.txt` holds 1,100 entries, `docs/CONVENTIONS.md` still says
1,095).

## Note for the merge (measured, and a prediction clearly labelled as one)

`post-alpha/integration` **moved while this lane worked**: it is now `336a0c6cf` ("Merge branch
`post-alpha/mmpz-git-depth` into `post-alpha/integration`"), 20 commits ahead of this branch's base
`2849c0934` (which is still an ancestor). This branch is 5 commits on top of `2849c0934`; nothing
was rebased, because rebasing would pull 20 commits of other lanes' product work into this tree and
invalidate the single build this lane was allowed. Everything measured in this document was measured
on this branch's tree.

Two consequences, the first measured and the second a prediction (inputs measured, gates not run
against the moved ref):

- **Measured:** `tools/local-ci.sh` is mode `100644` on this branch and `100755` on the moved ref —
  it was made executable by a later lane, which is why it must be invoked as
  `bash tools/local-ci.sh` here.
- **Predicted merge-time work (2 steps, nothing hidden):** the `mmpz-git-depth` lane is exactly the
  `tools/` work item 2 is about, and it grew the tooling substantially — measured at `336a0c6cf`,
  `tools/mmpz-git/mmpz_git.py` is **1,892 lines** (818 here) and
  `tools/mmpz-git/tests/test_mmpz_git.py` is **843** (269 here), with three tooling files that do
  not exist on this branch: `tools/mmpz-git/demo_check.py`, `tools/mmpz-git/depth-demo.sh`,
  `tools/mmpz-git/render-recipe.sh`. After the merge the gates will therefore report: Gate 9 →
  three unregistered `tools/` sources (add them to `tests/tools-sources.txt`); Gate 7 `--scope tools`
  → `mmpz_git.py` grew and `test_mmpz_git.py` is newly over 500 lines; Gate 4 `--scope tools` →
  new/changed over-target functions. That is the guards working as designed, and the fix is the
  documented one: register the three files, then re-anchor the two tools baselines with
  `--reanchor "reason"`. They were **not** applied here, because doing so against this branch would
  be re-anchoring against code that is not in this tree.

## What this work deliberately did not do

- No product behaviour change; no product source touched (only `tests/`, `.github/workflows/` and one new `docs/` file).
- No gate weakened, disabled, de-scoped or threshold-raised; no baseline re-anchored except the two
  **new** tools baselines, captured by the gates' own recorded `--reanchor` mechanism.
- Nothing pushed; no PR/issue opened, commented or read-modified; `origin` (LMMS/lmms) and
  `messmerd` untouched. Commits are local.
- Other lanes' worktrees and build directories untouched; one build directory (`build/`) used.
- Not fixed here: the `latency` ratchets' known nesting-depth gap (lizard does not compute ND), the
  Windows test-host limitation, and the stale whole-tree numbers in `docs/CONVENTIONS.md`.
