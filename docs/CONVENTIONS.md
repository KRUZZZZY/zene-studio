# Coding conventions — and how each one is enforced

Two sources define what "good" means here, and this file maps every rule to the thing that
actually checks it:

- **KB `adopted-code-quality-gates`** — the standing ruleset adopted 2026-09-06: per-method
  complexity 10, nesting depth 3, complexity-as-ratchet, no tautological tests, coverage ≥ 85%
  rising, mutation ≥ 80% on core, duplication < 5%, zero dead code, strict types at trust
  boundaries.
- **This repo's own tooling** — `.clang-format` (LLVM-derived: tabs, width 4, column limit 120,
  C++20), `.clang-tidy` (bugprone/modernize/performance/readability checks), `.editorconfig`.
- **`tests/QA-GATES.md`** is the executable contract. If a rule is not checked by a script, it is
  not a gate here — and every unenforced rule below says so explicitly instead of being implied.

## Enforced today (executable, in CI-worth shape)

| # | Convention | Where it comes from | Enforced by | Scope | State |
|---|---|---|---|---|---|
| 1 | Cyclomatic complexity ≤ 10 per method | KB ruleset | Gate 4 `tests/complexity-gate.sh` (lizard) | fork-NEW sources (99 files) | **green**; 808 functions, 23 over target all grandfathered; ratchet |
| 2 | Per-file ≤ 500 lines | KB ruleset | Gate 7 `tests/file-length-gate.sh` | fork-NEW sources | **green**; 8 files over 500 grandfathered; ratchet with recorded `--reanchor` |
| 3 | Token duplication < 5% | KB ruleset | Gate 8 `tests/duplication-gate.sh` (jscpd, headers included) | fork-NEW sources | **green**; 1.09% duplicated lines |
| 4 | No tautological tests | KB ruleset | Gate 3 `tests/no-tautology-gate.sh` | every registered QTest file | **green**; 21 files, 1,744 slots, 1,477 assertions, 0 tautologies |
| 5 | Coverage ≥ 85%, never falling | KB ruleset | Gate 2 `tests/coverage-gate.sh` (lcov ratchet, line-weighted) | fork-NEW sources | **below aspiration**: 76.07% (2661/3498) on the 2026-09-09 scope; ratchet prevents regression |
| 6 | Mutation kill score ≥ 80% on core | KB ruleset | Gate 5 `tests/mutation-gate.sh` (scoped harness) | one TU, `src/core/RoutingGraph.cpp` | **green**; 27/30 = 90%, scope stated as a limit |
| 7 | All tests pass | repo | Gate 1 (`ctest` from `build/tests`) | `tests/` | **green**; 24/24 (Debug, Qt6) |
| 8 | No *undeclared* divergence in inherited code | repo policy 2026-09-11 | Gate 6 `tests/no-upstream-regression-gate.sh` + `tests/upstream-modifications.txt` ledger | commits since `tests/gate-base.txt` | **green**; 10 declared files, blank reason refused (exit 2) |
| 9 | Realtime safety: no allocation/locking on audio-thread paths | program rule (AGENTS.md) | allocation-counter tests (`tests/src/core/AllocationProbe.h`, used by `RecordRingBufferTest` and the two-track capture/recording harnesses) | the paths that have such tests | **partially enforced** — a rule held by tests where they exist, not by a sweeping gate |

## Enforced nowhere — stated, not implied

| Convention | Why it is not a gate |
|---|---|
| **Cognitive complexity ≤ 10** | lizard exposes cyclomatic complexity and NLOC only; it has no cognitive-complexity metric. The KB rule is unimplementable with the tooling that exists here, so it is *not* claimed. |
| **Nesting depth ≤ 3** | lizard cannot threshold ND either. It is **reported** alongside every over-target function in Gate 4 output and reviewed by eye; it is not enforced. |
| **Zero dead code** | cppcheck `--enable=unusedFunction` cannot see callers in other translation units or the test binaries, so over the whole tree it reports ~689 hits dominated by vendored headers, and every hit inside fork sources is a public accessor the tests do call. The KB rule is therefore closed *by evidence*, not by a script — recorded in `tests/QA-GATES.md`. |
| **Strict typing / no `any`** | A TypeScript rule. The C++ analogue — no narrowing conversions at trust boundaries — has no free, reliable enforcer in this tree and is not wired. |
| **Formatting (`.clang-format`) and lint (`.clang-tidy`)** | Both configs are committed and neither is run by a gate or by CI. There is no `clang-format --dry-run -Werror` check and no `run-clang-tidy` job, so formatting is convention-by-imitation. Adding them is a known gap, not a hidden one; `.clang-tidy`'s checks are the closest thing to a house style that exists in writing. |
| **Documentation/commit conventions** | `hermes-agent-skill-authoring`-style rules and the KB creation protocol apply to *writing new KB articles*, not to this repo's source. |

## QA process conventions (KB `qa-gate-protocol`)

These are process rules, not scripts, and they bind whoever changes this repo:

1. **EVIDENCE** — every claim carries reproducible output; no self-reports. A gate that cannot
   show its command output did not run.
2. **INDEPENDENT CHECK** — significant work is re-checked from a fresh context (a different agent,
   or a clean worktree), not by re-reading the author's summary.
3. **FAIL-CLOSED** — a missing baseline, an unparsable tracefile, or an absent tool is an error
   (exit 2), never a pass. `SKIP` is reported as SKIP.
4. **Ratchet discipline** — baselines only move deliberately: `--reanchor "reason"` prints the
   reason, and an unrecorded re-anchor exits 2. Growth is re-anchored with a recorded reason or not
   at all; code is never trimmed to satisfy a metric.

## Scope: the gates watch 99 files, the repo has 1,095

`tests/fork-sources.txt` (99 files) is the fork's own code — the scope for the coverage, mutation,
length, duplication and complexity ratchets. `tests/all-sources.txt` (1,095 files) is every
first-party C/C++ source in the repo: upstream-inherited LMMS code plus everything this fork adds.
Vendored trees (git submodules, `src/3rdparty`, `plugins/NeuralAmp/{rtneural,nam,tests}`,
`plugins/RnnoiseDenoiser/rnnoise`, and the verbatim copies under `tests/reference`) are excluded
on purpose and listed in that file's header.

The whole-tree baselines are being measured (complexity, file length, duplication, dead code, and
whole-tree line coverage) so the same ratchets can be pointed at all 1,095 files without demanding
a retroactive rewrite of upstream code. Until those baselines land, a statement like "the codebase
passes the quality gates" is true **only** of the 99-file fork scope — say which scope you mean.

## Running it

```sh
bash tests/run-all-gates.sh                  # Gates 1, 3, 4, 5, 6, 7, 8
bash tests/run-all-gates.sh --with-coverage  # + Gate 2 (full coverage build)
bash tests/complexity-gate.sh --check        # one gate, CI mode (never writes a baseline)
```

Gate definitions, per-gate measurements and every known limitation live in
[`tests/QA-GATES.md`](https://github.com/KRUZZZZY/zene-studio/blob/main/tests/QA-GATES.md); the
current status of record is [`docs/STATUS.md`](STATUS.md).
