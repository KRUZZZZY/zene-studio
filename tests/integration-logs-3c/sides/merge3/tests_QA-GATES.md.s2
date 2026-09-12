# QA Gates

Executable quality gates for **`zene-studio`** — the derived product repo —
ported from the LMMS standards fork on 2026-09-09. Every gate is a script under
this directory; nothing in this document is aspirational — if it is not checked
by a script, it is not a gate.

Scope: the sources this product adds on top of upstream master `4e677cb6c6ab`,
listed in `tests/fork-sources.txt` — **129 files**, the count the file held on 2026-09-12
(`post-alpha/gate-hygiene`). It was 103 on 2026-09-11 and has grown with almost every merged
lane since; every gate prints the number of sources it measured, and the manifest's own header
carries the command that regenerates it and the five `tests/src/core` entries it registers by
name, so this paragraph's figure is a dated measurement rather than an invariant.

**Whole-tree scope (added 2026-09-11).** Gates 4, 7 and 8 also accept `--scope all`, which points
them at `tests/all-sources.txt` — **1,133 first-party files** on 2026-09-12 (upstream-inherited
code plus the fork's own). Their baselines are separate files (`tests/*-baseline-all.tsv`) so the
two ratchets cannot shadow each other, and `run-all-gates.sh --whole-tree` runs the set. Measured
whole-tree state on 2026-09-12, after the reconciliation recorded under "Scope policy" below:
**9,462 functions with 275 over CCN 10; 112 files over 500 lines; 1.21% duplicated lines**. The
whole-tree coverage ratchet lives in
[`docs/CONVENTIONS.md`](https://github.com/KRUZZZZY/zene-studio/blob/main/docs/CONVENTIONS.md) and
was **not** re-measured here (it needs an instrumented build). Everything in *this* document below
describes the 129-file fork scope unless it says otherwise. Vendored third-party trees are
excluded on purpose — 524 files under `src/3rdparty` (lua, luabridge),
`plugins/NeuralAmp/rtneural`, `plugins/NeuralAmp/nam`, `plugins/NeuralAmp/tests`
and `plugins/RnnoiseDenoiser/rnnoise`; they are not this repo's code and these
gates never refactor them. Upstream LMMS code is never refactored by these gates;
its coverage is intentionally excluded from the reports.

**Tooling scope (added 2026-09-11).** Gates 4, 7 and 8 also accept `--scope tools`, which points
them at `tests/tools-sources.txt` — the fork's own developer tooling under `tools/` — with its own
baselines (`tests/*-baseline-tools.tsv`), so the three ratchets cannot shadow each other. `tools/`
does not exist upstream (`git ls-tree -r --name-only 4e677cb6c6ab -- tools` is empty), so every file
under it is fork-authored by construction. Before this scope existed such a file had no honest home:
`tests/fork-sources.txt` is the C/C++ **product** scope, and registering tooling there widens the
product ratchets onto plain Python — measured, `tools/mmpz-git/mmpz_git.py` is 818 lines with
functions reaching CCN 83 — while `tests/upstream-modifications.txt` is the upstream-**divergence**
ledger, where a fork-authored file is a false statement about it. Gate 9 now scans `tools/` and
reports a tooling file that is in no scope list. `run-all-gates.sh` runs the tools ratchets in the
same gate rows as the product ones (a red tools scope is a red gate 4/7/8), and the CI `static-gates`
job runs them as their own steps. Measured entry state at creation — **SUPERSEDED**, the
paragraphs below carry the current figures; this one is kept because it is the state the scope was
created from: **8 tooling files, 105 functions with 8 over CCN 10** (grandfathered in
`tests/complexity-baseline-tools.tsv`), **1 file over 500 lines** (818, grandfathered in
`tests/file-length-baseline-tools.tsv`), **0.00% duplicated lines**. Two limits,
stated: the two `.sh` entries are counted by Gates 4 and 7 only (jscpd in the version wired here has
no shell format), and tooling is outside coverage and mutation by construction (lcov instruments
C/C++ builds; the mutation harness targets one C++ TU).

**Re-anchored at integration (2026-09-12, `post-alpha/integration`).** The tools scope and its two
baselines were created by `post-alpha/pipeline-hardening` from the tooling as it stood on that
lane's branch — `tools/mmpz-git/mmpz_git.py` at **818** lines. Three files the fork added after that
measurement (`tools/mmpz-git/demo_check.py`, `tools/mmpz-git/depth-demo.sh`,
`tools/mmpz-git/render-recipe.sh`) were registered in `tests/tools-sources.txt` at integration, which
made the scope **11 tooling files**; the same merge pulled in the already-landed
`post-alpha/mmpz-git-depth` lane, which had deepened `mmpz_git.py` to **1896** lines and added
`tools/mmpz-git/tests/test_mmpz_git.py` (**855**). Both tools ratchets therefore failed on the merged
tree for reasons that predate it, and both baselines were refreshed through the gate's own documented
mechanism — never by hand, never by trimming code (`docs/CONVENTIONS.md` rule 4):

```
bash tests/file-length-gate.sh --reanchor "<reason>" --scope tools   # EXIT=0
  RE-ANCHORED: 2 file(s) over 500 lines: mmpz_git.py 1896 (was 818), test_mmpz_git.py 855 (new)
bash tests/complexity-gate.sh  --reanchor "<reason>" --scope tools   # EXIT=0
  RE-ANCHORED: 13 over-target function(s), incl. main@demo_check.py CCN 20
```

The recorded reason on both: *post-alpha/mmpz-git-depth (already merged) deepened
tools/mmpz-git/mmpz_git.py (818 → 1896 lines) and added tools/mmpz-git/tests/test_mmpz_git.py and
tools/mmpz-git/{demo_check.py,depth-demo.sh,render-recipe.sh}, which the integration merge registers
in tests/tools-sources.txt; the tools scope and its baselines were created by
post-alpha/pipeline-hardening from a pre-depth measurement of that tool, so this re-anchor records
the merged tree's own measurements for the tools scope only. No product source is measured by this
scope, no code was trimmed to satisfy the metric (docs/CONVENTIONS.md rule 4), and the fork/all
scopes keep their separate, untouched baselines.*

Two facts worth keeping: the re-anchor is **not** a widening of any product ratchet — `fork` and
`all` keep their own baselines and their own measured numbers, and any *other* tooling file over 500
lines or over CCN 10 still fails. And the previous top entry, `merge_elem` at CCN 53, is absent from
the refreshed baseline because `post-alpha/mmpz-git-depth` split that function: it now measures
**CCN 2**, so the ratchet correctly stopped grandfathering it. After the refresh,
`file-length-gate.sh --check --scope tools` → EXIT=0 and `complexity-gate.sh --check --scope tools`
→ EXIT=0, with all three tools-scope steps green in `run-all-gates.sh`.

**Re-anchored again (2026-09-12, `post-alpha/gate-hygiene`).** Two changes move this scope, and one
of them fixes a **false red** that made the suite fail for everyone without a built binary:

- `tools/stem-export-demo.py` moved **into** this scope from `tests/fork-sources.txt`. Tooling has
  one home, and `tests/tools-sources.txt` is the home its own header declares; `fork-sources.txt`
  was carrying the demo as a second hand-added `tools/` entry, which contradicted that file's own
  header and the one-file-one-home rule. The move also needed a Gate 6 fix: a `tools/` path
  registered here is now classified as fork tooling, so its (untrue) upstream-divergence ledger
  entry could be deleted rather than replaced — see the Gate 6 section.
- `tools/mmpz-git/tests/test_mmpz_git.py`: on a clean checkout
  `PureAudioMaths::test_missing_renderer_is_an_error_not_a_crash` **FAILED** (independent audit
  reproduction: `Ran 44 tests in 155.0s` / `FAILED (failures=1, skipped=6)` / `EXIT=1`) because
  `audible-diff` refuses with "no renderer found" (exit 2) *before* it reaches the "no such file"
  input check the test asserts. Five tests in the file skipped on the missing binary and one
  failed, which is the worst of both. It now **skips, naming the missing dependency**, on
  `mmpz_git.find_renderer()` — the tool's own resolution, not a hardcoded path — and the
  assertions are unchanged and still run when a renderer exists. Measured both ways:
  no renderer → `OK (skipped=7)`, EXIT=0; renderer present → `OK`, EXIT=0. The guard and its
  docstring take the file from **855 to 877** lines:

```
bash tests/file-length-gate.sh --scope tools --reanchor "<reason>"   # EXIT=0
  RE-ANCHORED: 2 file(s) over 500 lines (mmpz_git.py 1896, test_mmpz_git.py 877)
```

The recorded reason names the file, both sizes, the failing test and the audit's reproduction.
Current tools scope, measured 2026-09-12: **12 tooling files, 201 functions with 13 over CCN 10**
(grandfathered), **2 files over 500 lines** (1896, 877 — grandfathered), **0.00% duplicated lines**.

## Scope policy: what runs by default, and what does not — 2026-09-12

Three scopes exist, each with its own manifest, its own baselines and its own measured numbers.
Only two of them run by default, and that gap is the defect this section exists to state:

| scope | manifest | who runs it | status on 2026-09-12 |
|---|---|---|---|
| **fork** (default) | `tests/fork-sources.txt` (129) | `run-all-gates.sh`, CI `static-gates`, every gate's bare invocation | **GREEN** — and this is the **release gate**, per `docs/CONVENTIONS.md` |
| **tools** (default) | `tests/tools-sources.txt` (12) | `run-all-gates.sh` (same gate rows), CI `static-gates` (own steps) | **GREEN** |
| **whole tree** (advisory) | `tests/all-sources.txt` (1,133) | **nothing by default** — `run-all-gates.sh --whole-tree`, or `--scope all` by hand | **GREEN** as of 2026-09-12; was RED and unreported until then |

**The whole-tree scope was red from the post-alpha merges until 2026-09-12, and no default runner,
CI job or merge record said so.** At `87b9a5397` it stood at 23 file-length regressions (5 new files
over 500, including `src/core/CrashReporter.cpp` 526, `include/Song.h` 539 and
`src/core/PluginFactory.cpp` 580) and 13 complexity regressions (the worst being
`lmms::PluginFactory::discoverPlugins` CCN 15 → 32). `run-all-gates.sh` is fork-scoped unless
`--whole-tree`, CI's `static-gates` job runs the fork scope plus the tools scope, and
`docs/INTEGRATION-MERGES.md` published the fork-scope PASS without mentioning the other one.

**Why the whole tree is not the release gate, stated rather than implied.** The fork scope is the
scope this product's own code is judged in: it is what CI enforces on every push, what
`run-all-gates.sh` runs, and what `docs/CONVENTIONS.md` names as the product ratchet. The whole-tree
scope exists to stop *unnoticed* growth in code the fork inherits and edits — it is a monitoring
ratchet over upstream LMMS plus the fork's own sources, and most of what it measures is upstream
code this repo deliberately does not refactor. A red whole-tree scope is therefore a signal to
review and record, not a release blocker by itself; a red **fork** scope is a blocker. What was
wrong before 2026-09-12 was not the priority order, it was that the second signal was **invisible**:
a scope nobody runs and nobody reports is a scope that cannot fail.

What was done about the red whole-tree scope, in the order it happened:

1. **The genuine code regression was fixed by extraction, not by a baseline.**
   `PluginFactory::discoverPlugins` (222 lines, CCN 32) was split into named stages —
   `candidatePluginFiles()`, `dropQuarantinedPlugins()`, `planPluginScan()` + `planForCandidate()`,
   `preloadPluginLibraries()`, `scanOnePlugin()`, `loadPluginDescriptor()`, `appendLoadedPlugin()`,
   `appendCacheServedPlugin()`, `addSupportedFileTypes()` — with the load path's three outcomes
   (did not load / loaded but no descriptor / descriptor resolved) carried explicitly instead of by
   fallthrough. `discoverPlugins` is now **CCN 7**, `NLOC 40`, and nothing in the file exceeds the
   target. Behaviour is held by `tests/src/core/PluginScanCacheTest.cpp` (cold scan discovers the
   real module, warm scan serves it from the cache without `dlopen`, a changed file is re-scanned,
   a quarantined plugin leaves discovery, a corrupt cache degrades to a full scan); the run is in
   `docs/GATE-HYGIENE.md`.
2. **Every remaining regression got its own decision, one file at a time — never a scope-wide
   re-anchor.** `--reanchor "reason"` rewrites the *whole* baseline, so using it for the merged tree
   would have grandfathered 23 file-length entries and 12 complexity entries **unreviewed**, which
   is a real weakening of the ratchet even though it is done through the documented mechanism. A
   sibling lane reached the same conclusion from the other side and refused to move its 511-line
   `SampleClipWindowTest.cpp` into this scope for exactly that reason. So the gates gained a
   **single-file** mode:

```sh
bash tests/file-length-gate.sh --scope all --reanchor-file <path> "<reason>"   # one entry
bash tests/complexity-gate.sh  --scope all --reanchor-file <path> "<reason>"   # one file's functions
```

   It carries every other baseline entry over unchanged, prints each key with its old and new value,
   refuses a blank reason (exit 2) and refuses a path that is not over the limit or not in the
   scope's manifest (exit 2). One invocation per file, each with a reason naming the file, its
   measured size (or CCN), the delta, the commit that last touched it, and why the growth is
   legitimate: 23 file-length entries and 10 complexity paths, all printed and committed under
   `tests/gate-hygiene-logs/reanchor/per-file-all.log`. The one file that grew *because of this
   pass* is `src/core/PluginFactory.cpp` (580 → 688, the extraction in step 1) — recorded as its own
   entry rather than paid for by trimming code (`docs/CONVENTIONS.md` rule 4).
3. **The gap is now visible where it hid.** Every `run-all-gates.sh` run prints a scope line saying
   which scopes it measured and that a default run does not measure the whole-tree scope; the gate
   banners no longer print a stale file count; and this section carries the policy: the all scope is
   refreshed **per file, with a printed reason**, and must be run with `--whole-tree` before a
   freeze.

Deliberately **not** done: the whole-tree scope is still absent from CI's `static-gates` job.
Enforcing it is an owner decision about runner minutes (that job is the only one that runs on every
push) and about whether the integration branch's CI may go red on the next merge before a freeze.
The local exit codes for all three scopes are recorded instead, in `docs/GATE-HYGIENE.md`.

The **standards fork** numbers below are inherited from the fork's run (kept for
provenance); the **product** numbers are this repo's own measured run. The
product's first run was captured 2026-09-09 on the port commit.

## Gate 1: Unit tests (ctest)

**Command** (headless machines require the offscreen Qt platform; ctest must
run from `<build>/tests` — the top-level build dir reports 0 tests):

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON
cmake --build build -j4
cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

**Pass criterion**: 100% of tests pass, exit code 0.

**Measured (2026-09-12, `post-alpha/gate-hygiene`):** `100% tests passed, 0 tests failed out of 37`
(`tests/gate-hygiene-logs/after-ctest.log`), up from 36 — the extra test is
`MixerRoutingBackwardCompatTest`, see below.

**Two test files that had never run (found 2026-09-12).** `tests/src/core/MixerRoutingBackwardCompatTest.cpp`
and `tests/src/core/PhaseDSidechainTest.cpp` are QTest suites with `QTEST_GUILESS_MAIN` that were in
**neither** `tests/CMakeLists.txt` nor any other build list: never compiled, never executed, while
`docs/phase-f/CRITERIA-TO-EVIDENCE.md` cited both as evidence. A test that does not run is not
evidence, and "0 tests is a pass" is already this repo's stated error.

- `MixerRoutingBackwardCompatTest.cpp` — **registered** (`tests/CMakeLists.txt:19`), built, and run
  by name: `QT_QPA_PLATFORM=offscreen ./MixerRoutingBackwardCompatTest` → EXIT=0,
  `Totals: 5 passed, 0 failed, 0 skipped`, which is what its documentation claims.
- `PhaseDSidechainTest.cpp` — **does not compile**, so it is **not** registered and the exclusion is
  recorded in `tests/CMakeLists.txt` with the error and its cause:
  `error: 'PART_D_COMPRESSOR_LIBRARY' was not declared in this scope` at line 103 — the macro is
  referenced exactly once in the repo and defined nowhere. The fix is a one-line interface change to
  take the built plugin from `LMMS_TEST_PLUGIN_DIR` as the other test TUs do; it was **not** applied,
  because a test must not be bent until it passes. Register the file when it compiles.

**Scope**: `tests/` — Qt test binaries plus the migration harness.
**Current baseline (product): 26/26 passing** — Debug, `WANT_QT6=ON`,
`WANT_STEM_SPLIT=ON`, `WANT_WASM=ON`, 37.85 s. The six test files the product
was missing were ported from the standards fork on 2026-09-09
(`AudioPortsModelTest`, `MultiTrackRecorderTest`, `PluginAudioPortsTest`,
`RemotePluginAudioPortsTest`, `ScriptBindingsTest`, `AudioPluginTest` with
`SyntheticAudioPlugin`), and two real product defects they caught were fixed
(both recorded in the Gate 5 note). The standards-fork baseline was 17/17.

## Gate 2: Coverage ratchet (`coverage-gate.sh`)

**Command**:

```sh
bash tests/run-coverage.sh build-coverage
bash tests/coverage-gate.sh build-coverage/coverage/coverage-fork.info
```

`run-coverage.sh` configures `build-coverage` with `-DWANT_COVERAGE=ON`
(Debug + `--coverage` on test targets), builds capped at `-j4`, runs the full
ctest suite (stopping on any red test), captures with lcov (system paths,
Qt and 3rd-party code excluded), filters to the fork's own sources, prints a
per-directory summary, and writes `build-coverage/coverage/coverage-fork.info`
plus a `genhtml` report under `build-coverage/coverage/html/`.

`coverage-gate.sh` implements the ratchet against `tests/coverage-baseline.tsv`
(one repo-relative path per line, with that file's measured line coverage):

- a file whose coverage drops by more than `COVERAGE_TOLERANCE` (default 0.05
  percentage points) **fails** the gate (exit 1);
- a file whose coverage rises updates the baseline (ratchet up);
- **a NEW fork source enters the baseline only at or above the entry floor**
  (`COVERAGE_ENTRY_FLOOR`, default 50.00 percentage points). Below the floor the
  gate **fails** (exit 1) and names the file. The floor is an *entry* rule: files
  already in the baseline are grandfathered, exactly as in Gates 4 and 7;
- a deliberate exception to the floor must be declared **with a reason** in
  `tests/coverage-entry-floor-exempt.txt` (`<path><TAB><reason>`); a blank reason
  exits 2 rather than being honoured, like the Gate 6 ledger;
- **a file with zero instrumented lines is not measurable** and is recorded as
  `n/a` in the baseline and reported as `unmeasurable` — never as `100.00%`. The
  old behaviour banked such a file at 100% permanently, a claim the tracefile did
  not support (fixed 2026-09-11, defect recorded in `docs/STATUS.md`);
- a file recorded `n/a` that later becomes measurable is treated as a new entry,
  so the floor applies to it too; a previously measured file that stops reporting
  instrumented lines is reported as `unmeasured` (loudly, not fatally — it is a
  measurement loss, not a coverage drop);
- **a file that still exists but produced no tracefile record was not compiled in
  this configuration**: it is reported as `unmeasured-by-config` and its baseline
  entry is **preserved**. It does *not* count as a removed source (fixed
  2026-09-12: the old rule deleted eight entries that only `WANT_STEM_SPLIT=OFF`
  kept out of the build, and a write-mode run would have made them re-enter as
  new files and be refused by the floor when the feature came back);
- only a removed source — no record **and** the path is gone — drops out of the
  baseline.

**Per-entry rows carry the instrumented-line count and a content fingerprint**
(2026-09-12), because the reconstructed comparison `round(pct * LF_now)` is only
like-for-like while LF is stable, and LF is a *compile-time* quantity while LH is
a run-time one. A header of templates gains instrumented lines whenever any
including TU is added, with no edit to the header at all: `include/AudioPorts.h`
is byte-identical to the commit the baseline was taken at, yet the old arithmetic
reported 81 of its 233 lines lost. The row is now
`<path><TAB><pct|n/a><TAB><lf|-><TAB><sha256-16|->` and the comparison is chosen
by what moved: LF unchanged → the original hit-line ratchet; LF moved with the
bytes unchanged → judged on covered lines and reported as `denominator-moved`;
LF moved with the bytes changed for a **header** → both causes are present and
inseparable, so the entry is `REANCHOR-REQUIRED` (exit 1) until a reason is
recorded; LF moved with the bytes changed for a **source** → the original strict
rule, a REGRESSION; LF unknown (a legacy row) → the original arithmetic, named as
legacy. Covered lines are held in every branch.

A single entry is reconciled with
`coverage-gate.sh <tracefile> --reanchor-file <path> "reason"` (repeatable): it
moves only that entry, exits 2 on a blank reason, and exits 2 when the named path
is not failing — a re-anchor is a decision with a reason, not a silencer.

The gate also prints its own scope accounting (`scope: N entries in
tests/fork-sources.txt; M produced a record; K did not`) so the headline cannot be
read as a claim about the whole manifest. `tests/coverage-green/classify-scope.py`
names the reason per entry.

`--check` runs in CI mode: report only, baseline never written.

**Measured (2026-09-11):** the 2026-09-09 product baseline (47 measured files,
76.07%) still passes unchanged under the new rules — this is an entry floor, not
a retroactive one, so no re-anchor was needed or made.

**Measured (2026-09-12, `post-alpha/coverage-green`):** Gate 2 is **exit 0**.
The headline is **84.34% (4523/5363) over the 67 of the 139 fork-scope entries
that produced a record** — 72 entries produce no record in this configuration (29
feature-gated sources, 41 headers no compiled TU instantiates, 2 non-source).
Three entries were reconciled with recorded reasons (`include/AudioPorts.h`,
`include/RemotePluginAudioPorts.h`: pure denominator moves, proven by the
byte-identical files and a covered-lines bound; `include/AudioPlugin.h`: mostly
denominator with a named residual of ≤8 lines no test drives). The migration
added 28 entries, changed 5 percentages, and **dropped none**. Evidence and the
21-control harness: `docs/COVERAGE-GATE-GREEN.md`,
`tests/coverage-green/gate-controls.sh`.

**Measured numbers — standards fork** (2026-09-09, gcc 13 / lcov 2.0, after the coverage push):

| Directory            | Lines | Hit  | Rate   |
|----------------------|-------|------|--------|
| `src/core` (fork)    | 1730  | 1660 | 95.95% |
| `src/core/audio`     | 88    | 82   | 93.18% |
| `include` (fork)     | 474   | 441  | 93.04% |
| `src/gui` (fork)     | 269   | 0    | 0.00%  |
| **Total fork code**  | 2561  | 2183 | **85.24%** |

The headline is line-weighted (hit lines / instrumented lines). **Correction:**
until 2026-09-09 `coverage-gate.sh` printed an unweighted mean of per-file
percentages (77.14% on this same run), which let a 2-line 0% file weigh as much
as a 269-line one and disagreed with this table's own definition. The script now
prints the weighted figure and both numbers in the pair `(hit/total lines)`.

Per-module highlights: `RoutingGraph.cpp` 100%, `RoutingNode.cpp` 100%,
`AudioBus.h` 100%, `AudioPortsModel.h` 100%, `AudioPortsModel.cpp` 99.22%,
`ScriptBindings.cpp` 97.65%, `RoutingNodes.cpp` 96.10%, `AudioBus.cpp` 95.77%,
`AudioPorts.h` 91.43%, `ScriptEngine.cpp` 89.36%, `ScriptBindings.h` 87.18%.
Fourteen fork files are at **100%**: `RoutingGraph.cpp`, `RoutingNode.cpp`,
`MultiTrackRecorder.cpp`, `RoutingGraph.h`, `RoutingNode.h`, `RoutingNodes.h`,
`AudioBus.h`, `AudioPortsModel.h`, `PluginAudioPorts.h`,
`RemotePluginAudioPorts.h`, `MultiTrackRecorder.h`, `RecordRingBuffer.h`,
`ScriptEngine.h`, `TrackRecorder.h`.

Journey: **63.0%** (gate introduction) -> **66.17%** (pre-push baseline) ->
**82.75%** -> **85.24%** (2026-09-09). Every step is held by the ratchet.

**Product run — `zene-studio`, first measured 2026-09-09**: **76.07%
(2661/3498 lines)** over **47 measured files** of the 97 in scope.

| Directory                 | Lines | Hit  | Rate   |
|---------------------------|-------|------|--------|
| `src/core` (fork)         | 2420  | 2211 | 91.36% |
| `include` (fork)          | 493   | 450  | 91.28% |
| `src/gui` (fork)          | 370   | 0    | 0.00%  |
| `plugins/NeuralAmp`       | 158   | 0    | 0.00%  |
| `plugins/RnnoiseDenoiser` | 57    | 0    | 0.00%  |
| **Total measured**        | 3498  | 2661 | **76.07%** |

Product journey: **57.59%** (20-test baseline, port commit) -> **73.91%** (six
missing test files ported) -> **76.07%** (RoutingGraphTest / AudioBusTest synced
from the standards fork). Still short of the 85% aspiration, and the gap is
structural rather than untested paths: `src/gui/PinConnector.cpp` (269 lines,
a QWidget whose behaviour is the GUI event loop) and the two unbuilt plugin
hosts account for all of it.

**50 of the 97 files in the 2026-09-09 scope have no coverage records at all** in this
configuration and are therefore outside the denominator above:
`plugins/Vst3Effect` (15, VST3 SDK absent), `plugins/ClapEffect` (15, CLAP
headers absent — see below), `src/wasm` plus its `include` counterparts (13),
`plugins/WasmEffect` (6, wasmtime absent), and
`plugins/RnnoiseDenoiser/testdata/rnn_harness.c` (1, not built). Enabling CLAP
is one `git clone` away (pinned `195b42a0`), but doing so **fails to compile**:
`include/AudioPlugin.h:315` static_asserts that the legacy single-buffer
interface can only be bridged to in-place, interleaved effects, and
`ClapEffect.cpp` declares a non-in-place port set. That is a real product
defect, invisible while the headers are absent — filed, not hidden.

Still at 0%: `src/gui/PinConnector.cpp` (269 lines), plus three headers with no
line reached (`LmmsPolyfill.h` 2, `PinConnector.h` 5, `RemotePluginAudioPorts.h`
6). `PinConnector` is a `QWidget` whose behaviour is the GUI event loop and the
`AudioPortsModel` it renders (`paintEvent` at `PinConnector.cpp:184`); the
unit-test binaries run offscreen with no event-loop interaction. This is an
**evidenced exclusion, not an oversight**: no file under `tests/src/` references
`PinConnector` (the only hits in `tests/` are the gate metadata files
`fork-sources.txt`, `QA-GATES.md` and the three baselines).

Gate 2 **met on the standards fork**: the ratchet is live and green there at
**85.24%**, clearing the adopted ruleset's 85% aspiration. **The product's own
measured figure is 76.07%** (2661/3498 lines, 47 of the 97 files in the 2026-09-09
scope), still short of that aspiration for the structural reasons above.

**Scope note (2026-09-11).** `tests/fork-sources.txt` grew from 97 to 99 files when
`include/LatencyCompensation.h` and `src/core/LatencyCompensation.cpp` were added: #605 PDC
had landed (2026-09-10) without appearing in the scope file, so no gate — coverage included —
was watching it. No coverage run has been taken on the new scope, so those two files have no
coverage record at all and are absent from the figures above. The ratchet will admit them at
their measured coverage on the next `run-coverage.sh`.
**Corrected 2026-09-11 (later the same day):** this paragraph used to end "Gate 2 has no entry
floor, so a new file **cannot** fail on entry — that is one of this suite's open defects,
recorded as such." The defect is fixed: a new file below `COVERAGE_ENTRY_FLOOR` (50.00%) now
fails the gate, and one with zero instrumented lines is recorded `n/a` instead of `100.00%`
(see the rules above and `docs/VERIFICATION-DEBT-FIXES.md`). Gate 9 now catches the sibling
failure mode — a new source file that is in *no* scope list at all.
**Gate 2 is not green on the product:** the measured 76.07% is below the adopted 85%
aspiration, and the criteria above only test that coverage does not *fall*. `AudioBus.cpp`
(95.77%) and `ScriptEngine.cpp` (89.36%) remain short of 100%;
`PinConnector.cpp` stays excluded for the reason above.
The only other 0% lines in scope are `LmmsPolyfill.h` (2 lines) and
`PinConnector.h` (5 lines), both header-only GUI/polyfill declarations.

Residual uncovered lines outside the exclusions (95 in total, all in files already
above the aspiration): `ScriptEngine.cpp` 45, `ScriptBindings.cpp` 14,
`AudioPorts.h` 12, `AudioPlugin.h` 9 (the `AudioPluginExt` GUI / instrument-scanner
paths, not exercisable headlessly), `AudioBus.cpp` 6, `TrackRecorder.cpp` 6,
`ScriptBindings.h` 5, `RoutingNodes.cpp` 3, `AudioPortsModel.cpp` 2. Recorded for
the next increment; none of them block the gate.

## Gate 3: No tautological tests (`no-tautology-gate.sh`) — WIRED 2026-09-09

**Command**: `bash tests/no-tautology-gate.sh` (add `--strict` to also require at
least one assertion per test slot).

This was review-only, which contradicted this document's own first line ("if it is
not checked by a script, it is not a gate"). It is now mechanical. For every QTest
class registered in `tests/CMakeLists.txt`:

1. it must declare at least one test slot (a function under `private slots:`);
2. it must contain at least one real assertion macro
   (`QVERIFY`/`QVERIFY2`/`QCOMPARE`/`QEXPECT_FAIL`/`QTRY_*`);
3. it must not contain a literal tautology (`QVERIFY(true|false|1|0)`,
   `QVERIFY2(true|false|1|0, ...)`).

Helper executables that are registered but are not QTest classes
(`TwoTrackAlsaCaptureProbe.cpp`, `TwoTrackRecordingHarness.cpp`,
`PluginPortsMigrationReference.cpp`) are listed by the script as explicitly
out of scope rather than silently skipped.

**Measured (2026-09-12, `post-alpha/gate-hygiene`):** 31 registered QTest files, **all PASS** — 0
tautologies (2,689 slots, 2,193 assertions). Earlier measurements, kept in order: 2026-09-11 —
21 files, 1,744 / 1,477; 2026-09-09 — 20 files, 1,597 / 1,408. Coverage (Gate 2) is the corroborating signal:
test-unreachable code shows 0%.

## Gate 4: Per-method complexity (`complexity-gate.sh`) — WIRED 2026-09-09

Target: **cyclomatic complexity (CCN) <= 10** and **nesting depth (ND) <= 3** per method.
This gate is now mechanical. `cppcheck` 2.13 does **not** emit complexity for this
codebase (verified: zero matches with `--enable=all` / `--enable=style`), so the tool
is **lizard** (`pip install lizard`, reports CCN and ND for C/C++).

**Command**:

```sh
bash tests/complexity-gate.sh --reanchor-file <path> "reason"   # ONE file's entries only
bash tests/complexity-gate.sh            # ratchet: refresh baseline, fail on regressions
bash tests/complexity-gate.sh --check    # CI: never writes the baseline, still fails on regressions
bash tests/complexity-gate.sh --strict   # fail if ANY function is over the target
bash tests/complexity-gate.sh --reanchor "why"   # deliberate, recorded baseline refresh
bash tests/complexity-gate.sh --check --scope tools   # the fork's own tooling under tools/
```

**Policy — a ratchet, never a rewrite order** (matching Gate 6's no-refactor rule):
functions already over the target are grandfathered in `tests/complexity-baseline.tsv`
at their measured CCN; a **new** function over the target, or an existing one whose CCN
**rises**, fails the gate. Existing over-target functions are reported, not rewritten.

**Honest limitation, stated not hidden — corrected 2026-09-11:** earlier revisions of this
document said ND was *reported* alongside each over-target function and reviewed manually. It is
not reported: **lizard 1.24.0 never computes nesting depth.** `lizard.py:324` initialises
`max_nesting_depth = 0` and no code path increments it (`grep -nE 'nesting_depth\s*\+=' lizard.py`
→ no matches); the warning header lists only `cyclomatic_complexity / length / nloc /
parameter_count`; and a deliberately 6-deep nested probe reports CCN 7 with no ND field at all.
**The ruleset's nesting-depth rule (≤ 3) is therefore neither enforced nor measured by this
tooling** — an earlier reading of "ND 0 everywhere" in this file was reporting an uninitialised
field, not a measurement. Enforcing it needs a different tool, and until one is wired every ND
figure in this repo should be read as 0-by-omission.

**Measured (2026-09-11, gcc 13, the 99-file scope as it then was):** `complexity-gate.sh` scans
**807 functions**; **24 exceed CCN 10** (the number in this sentence is the pre-fix count; the
paragraph three below, on the same run, says 23 — the fixed gate reports 23, which is what
`complexity-baseline.tsv` holds). Two defects found in the gate itself were fixed the same day:

- **The baseline was keyed by function line span.** `complexity-baseline.tsv` stored
  `applyCommand@485-601`; the function's span had moved to `485-603`, so a two-line change
  orphaned its own baseline entry and it — and `resolveProjectPath` — re-reported as *new*
  functions. Keys are now `function@path`, with legacy line-span keys migrated on read. That
  removed both false regressions.
- **`--check` exited 0 unconditionally**, so the ratchet could not fail in CI. `--check` now means
  "never write the baseline" and still exits 1 on a regression.

**State after those fixes: gate 4 is GREEN.** The one genuine regression,
`LatencyCompensation::processPlanar` (CCN 11), was refactored in `d9d5deee2`: the wrap-around ring
read that `process()` and `processPlanar()` duplicated verbatim is now the private helper
`readWrapped()`, which takes the function to CCN 10. Verified by a real Debug build (exit 0) and
the full suite from `build/tests` (100% tests passed, 24/24, `PdcMixerTest` included). The gate
now scans **1,093 functions in the 129-file fork scope with 23 over target, all grandfathered**
(2026-09-12), and `--check` exits 0. The gate also **reports** a baseline key whose function is no
longer over target (`improved: … is no longer over CCN 10 (baseline entry is dead weight; remove it
deliberately)`) — it never pruned dead entries before, and a dead entry masks a future rise up to its
recorded value. Two such keys were removed from the all-scope baseline with the 2026-09-12 pass:
`lmms::PluginFactory::discoverPlugins` (CCN 32 → 7, so its grandfathered 15 was dead weight) and
`lmms::RemotePlugin::process`. For the all scope the same run measures **9,462 functions with
275 over target** and, after the 2026-09-12 re-anchor, exits 0 as well (see "Scope policy").

The figures this section previously carried (13 of 514 functions, highest CCN 27) were measured
on the standards fork's 42-file scope and no longer describe the product. The highest CCN in the
fork scope is **41** (`ExternalProcessStemSeparator::separate`, grandfathered in
`tests/complexity-baseline.tsv`), and in the whole tree **155** (`main` in `src/core/main.cpp`,
grandfathered in `tests/complexity-baseline-all.tsv` by the 2026-09-12 re-anchor; it was 136 before
the post-alpha merges and 141 at `a4fe66c4f` — see `docs/CONVENTIONS.md`); an earlier revision of this paragraph said 29, which was
`ScriptEngine::applyCommand`'s figure and does not describe the current tree. Baselines can only be moved deliberately now: `--reanchor "reason"` (an unrecorded re-anchor
is refused with exit 2).

## Gate 5: Mutation testing (`mutation-gate.sh`) — WIRED 2026-09-09

The ruleset target is **>= 80% kill score on core modules**. No packaged C++
mutation tool exists in this environment (verified: `apt-cache search --names-only
'mull|mutation'` returns nothing relevant; LLVM 18 is installed but `mull` is not
packaged; `mutmut` is absent), so the fork uses a **small, self-written harness
scoped to one translation unit** rather than pretending a tool exists.

**Command**:

```sh
bash tests/mutation-gate.sh                    # default: 30 mutants, seed 0
bash tests/mutation-gate.sh --max-mutants N    # smaller sweep
bash tests/mutation-gate.sh --seed S           # different deterministic sample
bash tests/mutation-gate.sh --all              # every generated candidate (~15-20 min)
bash tests/mutation-gate.sh --self-test-only   # prove INVALID/KILLED/SURVIVED (~30 s)
bash tests/mutation-gate.sh --list             # print the candidate pool, mutate nothing
```

**Scope — one TU, stated plainly**: `src/core/RoutingGraph.cpp` (364 lines), the only
fork-NEW core TU that is both 100% line-covered and has a dedicated test binary
(`build/tests/RoutingGraphTest`, 12 QTest functions, `Totals: 16 passed`). Whole-project mutation is deliberately not
attempted: a scoped, correct gate beats a broad, flaky one. Widening the scope means
editing the script's `SRC_REL` / `TEST_NAME` / `OBJ_REL` block — the pipeline is generic.

**What the script actually does** (per mutant, every step verified by execution, no
claim without proof):

1. generates candidates from the pristine file with an embedded Python scanner that
   **masks comments and string/char literals first**, so a mutation never lands inside
   a comment or a literal;
2. applies the mutation at an exact character position and **proves it landed**:
   sha256 of the written file against the in-memory expected content, plus
   `git status --porcelain` on the TU;
3. rebuilds only that TU and relinks the test binary, and **proves the rebuild
   happened**: the build log contains `Building CXX object ...RoutingGraph.cpp.o`,
   the object mtime advanced and the test binary mtime advanced;
4. runs the binary headless (`QT_QPA_PLATFORM=offscreen`, 10 s timeout): **KILLED**
   iff exit != 0 or crash/timeout, **SURVIVED** iff it still exits 0;
5. restores the pristine file and verifies the restore by sha256;
6. a **control** runs before (3 clean runs) and after (1 clean run) the sweep. A flaky
   control aborts with exit 2 and **no score** — a flaky harness produces no number
   rather than a wrong one.

Mutants that fail to compile are **INVALID**: counted, listed separately and excluded
from the score (they are not evidence of test strength).

**Score**: `killed / (killed + survived)`; exit 0 iff `>= --threshold` (default 80%),
1 if below, 2 if the harness itself cannot be trusted (build/control/restore failure).
Machine-readable outputs: `build/mutation-gate/{plan.tsv,results.tsv,summary.txt,logs/}`.

**Self-test (`--self-test-only`)**: proves the classifier is not blind. It applies a
known-lethal mutant (flip the channel-loop bound in `process()`) and requires KILLED, a
known-equivalent mutant (delete `plan.reserve(count);`) and requires SURVIVED, and a
deliberately uncompilable mutant and requires INVALID.

**Measured (2026-09-09, gcc 13, seed 0, 30 of 170 candidates): 27 killed, 3 survived,
0 invalid → kill score 27/30 = 90.0%** (threshold 80%).

**Product run (`zene-studio`, 2026-09-09, same seed/candidates): the first
attempt scored 43.3% — FAIL.** The cause was test drift, not weak code: the
product's `RoutingGraphTest.cpp` was 355 lines against the standards fork's
641, so mutants on paths the fork's later tests cover survived here. After
syncing the test file the product scores **27/30 = 90.0%** (threshold 80%),
matching the standards fork.

Two product defects the ported suite caught, both fixed on this branch — each
was invisible before, because the product carried no test that exercised it:

- `src/core/ScriptEngine.cpp` `SetMasterVolume` applied `command.f0`, but the
  enqueue side (`LuaSong::setMasterVolume`) fills `command.i0` — every script
  `setMasterVolume()` silently applied 0. Now reads `i0`, matching `SetTempo`.
- `src/core/AudioPortsModel.cpp` connected `AudioEngine::sampleRateChanged`
  with no context object, so a destroyed model's dangling `this` was invoked on
  the next emission (reproducible SIGSEGV). Now passes `this` as context — the
  fix the standards fork already had.

Survivors, each named with why it survives:

| site | mutation | why it survives |
|---|---|---|
| `RoutingGraph.cpp:44` | `GRAPH_VERSION 1 -> 0` | the version is written by `save()` but never read by `load()`; no public-API behaviour depends on it (observable only in the raw XML text) |
| `RoutingGraph.cpp:226` | delete `setAttribute("frames", ...)` | same class: `load()` ignores the `frames` attribute entirely, so the graph behaves identically (observable only in the raw XML text) |
| `RoutingGraph.cpp:53` | `++i -> --i` in the free-slot scan | undefined-behaviour mutant: the decrement walks `m_nodes[-1]`, `[-2]`; in this allocator layout the out-of-bounds word reads as null, so the loop breaks and `id < 0` takes the same fallback path as the pristine scan. Not killable by any well-defined test. |

**Limitations, stated not hidden:**

- **Scope is one TU.** The 80% target is met for `src/core/RoutingGraph.cpp`, not for
  the fork as a whole; the ruleset's "core modules" (plural) is not yet covered.
- **Sampling.** The default 30-mutant sample is a deterministic stratified draw (one
  per rule class per round, sha256-ordered within a class, seed 0). A different seed
  selects different mutants and can produce a different score; only `--all` (170
  mutants) is exhaustive.
- **Equivalent mutants survive by construction.** Two of the three survivors are
  equivalent-in-practice (write-only data with no reader). There is no automatic
  equivalent-mutant detector; they are named here instead of being hidden.
- **UB mutants can flip.** The `++i -> --i` survivor depends on heap layout; an
  unrelated test change can turn it into a crash (KILLED) or back. Its classification
  is not a reliable signal.
- **Small, regex-based operator set**: comparison-operator flips, `++`/`--` and
  `std::min`/`std::max` swaps, integer-constant changes, condition negation, `return`
  value flips, and single-statement deletion. It does not do block deletion or
  loop-boundary analysis, so a survivor can mean "no test catches this" *or* "this
  operator set did not generate the interesting mutant".
- **Serial and slow-ish**: ~5-6 s per mutant (one TU rebuild + link + test); the
  default run is ~3 min, `--all` ~15-20 min.
- **Build-layout coupling**: the harness assumes `build/` is configured (Debug,
  `WANT_QT6=ON`), the object path
  `build/src/CMakeFiles/lmmsobjs.dir/core/RoutingGraph.cpp.o`, and a headless Qt
  platform plugin. A layout change breaks it loudly (exit 2), not silently.

**What the gate found (2026-09-09).** The first full run scored **23/30 = 76.7%**,
below target. Four survivors were genuine test gaps, not equivalents: negative
source/dest ports were never rejected, `removeNode()` never checked that connections
are dropped, and `process()` was never exercised with a buffer larger than the prepared
window. They were closed with real assertions in `RoutingGraphTest.cpp` (one new test
slot plus assertions in an existing slot; QTest `Totals: 15 -> 16 passed`), and the
score above is the re-measured result. The gate did its job: it found
missing tests, and the tests were strengthened rather than the threshold lowered.

## Gate 6: No UNDECLARED divergence in upstream code (`no-upstream-regression-gate.sh`) — WIRED 2026-09-09, policy corrected 2026-09-11

**Command**: `bash tests/no-upstream-regression-gate.sh [base]` (default base from
`tests/gate-base.txt`).

Rule: behavioural changes to code INHERITED from upstream `origin/master` are allowed **only
when they are declared, with a reason, in `tests/upstream-modifications.txt`** — this repo's
divergence ledger. Anything changed in inherited code that is not in the ledger is a violation.
Fixes to fork-NEW code (`tests/fork-sources.txt`) need no entry, but must ship a regression test
in the same change; an entry with a blank reason makes the gate exit 2 rather than honour it.

**Why the rule changed (2026-09-11).** The 2026-09-09 form forbade *any* behavioural change to
inherited code. That rule was written for an upstream-patch series, where divergence is a cost
someone else pays; this repo is a product, and a blanket ban outlaws exactly the work a DAW
needs — you cannot implement plugin delay compensation without changing the mixer. As written it
was red from the first behavioural change and would have stayed red forever, which means it was
not a gate at all. The ledger keeps the part that has value — nobody diverges *silently*; every
divergence is reviewable, greppable and owed to a reason — and drops the part that was wrong.

The gate is scoped to the commits on top of the base recorded in `tests/gate-base.txt`. For
every changed file it requires the file to be: under `tests/`, a build/config file
(`CMakeLists.txt`, `.gitignore`, `*.cmake`), CI config (`.github/**`), documentation (`*.md`), a
**fork-NEW** source, or a **declared divergence** in the ledger (path + non-empty reason).

The gcc-13 fix shipped with this gate is in `src/core/AudioBus.cpp`
(`#include <iostream>` for `std::cout` under `LMMS_DEBUG` — compile-only, no
behaviour change) — and `AudioBus.cpp` is fork-NEW, so it is allowed by the rule
rather than by an exception.

**Measured (2026-09-11, `main` = `7f08809e4`):** **PASS — every change to upstream-inherited
code since `01148947e` is declared (10 files in the ledger).** All ten landed before the ledger
existed; each entry now carries its reason, and the gate prints them:

| file(s) | why |
|---|---|
| `include/AudioBusHandle.h`, `include/AudioEngine.h`, `include/Effect.h`, `include/EffectChain.h`, `include/Mixer.h` | #605 PDC: latency surface, alignment points, per-edge compensation delays |
| `src/core/AudioBusHandle.cpp`, `src/core/Effect.cpp`, `src/core/EffectChain.cpp`, `src/core/Mixer.cpp` | #605 PDC: per-period latency recompute and summing-point alignment |
| `src/gui/MainWindow.cpp` | compile-only Qt6/`-Werror` fixes: missing `<QDebug>` (`7cd9da2b1`), `QMenu::addAction` deprecation (`7f08809e4`) |

Before the ledger, the same run reported **12 violations (exit 1)**: these ten, plus
`include/LatencyCompensation.h` and `src/core/LatencyCompensation.cpp` — which never belonged on
that list at all: they are fork-NEW and were missing from `tests/fork-sources.txt` (added
2026-09-11). **Negative test:** replacing a ledger reason with an empty field makes the gate
exit 2 with `ledger error: ... has no reason`, so a blank declaration cannot be used to launder
a change.

**Re-measured on `post-alpha/pipeline-hardening` (2026-09-11):** **PASS — 38 changed paths
declared by that ledger, 0 violations.** (Wording: the gate counts the changed *paths* it classified
as declared, which is not the number of *entries* the ledger holds — see the next paragraph.) Four of those had to be declared to get there: the branch carried them as undeclared
divergences (Gate 6 exit 1, 4 violations), a declaration omission rather than a product defect.

| file(s) | why |
|---|---|
| `include/MainWindow.h`, `include/MidiController.h`, `src/core/midi/MidiAlsaSeq.cpp`, `src/core/midi/MidiClient.cpp` | #607 MIDI learn: the Edit-menu action and its slots, the `MidiController::midiPort()` accessors, and the two MIDI input paths that call `MidiLearn::handleMidiEvent()` before the port mask. The lane shipped its regression test (`tests/src/core/MidiLearnTest.cpp`, `22617a93c`), which is what Gate 6 asks for, but no ledger entry (`5d6ccdf1f`). |

The three test files the same branch left in no scope list are a Gate 9 matter, not Gate 6 — see the
Gate 9 section.

**Re-measured on `post-alpha/gate-hygiene` (2026-09-12):** **PASS — 88 changed paths declared; the
ledger holds 112 entries, exit 0.** The two figures are different numbers and the gate now prints
both, each labelled: `$declared` counts the changed paths this run classified as declared, while the
ledger holds one entry per declared path whether or not it changed since `01148947e` (deleted paths
and rename sources keep their entry). Printing the first as "files in the ledger" was read as a
contradiction against the ledger's 112–114 entries for as long as it stood.

Two changes to this gate came with that measurement, both of them corrections rather than
relaxations:

- **`tools/` is a category now.** Gate 6 reads `tests/tools-sources.txt` and classifies a path
  registered there as *fork tooling (allowed)*. `tools/` does not exist at the fork point
  (`git ls-tree -r --name-only 4e677cb6c6ab -- tools` is empty), so every path under it is
  fork-authored by construction and cannot be a divergence of inherited code. Before this, a
  `tools/` file could only get past Gate 6 by being declared in
  `tests/upstream-modifications.txt`, which made the ledger say something untrue about it; the
  **seven** such entries (`tools/mmpz-git/{mmpz_git.py,demo_edits.py,demo_check.py,depth-demo.sh,render-recipe.sh,run-demo.sh,tests/test_mmpz_git.py}`)
  have been deleted, taking the ledger from 119 entries to **112**.
- Nothing else moved: an inherited file still needs a ledger entry with a reason, and a blank reason
  still exits 2.

**Window size, stated plainly:** the gate examines `01148947e..HEAD`, which on `main` is **14
of the product's 136 commits** over upstream master (`git rev-list --count origin/master..HEAD`
= 136 at `7f08809e4`). These counts advance with every commit (133/11 at `b61e14c75`, 134/12 at
`4f1acd5e6`), so re-measure rather than quote. Earlier prose in this document cited `0cea9b0b6`, which is 93 commits behind `HEAD`;
the file is authoritative.

## CI enforcement (`.github/workflows/quality-gates.yml`) — 2026-09-09, trigger policy corrected 2026-09-11

The gates are wired into CI in two tiers. **static-gates** — Gates 3, 4, 6, 7, 8 and 9, no build
needed — runs on every **push and pull request**: 0.2-0.4 min of runner time measured, roughly
0.1-0.2% of the minutes `build.yml` already spends per push. **unit-tests** (Gates 1 and 5) and
**coverage** (Gate 2) stay **workflow_dispatch-only** behind their own
`if: github.event_name == 'workflow_dispatch'` guards: 14-24.5 min measured in CI, `build.yml`
already builds and ctest-runs this tree on every push, and a coverage-baseline capture must stay
a deliberate act. A green check here therefore covers the static gates and nothing else. Run the
rest locally or dispatch the workflow:

- **static-gates** — Gates 3, 4, 6, 7, 8 and 9 (no build needed; `fetch-depth: 0` for Gate 6),
  plus the three tools-scope steps (Gates 4/7/8 with `--scope tools`, added 2026-09-11).
  All six can now **fail** the job. That was not true before 2026-09-11: Gates 4 and 7 were
  invoked with `--check`, and `--check` used to exit 0 unconditionally, so both ratchets were
  red locally and green in CI. `--check` now means "never write the baseline" and still exits 1
  on a regression (see Gates 4 and 7).
- **unit-tests** — Gate 1: Debug + Qt6 configure, build, then ctest from `build/tests`;
  Gate 5 then reuses that same build for the ~3 min mutation sweep.
- **coverage** — Gate 2 in `--check` mode (the baseline is never written in CI), with the
  HTML report uploaded as an artifact.

Trigger scope is not just an `on:` edit: a job that must stay dispatch-only needs its own `if:`
guard, and **unit-tests** and **coverage** each carry
`if: ${{ github.event_name == 'workflow_dispatch' }}` — so restoring (or widening) per-push
enforcement means enabling the trigger **and** keeping those guards on the jobs that must not run
on push.

**Packaging guard (added 2026-09-11).** `.github/workflows/build.yml` carries
`if-no-files-found: error` on all six package-upload steps, so a package-producing job **fails**
when its package glob matches nothing instead of warning and going green with zero assets for that
platform (which is what a rename that misses one packaging path would have produced). The one
non-package upload — the msvc-x64 ctest log, which runs on `failure()` and may legitimately not
exist — keeps `if-no-files-found: warn`, with that exemption written beside it. Flag verified
against the pinned action (`actions/upload-artifact@v7`, ref
`043fb46d1a93c77aae656e7c1c64a875d1fc6a0a`): its `action.yml` documents
`error: Fail the action with an error message`, and `src/upload/upload-artifact.ts` calls
`core.setFailed("No files were found with the provided path: <path>. No artifacts will be
uploaded.")`. `tests/test-package-upload-guard.sh` exercises the glob decision both ways and
rejects the workflow if any package step loses the flag; the `checks` workflow runs it on every
push. Rationale and evidence: `docs/PIPELINE-HARDENING.md` item 1.

All nine gates are wired. Gate 5 lives in the `unit-tests` job because it needs the built
test binary; it costs ~3 min there, which is acceptable when it reuses the build. Locally it
runs by default in `tests/run-all-gates.sh` (`--no-mutation` skips it). An earlier revision of
this document said Gate 5 was deliberately excluded from CI — that was superseded on
2026-09-09 when the harness was added to the build job, because a workflow that runs seven of
eight gates while looking complete is worse than one that says which gate is missing.

**Runner status (2026-09-11).** The workflow *has* now run on a GitHub runner: the
standards-fork run `34413527781` (`standards/quality-gates`) succeeded, and the product's own
dispatch run #2 (`main`, 2026-09-10T00:20Z) **failed**. An earlier revision of this section
said the runner environment had never been exercised because the fork had not been pushed;
both halves of that are now false (pushed and public since 2026-09-09). CI state is a moving
target — read it from `gh run list --repo KRUZZZZY/zene-studio` rather than from this page.

## Gate 7: Per-file length (`file-length-gate.sh`) — 2026-09-09

Source: the adopted code-quality ruleset (KB `adopted-code-quality-gates`) requires
"<= 500 lines default per file (generated tables/fixtures exempt)". QA-GATES.md's original
six gates did not cover it, so the fork claimed a ruleset item it did not enforce. This
gate closes that gap using the same ratchet policy — **no retroactive rewrite**:

- a fork-NEW file already over 500 lines is grandfathered in `tests/file-length-baseline.tsv`;
- a new file over 500 lines fails;
- a grandfathered file that grows fails;
- a file that shrinks drops out of the baseline (the ratchet moves one way only);
- exemptions live in `tests/file-length-exempt.txt` with a stated reason. **That file exists
  as of 2026-09-12** (`post-alpha/gate-hygiene`):  it was named here and by
  `file-length-gate.sh:14,36` from the start but had never been created, which makes a documented
  fail-closed input absent — the same defect class as a gate that cannot fail. It now exists with
  its contract written out and **zero active exemptions** (no file needs one: everything over the
  target is a hand-written source that is grandfathered in its scope's baseline, and listing one
  here instead would hide the decision rather than record it). The gate also **refuses a blank
  reason** (exit 2), and reports an exempt file leaving a baseline as exempt rather than as "no
  longer a fork source". Verified with a scratch entry: the exemption is honoured (measured count
  130 → 129) and a blank reason exits 2 with
  `exempt error: tests/file-length-exempt.txt entry '…' has no reason`.

