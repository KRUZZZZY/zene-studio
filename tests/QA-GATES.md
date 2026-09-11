# QA Gates

Executable quality gates for **`zene-studio`** — the derived product repo —
ported from the LMMS standards fork on 2026-09-09. Every gate is a script under
this directory; nothing in this document is aspirational — if it is not checked
by a script, it is not a gate.

Scope: the sources this product adds on top of upstream master `4e677cb6c6ab`,
listed in `tests/fork-sources.txt` (**99 files** — 97 at the 2026-09-09 port, plus
`include/LatencyCompensation.h` and `src/core/LatencyCompensation.cpp`, added
2026-09-11 because the #605 PDC work was shipping outside every gate's scope; see the
Gate 2 scope note).

**Whole-tree scope (added 2026-09-11).** Gates 4, 7 and 8 also accept `--scope all`, which points
them at `tests/all-sources.txt` — **1,095 first-party files**, i.e. upstream-inherited code plus
the fork's own. Their baselines are separate files (`tests/*-baseline-all.tsv`) so the two ratchets
cannot shadow each other, and `run-all-gates.sh --whole-tree` runs the set. The measured whole-tree
state (8,953 functions with 273 over CCN 10; 107 files over 500 lines; 1.29% duplicated lines;
32.33% line coverage over 919 instrumented files) is in
[`docs/CONVENTIONS.md`](https://github.com/KRUZZZZY/zene-studio/blob/main/docs/CONVENTIONS.md),
together with the coverage ratchet for the whole tree. Everything in *this* document below
describes the 99-file fork scope unless it says otherwise. Vendored third-party trees are
excluded on purpose — 524 files under `src/3rdparty` (lua, luabridge),
`plugins/NeuralAmp/rtneural`, `plugins/NeuralAmp/nam`, `plugins/NeuralAmp/tests`
and `plugins/RnnoiseDenoiser/rnnoise`; they are not this repo's code and these
gates never refactor them. Upstream LMMS code is never refactored by these gates;
its coverage is intentionally excluded from the reports.

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
- new fork sources enter the baseline at their measured coverage;
- removed sources drop out of the baseline.

`--check` runs in CI mode: report only, baseline never written.

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
their measured coverage on the next `run-coverage.sh`, and Gate 2 has no entry floor, so a new
file **cannot** fail on entry — that is one of this suite's open defects, recorded as such.
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

**Measured (2026-09-11):** 21 registered QTest files, **all PASS** — 0 tautologies
(1,744 slots, 1,477 assertions; 2026-09-09: 20 files, 1,597 / 1,408). Coverage (Gate 2) is the corroborating signal:
test-unreachable code shows 0%.

## Gate 4: Per-method complexity (`complexity-gate.sh`) — WIRED 2026-09-09

Target: **cyclomatic complexity (CCN) <= 10** and **nesting depth (ND) <= 3** per method.
This gate is now mechanical. `cppcheck` 2.13 does **not** emit complexity for this
codebase (verified: zero matches with `--enable=all` / `--enable=style`), so the tool
is **lizard** (`pip install lizard`, reports CCN and ND for C/C++).

**Command**:

```sh
bash tests/complexity-gate.sh            # ratchet: refresh baseline, fail on regressions
bash tests/complexity-gate.sh --check    # CI: never writes the baseline, still fails on regressions
bash tests/complexity-gate.sh --strict   # fail if ANY function is over the target
bash tests/complexity-gate.sh --reanchor "why"   # deliberate, recorded baseline refresh
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

**Measured (2026-09-11, gcc 13, 99-file scope):** `complexity-gate.sh` scans **807 functions**;
**24 exceed CCN 10**. Two defects found in the gate itself were fixed the same day:

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
now scans **808 functions with 23 over target, all grandfathered**, and `--check` exits 0.

The figures this section previously carried (13 of 514 functions, highest CCN 27) were measured
on the standards fork's 42-file scope and no longer describe the product. The highest CCN in the
fork scope is **41** (`ExternalProcessStemSeparator::separate`, grandfathers in
`tests/complexity-baseline.tsv`), and in the whole tree **136** (`src/core/main.cpp` — see
`docs/CONVENTIONS.md`); an earlier revision of this paragraph said 29, which was
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

**Window size, stated plainly:** the gate examines `01148947e..HEAD`, which on `main` is **14
of the product's 136 commits** over upstream master (`git rev-list --count origin/master..HEAD`
= 136 at `7f08809e4`). These counts advance with every commit (133/11 at `b61e14c75`, 134/12 at
`4f1acd5e6`), so re-measure rather than quote. Earlier prose in this document cited `0cea9b0b6`, which is 93 commits behind `HEAD`;
the file is authoritative.

## CI enforcement (`.github/workflows/quality-gates.yml`) — 2026-09-09, trigger policy corrected 2026-09-11

The gates are wired into CI in two tiers. **static-gates** — Gates 3, 4, 6, 7 and 8, no build
needed — runs on every **push and pull request**: 0.2-0.4 min of runner time measured, roughly
0.1-0.2% of the minutes `build.yml` already spends per push. **unit-tests** (Gates 1 and 5) and
**coverage** (Gate 2) stay **workflow_dispatch-only** behind their own
`if: github.event_name == 'workflow_dispatch'` guards: 14-24.5 min measured in CI, `build.yml`
already builds and ctest-runs this tree on every push, and a coverage-baseline capture must stay
a deliberate act. A green check here therefore covers the static gates and nothing else. Run the
rest locally or dispatch the workflow:

- **static-gates** — Gates 3, 4, 6, 7 and 8 (no build needed; `fetch-depth: 0` for Gate 6).
  All five can now **fail** the job. That was not true before 2026-09-11: Gates 4 and 7 were
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

All eight gates are wired. Gate 5 lives in the `unit-tests` job because it needs the built
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
- exemptions live in `tests/file-length-exempt.txt` with a stated reason.

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
```

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

```sh
bash tests/duplication-gate.sh
```

Requires `npx`; if Node is absent the gate reports **SKIP** rather than a false PASS.

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
bash tests/run-all-gates.sh                  # Gates 1, 3, 4, 5, 6, 7, 8 (Gate 5 ≈3 min)
bash tests/run-all-gates.sh --no-mutation    # skip the Gate 5 sweep
bash tests/run-all-gates.sh --with-coverage  # + Gate 2 (full coverage build)
```

Gate 5 runs by default and reports a real score; `--no-mutation` is the only way to
skip it. Gate 2 stays opt-in because it rebuilds the whole tree.

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
  links the `lmms` executable, so an MSVC module's import descriptor names `lmms.exe` and the
  Windows loader fails with `ERROR_MOD_NOT_FOUND` (126) — CI msvc-x64, with
  `QT_FORCE_STDERR_LOGGING=1`, prints it verbatim (*"Cannot load library
  …\plugins\tripleoscillator.dll: The specified module could not be found."*). On Linux/macOS the
  same tests run unchanged: a module's undefined lmms symbols bind from the loading process's
  exported symbol table (the test targets set `ENABLE_EXPORTS`). This is a **test-host limitation,
  not a masked product defect** — the product loads the same modules inside `lmms.exe`, where the
  import resolves by construction. **Boarded follow-up (not landed):** a coverage-preserving
  Windows test that drives the real host instead of a test host — a ctest that runs the built
  `lmms.exe` headless (`QT_QPA_PLATFORM=offscreen lmms render <fixture>`) and asserts exit 0 plus
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
