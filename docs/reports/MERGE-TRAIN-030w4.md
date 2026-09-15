# WAVE-4 INTEGRATION TRAIN — state at the merged tip

> **Location (2026-09-15, wave-5 train).** Moved from `.merge-logs-030w4/REPORT-030w4.md`
> to `docs/reports/` because Gate 6 classifies every path changed since its base and a
> log directory at the repository root matches no class it allows (`.merge-logs-030w4/*.py`,
> `*.sh` and `*.txt` were reported as "undeclared change to upstream-inherited code",
> 14 of them). The record's own text is unchanged, and the working logs it names —
> `.merge-logs-030w4/…` — are all still on this box at that path; the directory is now
> untracked (`.gitignore: /.merge-logs-*/`), the way the w1/w2/w3 trains' directories
> always were. Nothing was deleted: only the record is in git, at the path above.

**Train:** eight verified lane branches merged into `release/0.3.0` in `zene-030`, with
VERIFIED UNION merges, the FIXUP section-5a defect fixed with a test, the A16 histogram
constant re-measured, the MCP commands snapshot regenerated from a live instance, the
merged branches' registered proofs built and RUN, and the gate suite re-run.

**Tip:** see `git log --oneline -1`. **Log dir:** `.merge-logs-030w4/` (every command's
output, unpiped exit codes included).

## 1 · The merges, in order (one `--no-ff` merge commit each)

| # | branch | tip SHA | merge commit | conflicted files | union proof |
|---|---|---|---|---|---|
| 1 | `030/lua-daw-binding` | `6634d714f` | `190f1dbb8` | 5 | `counts-lua-daw-binding-*.txt` |
| 2 | `030/mcp-coverage` | `4f828fd8b` | `c3aa518a7` | 3 | `counts-mcp-coverage-*.txt` |
| 3 | `030/patcher-graph` | `d80488760` | `d7e62c92e` | 9 | `counts-patcher-graph-*.txt` |
| 4 | `030/revision-timeline` | `607c4e6bd` | `645a22d85` | 9 | `counts-revision-timeline-*.txt` |
| 5 | `030/import-detection` | `e93c57c5d` | `19b5adb85` | 16 | `counts-import-detection-*.txt` |
| 6 | `030/safe-start` | `d14289095` | `535cbb380` | 7 | `counts-safe-start-*.txt` |
| 7 | `030/sample-accurate-automation` | `cc39c6c5b` | `29decdbbb` | 9 | `counts-sample-accurate-automation-*.txt` (+ the 5a fix) |
| 8 | `030/audit` | `c967f8f24` | `4a4c49139` | 2 | `counts-audit-*.txt` |

Every conflicted append-only registry was resolved as a **union of entries** by
`.merge-logs-030w4/union-resolve.py` (rules: entry-union, ledger-union, section-union,
preamble-union, scope-union, join-union, keep-ours-for-a-measurement, last-writer for a
lane-owned scratch file), then checked by `verify-merge.py` (no added line from either side
missing from the merged tree, on the staged tree) plus a marker/duplicate/trailing-newline
sweep. Per-file before/after counts are in the `counts-<lane>-*.txt` triples and in each
merge commit message.

**Not merged (live continuations, by instruction):** `030/session-completion`,
`030/doc5-specs`. `030/w20-wasm-abi` was already content-merged.

## 2 · The section-5a defect (fixed in merge 7's own commit)

`AutomationMode::Off` was honoured by the per-tick apply pass (`Song::processAutomations`)
but NOT by the sample-accurate ramp publish (`Song::buildAutomationRamps`, `Song.cpp:1164`),
so an Off control followed its curve at sample precision.

* fix: `src/core/Song.cpp` — the publish is gated on
  `model->automationMode() != AutomatableModel::AutomationMode::Off`.
* test: `tests/src/core/SampleAccurateAutomationTest.cpp` —
  `anOffControlDoesNotFollowItsSampleAccurateRamp()`: half 1 requires
  `AutomatableModel::automationRamp() == nullptr` for **every** rendered period under Off
  and no movement inside a block; half 2 is the control (same tree/curve/clip, mode Read,
  the ramp must be published every period), so a ramp path switched off entirely fails too.