**Measured (2026-09-09):** 42 fork sources; 6 grandfathered over 500 lines —
`ScriptBindings.cpp` 1127, `AudioPorts.h` 992, `ScriptEngine.cpp` 900,
`Vst3Host.cpp` 714, `AudioPortsModel.cpp` 549, `PinConnector.cpp` 529. No new violations.

**Measured (2026-09-11, 99-file scope):** 99 fork sources measured, **8 over 500 lines**
(`ScriptBindings.cpp` 1217, `AudioPorts.h` 992, `ScriptEngine.cpp` 910,
`ClapEffect/ClapHost.cpp` 875, `Vst3Host.cpp` 714, `WasmSandbox.cpp` 597,
`AudioPortsModel.cpp` 549, `PinConnector.cpp` 529). Two grandfathered files had grown before this
ratchet could fail anywhere — `ScriptBindings.cpp` 1216 → 1217 and `ScriptEngine.cpp` 908 → 910 —
so the baseline was **re-anchored deliberately on 2026-09-11**, with the reason recorded in
`--reanchor "reason"` output and in the commit (trimming code to satisfy a line count is the
worse trade). `--check` no longer exits 0 unconditionally: it performs the same comparison and
exits 1 on a regression, so this ratchet can now fail a CI run.

```sh
bash tests/file-length-gate.sh                    # ratchet: refresh baseline, fail on regressions
bash tests/file-length-gate.sh --check            # CI: never writes the baseline, still fails on regressions
bash tests/file-length-gate.sh --reanchor "why"   # deliberate, recorded baseline refresh
bash tests/file-length-gate.sh --reanchor-file <path> "reason"   # ONE file's entry only
bash tests/file-length-gate.sh --check --scope tools   # the fork's own tooling under tools/
```

