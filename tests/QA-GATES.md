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

**Measured numbers** (first full run, gcc 13 / lcov 2.0):

| Directory            | Lines | Hit  | Rate   |
|----------------------|-------|------|--------|
| `src/core` (fork)    | 1730  | 1296 | 74.91% |
| `src/core/audio`     | 88    | 74   | 84.09% |
| `include` (fork)     | 417   | 207  | 49.64% |
| `src/gui` (fork)     | 269   | 0    | 0.00%  |
| **Total fork code**  | 2504  | 1577 | **63.0%** |

Per-module highlights: `RoutingGraph.cpp` 100%, `RoutingNode.cpp` 100%,
`RoutingNodes.cpp` 96.1%, `AudioBus.cpp` 76.8%, `AudioPortsModel.cpp` 75.6%,
`ScriptEngine.cpp` 79.7%. The 0% areas are GUI-only (`PinConnector.cpp`) and
hosting glue exercised only through the plugin/RemotePlugin binaries, which
the unit-test coverage run does not load.

Gate 2 **partially met**: the ratchet mechanism is live and green, but the
fork's overall line coverage (63%) is below the 85% aspiration from the
adopted quality-gates KB article, and the "100% on core logic modules" goal
is met for the routing core (`RoutingGraph.cpp`, `RoutingNode.cpp`) but not
yet for `AudioBus.cpp`/`AudioPortsModel.cpp`. The ratchet makes every future
improvement permanent.

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

## Gate 5: Mutation testing (advisory / deferred)

The adopted quality-gates KB article calls for mutation testing; for C++ the
tooling (mull, CCmutator) is immature for a Qt codebase of this size (LLVM
version pinning, long runtimes, no gcc-13 support). **Marked advisory/deferred
rather than pretended**: the coverage ratchet (Gate 2) plus real-value
assertions (Gate 3) are the pragmatic substitutes until tooling matures.

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
bash tests/run-all-gates.sh                  # Gates 1, 3, 4, 6 (fast)
bash tests/run-all-gates.sh --with-coverage  # + Gate 2 (full coverage build)
```

Gate 5 remains advisory/deferred; the runner reports it as SKIP rather than pretending.

## Notes

- Coverage flags live behind `WANT_COVERAGE` (top-level `CMakeLists.txt`),
  following the repo's sanitizer convention (`WANT_DEBUG_*`). Test targets
  compile with `--coverage`; because they link the instrumented `lmmsobjs`,
  library code under test is covered without per-target plumbing.
- `PluginPortsMigrationTest` needs `QT_QPA_PLATFORM=offscreen` in headless
  shells (both scripts set it).
- Tests must run against a real build: "it compiles" never substitutes for a
  passing `ctest`.
