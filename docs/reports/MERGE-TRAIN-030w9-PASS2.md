# Wave 9, SECOND pass — the integration train's record (2026-09-16)

Branch `release/0.3.0` in `zene-030`, from `c6a35c0d6` (the first pass's tip) to this record's own
commit. Five finished wave-9 lane branches merged BY PINNED SHA, then re-measured, built once, and
every newly registered proof run — including the four that had never executed.

## 1 · The merges

| # | pinned sha | lane | forecast | outcome / resolution |
|---|---|---|---|---|
| 1 | `5f3bd1224` | 030/proof-debt | clean | clean (`git merge-tree --write-tree` EXIT=0); docs only: 6 FEATURE-LIST rows (8/17/40/47/63/85) + `docs/reports/PROOF-DEBT-030w9-REPORT.md` |
| 2 | `719c08dbc` | 030/ratchet-decision | CONFLICT `tests/fork-sources.txt` | 5 hunks. Hunk 1 (the recipe's python-pathspec block) = the UNION of both sides' pathspec additions (this train's `ImportDetectionTest\|ScriptDawBindingTest` keep-list fix + the lane's named-pathspec line), sound because the recipe ends in `sort -u`; hunks 2–5 are note blocks where the other side is EMPTY, so HEAD's side is the union. Entries: 626 = both sides' lists united |
| 3 | `5d3fd0139` | 030/coverage-closure | clean | clean; 2 new fork-NEW tests (`ControlSurfaceReferenceTest`, `ModulationLayerProjectRoundTripTest`), their registrations, `ControlStableIds` + `ControlReversibilityTranscript` transcript registrations, the closure report |
| 4 | `1af7ccd7b` | 030/vst3-instrument | CONFLICT `tests/upstream-modifications.txt` | 1 line-vs-line hunk (the `tests/CMakeLists.txt` ledger entry, both sides appending their own reason). Resolved as the line-level union: longest common prefix + both remainders (10354 chars, both reasons verbatim). Every non-comment line still carries a TAB; the file still ends with a newline (LANE-BRIEF §6's two traps) |
| 5 | `53b54fec0` | 030/m1-demo | clean | clean; the M1 transcript re-captured on the tip's own build + the grid's one-line UI absence in both documents |

Not one of the five carries a C++ change that moves an A16 row; `030/vst3-instrument`'s only schema
change is `plugin.list`'s new `vst3` format enum value on the row that command already had.

### The manifests (both derived-with-recipe, never hand-merged)

Measured at each tip, by the manifests' own recipes:

- `tests/all-sources-reproduce.sh` (Gate 9 now RUNS this) — after merge 3 it exited 1 on two `+`
  lines (the two new test sources were missing from the whole-tree list). Fixed by **re-deriving**
  the entry block from the manifest's own `Regenerate with` output (asserted first: the block was
  contiguous, already in the C collation, and carried no entry the recipe does not derive).
- `tests/fork-sources.txt`'s `Verify it` — the same two paths were in the entry list but the awk
  keep-list (a KEEP list: a `tests/` path it does not name is dropped) did not name them, so the
  emission could not derive them; the lane had also appended its two entries at the END of the list
  instead of in collation order. Fixed by adding both names to **every** copy of the awk line and
  rewriting the entry lines to the recipe's own order — same 628 paths, no path added or removed.

**Both recipes print `REPRODUCES` at the fully merged tip** (`all-sources-reproduce.sh` EXIT=0).

## 2 · The A16 histogram, both configurations (probe: `bash tools/dawproject-proof.sh`, part 2)

| configuration | rows | true_inverse | snapshot | irreversible | not_mutating |
|---|---|---|---|---|---|
| release (telemetry on, `LMMS_HAVE_WASM=1`, stem off) | 334 | 158 | 32 | 10 | 134 |
| telemetry off (`lmmsconfig.h` shadowed) | 332 | 158 | 32 | 10 | 132 |

Documented base `{326, 158, 29, 10, 129}` + the telemetry guard's 2 rows = the release figure
exactly, so **the constant is already the merged tip's own measurement and no digit changed**
(recorded in the test's own note). `ctest -R '^ReversibilityContractTest$'` reaches and
**PASSES** `theTableHistogramIsTheDocumentedOne()` (and `everyRegisteredCommandHasAContractRow()`);
the binary then aborts in `irreversibleUndoFailsTypedAndDoesNotUndoAnOlderStep()` on an INHERITED
Lua defect — see §5.

## 3 · The snapshot (never hand-edited)

`tools/mcp-zene-control/zene_control/commands_snapshot.json` regenerated from a LIVE instance of
this build (`0.2.1-alpha.484+6d8ddde`) over `--control-socket`: **334 commands across 52 groups**,
`ids_sha256 7dc75a5ff846671889fdbfc18d35640271771784ad557cbce223997c81f2bab2`. The content diff is
exactly the vst3 merge's: `plugin.list`'s args-schema enum gains `vst3` and its description names
VST3 in the format order.

## 4 · The proofs (`ctest` from `build/tests`, 13 registered tests)

Two runs. **Run 1** (clean environment): 7 passed / 6 failed — every failure was
`the instance exited (code 127)`, i.e. the built binary could not start: `libwasmtime.so` is not
findable because `build/zene` carries NO rpath and the library lives in
`third_party/wasmtime/lib`. The first wave-9 train recorded the same finding in its own §6.

**Run 2** (`LD_LIBRARY_PATH=<tree>/third_party/wasmtime/lib`, the environment the tests need):
**11 of 13 passed** —

| test | first run ever? | result |
|---|---|---|
| ControlDeviceCatalogueTest | no | PASS |
| ControlSurfaceReferenceTest | **YES** | **PASS** |
| ModulationLayerProjectRoundTripTest | **YES** | **FAIL** — `modulator.target_set` refused (`routeArgs(wobbleId, kGainName, 0.75)`), `ModulationLayerProjectRoundTripTest.cpp:187`; 3 of its 4 slots pass |
| SessionFollowTest | no | PASS |
| SessionArrangementRecordTest | no | PASS |
| Vst3InstrumentFixtureProbe / Vst3InstrumentTest / Vst3InstrumentIntegrationTest | no | PASS |
| ControlSessionLaunch | no | PASS (4.51 s) |
| ControlSessionApiProof | no | PASS (5.55 s) |
| ControlGoldenAudio | no | PASS (23.99 s) |
| ControlStableIds | **YES** | **PASS** (7.43 s) |
| ControlReversibilityTranscript | **YES** | **FAIL** — the instance DIES at `script.run`: `Blocked: the server closed the connection without answering` |

`bash tests/test-release-ref-fitness.sh` → **EXIT=0** ("the oracle refuses a deliberately red ref,
passes its base, refuses an unmeasurable leg, and the staging-path policy gate has been seen red").

## 5 · The inherited Lua defect (one cause, three symptoms)

`luabridge::LuaException: what(): No writable member 'apiSurface'` → uncaught → `std::terminate` →
SIGABRT. It is raised by the raw `lua_setfield(L, -2, "apiSurface")` at
**`src/core/ScriptDawBindings.cpp:307`** when a `script.run` registers the DAW bindings into a
`zene` namespace table LuaBridge has already made read-only. Reproduce:
`cd build/tests && QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=<tree>/third_party/wasmtime/lib ./ReversibilityContractTest irreversibleUndoFailsTypedAndDoesNotUndoAnOlderStep` → SIGABRT (134).

It is **NOT this pass's work**: `git diff c6a35c0d6 HEAD` touches no Lua/Script source; the last
commits on `ScriptDawBindings.cpp` are in the base. It surfaces now because the histogram slot that
used to abort first (the state `030/m1-demo` measured) now passes, so the later slots run for the
first time.

## 6 · The gate suite — `bash tests/run-all-gates.sh` → **EXIT=1** (not 3)

Log: `.merge-logs-030w9/gates-w9-pass2.log` (gitignored, on disk).

| gate | train tip | merged tip | note |
|---|---|---|---|
| 1 ctest | FAIL 41/205 | **FAIL 43/209** | **+2 failures, both the never-before-executed coverage-closure proofs** (`ModulationLayerProjectRoundTripTest`, `ControlReversibilityTranscript`). NO previously-failing test changed class and NO previously-passing test newly fails (name-level diff against `.merge-logs-030w9/gates-w9.log`) |
| 2 coverage | SKIP | SKIP | needs `--with-coverage` |
| 3 no-tautology | PASS | PASS | |
| 4 complexity (fork+tools) | FAIL (78 + 13 over) | **FAIL (77 + 13 over)** | one fewer than the train tip; **no new over-target line** |
| 5 mutation | PASS | PASS | |
| 6 upstream-regression | PASS | **FAIL — NEW CLASS** | ONE violation: `docs/reports/VST3-INSTRUMENT-ROW78-EVIDENCE.log  VIOLATION: undeclared change to upstream-inherited code` |
| 7 file-length (fork) | FAIL (11 regressions) | **FAIL** | no new regression line; one line fixed |
| 8 duplication | PASS | PASS | |
| 9 fork-sources | PASS | PASS | both manifests reproduce |
| 10 unregistered-tests | PASS | PASS | |
| 11 evidence | PASS | **FAIL — NEW CLASS** | `[REFUSED] docs/reports/VST3-INSTRUMENT-ROW78-EVIDENCE.log (evidence file type: the output of a run)`; 6611 scanned, 1 refused |

The two new classes have ONE cause between them: the vst3 lane's committed raw evidence file.
Fix-up pass decides (declare it in the ledger and `tests/evidence-gate-exempt.txt`, or move the run
output out of the tree — never weaken the gate).

### The whole-tree scope (gates 4/7/8 `--scope all`, the ADVISORY scope)

Measured at the merged tip, unpiped:

- `bash tests/complexity-gate.sh --check --scope all` → EXIT=1 first, on exactly ONE open line:
  `REGRESSION: new function over target: lmms::control::addressableParameterForModel@252-302@src/core/ControlAutomationSupport.cpp (CCN 12)`
  — inherited from `a36e8b0d4` (030/sample-accurate-automation, row 9), which is AFTER
  `030/ratchet-decision`'s fork point `859883b62`, so the lane that disposed this scope could not
  see it. Disposed the sanctioned way: ONE `--reanchor-file src/core/ControlAutomationSupport.cpp
  "<reason>"` (baseline +1 line, no scope-wide move) with the reason recorded verbatim in
  `tests/QA-GATES.md`'s Complexity disposition table (now 38 paths / 50 lines). **Re-run: EXIT=0**
  (14565 functions, 325 over CCN 10).
- `bash tests/file-length-gate.sh --check --scope all` → **EXIT=0** (1668 sources, 137 over 500).
- `bash tests/duplication-gate.sh --scope all` → **EXIT=0**.

## 7 · The branch sweep (`git branch --list '030/*'` vs the merged tip)

82 branches; exactly **TWO** are ahead of the merged tip and **neither was merged**:

- `030/w20-wasm-abi` (`e292eb694`) — #614 WASM effect ABI docs + conformance suite (8 files).
- `030/audit` (`fa24b1889`) — feature-list fold for the wave-9 rows (docs only); it landed WHILE
  this pass ran.

## 8 · What could not be verified

- The Lua defect's true trigger sequence (first vs second `script.run` in one process): the raw
  registration site is named and the abort is reproducible, but no lane of this pass diagnosed the
  fix, and `script.run` is on the critical path of every socket transcript.
- `ModulationLayerProjectRoundTripTest`'s refusal reason beyond the assertion text: its
  `QVERIFY2` prints the test's own literal, not the command's `errorMessage`, so the engine's
  message ("`'Gain' does not resolve to a parameter: the rack has no chain 1 (it has 1)`" — the
  message its sibling `ControlModulatorCommandsTest` reports) is inference, not measurement here.
- Whether gate 2 (coverage) would pass: not run (`--with-coverage` not passed).
- Any third-party VST3 instrument: none can be installed on this box (the fixture is the witness).
