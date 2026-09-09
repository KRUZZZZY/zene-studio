# QA Gates

Executable quality gates for the LMMS standards fork. Every gate is a script
under this directory; nothing in this document is aspirational — if it is not
checked by a script, it is not a gate.

The gates apply to the fork's NEW code (sources added on top of upstream
master `4e677cb6c6ab`, listed in `tests/fork-sources.txt`). Upstream LMMS code
is never refactored by these gates; its coverage is intentionally excluded
from the reports.

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
**Current baseline: 17/17 passing** (16 inherited + `AudioBusTest`).

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

**Measured numbers** (2026-09-09, gcc 13 / lcov 2.0, after the coverage push):

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

Still at 0%: `src/gui/PinConnector.cpp` (269 lines), plus three headers with no
line reached (`LmmsPolyfill.h` 2, `PinConnector.h` 5, `RemotePluginAudioPorts.h`
6). `PinConnector` is a `QWidget` whose behaviour is the GUI event loop and the
`AudioPortsModel` it renders (`paintEvent` at `PinConnector.cpp:184`); the
unit-test binaries run offscreen with no event-loop interaction. This is an
**evidenced exclusion, not an oversight**: no file under `tests/src/` references
`PinConnector` (the only hits in `tests/` are the gate metadata files
`fork-sources.txt`, `QA-GATES.md` and the three baselines).

Gate 2 **met**: the ratchet is live and green at **85.24%**, clearing the adopted
ruleset's 85% aspiration. `AudioBus.cpp` (95.77%) and `ScriptEngine.cpp` (89.36%)
remain short of 100%; `PinConnector.cpp` stays excluded for the reason above.
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

**Measured (2026-09-09):** 16 registered QTest files, **all PASS** — 0 tautologies;
the two heaviest are `ScriptBindingsTest.cpp` (267 slots / 246 assertions) and
`RoutingGraphTest.cpp` (144 / 127). Coverage (Gate 2) is the corroborating signal:
test-unreachable code shows 0%.

## Gate 4: Per-method complexity (`complexity-gate.sh`) — WIRED 2026-09-09

Target: **cyclomatic complexity (CCN) <= 10** and **nesting depth (ND) <= 3** per method.
This gate is now mechanical. `cppcheck` 2.13 does **not** emit complexity for this
codebase (verified: zero matches with `--enable=all` / `--enable=style`), so the tool
is **lizard** (`pip install lizard`, reports CCN and ND for C/C++).

**Command**:

```sh
bash tests/complexity-gate.sh            # ratchet: refresh baseline, fail on regressions
bash tests/complexity-gate.sh --check    # CI: report only, never writes the baseline
bash tests/complexity-gate.sh --strict   # fail if ANY function is over the target
```

**Policy — a ratchet, never a rewrite order** (matching Gate 6's no-refactor rule):
functions already over the target are grandfathered in `tests/complexity-baseline.tsv`
at their measured CCN; a **new** function over the target, or an existing one whose CCN
**rises**, fails the gate. Existing over-target functions are reported, not rewritten.

**Honest limitation, stated not hidden:** lizard exposes thresholds for
`nloc`, `cyclomatic_complexity`, `token_count`, `parameter_count`, `length` — **not for
nesting depth**. ND is therefore *reported* alongside each over-target function and
reviewed manually; it is not enforced mechanically.

**Measured baseline (2026-09-09, gcc 13):** 13 of the fork's 514 functions exceed CCN 10.
Highest: `ScriptEngine::applyCommand` 27, `HostedPlugin::load` 25,
`AudioPortsModel::updateDirectRouting` 23, `HostedPlugin::loadState` 18,
`AudioBus::update` 17. **All 12 have ND 0**, i.e. nesting is within target everywhere;
the overage is branch count, not depth.

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
(`build/tests/RoutingGraphTest`, 16 slots). Whole-project mutation is deliberately not
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
window. They were closed with real assertions in `RoutingGraphTest.cpp` (15 -> 16
slots), and the score above is the re-measured result. The gate did its job: it found
missing tests, and the tests were strengthened rather than the threshold lowered.

## Gate 6: No behavioural regressions in upstream code (`no-upstream-regression-gate.sh`) — WIRED 2026-09-09

**Command**: `bash tests/no-upstream-regression-gate.sh [base]` (default base from
`tests/gate-base.txt`).

Rule: behavioural changes to code INHERITED from upstream `origin/master` are
forbidden in this fork; fixes to fork-NEW code are allowed and must ship a
regression test in the same change.

The fork's feature branches deliberately modify upstream files (that is their
purpose), so the gate is scoped to the **standards workstream** — the commits on top
of the integration base recorded in `tests/gate-base.txt`. For every changed file it
requires the file to be: under `tests/`, a build/config file (`CMakeLists.txt`,
`.gitignore`, `*.cmake`), documentation, a **fork-NEW** source
(`tests/fork-sources.txt`), or an explicitly allowlisted upstream fix in
`tests/upstream-modifications.txt` (which must state the reason). Anything else is a
violation.

The gcc-13 fix shipped with this gate is in `src/core/AudioBus.cpp`
(`#include <iostream>` for `std::cout` under `LMMS_DEBUG` — compile-only, no
behaviour change) — and `AudioBus.cpp` is fork-NEW, so it is allowed by the rule
rather than by an exception.

**Measured (2026-09-09):** PASS — every file changed since `0cea9b0b6` is tests/,
build-config, docs, or fork-NEW.

## CI enforcement (`.github/workflows/quality-gates.yml`) — 2026-09-09

The gates are now wired into CI, so they run on every push/PR rather than only when
someone remembers:

- **static-gates** — Gates 3, 4 and 6 (no build needed; `fetch-depth: 0` for Gate 6).
- **unit-tests** — Gate 1: Debug + Qt6 configure, build, then ctest from `build/tests`.
- **coverage** — Gate 2 in `--check` mode (the baseline is never written in CI), with the
  HTML report uploaded as an artifact.

Gate 5 is deliberately **not** in CI: a 30-mutant sweep is ~3 min of serial
rebuild+test cycles, and a mutation score is a periodic quality signal, not a
per-push gate. It is enforced by `tests/run-all-gates.sh` (which CI does not run);
the distinction is stated here rather than implied.

Every command in the workflow is one that has been executed locally; the GitHub runner
environment itself has **not** been exercised (this fork has not been pushed), so the
workflow is *syntax-validated and command-verified, not CI-verified* — stated plainly
rather than assumed green.

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

```sh
bash tests/file-length-gate.sh          # ratchet: refresh baseline, fail on regressions
bash tests/file-length-gate.sh --check  # CI mode: report only
```

## Gate 8: Token duplication (`duplication-gate.sh`) — 2026-09-09

Source: the adopted code-quality ruleset requires "token duplication < 5% (jscpd)". The
original six gates did not cover it. jscpd's `cpp` format does **not** claim `.h` files by
default, so the gate passes an explicit extension map (`cpp:cpp,h,hpp,cc,cxx`) — without it,
header-to-header clones are invisible (verified: a `.h`-only scan reported 0 files).

**Measured (2026-09-09):** 42 fork sources, 10,658 lines, 4 clones, **0.99% duplicated lines**
(2.85% of tokens) against a 5% budget. All four clones are the shared license header — counted,
not suppressed, so the number stays honest.

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
- Tests must run against a real build: "it compiles" never substitutes for a
  passing `ctest`.
