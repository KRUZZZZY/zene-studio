# Gate hygiene: the repo's own quality gates and manifests vs the tree

Lane: `post-alpha/gate-hygiene`, branched from `post-alpha/integration` at `1ef366607`.
Worktree: `projects/lmms-fl-research/zene-pa-gatehygiene`.
Input: the independent tree-level audit `projects/lmms-fl-research/feedback/compliance-D-tree.md`
(§7A "Real defects" D-1…D-9, §3 manifests, §4 ratchet discipline), read as the specification.

Evidence: **`tests/gate-hygiene-logs/`** (committed, not `/tmp` — that is audit finding D-8):

```
tests/gate-hygiene-logs/before/          the base commit: every gate, red where it was red
tests/gate-hygiene-logs/pre-reanchor/    the all-scope state before the per-file decisions
tests/gate-hygiene-logs/reanchor/        the per-file re-anchor runs and their printed reasons
tests/gate-hygiene-logs/after/           the reconciled tree: every gate, every scope
tests/gate-hygiene-logs/after-*.log      the build/configure/ctest runs of the final tree
```

Why not `docs/`: Gate 6 classifies an unrecognised path as an undeclared upstream divergence, and a
`.log` file under `docs/` is exactly that (it failed on my first attempt). `tests/**` is a category
Gate 6 allows, which is where this repo already keeps durable evidence (`tests/reference/ORIGIN.tsv`,
`tests/scripted/`). Committed evidence has to live where the divergence gate already classifies it.

## Gate exit codes: before → after

Every code below is from an unpiped run (`cmd > log 2>&1; echo EXIT=$?`), committed under
`tests/gate-hygiene-logs/`.

| gate / scope | command | before | after |
|---|---|---|---|
| 7 file-length, all | `bash tests/file-length-gate.sh --scope all --check` | **1** | **0** |
| 4 complexity, all | `bash tests/complexity-gate.sh --scope all --check` | **1** | **0** |
| 7 file-length, fork (default) | `bash tests/file-length-gate.sh --check` | 0 | **0** |
| 4 complexity, fork (default) | `bash tests/complexity-gate.sh --check` | 0 | **0** |
| 7 file-length, tools | `bash tests/file-length-gate.sh --scope tools --check` | 0 | **0** (went 1 on the mmpz guard, then 0 after its recorded re-anchor) |
| 4 complexity, tools | `bash tests/complexity-gate.sh --scope tools --check` | 0 | **0** |
| 8 duplication, fork / all / tools | `bash tests/duplication-gate.sh [--scope X] --check` | 0 / 0 / 0 | **0 / 0 / 0** |
| 3 no tautological tests | `bash tests/no-tautology-gate.sh` | 0 | **0** |
| 9 registration | `bash tests/fork-sources-gate.sh` | 0 | **0** |
| 6 upstream divergence | `bash tests/no-upstream-regression-gate.sh` | 0 | **0** |
| runner | `bash tests/run-all-gates.sh` | 3 | **3** (`PASS-WITH-SKIPS`; gate 2 not requested) |
| runner | `bash tests/run-all-gates.sh --no-mutation` | 3 | **3** |
| 1 unit tests | `cd build/tests && ctest` | 36 pass / 0 fail (1 registered-but-never-built suite, below) | **37 pass / 0 fail** — see the `PdcMixerTest` teardown abort under "Limits" |
| 2 coverage | needs an instrumented build | **not run** | **not run** |
| 5 mutation | needs a build + relink | not run via the runner | **not run** (skipped by `--no-mutation`) |

Gate 2 (coverage) and Gate 5 (mutation) were **not run**: the first needs a
`-DWANT_COVERAGE=ON` build and lcov, the second rebuilds a TU and relinks a test binary. Stated
rather than claimed.

## D-2 / D-3 — the whole-tree ratchets were red and unreported, and one file was green in one scope

**What was wrong.** At the base commit `bash tests/file-length-gate.sh --scope all --check` and
`bash tests/complexity-gate.sh --scope all --check` both exited **1**, and nothing reported it:
`run-all-gates.sh` is fork-scoped unless `--whole-tree`, CI's `static-gates` job runs fork + tools,
and `docs/INTEGRATION-MERGES.md` published the fork-scope PASS. 23 file-length regressions (5 new
files over 500) and 13 complexity regressions; the worst was ours —
`lmms::PluginFactory::discoverPlugins` **CCN 15 → 32**. `src/core/CrashReporter.cpp` (526) had been
re-anchored in the *fork* baseline at `349aa0732` and left as a new-file violation in the *all*
baseline: the same file green in one scope, red in the other.

