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

## Gate 3: No tautological tests

**Rule** (enforced by review, aided by coverage): every test must reference at
least one SUT symbol and contain real assertions. Empty tests, `QVERIFY(true)`
tests, and tests without assertions against production behaviour are banned.
The two test files added with this gate (`AudioBusTest.cpp` — 13 slots,
`RoutingGraphTest.cpp` — extended to 14 slots) each assert concrete values
produced by the SUT, which the coverage numbers above confirm (unreachable-by-test
code would show 0%).

## Gate 4: Per-method complexity (advisory)

Target: cyclomatic complexity <= 10 and nesting depth <= 3 per method, as a
**report first** — no rewrite of existing code. `cppcheck` is not installed on
the build host at gate-introduction time, so the mechanical check is deferred;
when available, wire:

```sh
cppcheck --enable=all --std=c++20 --check-level=exhaustive src/core \
  2>&1 | grep -E "normalCheckLevelMaxNodes|cyclomatic"
```

until then, complexity is reviewed manually on fork-NEW code only.

## Gate 5: Mutation testing (advisory / deferred)

The adopted quality-gates KB article calls for mutation testing; for C++ the
tooling (mull, CCmutator) is immature for a Qt codebase of this size (LLVM
version pinning, long runtimes, no gcc-13 support). **Marked advisory/deferred
rather than pretended**: the coverage ratchet (Gate 2) plus real-value
assertions (Gate 3) are the pragmatic substitutes until tooling matures.

## Gate 6: No behavioural regressions in upstream code

Behavioural changes to code inherited from upstream `origin/master` are
forbidden in this fork. Fixes to fork-NEW code are allowed and require a
regression test in the same change. The only production-file change shipped
with this gate is the gcc-13 fix in `src/core/AudioBus.cpp`
(`#include <iostream>` for `std::cout` under `LMMS_DEBUG` — compile fix,
no behaviour change).

## Notes

- Coverage flags live behind `WANT_COVERAGE` (top-level `CMakeLists.txt`),
  following the repo's sanitizer convention (`WANT_DEBUG_*`). Test targets
  compile with `--coverage`; because they link the instrumented `lmmsobjs`,
  library code under test is covered without per-target plumbing.
- `PluginPortsMigrationTest` needs `QT_QPA_PLATFORM=offscreen` in headless
  shells (both scripts set it).
- Tests must run against a real build: "it compiles" never substitutes for a
  passing `ctest`.