* **EXECUTION STATUS: the slot does not run.** The whole file fails its fixture in this
  build: all six slots stop at `channelGainParameter(...) -> FALSE` ("the channel's fx-0
  effect exposes no parameter named Gain"), the lane's own five slots included. So the 5a
  gate is IN PLACE but UNVERIFIED BY EXECUTION — see §5.

## 3 · The A16 histogram constant (a measurement, re-taken)

| | rows | true_inverse | snapshot | irreversible | not_mutating |
|---|---|---|---|---|---|
| before (as the file carried it, wave-3 tip) | 285 | 152 | 21 | 6 | 106 |
| **after (measured on the merged tree)** | **314** | **156** | **27** | **10** | **121** |

Measured with `bash .merge-logs-030w4/measure-a16.sh` (the A16 probe from
`tools/dawproject-proof.sh`, part 2, against this tree's `ControlReversibilityTable*.cpp`
with this build's flags) → `MEASURED rows=322 true_inverse=156 snapshot=30 irreversible=10
not_mutating=126`, minus the guards' own additions for telemetry (-2/-2) and wasm (-6/-3/-3).
The four classes sum to 314 exactly. **Corroborated by the built test**:
`ReversibilityContractTest::theTableHistogramIsTheDocumentedOne()` PASSES.

## 4 · The MCP commands snapshot (derived — regenerated once, at the merge)

* before: 283 commands, captured_at `2026-09-15T12:56:22Z`
* **after: 322 commands, captured_at `2026-09-15T20:50:04Z`, 52 groups, version
  `0.2.1-alpha.368+48af0d2`**, `ids_sha256 021bc354…34d5` — commit `5d6fac809`.

## 5 · Proof run: what was built and what actually ran

The tree builds: `make -j2` in `build/` → **BUILD_EXIT=0** (`build-merged4.log`); the binary
is `build/zene` (not `build/bin/zene`) and needs
`LD_LIBRARY_PATH=<root>/third_party/wasmtime/lib` for `libwasmtime.so`.

**ENVIRONMENT FINDING (measured twice, this is why the two counts differ).** The suite run
without that library path reports **78 of 196 failed**; the same suite with it set reports
**41 of 196 failed**. 37 failures were the loader, not the code: any test that spawns
`build/zene` or dlopens a module dies without `libwasmtime.so` on the path. The gated run
(`run-all-gates.sh`, which inherits the exported path) is the one to believe: **41 failures**.

**The eight tests the merged branches registered** (found by diffing `tests/CMakeLists.txt`
against `3fae5a5e1`: 3 `add_test(NAME)` + 5 `LMMS_TESTS` sources), run from `build/tests`:

| ctest | exit | verdict (with the library path) |
|---|---|---|
| `LuaApiSurface` | 0 | PASS |
| `RevisionTimelineTest` | 0 | PASS |
| `ControlMcpGroupCoverage` | 8 | FAIL |
| `ControlDetectCommands` | 8 | FAIL |
| `PatcherCommandsTest` | 8 | FAIL (`the engine is not addressable yet [engine_starting]`) |
| `SafeStartTest` | 8 | FAIL (3 assertion mismatches: predicate, marker cap) |
| `SafeStartLoadPathTest` | 8 | FAIL (**SEGFAULT**) |
| `SampleAccurateAutomationTest` | 8 | FAIL (fixture: no `fx-0/Gain`; the 5a slot included) |

**Full suite:** 196 tests registered in `build/tests`, 41 fail, `ctest` EXIT=8, 321 s
(`ctest-suite.log` = the 78-failure no-library run; the gated run's list is in
`gates-merged4.log`; `failed-tests.txt`). This is the FIRST full test run this tree has ever
had (no binary existed before). The fix-up list's ctest row (4 of 176) is the *previous*
tip's, not this one's: **re-base it on this list**.

## 6 · Gate suite

`bash tests/run-all-gates.sh` (enforced scope: fork + tools; `LD_LIBRARY_PATH` set) →
**RESULT: FAIL, exit 1** (`gates-merged4.log`):

| gate | result | vs the previous FAIL (fix-up list) |
|---|---|---|
| 1 ctest | **FAIL** | 41 of 196 failed (was 4 of 176) |
| 2 coverage | SKIP | same (needs `--with-coverage`) |
| 3 no-tautology | PASS | same |
| 4 complexity | **FAIL** | 5586 fork functions, **59 over CCN 10, 39 not grandfathered**; +13 in the tools scope (was 37 over / 7 new) |
| 5 mutation | PASS | kill score **23/26 = 88.5 %** ≥ 80 % |
| 6 upstream-regression | **FAIL** | **1 violation: `docs/lua-api-surface.txt`** (introduced by merge 1) |
| 7 file-length | **FAIL** | 7 regressions (was 5) |
| 8 duplication | PASS | 0.00 % duplicated lines |
| 9 fork-sources | PASS | 597 fork-NEW, 1097 inherited, 39 tooling, 0 stale |
| 10 unregistered-tests | PASS | 162 sources scanned, 160 registered, 2 declared-not-built |
| 11 evidence | PASS | 6537 files scanned, 0 refused |

**Gate 6's violation, stated precisely (NOT weakened).** `docs/lua-api-surface.txt` is a
fork-NEW generated artefact (added by `030/lua-daw-binding`, written by
`tests/lua-api-surface.py`, ratcheted by the `LuaApiSurface` ctest). The gate's classifier
allows `*.md` under docs/, `tests/*`, build files, `tools/*` and paths listed in
`tests/fork-sources.txt`; a `docs/*.txt` file matches none, so it falls to "undeclared
change to upstream-inherited code" — the wrong diagnosis for a fork-NEW file. Its own
message suggests declaring it in `tests/upstream-modifications.txt`, and that file's header
forbids exactly that ("fork-authored paths do not belong here"). The two honest fixes are
(i) rename the artefact to a `*.md` path and update the ctest/scripts that read it, or
(ii) extend the gate's docs rule to `docs/*.txt` — **a gate change, which this train does
not make**. Left red, named here.

## 7 · Defects this train found and fixed while merging (each its own commit)

| commit | what |
|---|---|
| `bdaf796c6` | the A16 join statement re-emitted — a lane comment inside the id list had commented out the closing `})` (`error: expected unqualified-id before ')' token`) |
| `48af0d23a` | the A16 histogram constant re-measured (see §3) |
| `4bfc5ccee` | `tests/CMakeLists.txt` rebuilt: three lanes' mid-list insertions had been tail-appended (`Parse error … 'src/core/ScriptDawBindingTest.cpp'`) and a first anchor-repair crossed four if/endif pairs |
| `6c1aed2f6` | `ControlCommandsPatcherEdit.cpp`'s unused helper `[[maybe_unused]]` (`-Werror=unused-function`) |
| `5d6fac809` | the MCP snapshot regenerated (see §4) |

## 8 · OPERATIONAL WARNING for the next runner (cost this train real time)

`tests/mutation-gate.sh` **mutates tracked sources and rebuilds in the same `build/` tree**.
Running it concurrently with `make -j2` corrupted `RoutingGraph.cpp.o` (the linker reported
`invalid string offset … for section '.strtab'`, `unsupported relocation type 0x3a`,
`relocation truncated to fit`) and, when the gate was killed, left
`src/core/RoutingGraph.cpp` MUTATED in the working tree. The train restored the source
(`git checkout --`), deleted the raced object, and rebuilt. **Run the gate suite and any
build serially, never together.**

## 9 · What is left (precise)

1. **Gate 1 (41 ctest failures)** on the merged tip — `gates-merged4.log` has every failure
   verbatim. Classes seen: assertion mismatches in the merged tests
   (`ControlRegistryTest::mixerSetPanRefusesTyped`, `ClipSerialisationTest`,
   `ModulationLayerTest`, `ClipLinkTest`'s `note-1`), 7 subprocess aborts (the Script*
   family and `ReversibilityContractTest::irreversibleUndoFailsTypedAndDoesNotUndoAnOlderStep`),
   1 SEGFAULT (`SafeStartLoadPathTest`).
2. **The 5a test cannot execute** until `SampleAccurateAutomationTest`'s fixture resolves
   `fx-0/Gain` (all six slots in that file stop there, the lane's own five included).
   The gate and its control half are in the tree; the execution proof is owed.
3. **Gate 6's one violation** — one of the two fixes in §6, by the parent's ruling.
4. **Gates 4 and 7** — 52 new over-target functions and 7 over-length files; split or
   grandfather-with-a-reason at the merge point, never a blanket `--reanchor`.
5. **The A16 paragraph in `docs/RELEASE-NOTES-v0.3.0-alpha.md`** still carries the wave-3
   train's figures (284 rows); the constant in the test now says 314 base / 322 measured.
   The two are meant to agree — re-take the paragraph (the test's comment block names it).
6. **Whole-tree scope** (`--whole-tree`) and **coverage** were not measured by this run.

