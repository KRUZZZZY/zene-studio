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
| 5 | Coverage ≥ 85%, never falling | KB ruleset | Gate 2 `tests/coverage-gate.sh` (lcov ratchet, line-weighted) | fork-NEW sources | **ratchet green, aspiration not met and not claimed**: 84.34% (4523/5363) over the **67 of the 139 fork-scope entries that produced a coverage record** (2026-09-12). The headline is a claim about those 67 files, not about the scope — 72 entries produce no record in this configuration (see "Scope" below). The recorded baseline's per-file entries are capped at what each file measured, and the gate stores each entry's instrumented-line count, so a percentage that moves because the *compiled TU set* changed is no longer read as a regression |
| 6 | Mutation kill score ≥ 80% on core | KB ruleset | Gate 5 `tests/mutation-gate.sh` (scoped harness) | one TU, `src/core/RoutingGraph.cpp` | **green**; 27/30 = 90%, scope stated as a limit |
| 7 | All tests pass | repo | Gate 1 (`ctest` from `build/tests`) | `tests/` | **green**; 24/24 (Debug, Qt6) |
| 8 | No *undeclared* divergence in inherited code | repo policy 2026-09-11 | Gate 6 `tests/no-upstream-regression-gate.sh` + `tests/upstream-modifications.txt` ledger | commits since `tests/gate-base.txt` | **green**; 10 declared files, blank reason refused (exit 2) |
| 9 | Realtime safety: no allocation/locking on audio-thread paths | program rule (AGENTS.md) | allocation-counter tests (`tests/src/core/AllocationProbe.h`, used by `RecordRingBufferTest` and the two-track capture/recording harnesses) | the paths that have such tests | **partially enforced** — a rule held by tests where they exist, not by a sweeping gate |

## Enforced nowhere — stated, not implied

| Convention | Why it is not a gate |
|---|---|
| **Cognitive complexity ≤ 10** | lizard exposes cyclomatic complexity and NLOC only; it has no cognitive-complexity metric. The KB rule is unimplementable with the tooling that exists here, so it is *not* claimed. |
| **Nesting depth ≤ 3** | Not enforced **and not measured**: lizard 1.24.0 initialises `max_nesting_depth = 0` and never increments it (`lizard.py:324`; no `nesting_depth +=` anywhere in the file), and a deliberately 6-deep probe reports no ND field. Any "ND 0" figure previously reported in this repo was an uninitialised field. The rule needs a different tool. |
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

## Scope: 139 files by default, 1,120 on demand — and 67 of the 139 are measured

`tests/fork-sources.txt` (**139** entries) is the fork's own code — the default scope for the coverage,
mutation, length, duplication and complexity ratchets. `tests/all-sources.txt` (**1,120** entries) is
every first-party C/C++ source in the repo: upstream-inherited LMMS code plus everything this fork
adds. Vendored trees (git submodules, `src/3rdparty`, `plugins/NeuralAmp/{rtneural,nam,tests}`,
`plugins/RnnoiseDenoiser/rnnoise`, and the verbatim copies under `tests/reference`) are excluded
on purpose and listed in that file's header.

**A scope entry is not a measured file.** Of the 139 fork-scope entries, **67 produce a coverage
record** in the configuration this repo builds, and the other 72 cannot: 29 are sources no binary in
this configuration compiles (7 CLAP — no CLAP SDK; 7 VST3 — no SDK; 5 wasm and 3 Session View — gates
off; 6 stem separation — `WANT_STEM_SPLIT=OFF`; 1 standalone harness), 41 are headers no compiled TU
instantiates code from, and 2 are shell/Python tooling that no C++ instrumentation can ever see.
Gate 2 prints `scope: 139 entries … 67 produced a record … 72 did not` on every run, and
`python3 docs/coverage-green/classify-scope.py <tracefile> <build-dir>` names the reason for each.
**Quote a coverage figure with the file count it was taken over** — "84.34% over 67 files" is a
measurement; "84.34%" alone is not.

