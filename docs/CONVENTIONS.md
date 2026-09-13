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
| 1 | Cyclomatic complexity ≤ 10 per method | KB ruleset | Gate 4 `tests/complexity-gate.sh` (lizard) | fork-NEW sources (244 files) | **green**; 808 functions, 23 over target all grandfathered; ratchet |
| 2 | Per-file ≤ 500 lines | KB ruleset | Gate 7 `tests/file-length-gate.sh` | fork-NEW sources | **green**; 8 files over 500 grandfathered; ratchet with recorded `--reanchor` |
| 3 | Token duplication < 5% | KB ruleset | Gate 8 `tests/duplication-gate.sh` (jscpd, headers included) | fork-NEW sources | **green**; 1.09% duplicated lines |
| 4 | No tautological tests | KB ruleset | Gate 3 `tests/no-tautology-gate.sh` | every registered QTest file | **green**; 21 files, 1,744 slots, 1,477 assertions, 0 tautologies |
| 5 | Coverage ≥ 85%, never falling | KB ruleset | Gate 2 `tests/coverage-gate.sh` (lcov ratchet, line-weighted) | fork-NEW sources | **measured on the release tree 2026-09-12, before merge train 3F grew the fork scope from 175 to 244 entries: 81.60% (7113/8717) over the 119 of the 175 fork-scope entries the scope then held that produced a record** in the fuller configuration (VST3 SDK and CLAP provisioned, `WANT_VST3_TEST_INSTRUMENT=ON`). The **ratchet scope** — the gate's per-file baseline, `tests/coverage-baseline.tsv` — is at **85.77%**, above the aspiration. The headline is a claim about those 119 files, not about the scope: 56 entries produced no record in that capture and the gate prints that split itself. The gate stores each entry's instrumented-line count, so a percentage that moves because the *compiled TU set* changed is read as `denominator-moved` rather than as a regression. **Gate 2 currently exits 1 on its 50% entry floor with 15 new files below it** — ten dialogs/views/browser files a headless binary cannot construct, two telemetry files inert by design, and three genuinely untested (including the VST3 *effect* module's class, because the fixture is an *instrument*). Documented in `docs/KNOWN-LIMITATIONS.md`; exemptions are deliberately not being written at the tag. |
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

## Scope: 244 files by default, 1,263 on demand — and the coverage capture predates the last merge

`tests/fork-sources.txt` (**244** entries) is the fork's own code — the default scope for the coverage,
mutation, length, duplication and complexity ratchets. `tests/all-sources.txt` (**1,263** entries) is
every first-party C/C++ source in the repo: upstream-inherited LMMS code plus everything this fork
adds. Vendored trees (git submodules, `src/3rdparty`, `plugins/NeuralAmp/{rtneural,nam,tests}`,
`plugins/RnnoiseDenoiser/rnnoise`, and the verbatim copies under `tests/reference`) are excluded
on purpose and listed in that file's header. Both figures are what the files hold on this tree:
`bash tests/fork-sources-gate.sh` prints `244 fork-NEW … 34 tooling` on every run, and the entry list in
each file is the byte-exact output of its own documented regeneration command.

**A scope entry is not a measured file.** The coverage figure of record was measured on **2026-09-12**,
when the fork scope held **175** entries: **119** produced a coverage record in the fuller configuration
(SDK and CLAP provisioned, `WANT_VST3_TEST_INSTRUMENT=ON`) and the other **56** did not. The classifier
names each one's reason —
`python3 tests/coverage-green/classify-scope.py <tracefile> <build-dir>` — and its categories are
properties of the *configuration*, not of the entry count: entries that instantiate no compiled
translation unit (headers), entries behind `WANT_STEM_SPLIT=OFF`, entries needing `wasmtime`, entries
behind `WANT_SESSION_VIEW=OFF`, entries with no object in this build, and one plugin module built as a
loadable `.so` that no test binary links. **Merge train 3F has since grown the fork scope from 175 to 244
entries** — it merged the control-surface modules and their tests, which are compiled and have tests — so
**the measured count over the current scope is not yet recorded**, and "119 of the 175" must not be read
as a ratio over all 244. Gate 2 prints `scope: N entries … M produced a record … K did not` on every run,
so the split is never lost. **Quote a coverage figure with the file count it was taken over** — "81.60%
over 119 files" is a measurement; "81.60%" alone is not.

**Every per-gate measurement below is dated 2026-09-11** and was taken when the fork scope held 99 files
and the whole-tree scope 1,095; both scope files are larger today (244 and 1,263), which is why the dated
tables below carry their own scope sizes.

**The two manifest defects this section used to record as open are fixed** (they belonged to the
gate-hygiene lane, which has merged): the entry counts above now match the files, and all three
manifests reproduce byte-exactly from their own documented commands — `ALL-REPRODUCE` on
fork-sources, all-sources and tools at every merge of trains 3A-3D. Older text quoting **99 / 1,095**,
and the conventions pass before this one quoting **175 / 1,209**, is stale; the current figures are the
ones in this paragraph.

**Wired 2026-09-11.** Gates 4, 7 and 8 take `--scope all` (or `GATE_SCOPE=all`), and
`run-all-gates.sh --whole-tree` runs the set over all 1,263 files:

```sh
bash tests/run-all-gates.sh --whole-tree
bash tests/complexity-gate.sh  --scope all --check
bash tests/file-length-gate.sh --scope all --check
bash tests/duplication-gate.sh --scope all
```

Whole-tree baselines live in `tests/complexity-baseline-all.tsv` (**274** entries) and
`tests/file-length-baseline-all.tsv` (**112** entries) — separate files from the fork baselines so a
fork regression is never shadowed by upstream grandfathering, or vice versa. Those are the counts the
files hold on this tree (checked 2026-09-13); the 2026-09-11 table below carries the numbers of its
own date.

Measured 2026-09-11 with the gates' own tools:

| measurement | fork scope (99 files at this 2026-09-11 measurement) | whole tree (1,095 files at that measurement) |
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
of it (the capture measured 119 of the 175 entries the scope then held; see the scope note above).

**The whole-tree scope is NOT green at the 0.2.1-alpha tip** (checked 2026-09-13,
`post-alpha/integration` @ `5565b4b1b`): `bash tests/complexity-gate.sh --check --scope all` exits **1**
with **28** regression lines and `bash tests/file-length-gate.sh --check --scope all` exits **1** with
**34** — 62 lines over 35 files. The fork and tools scopes are green on both gates. The two
disagree because the fork manifest holds 244 files and the all manifest 1,263, and **55 of the 62
failing lines are in files `tests/fork-sources.txt` does not list at all**; the rest are in files both
scopes list, where the fork-scope baseline entry is current and the all-scope one is stale. So it is a
stale whole-tree baseline, not product code the fork ratchets missed.
**The smallest honest action is a decision, not a silence:** one recorded
`--reanchor-file <path> "<reason>"` per failing file on the all-scope baseline — the reason citing
`tests/upstream-modifications.txt` where the growth is in inherited code — **or** leaving the scope red
and recorded as the owner's call (the planned GATE-1/GATE-2 rows in the program's planned-work master
list). `tests/QA-GATES.md` "Scope policy" carries the per-gate detail; nothing here re-anchors a
baseline, and no baseline file is edited by this document.

## Running it

```sh
bash tests/run-all-gates.sh                  # Gates 1, 3, 4, 5, 6, 7, 8
bash tests/run-all-gates.sh --with-coverage  # + Gate 2 (full coverage build)
bash tests/complexity-gate.sh --check        # one gate, CI mode (never writes a baseline)
```

Gate definitions, per-gate measurements and every known limitation live in
[`tests/QA-GATES.md`](https://github.com/KRUZZZZY/zene-studio/blob/main/tests/QA-GATES.md); the
current status of record is [`docs/STATUS.md`](STATUS.md).