**Single-file re-anchors (added 2026-09-12).** `--reanchor` rewrites the whole baseline, so it can
only be used for a reviewed, scope-wide reconciliation; using it for a merged tree grandfathers
every other entry unreviewed, which is a real weakening of the ratchet even though it is the
documented mechanism. `--reanchor-file <path> "reason"` moves exactly one path's entries and carries
everything else over untouched; it prints each key with its old and new value, exits 2 on a blank
reason, and exits 2 on a path that is not over the limit or not in the scope's manifest. Both
ratchets have it, and the whole-tree scope's 2026-09-12 decisions were made with it, one file at a
time — see "Scope policy".

**Measured (2026-09-12, `post-alpha/gate-hygiene`, 129-file scope):** 129 fork sources measured,
**9 over 500 lines** — the same nine as below. The all scope measures **1,133 sources with 112 over
500** and the tools scope **12 with 2 over 500**; both were re-anchored on 2026-09-12 (see "Scope
policy").

**Measured (2026-09-11, 115-file scope — post-alpha/integration):** 115 fork sources measured,
**9 over 500 lines**. The ninth is `src/core/CrashReporter.cpp` (526), added by
`post-alpha/crash-report` and exposed to this ratchet for the first time when integration
registered it in `tests/fork-sources.txt`. It is one self-contained offline crash reporter —
the async-signal-safe formatting primitives, the handler, the install/teardown path and the
report writer — plus the Windows no-op stub, deliberately one unit. `docs/CONVENTIONS.md`
rule 4 forbids trimming code to satisfy a metric, so the baseline was **re-anchored
deliberately** for that one file (26 lines over) with the reason recorded by `--reanchor` and
in the merge commit, following the same trade the 2026-09-11 re-anchor made. The eight
pre-existing entries were unchanged; the ratchet still fails any *other* new file over 500.

**Re-anchored again on 2026-09-12 (merging `post-alpha/clip-slice0` into `post-alpha/integration`):**
the new file `tests/src/tracks/SampleClipWindowTest.cpp` measures **511 lines** (11 over the
target) and was in no baseline, because this merge is what registered it in
`tests/fork-sources.txt`. A sibling lane's report claimed "gate 7 PASS" for that branch; it does
not reproduce — measured on the merged tree, the gate was red on exactly this one file (the
other nine were already grandfathered: 1169, 992, 880, 875, 714, 597, 549, 529, 526). The file
is **one coherent QTest class**: twelve slots that all share a single nine-helper
anonymous-namespace block (`makeTone`/`makeStep`/`makeToneClip`/`makeStepClip`/
`drainPlayHandles`/`trimIn`/`trimOut`/`asNumber`) plus one RED-test provenance comment; ~45 of
its lines are the mandatory GPL header and ~50 are the comments naming the defect the file
exists to catch. Fitting the count would mean extracting the shared helpers into a new support
header and adding a second test binary purely for the metric, and trimming is forbidden by
`docs/CONVENTIONS.md` rule 4 — so it was grandfathered at its measured 511 lines via the same
documented valve, taking the `src/core/CrashReporter.cpp` (526) trade one step further rather
than inventing a new one:

```
bash tests/file-length-gate.sh --reanchor "<reason>"     # EXIT=0, fork scope
  RE-ANCHORED: baseline rewritten from the current tree (10 file(s) over 500 lines)
  new entry: tests/src/tracks/SampleClipWindowTest.cpp   511
```

The nine pre-existing entries are unchanged, and any *other* new fork file over 500 still fails:
`file-length-gate.sh --check` → EXIT=0 afterwards, `--check --scope tools` and
`--check --scope all` are untouched by this change.

## Gate 8: Token duplication (`duplication-gate.sh`) — 2026-09-09

Source: the adopted code-quality ruleset requires "token duplication < 5% (jscpd)". The
original six gates did not cover it. jscpd's `cpp` format does **not** claim `.h` files by
default, so the gate passes an explicit extension map (`cpp:cpp,h,hpp,cc,cxx`) — without it,
header-to-header clones are invisible (verified: a `.h`-only scan reported 0 files).

**Measured (2026-09-09):** 42 fork sources, 10,658 lines, 4 clones, **0.99% duplicated lines**
(2.85% of tokens) against a 5% budget. All four clones are the shared license header — counted,
not suppressed, so the number stays honest.

**Measured (2026-09-11, 99-file scope):** 99 fork sources, **1.09% duplicated lines** — still a
PASS against the 5% budget, and the only gate that both grew its scope and stayed green.

**Measured (2026-09-12, all three scopes):** fork **0.41%** (129 sources), whole tree **1.21%**
(1,133 sources), tools **0.00%** (12 sources) — every one a PASS against the 5% budget. The fork
figure is measured, not explained: the 1.09% → 0.41% drop coincides with the scope growing
99 → 129 sources, so a constant absolute clone volume would produce the same direction.

```sh
bash tests/duplication-gate.sh
bash tests/duplication-gate.sh --scope tools   # fork tooling (python + cpp formats)
```

Requires `npx`; if Node is absent the gate reports **SKIP** rather than a false PASS.

## Gate 9: Every tracked source is registered in a scope manifest (`fork-sources-gate.sh`) — 2026-09-11

**Command**: `bash tests/fork-sources-gate.sh` (add `--verbose` to print the verdict for every file).

Rule: every tracked source file under `src/`, `include/`, `plugins/`, `tests/` and `tools/` must be in
one of the three scope manifests:
**`tests/fork-sources.txt`** (this product's own new PRODUCT code — the fork-scoped ratchets and
Gate 2's per-file baseline watch it), **`tests/all-sources.txt`** (the whole-tree first-party C/C++
scope: upstream code plus the fork's C/C++ tests and probes), or **`tests/tools-sources.txt`**
(the fork's own tooling under `tools/`, measured by Gates 4/7/8 with `--scope tools`). A file in
none of them is in **no** scope: no ratchet measures it, and the gate names it with the verdict
`NOT IN tests/fork-sources.txt`, plus what to do about it (register it as fork-new, as
upstream-inherited, or as fork tooling). Stale manifest entries — a listed file that does not exist —
are reported too.

**What this gate caught on `post-alpha/integration` (2026-09-11).** Three fork-authored test files
were in no scope list at all — `tests/src/core/LufsMeterTest.cpp`, `tests/src/core/MidiLearnTest.cpp`
and `tests/src/core/SessionModelTest.cpp` (Gate 9 exit 1). They are registered in
`tests/all-sources.txt` — the whole-tree scope — which is where **every** fork-authored test in
this repo is registered (43 of the 51 `tests/src/**` entries there are absent at the fork point,
i.e. fork code; the other 8 are upstream's own tests); the fork ratchets and Gate 2's per-file
baseline deliberately measure product sources, not test harnesses. That is the convention this gate
is here to make visible.

**Why (2026-09-11).** `include/LatencyCompensation.h` and `src/core/LatencyCompensation.cpp` shipped
on 2026-09-10 in no scope list. The only gate that reacted was Gate 6, which reported them as an
"undeclared change to upstream-inherited code" — the wrong diagnosis for two files upstream has never
had, and it cost a day. Gate 9 says the right thing in one line. It is a naming check, not a scope
widener: it does not add files to any ratchet, it refuses to let one be invisible.

Excluded, stated in the script rather than silently skipped: the vendored third-party trees
(`src/3rdparty/`, `plugins/NeuralAmp/{rtneural,nam,tests}/`, `plugins/RnnoiseDenoiser/rnnoise/`) and
`tests/reference/**` (frozen verbatim copies of upstream files, provenance in
`tests/reference/ORIGIN.tsv`).

**Measured (2026-09-11, `post-alpha/gate-debt`):** 1,091 tracked sources scanned; 100 fork-NEW, 992
inherited, **0 unregistered, 0 stale → exit 0**. Wired into the `static-gates` job, which runs on
every push and pull request.

**Measured (2026-09-12, `post-alpha/gate-hygiene`):** **1,146 tracked sources scanned; 129 fork-NEW,
1,005 whole-tree, 12 tooling; 0 unregistered, 0 stale → exit 0.** Two changes came with it:

- **`modules/` is scanned now**, which is what closes the registration-that-could-never-be-checked:
  `modules/wasm/demo/gain_clip.c` was registered in `tests/all-sources.txt` while sitting outside
  this gate's pathspec (`src include plugins tests tools`), so no run could ever validate the
  entry — and the all scope never measured the file either, making it registered and unmeasured at
  once. `modules/wasm/*.wat` are not sources by extension and need no home.
- **`tests/all-sources.txt` is now checked in the other direction too** (an entry with no file is
  reported as a stale entry, as it already was for the fork and tools manifests), because the
  all-sources manifest is the one whose entries were previously unvalidated in both directions:
  nothing checked file→registered for it, and nothing checked registered→exists either.

**Red/green proof**: `bash tests/test-verification-debt.sh` builds a fixture in which a new file is
committed in no scope list: at the pre-fix revision no such check exists and Gate 6 calls it an
undeclared change to upstream code; the fixed Gate 9 names it and exits 1, then exits 0 once the file
is registered.

## Evaluated and NOT wired: dead code (`cppcheck --enable=unusedFunction`) — 2026-09-09

The adopted ruleset requires "dead code: zero (ruff/vulture)". The C++ equivalent is
cppcheck's `unusedFunction`, and this fork has a `compile_commands.json`, so the check is
runnable. **It is not a usable gate for this codebase**, and the evidence is why:

```sh
cppcheck --project=build/compile_commands.json --enable=unusedFunction \
  --quiet --template='{file}|{line}|{id}'
# -> 689 unusedFunction hits project-wide
```

689 is dominated by vendored/3rdparty headers (`plugins/LadspaEffect/swh/**`,
`ladspa-util.h`, …). Scoped to `tests/fork-sources.txt` the number is **11**, and every one
of the 11 is an accessor or a small API method, not dead code:

| file | line | symbol |
|---|---|---|
| `include/RoutingNodes.h` | 75, 98, 99 | `alpha()`, `gain()`, `setGain()` |
| `include/RoutingGraph.h` | 93, 96, 100 | `connections()`, `processingOrder()`, `outputNodeId()` |
| `src/core/RoutingGraph.cpp` | 75 | `RoutingGraph::removeNode()` |
| `include/TrackRecorder.h` | 90 | `isWriterRunning()` |
| `src/core/audio/TrackRecorder.cpp` | 64 | `inputChannel()` |
| `src/core/audio/MultiTrackRecorder.cpp` | 90 | `totalOverflowCount()` |
| `include/RecordRingBuffer.h` | 91 | `writeBlock()` |

cppcheck cannot see callers in other translation units or in the test binaries, so it flags
public API surface. Wiring this would force either a fake baseline or the deletion of used
methods — both worse than an honest "not gated". The ruleset item is therefore **closed by
evidence, not by a script**, and the distinction is recorded here deliberately.

## Release-honesty guard (`release-honesty-gate.sh`) — 2026-09-12, release path

**Command** (as `build.yml` runs it, after the tests and before `Package`, on every job):

```sh
bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build
```

**Pass criterion**: exit 0. For every feature in `tests/advertised-features.tsv` — the
single home for "what this release documents", which the gate reads and no list of its own
— the build's own reported option must hold the documented value, and (when `--artifacts`
is given) the plugin module that option implies must exist in the build tree. An option
that is *missing* from the report is a failure, not a pass: that is the shape a skipped
host takes (`plugins/Vst3Effect/CMakeLists.txt` returns before its `SET(... CACHE ...)`
runs, so `WANT_VST3` never reaches `lmmsversion.h`). A feature documented as absent must
report itself as off.

**Why it is not a numbered gate in `run-all-gates.sh`**: it needs a built tree and a
packaged job's build directory, so it is wired into `build.yml` on the release path rather
than into the gate runner, and it is deliberately unnumbered — the Gate 9 slot
(`fork-sources-gate.sh`) belongs to the gate-debt lane and does not exist on every branch.
`--dump <lmms --version output>` judges the same text from the binary instead of the
header; the local proof for the guard compares the two.

**Why it exists**: the published `v0.1.0-alpha` advertised VST3 and CLAP hosting while all
seven jobs of its release run printed `VST3 hosting skipped` / `CLAP hosting skipped` and
shipped neither. Nothing failed and every test passed, because a host whose dependency is
absent is compiled out with a `STATUS` line rather than an error. See
[`docs/PLUGIN-HOSTING-IN-RELEASE.md`](../docs/PLUGIN-HOSTING-IN-RELEASE.md).

## Running all gates

```sh
bash tests/run-all-gates.sh                  # Gates 1, 3, 4, 5, 6, 7, 8, 9 (Gate 5 ≈3 min)
bash tests/run-all-gates.sh --no-mutation    # skip the Gate 5 sweep
bash tests/run-all-gates.sh --with-coverage  # + Gate 2 (full coverage build)
bash tests/run-all-gates.sh --whole-tree     # gates 4, 7, 8 over tests/all-sources.txt as well
```

Every run's summary prints the **scope line** — which scopes it measured and, in a default run, that
the whole-tree scope was not measured. A default run is the fork scope plus the tools scope; the
whole-tree scope is run deliberately (`--whole-tree`) and before a freeze. See "Scope policy".

Gate 5 runs by default and reports a real score; `--no-mutation` is the only way to
skip it. Gate 2 stays opt-in because it rebuilds the whole tree.

**Exit codes (changed 2026-09-11): 0 = every gate ran and passed; 1 = at least one FAIL;
3 = every gate that ran passed but at least one was SKIPPED.** Before the change `record()`
only failed on `FAIL`, so a run with two skipped gates printed `RESULT: PASS — every executed
gate passed` and exited 0 — the two gates it laundered being the two most expensive ones
(ctest, which needs `build/`, and coverage, which needs `--with-coverage`). The summary now
counts the skipped gates, names each one and says how to run it for real. A plain
`run-all-gates.sh` must therefore not be read as a green run: it exits 3 whenever Gate 2 was
not requested.

`bash tests/test-verification-debt.sh` runs the red/green proof for the three
2026-09-11 verification-debt fixes (`docs/VERIFICATION-DEBT-FIXES.md`). It is a fixture
harness, not a gate: it builds synthetic trees in a temp directory and exits non-zero only if
one of its assertions fails.

## Notes

- Coverage flags live behind `WANT_COVERAGE` (top-level `CMakeLists.txt`),
  following the repo's sanitizer convention (`WANT_DEBUG_*`). Test targets
  compile with `--coverage`; because they link the instrumented `lmmsobjs`,
  library code under test is covered without per-target plumbing.
- `PluginPortsMigrationTest` needs `QT_QPA_PLATFORM=offscreen` in headless
  shells (both scripts set it).
- **Windows test-host limitation — three suites skip (2026-09-11).** `AudioPluginTest`,
  `PluginPortsMigrationTest` (all six slots) and `ScriptEngineTest::testInstrumentParameterReadWrite`
  load plugin MODULE libraries at runtime, and a Windows test host cannot: every plugin module
  links the `zene` executable, so an MSVC module's import descriptor names `zene.exe` and the
  Windows loader fails with `ERROR_MOD_NOT_FOUND` (126) — CI msvc-x64, with
  `QT_FORCE_STDERR_LOGGING=1`, prints it verbatim (*"Cannot load library
  …\plugins\tripleoscillator.dll: The specified module could not be found."*). On Linux/macOS the
  same tests run unchanged: a module's undefined lmms symbols bind from the loading process's
  exported symbol table (the test targets set `ENABLE_EXPORTS`). This is a **test-host limitation,
  not a masked product defect** — the product loads the same modules inside `zene.exe`, where the
  import resolves by construction. **Boarded follow-up (not landed):** a coverage-preserving
  Windows test that drives the real host instead of a test host — a ctest that runs the built
  `zene.exe` headless (`QT_QPA_PLATFORM=offscreen zene render <fixture>`) and asserts exit 0 plus
  non-silent output on a project using `tripleoscillator` and one migrated effect. Until it lands,
  **Windows has no coverage of**: loading a migrated module at all, the sample-exact migration
  comparison, the legacy single-buffer `AudioPlugin` bridge, and the Lua instrument-parameter
  binding.
- Tests must run against a real build: "it compiles" never substitutes for a
  passing `ctest`.
- `include/AudioPlugin.h`'s legacy single-buffer bridge
  (`AudioPlugin::processImpl(SampleFrame*, f_cnt_t)`) deliberately routes the
  legacy interleaved buffer through the audio ports router instead of
  constructing a buffer view directly. A view over the interleaved buffer only
  exists for in-place interleaved settings, so the view-based bridge could not
  compile for planar, non-in-place effects (`ClapEffect`, `Vst3Effect`) —
  task #607. Routing keeps both entry points working for every
  `AudioPortsSettings`;
  `ClapEffectIntegrationTest::testLegacyAudioBufferPathRoutesPlanarPorts`
  holds the planar case and
  `AudioPluginTest::legacyAudioBufferPathRoutesInPlacePorts` holds the in-place
  case sample-exactly.
- **Remote-plugin (out-of-process) host coverage and its two known, unfixed defects
  (2026-09-11).** `RemotePluginAudioPortsTest` drives the host side of the socket
  protocol against a client stand-in it writes itself (`test-peer.py`, run with python3;
  the slots `QSKIP` when python3 is absent and do not exist where the build has no socket
  path, i.e. `SYNC_WITH_SHM_FIFO` platforms like Windows). `RemotePlugin::init()` starts
  the peer, the peer connects and logs every message id it receives, and each slot picks
  its behaviour: never answer ("silent"), answer `IdProcessingDone` ("reply"), or exit on
  the first period request ("die-on-start"). Covered: a client that dies mid-period must
  leave the output planes silent and make `process()` report false (its wait's result used
  to be discarded, so a partially written period was mixed); a peer that answers still
  yields true (positive control); a plugin with zero output channels never sends
  `IdStartProcessing` (the request must not be sent when nothing can consume the reply —
  replies otherwise accumulate until a socket blocks on the audio thread); and a failed
  reallocation leaves the ports reporting NOT initialized. **The real client half is still
  not exercised on this box**: `RemoteVstPlugin`'s client needs a real VST library to load
  and the Windows client binaries are CI-only. `NativeLinuxRemoteVstPlugin64` compiles the
  same client sources locally (with the CI flags, `USE_WERROR=ON`), which is what carries
  the client-side change here. **Known and deliberately not fixed, scheduled for the next
  release:** (a) a client-initiated channel-count change (`IdChangeInputOutputCount`) is
  applied inline on the audio thread by the message pump inside `process()`'s
  `waitForMessage()` (`src/core/RemotePlugin.cpp` -> `processMessage` ->
  `setChannelCounts` -> `AudioPortsModel::bufferPropertiesChanging` ->
  `RemotePluginAudioPorts::updateBuffers`), so it can allocate shared memory, unmap the
  block the caller's plane views point into, and emit a Qt signal from the audio thread;
  (b) the same rebuild is reachable from a GUI-thread pump, because the VstPlugin
  parameter/info entry points that run pumping waits (`waitForMessage(..., busyWaiting=true)`,
  `plugins/VstBase/VstPlugin.cpp:591-596` and the helpers beside it) are called from GUI
  constructors (`plugins/Vestige/Vestige.cpp:1008-1009`,
  `plugins/VstEffect/VstEffectControls.cpp:398-399`).