**1. The code regression was fixed by extraction** (`src/core/PluginFactory.cpp`,
`include/PluginFactory.h`). `discoverPlugins` (222 lines) became the sequence, with nine new
private methods carrying the stages:

| function | before | after |
|---|---|---|
| `PluginFactory::discoverPlugins` | CCN 32, NLOC 172 | **CCN 7, NLOC 40** |
| `candidatePluginFiles()`, `dropQuarantinedPlugins()`, `planPluginScan()`, `planForCandidate()`, `preloadPluginLibraries()`, `scanOnePlugin()`, `loadPluginDescriptor()`, `appendLoadedPlugin()`, `appendCacheServedPlugin()`, `addSupportedFileTypes()` | inline | each ≤ CCN 7 |

`src/core/PluginFactory.cpp` has no function above CCN 10 now. The load path's three outcomes
(did not load / loaded but names no descriptor / descriptor resolved) are explicit fields of a
`PluginLoadOutcome` instead of fallthrough into an `else` branch; the warning that distinguishes
"exported `lmms_plugin_main` but the named symbol is missing" from "not a plugin at all" is carried
in that struct and still printed. Verified: complexity-all regression list loses the
`discoverPlugins` line entirely (`tests/gate-hygiene-logs/pre-reanchor/complexity-all.log` line 307
→ `tests/gate-hygiene-logs/after/complexity-all.log`), and the behaviour is held by
`tests/src/core/PluginScanCacheTest.cpp` (see "The extraction is proven" below).

The extraction grew the file 580 → 688 lines. That consequence is recorded as its own baseline entry
with its own reason; it was **not** paid for by trimming code (rule 4), and it is the reason the
file has a file-length entry at all.

**2. Every other regression got one decision of its own — no scope-wide re-anchor.** The gates'
only re-anchor primitive, `--reanchor "reason"`, rewrites the whole baseline; using it here would
have grandfathered 23 file-length and 12 complexity entries unreviewed, which is a real weakening
even though it goes through the documented mechanism. A sibling lane reached the same conclusion
independently and refused to move its 511-line `SampleClipWindowTest.cpp` into this scope for
exactly that reason. So both gates gained a **single-file** mode:

```
tests/file-length-gate.sh:24 (usage), 52-60 (parse), 75-77 (validation), 143-172 (the mode)
tests/complexity-gate.sh:24 (usage), 47-55 (parse), 73-75 (validation), 165-201 (the mode)
```

It carries every other baseline entry over untouched, prints each key with its old and new value,
exits 2 on a blank reason, and exits 2 if the path is not over the limit or not in the scope's
manifest. Both negative controls were run: blank reason → **EXIT=2**; a path that is not measured
(`no/such/file.cpp`) → **EXIT=2**; the real case on `include/Song.h` added exactly one baseline line
(`diff` of `tests/file-length-baseline-all.tsv`: `100a101 > include/Song.h 539`) and left the count
107 → 108.

**23 file-length entries and 10 complexity paths** were re-anchored this way, one invocation each,
every reason naming the file, the measured size (or CCN), the delta, the commit that last touched it
and why the growth is legitimate. Full transcript:
`tests/gate-hygiene-logs/reanchor/per-file-all.log`. As instructed, the four tests that had *grown
past their own recorded baselines* were decided as growth, not as "never had an entry":
`tests/src/core/ScriptEngineTest.cpp` 546 → 592, `tests/src/plugins/PluginPortsHarness.h`
1156 → 1196, `tests/src/plugins/PluginPortsMigrationTest.cpp` 632 → 731,
`tests/src/wasm/WasmSandboxTest.cpp` 948 → 1022 — each with the commit that grew it named in its
reason.

**Fix or grandfather, per file, and why.** Nothing in the list was split. The reasoning, recorded per
entry rather than once: every one of them is a test suite or a declared-divergence product file whose
growth is the feature's own work; splitting such a TU is a refactor in its own right, and the
ratchet's job is to force the decision, which is now recorded per file. The one exception in
direction is `PluginFactory.cpp`, fixed by extraction and *then* recorded.