Two manifest defects are open and belong to the gate-hygiene lane, not here: the entry counts above
are stale in older text that quoted 99 / 1,095, and `tests/all-sources.txt` is not a superset of
`tests/fork-sources.txt` (see `post-alpha/gate-hygiene`).

**Wired 2026-09-11.** Gates 4, 7 and 8 take `--scope all` (or `GATE_SCOPE=all`), and
`run-all-gates.sh --whole-tree` runs the set over all 1,095 files:

```sh
bash tests/run-all-gates.sh --whole-tree
bash tests/complexity-gate.sh  --scope all --check
bash tests/file-length-gate.sh --scope all --check
bash tests/duplication-gate.sh --scope all
```

Whole-tree baselines live in `tests/complexity-baseline-all.tsv` (272 entries) and
`tests/file-length-baseline-all.tsv` (107 entries) — separate files from the fork baselines so a
fork regression is never shadowed by upstream grandfathering, or vice versa.

Measured 2026-09-11 with the gates' own tools:

| measurement | fork scope (99 files) | whole tree (1,095 files) |
|---|---|---|
| functions scanned / over CCN 10 | 808 / 23 | **8,953 / 273** (max CCN 136, `src/core/main.cpp`) |
| files over 500 lines | 8 | **107** (24 of them over 1,000) |
| duplicated lines (jscpd) | 1.09% | **1.29%** (3.83% of tokens, 114 clones) — PASS < 5% |
| line coverage (lcov) | 76.07% | **32.33%** (23,894/73,918 lines over 919 instrumented files) |
| files that ran zero lines | — | **489** of the 919 instrumented |
| files with no coverage record | — | **154** of 1,095 (feature off in this config, or declaration-only header) |

Whole-tree coverage ratchet: `tests/coverage-baseline-all.tsv` (919 per-file entries) plus a
whole-tree tracefile — `bash tests/coverage-gate.sh <tracefile> tests/coverage-baseline-all.tsv
--check`. Verified both ways: it passes against the tracefile it was built from, and a copy with
one entry deliberately raised fails with `REGRESSION … (5 of 142 covered lines lost)`, exit 1.

Gate 2 and Gate 5 stay fork-scoped in `run-all-gates.sh`: their cost is per-build and their
baselines are per-file. **Dead code is still closed by evidence, not a gate** — whole tree,
cppcheck reports 745 `unusedFunction` hits, 419 inside the scope and 44% of the total in vendored
trees; the top scope file is `include/ConfigManager.h` with 23 hits, all accessors the tests call.

Where the untested mass is: **`src/gui` has 19,820 instrumented lines and 44 of them were hit**
(0.22%) — the unit suite never drives GUI code. Largest single gaps: `PianoRoll.cpp` (2,905 lines,
0 hit), `AutomationEditor.cpp` (1,107, 0), `DataFile.cpp` (1,036 uncovered), `SetupDialog.cpp`
(837, 0), `MainWindow.cpp` (836, 0). `tests/src` is at 97.78% and `include` at 52.24%.

Until a whole-tree run has been made green, a statement like "the codebase passes the gates" is
true only of the fork scope — say which scope you mean, and for coverage say which *measured subset*
of it (67 of its 139 entries; see the scope note above).

## Running it

```sh
bash tests/run-all-gates.sh                  # Gates 1, 3, 4, 5, 6, 7, 8
bash tests/run-all-gates.sh --with-coverage  # + Gate 2 (full coverage build)
bash tests/complexity-gate.sh --check        # one gate, CI mode (never writes a baseline)
```

Gate definitions, per-gate measurements and every known limitation live in
[`tests/QA-GATES.md`](https://github.com/KRUZZZZY/zene-studio/blob/main/tests/QA-GATES.md); the
current status of record is [`docs/STATUS.md`](STATUS.md).