**3. The all-scope status is now written down.** `tests/QA-GATES.md` has a new
"Scope policy: what runs by default, and what does not" section stating: the fork scope is the
release gate (per `docs/CONVENTIONS.md`); the tools scope is enforced alongside it; the whole-tree
scope is an **advisory monitoring ratchet** over upstream LMMS plus the fork's own sources, most of
which this repo deliberately does not refactor — so a red all-scope is a signal to review and record,
not a release blocker, while a red fork scope is a blocker. Every `run-all-gates.sh` run now prints
the scopes it measured and says that a default run does **not** measure the whole-tree scope
(`tests/run-all-gates.sh:180-191`), and the gate banners no longer print the stale "1,095 files".

**Deliberately NOT done.** (a) No scope-wide `--reanchor`. (b) The whole-tree scope is still absent
from CI's `static-gates` job: enforcing it is an owner decision about metered runner minutes and
about whether the integration branch's CI may go red on the next merge before a freeze. (c) The
fork scope was not touched to make anything else green: it was green throughout and its baselines
are byte-identical to the base (`git diff` for `tests/*-baseline.tsv`, `tests/*-baseline-tools.tsv`:
the fork baseline is unchanged).

## D-4 — `all-sources.txt` did not reproduce from its own command, and missed 20 fork sources

**What was wrong.** The header's command — `git ls-files '*.c' … | grep -vE '<exclusions>' | sort >
tests/all-sources.txt` — yielded **1,133** entries against a file holding **1,113**: 20 files the
command produced were absent from the file, every one of them fork-NEW and every one registered in
`fork-sources.txt`, which is why `docs/CONVENTIONS.md:57-58` ("every first-party C/C++ source …
plus everything this fork adds") described a file that did not exist. Nothing caught it: Gate 9
checks registered→exists and never file→registered.

Two independent reasons it could not reproduce: the 20 missing entries, and the order. Neither
manifest was in *any* locale's `sort` order — the all-scope file had ~100 adjacent pairs out of
order together with two entries appended out of place, so `| sort` reported a hundred spurious
moves on every regeneration. That is the mechanism behind the drift: nobody could regenerate the file
and get a diff small enough to review.

**What changed** (`tests/all-sources.txt`, ledger commit):
- the entry list is now **the byte-exact output of the documented command** (1,133 entries), with
  `LC_ALL=C sort` pinned so the order is the same on every machine, plus an explicit verify step
  (`diff <(grep -vE '^[[:space:]]*(#|$)' tests/all-sources.txt) <(<the command>) && echo REPRODUCES`)
  that must print `REPRODUCES`. Verified: it prints `REPRODUCES`, and the entry-set delta against
  the base file is **+20 added, 0 removed**;
- the header now states the definition it actually satisfies, including that a fork source belongs
  to *both* scopes (fork scope ratchets it as new code; the whole-tree scope measures it beside the
  code it was written against).

## D-5 — `fork-sources.txt`: 7 unexplained entries, and a stray `tools/` file

**What was wrong.** Its command's pathspec is `-- src include plugins`, so it can never produce a
`tests/` or `tools/` entry, and the header disclosed that only for `tools/`. There were **7**
entries it did not produce: five `tests/src/core` test sources (three registered by `a42c68f24`,
two by `117068e76`) plus `tools/local-ci.sh` plus `tools/stem-export-demo.py`. The last one was a
second hand-added `tools/` file, contradicting the file's own header and the "one file, one home"
rule `tools-sources.txt` established. Its order was also not reproducible (~10 misplaced entries).

**What changed** (`tests/fork-sources.txt`, `tests/tools-sources.txt`, ledger commit):
- `tools/stem-export-demo.py` **moved to `tests/tools-sources.txt`** (12 entries there now), its
  correct home: 267 lines, no function above CCN 9, so the tools scope stays green (verified);
- the regeneration command now literally reproduces the file: pathspec
  `src include plugins tests/src/core tools/local-ci.sh`, an `awk` allow-list that admits exactly
  the five registered test sources (with the reason each is in this scope rather than
  `all-sources.txt` written out, including the rule for the next one), a second explicit pathspec
  line for `tools/local-ci.sh`, `LC_ALL=C sort`, and a verify step that prints `REPRODUCES`;
- the `.sh` extension was *not* added to the product command's extension filter: doing so also
  admits five fork-authored harness scripts under `plugins/RnnoiseDenoiser/testdata/`, which would
  widen the fork scope onto shell tooling. That narrowing decision is written into the header;
- `tools-sources.txt` gained a collation-pinned verify step (with the six paths whose home is
  another manifest subtracted explicitly) and its superseded "ADDED AT INTEGRATION" paragraph
  rewritten, since Gate 6 now classifies registered tooling itself.

Entry-set delta: fork **+0 / −1**, tools **+1 / −0**. Both regeneration checks print
`REPRODUCES`/`REPRODUCES-FORK`/`REPRODUCES-TOOLS`.

## D-6 — the exempt file did not exist, and its absence was a silent pass

**What was wrong.** `tests/file-length-exempt.txt` is named by `QA-GATES.md:590` (the line in the base tree; it is
`:749` after this pass) and `file-length-gate.sh:14,36` but was absent, and `is_exempt()` returned 1 when the file was missing —
so the documented fail-closed input was not merely absent, its **absence was tolerated**.

**What changed.**
- The file exists, with its contract written out (format, when an exemption is the right instrument,
  and why hand-written growth must go through a recorded re-anchor instead).
- The gate now **requires** it: missing → `error: … is missing …` and **EXIT=2**. Proven both ways:
  with the file moved aside, `--check` exits **0 before the change** (the defect, demonstrated) and
  **2 after**; with the file in place, fork/tools/all all exit 0.
- A blank reason still exits 2 (`exempt error: tests/file-length-exempt.txt entry 'x' has no reason`),
  and an exempt file leaving a baseline is now reported as exempt rather than as "no longer a fork
  source". The mechanism was exercised with a scratch entry: the scratch exemption was honoured
  (measured count 130 → 129) and then reverted.
- **Reconciliation with the mixer-concurrency lane**, which created its own copy of this file to
  exempt its 677-line `MixerConcurrencyTest.cpp`: that filing is compatible with this one — the
  contract is the same and their line is the first entry. When the two meet, the union is this
  header plus their entry line; nothing in their reason contradicts this header's rules. Their
  entry lands with its own recorded reason in their commit, which is what this file requires.
- **Not done, same class, deliberately out of scope:** `tests/coverage-gate.sh:146` also tolerates a
  missing `coverage-entry-floor-exempt.txt` (`if os.path.exists(...)`). That is the identical
  fail-open shape in Gate 2, which is not one of the gates in this brief. Reported, not fixed.

## D-7 — Gate 6's summary line mislabelled its count

**What was wrong.** The gate printed "`(88 file(s) in the ledger)`" while the ledger held 112
entries. Both numbers are real: 88 was the count of changed **paths** that run classified as
declared; the ledger holds one entry per declared path whether or not it changed since the gate base
(deleted paths and rename sources keep theirs).

**What changed** (`tests/no-upstream-regression-gate.sh:93-108`): the summary now prints both,
labelled — `(88 changed path(s) declared; the ledger holds 112 entries)`, with the noun pluralised
and a comment explaining why the two differ. Numbers unchanged.

## D-9 — a registration that could never be checked

**What was wrong.** `modules/wasm/demo/gain_clip.c` was registered in `all-sources.txt` while lying
outside Gate 9's pathspec (`src include plugins tests tools`), so no run could validate the entry —
and the all scope never measured the file either: registered and unmeasured at the same time.

**What changed** (`tests/fork-sources-gate.sh:5-6,36-42`, and the scan pathspec at `:128`): `modules/` is part of the scan now
(1,146 tracked sources scanned, 0 unregistered, 0 stale), which makes the existing registration
checkable and gives any future module source a home. `modules/wasm/*.wat` are not sources by
extension and need no home. Additionally, `all-sources.txt` is now checked in the **other**
direction too (`list_stale "$ALL_FILE"`), the direction whose absence let the 20 D-4 entries stay
missing; that is a new capability of the gate, not a change of its pass criteria.

## Gate 6 — a `tools/` category, and seven ledger entries deleted

This was not in the D-list; it fell out of D-5 and is the honest completion of a compromise the repo
had already written down ("until Gate 6 learns the tools/ category the way Gates 4, 7 and 8 already
have", `tools-sources.txt`).

- `tests/no-upstream-regression-gate.sh:22-27,54-57,88-90`: a path registered in
  `tests/tools-sources.txt` is classified **fork tooling (allowed)**. `tools/` does not exist at the
  fork point, so nothing under it can be an inherited divergence — an inherited file (`src/`,
  `include/`, `plugins/`) still needs a ledger entry with a reason, and a blank reason still exits 2.
- **Seven ledger entries deleted** from `tests/upstream-modifications.txt` (ledger 119 → **112**
  entries): `tools/mmpz-git/{mmpz_git.py,demo_edits.py,demo_check.py,depth-demo.sh,render-recipe.sh,run-demo.sh,tests/test_mmpz_git.py}`.
  Each asserted a divergence of inherited code about a file upstream has never had. Verified: Gate 6
  exits 0 and classifies 8 paths as fork tooling.

## Make the tests honest: two test files that had never run

`tests/src/core/MixerRoutingBackwardCompatTest.cpp` and `tests/src/core/PhaseDSidechainTest.cpp`
existed (376 and 455 lines) with `QTEST_GUILESS_MAIN`, and were **not** in `tests/CMakeLists.txt`:
never compiled, never executed, while `docs/phase-f/CRITERIA-TO-EVIDENCE.md` cites them as evidence
("runs in ctest", "Totals: 5 passed, 0 failed", "16/16 including … MixerRoutingBackwardCompatTest,
PhaseDSidechainTest"). A test that does not run is not evidence.

Registered in `tests/CMakeLists.txt:19,30` and run by name:

- `tests/src/core/MixerRoutingBackwardCompatTest.cpp` — registered (`tests/CMakeLists.txt:19`),
  built and run by name from `build/tests`:
  `QT_QPA_PLATFORM=offscreen ./MixerRoutingBackwardCompatTest` → **EXIT=0**,
  `Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, 2079ms` (log:
  `tests/gate-hygiene-logs/after-orphan-MixerRoutingBackwardCompatTest.log`). Its documented
  evidence line in `docs/phase-f/CRITERIA-TO-EVIDENCE.md:140` ("Totals: 5 passed, 0 failed") is
  now true because the test actually runs. The suite total rises **36 → 37** tests
  (`tests/gate-hygiene-logs/after-ctest.log`: `100% tests passed, 0 tests failed out of 37`).
- `tests/src/core/PhaseDSidechainTest.cpp` — registered, and **it does not compile**, so the
  registration was reverted and the exclusion recorded in `tests/CMakeLists.txt` (a labelled
  comment, not silence) with the first error:

```
tests/src/core/PhaseDSidechainTest.cpp:103:62: error: 'PART_D_COMPRESSOR_LIBRARY' was not declared in this scope
    const QString compressor = QString::fromUtf8(PART_D_COMPRESSOR_LIBRARY);
```

  `PART_D_COMPRESSOR_LIBRARY` is referenced exactly once in the repo — in that line — and defined
  nowhere (no `CMakeLists.txt`, no header), so the test was written against a compile definition
  that was never wired; the other test TUs take their plugin from `LMMS_TEST_PLUGIN_DIR`, which
  `tests/CMakeLists.txt` defines for them. Per the brief ("do not fix the test to fit, report the
  defect") the one-line interface fix was **not** applied — the defect stands, named, with the
  failing compile in `tests/gate-hygiene-logs/after-reconfigure.log:199-207`. Until it compiles,
  `docs/phase-f/CRITERIA-TO-EVIDENCE.md:112,129,212-213` cites a test that cannot run as evidence;
  that claim is stale and is left to its own lane to correct.

## Make the tests honest: `test_mmpz_git.py` failed on a clean checkout instead of skipping

`tools/mmpz-git/tests/test_mmpz_git.py` is documented to run via
`python3 tools/mmpz-git/tests/test_mmpz_git.py`. On a tree with **no built binary** it reported
`Ran 44 tests in 155.0s` / `FAILED (failures=1, skipped=6)` / **EXIT=1**: five tests skipped for the
missing binary and one failed. The failure is
`PureAudioMaths::test_missing_renderer_is_an_error_not_a_crash` — it asserts that `audible-diff`
rejects two nonexistent paths with `no such file`, but with no renderer anywhere the tool refuses
earlier, with `no renderer found` (also exit 2), before it looks at the paths.

**What changed** (`tools/mmpz-git/tests/test_mmpz_git.py:780-806`): the test now skips, naming the
missing dependency, on `M.find_renderer()` — the tool's own resolution (`--renderer`,
`$MMPZ_GIT_RENDERER`, a local build, `$PATH`), not a hardcoded path — so the skip disappears exactly
when the assertion becomes reachable. The assertions are unchanged.

Measured with the binary absent (a mirror of the repo built from symlinks with no `build/`, so
`ROOT` resolves to a tree with no renderer): `Ran 44 tests` / **`OK (skipped=7)` / EXIT=0**, with the
skip reason printed (`skipped 'no renderer found (build/zene or build/lmms, $MMPZ_GIT_RENDERER,
--renderer, or zene/lmms on $PATH): audible-diff exits before its input check …'`).

With the binary **present** (`build/zene` in this worktree, so `find_renderer()` resolves it):
`Ran 44 tests in 125.0s` / **`OK (skipped=1)` / EXIT=0** — the remaining skip is
`scratch/qtsave2 not built`, which predates this change. Log:
`tests/gate-hygiene-logs/after-mmpz-unittest-renderer-present.log`. Both directions of the guard
are therefore measured, not asserted: absent dependency → skip with the reason printed; present
dependency → the assertions run and pass.

**The pytest-vs-unittest count difference is benign collection reporting, not a second defect.**
`unittest` reports `Ran 44 tests`; pytest reports `43 passed, 1 skipped, 206 subtests`, i.e. 44
collected either way (43 + 1). pytest additionally reports `subTest` counts as "subtests"; unittest
folds them into the parent test. Same tests, same skips — measured, not assumed.

## Ledger lines changed (for the parent to re-apply as a union at merge)

The three ledgers the merge lane is rewriting were kept in their own final commit. The exact
changes, so a conflicted merge can be resolved by re-applying them rather than by taking either side:

**`tests/fork-sources.txt`** — entry list: **removed** `tools/stem-export-demo.py`; the remaining 129
entries have identical content but the file is now in `LC_ALL=C` order, so ~15 lines moved position.
Header: the "Regenerate with" command block and the `tools/` disclosure paragraph (`:3-6`, `:23-25`
in the old file) were replaced by the two-part command, the five-test allow-list and the verify step.

**`tests/all-sources.txt`** — entry list: **+20 entries** (the Lua / MIDI-learn / Session View /
LUFS + loudness-report / script-package sources):

```
include/LoudnessReport.h    include/LufsMeter.h        include/MidiLearn.h
include/MidiLearnGui.h      include/ScriptApiVersion.h include/ScriptConsole.h
include/ScriptLuaQtTypes.h  include/ScriptPackage.h    include/SessionModel.h
src/core/LoudnessReport.cpp src/core/LufsMeter.cpp     src/core/MidiLearn.cpp
src/core/ScriptApiVersion.cpp src/core/ScriptCommandQueue.cpp src/core/ScriptConsole.cpp
src/core/ScriptPackage.cpp  src/core/SessionClip.cpp   src/core/SessionModel.cpp
src/core/SessionModelPrivate.h src/gui/MidiLearnGui.cpp
```

and nothing removed; ~100 further lines moved position (the `LC_ALL=C` re-sort). Header: the
"Regenerate with" block replaced.

**`tests/upstream-modifications.txt`** — **7 lines deleted**:
`tools/mmpz-git/{demo_check.py,demo_edits.py,depth-demo.sh,mmpz_git.py,render-recipe.sh,run-demo.sh,tests/test_mmpz_git.py}`
(119 → 112 entries); one header paragraph added stating that an entry must be an *inherited* file.
`include/PluginFactory.h`'s existing entry was **not** edited: the file was already declared as a
divergence and the extraction adds two private declarations to the same declared surface.

**To union a conflict:** take the union of the entry *lines* from both sides, then run the file's own
documented command and `LC_ALL=C sort`; the result is the file. `tests/tools-sources.txt` is not one
of the three collision ledgers but changed too: `tools/stem-export-demo.py` added (12 entries) and
its header's integration note rewritten.

## The extraction is proven, not assumed

`PluginFactory::discoverPlugins`'s whole job is plugin discovery, so the proof is a run, not a
reading. `tests/src/core/PluginScanCacheTest.cpp` (registered, and it drives the real
`PluginFactory`) passed in the final suite: `100% tests passed, 0 tests failed out of 37`
(`tests/gate-hygiene-logs/after-ctest.log`), covering cold scan discovers the real plugin module
(`pluginInfo("tripleoscillator")` non-null, library loaded), warm scan serves it from the cache
**without** `dlopen` (`!info.library->isLoaded()`), a changed file is re-scanned, a quarantined
plugin disappears from discovery, `LoadFailed` records carry the last error text, and a corrupt
cache degrades to a full scan that rewrites it. `tests/src/core/PluginScanCacheTest.cpp`,
`ScriptEngineTest` and `ScriptStabilisationTest` are the tests that load a real module from the
build tree, so discovery is exercised end to end, not stubbed.

## Limits, and what is NOT proven

- **Gate 2 (coverage) and Gate 5 (mutation) were not run** (no instrumented build; the mutation
  sweep is not in my run-all invocations). No claim is made about either.
- **CI was not run** and the CI configuration was not changed. The all-scope exit codes are local.
- The per-file re-anchor decisions are **recorded, not deeply reviewed**: each reason names the file,
  its measured value, the delta and the commit that last touched it, but I did not audit whether each
  of those 33 files *should* be as large or as branchy as it is. The ratchet now fails on any further
  growth, which is the property that was missing.
- `docs/CONVENTIONS.md` was **not** touched (the parent refreshes it centrally at freeze); several of
  its numbers therefore remain stale — see the audit's §5 (S-1). Neither were
  `docs/KNOWN-LIMITATIONS.md`, the release notes, or any other lane's `docs/` file.
- Nothing was pushed, no PR or issue was touched, and `origin`/`messmerd` remotes were not contacted.
- **`PdcMixerTest` aborted once, in teardown, under the parallel ctest run — reported, not
  smoothed over.** The second runner invocation (`run-all-gates.sh --no-mutation --whole-tree`,
  `tests/gate-hygiene-logs/after/run-all-gates-whole-tree.log:49,82-88`) ended with
  `97% tests passed, 1 tests failed out of 37`: `PdcMixerTest` printed all 11 of its results as
  PASS, then `Finished testing of PdcMixerTest`, then `Received signal 6 (SIGABRT)` inside
  `cleanupTestCase` (`cleanupTestCase function time: 501ms`). The preceding full run was clean
  (`after-ctest.log`: `100% tests passed, 0 tests failed out of 37`) and the run before that too
  (`after/run-all-gates.log`), so this is **1 abort in 3 suite runs**, and it does not reproduce
  alone: **4/4 isolated runs pass, `Totals: 11 passed, 0 failed` each**
  (`tests/gate-hygiene-logs/after-pdcmixertest-flake.log`). It is a teardown-time crash under
  parallel load, not an assertion failure, and it is **not** attributable to this pass — the only
  product code changed here is `src/core/PluginFactory.cpp`/`include/PluginFactory.h`, and every
  PdcMixerTest test passed before the abort. Untriaged and outstanding: it makes "ctest green" a
  claim that holds most of the time, and it should be chased on its own lane.
- Two further small gate changes came with the per-file work and are worth naming:
  `tests/complexity-gate.sh` now **reports** baseline keys whose function is no longer over target
  (the file-length gate already ratcheted down; this one silently kept dead entries, and a dead
  entry masks a future rise up to its recorded value). Proven with a control entry: adding
  `discoverPlugins` CCN 15 back produced
  `improved: lmms::PluginFactory::discoverPlugins@src/core/PluginFactory.cpp is no longer over CCN 10`
  while `--check` stayed exit 0. Pruned with it: `lmms::PluginFactory::discoverPlugins`
  (CCN 32 → 7, so its grandfathered 15 was dead weight) and `lmms::RemotePlugin::process`
  (also no longer over target); the all-scope baseline holds 274 keys against 275 over-target
  functions because two rows can share one key.
- One observation recorded rather than fixed: `complexity-gate.sh` prints "275 over-target
  function(s)" for the all scope while its baseline holds 274 keys, because the writer collapses
  same-name keys in a file to the worst value (`complexity-gate.sh:127-133`, deliberate and
  documented). The two numbers will differ by the number of such collisions.
