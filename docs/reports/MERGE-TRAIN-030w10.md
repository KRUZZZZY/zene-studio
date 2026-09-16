# MERGE TRAIN — wave 10 (`release/0.3.0`, 2026-09-16)

The integration train for the four finished wave-10 lanes. Base `d7a402041` (the wave-9
second-pass train's record). **Nothing was pushed**; the train owns only
`zene-030` (`release/0.3.0`) and merged the lane BRANCHES by their pinned SHAs, never
entering another worktree.

## 1 · The four merges, forecast first

Every merge was forecast with `git merge-tree --write-tree HEAD <sha>` BEFORE it was run, and
each merged diff's hunks were read before the commit.

| # | lane | pinned SHA | forecast vs HEAD | result |
|---|---|---|---|---|
| 1 | `030/repo2-gate` | `b5fc50a43` | clean (EXIT=0) | clean merge `b79f912af` |
| 2 | `030/rt-safety` | `21a47e489` | ONE conflict: `.github/workflows/quality-gates.yml` (both lanes insert a CI step after the Gate 11 step) | union; `7a03d716e` |
| 3 | `030/a16-count` | `aae773611` | ONE conflict: `tests/upstream-modifications.txt` (both append a reason to the same `tests/CMakeLists.txt` ledger line) | union; `77e3d1cd8` |
| 4 | `030/clap-instrument` | `40e029f1e` | TWO conflicts: `tests/src/core/ReversibilityContractTest.cpp` (SEMANTIC) and `tests/upstream-modifications.txt` (four ledger lines) | both resolved by hand; `a86eb0d51` |

**Resolution 1 — the CI step (union).** repo2's `Gate 11 control - … --self-test` step and
rt-safety's `Gate 12 - real-time safety` step both belong; both were kept, in that order, after
the Gate 11 step. No other hunk of either workflow edit was touched.

**Resolution 2 — the ledger (union, ×5 lines total).** The ledger is a union-by-append file: for
every conflicting entry the train kept our side's line and appended the other lane's reason to
it, so all eight entries carry BOTH lanes' reasons, every line keeps its TAB, and the file ends
with a newline (the two traps LANE-BRIEF §6 names). The file is now 460 entries.

**Resolution 3 — `ReversibilityContractTest.cpp` (SEMANTIC, resolved to the a16 design).** The
clap lane moved the OLD hand-kept constant one row (`{326,158,29,10,129}` →
`{327,158,29,10,130}`, with a comment saying it was arithmetic rather than a measurement); the
a16 lane had removed that constant entirely and replaced it with a derivation that READS the one
published figure out of `docs/RELEASE-NOTES-v0.3.0-alpha.md` on every run. A constant cannot be
"moved" in a file that no longer has one, so the merged file is byte-identical to a16's version
(`git show HEAD:… | diff -q …` → identical), and the clap row's `+1 not_mutating` lands where
the design requires it: in the re-taken published block (§2). Nothing was dropped silently —
clap's intent is recorded in its lane report, its row table, its manifests, and the re-measured
figure.

Nothing else needed a hand: `ControlReversibilityTable.cpp` carries the a16 retirement note AND
the clap join entry, `tests/CMakeLists.txt` carries the rtsafety ctests, the `A16_HISTOGRAM_DOC`
define and the CLAP fixture/target wiring, and the release notes carry both the A16 block and
the CLAP section.

## 2 · A16: re-measured at the merged tip, and the ONE published figure re-taken

The published block was **stale by one row** the moment the clap lane landed: its
`plugin.host_notes` row is unconditional, so the release configuration's figure moves.

```
$ bash tools/dawproject-proof.sh            # part 1 EXIT=0, part 2 EXIT=0
BEFORE (block as merged):  MEASURED rows=334 true_inverse=158 snapshot=32 irreversible=10 not_mutating=134
AT THE MERGED TIP:         MEASURED rows=335 true_inverse=158 snapshot=32 irreversible=10 not_mutating=135
                           DECLARED rows=335 entries=335 duplicates=0
```

`docs/RELEASE-NOTES-v0.3.0-alpha.md`'s `A16-HISTOGRAM` block was re-taken with that line
(markers preserved), the prose beside it names the `+1 not_mutating` and its row file, and the
option deltas are unchanged (the new row carries no compile switch, so no option delta moves).
The ctest failed by design until this was done — that is the card's own contract.

```
$ cd build/tests && ./ReversibilityContractTest theTableHistogramIsTheDocumentedOne
PASS   : ReversibilityContractTest::theTableHistogramIsTheDocumentedOne()
Totals: 3 passed, 0 failed, 0 skipped
HIST_SLOT_EXIT=0
```

**The full binary still aborts LATER, on the KNOWN inherited defect** (not this train's, not
fixed): six slots pass —
`initTestCase`, `everyRegisteredCommandHasAContractRow`, `theTableHistogramIsTheDocumentedOne`,
`theOnlyUnclassedMutatingCommandsAreTheRefusals`, `transactionRecordStatesItsBounds`,
`coalescingIsDeclaredOnlyForCommandsWithALiveCheckpoint` — then
`Received signal 6 (SIGABRT)`, `what(): No writable member 'apiSurface'` in
`irreversibleUndoFailsTypedAndDoesNotUndoAnOlderStep` (`src/core/ScriptDawBindings.cpp:307`),
`REV_FULL_EXIT=134`. Identical to what the a16 lane recorded on its own branch, so the merges
neither caused nor cured it.

## 3 · Build — including the never-linked `clapinstrument`

The build tree `zene-030/build` was KEPT (the fix-up pass reuses it); it was already configured
`RelWithDebInfo / WANT_QT6=ON / WANT_CLAP=ON / WANT_WASM=ON (wasmtime found) /
ZENE_TELEMETRY_ENABLED / LMMS_HAVE_SESSION_VIEW / USE_WERROR=ON / USE_COMPILE_CACHE=ON`, with
`LMMS_CLAP_PATH=build/clap` (CLAP 1.2.10 MIT headers already in the tree, so the lane's
`cp -r …/build-ci/clap build/clap` hint was not needed).

```
$ cmake -S . -B build                       CONFIGURE_EXIT=0   (CLAP 1.2.10 headers found)
$ cmake --build build -j4 --target zene clapinstrument clapeffect \
      clap-test-instrument clap-test-gain ClapHostTest ControlDeviceCatalogueTest \
      ReversibilityContractTest Vst3InstrumentFixtureProbe Vst3InstrumentTest \
      Vst3InstrumentIntegrationTest          BUILD_EXIT=0
  Built target zene · clapinstrument · clapeffect · clap-test-instrument · ClapHostTest ·
              ControlDeviceCatalogueTest · ReversibilityContractTest · Vst3Instrument*
```

`clapinstrument` and `ControlDeviceCatalogueTest` had **never been linked before** (the clap
lane's window ended at `-fsyntax-only`); both link here.

## 4 · Proofs, every exit code unpiped (`cmd > log 2>&1; echo EXIT=$?`)

Run from `build/tests` with `QT_QPA_PLATFORM=offscreen` and the wasmtime lib on
`LD_LIBRARY_PATH`.

| proof | command | exit | result |
|---|---|---|---|
| CLAP host + note path | `./ClapHostTest` | **0** | `Totals: 14 passed, 0 failed, 0 skipped` — incl. `testInstrumentNotePath`: *256-frame request into a 64-frame block: 4 plug-in calls, 1 note(s) delivered, audio level 0.250000* |
| rt-safety controls | `ctest -R 'RtSafety'` | **0** | `2/2 Passed` (`RtSafetySweep` 0.28 s, `RtSafetySelfTest` 0.16 s) |
| A16 histogram slot | `./ReversibilityContractTest theTableHistogramIsTheDocumentedOne` | **0** | passes against the re-taken published block |
| VST3 instrument trio | `ctest -R '^Vst3Instrument'` | **0** | `3/3 Passed` |
| **CLAP catalogue half (FIRST EVER RUN)** | `./ControlDeviceCatalogueTest` | **1** | **FAILS — see §5.** `Totals: 9 passed, 1 failed` |

## 5 · The one NEW failure: the CLAP catalogue case, and its exact cause

```
FAIL!  : ControlDeviceCatalogueTest::clapInstrumentListsAndLoadsThroughTheSurface()
         'loaded.ok' returned FALSE.
         ('org.lmms.test.clap-instrument' is not a device id of the form dev-<n>)
Totals: 9 passed, 1 failed, 0 skipped, 0 blacklisted, 1613ms     EXIT=1
```

The first-ever run of row 79's socket half finds a real defect, and it is a **key collision, not
a missing feature**: `src/core/ControlDeviceCatalogue.cpp`'s `controlDeviceJson()` sets
`out["id"] = control::deviceId(index)` — the `dev-<n>` catalogue id, which is the ONLY id
`plugin.load` accepts (`ControlDeviceCatalogue.cpp:150`) — and then the clap lane's new branch
runs `out.insert("id", entry.name)`, **overwriting it** with the plug-in's own CLAP id. So for
`format: "clap"` entries the socket publishes an `id` no client can load, and the catalogue id is
not published at all; the VST3/LADSPA/LV2 entries keep theirs, which is why the VST3 case in the
same binary passes. The test was written against the same collision (`QCOMPARE(id, name)`), so it
takes the clap id and hands it to `plugin.load`.

Two candidate fixes, both small and both a decision about the socket contract rather than a
bug-fix in the dark (for the fix-up pass, NOT applied by the train):

1. **Preferred** — mirror the other formats: keep `id` = `dev-<n>`, publish the plug-in's own
   CLAP id under its own key (the LADSPA `label` / LV2 `uri` / VST3 `class` precedent, e.g.
   `clap_id`), and point the test's `plugin.load` at the catalogue id. The release-notes sentence
   "`(file, id)` — module path plus the plug-in's own CLAP id" then names the new key.
2. Or accept the CLAP id in `plugin.load`'s `device` argument for clap entries — but that makes
   the `device` argument's contract format-dependent, which is what `dev-<n>` exists to avoid.

Everything else the case asserts held: the fixture class IS listed (`format: "clap"`,
`kind: "instrument"`, `loadable: true`, `file` = the module), so discovery works; only the load
half is unreachable.

## 6 · Gates (`bash tests/run-all-gates.sh --no-mutation`)

Per-gate measurements taken individually and unpiped, then the suite (`/tmp/train10-suite.log`):

| gate | exit | what it says on the merged tip |
|---|---|---|
| 6 upstream-regression | **0** | `PASS … (422 changed path(s) declared; the ledger holds 460 entries)` |
| 11 evidence | **0** | `6635 file(s) scanned, 0 refused (cap 1048576 bytes, 4 exemption(s))` |
| 12 rt-safety | **0** | `27 declared pairs, 918 region lines, 4 hits in 3 keys`, all inherited |
| 9 fork-sources | **0** | `643 fork-NEW, 1104 inherited, 40 tooling`, 0 stale; the file's own "Verify it" recipe → `REPRODUCES` |
| 9 all-sources reproduce | **0** | `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` |
| 10 unregistered tests | **0** | `169 scanned (registered: 167, declared-not-built: 2, helpers: 4)` |
| 8 duplication | **0** | `0.54% (budget 5%)` |
| 3 no-tautology | **0** | `PASS` |
| 4 complexity | 1 | `6005 functions; 77 exceed CCN 10`; **57 regression lines, none naming a wave-10 path** |
| 7 file-length | 1 | `642 sources; 21 exceed 500 lines`; the same 12-path set as the base, three numbers moved |
| 1 ctest | 1 | **44 of 211 failed** — see below |

```
$ bash tests/run-all-gates.sh --no-mutation
gate   name                     result
1      ctest                    FAIL       4      complexity               FAIL
2      coverage                 SKIP       5      mutation                 SKIP
3      no-tautology             PASS       6      upstream-regression      PASS
7      file-length              FAIL       8      duplication              PASS
9      fork-sources             PASS      10      unregistered-tests       PASS
11     evidence                 PASS      12      rt-safety                PASS
scope: fork (tests/fork-sources.txt) + tools — the enforced scope, the same one CI's static-gates job runs
RESULT: FAIL — see the failing gate above            SUITE_EXIT=1
```

`SUITE_EXIT=1` is the expected shape: gates 4/7 are red on inherited classes and gate 1 carries
the inherited aborts, so the "pass-with-skips" exit 3 is only reachable after the fix-up pass.
**What this train changed in gate 1, name by name** (the same method the wave-9 train used) —
`ctest` at the base `d7a402041` was **43 failed of 209** (`.merge-logs-030w9/gates-w9-pass2.log`):

```
NAME-LEVEL DIFF (base 43/209  ->  merged tip 44/211)
  NEW failures : ControlDeviceCatalogueTest          <-- the CLAP case of §5, first-ever run
  FIXED        : (none)
  CLASS CHANGES: (none — all 43 keep Failed / Subprocess aborted / SEGFAULT)
  tests added  : RtSafetySweep, RtSafetySelfTest (both PASS)
```

So the train introduced exactly ONE new ctest red, it is diagnosed in §5, and nothing that
passed at the base stopped passing.

Suite summary (`SUITE_EXIT`) and the exact gate rows are in the train log; the two dispositions
below are what the merges ADDED to the gates.

### Dispositions — and there were no scope-wide moves

| path | gate(s) | disposition |
|---|---|---|
| `docs/reports/CLAP-INSTRUMENT-ROW79-EVIDENCE.log` | 11 (evidence suffix) + 6 (`docs/` admits `*.md` only) | **deleted on `CP-1`'s terms**: sha256 `73eefc7681c0dbaacf5d9cd6aa17e3f00a4bc829e27a79603377e7f74799b17c`, 6,452 bytes appended to `tests/evidence-manifest.tsv` (now **1,397** entries) with a block header; the lane's prose report stays; the cases it recorded are registered tests. Second live catch of the gate, exactly the wave-9 VST3 log's treatment. |
| `plugins/ClapInstrument/logo.png` | 6 (a NEW binary asset under `plugins/`) | **declared** in `tests/upstream-modifications.txt` with its reason (the module's own logo, bytes identical to `plugins/ClapEffect/logo.png`, loaded by `PluginPixmapLoader("logo")`; neither scope manifest's recipe can derive a `.png`) |

No `--reanchor-file` and no `--reanchor` was taken: gate 4 and gate 7 have **no new line** (§
`tests/QA-GATES.md` Gate 4/Gate 7), and the one tracked file-length growth
(`plugins/ClapEffect/ClapHost.cpp` 953 → 1158) is the fix-up pass's, as the dispatch says. Both
dispositions are recorded in `tests/QA-GATES.md`.

## 7 · Sweep — other unmerged `030/*` branches (report only, nothing merged)

Every other `030/*` branch is an ancestor of the merged tip except two:

* `030/audit` (`fa24b1889`) — one commit ahead: a `docs/FEATURE-LIST-0.3.0.md` fold of the
  wave-9 lanes' rows plus `docs/reports/WAVE9-FOLD-030-audit.md`. That worktree is the parent's.
* `030/w20-wasm-abi` (`e292eb694`) — one commit ahead, **and its work is already in the tree**
  under a different SHA (`5fba581a1`, same subject; `docs/WASM-EFFECT-ABI.md` and
  `tests/src/wasm/WasmAbiConformanceTest.cpp` both exist at the tip). Nothing to merge; the
  branch is a duplicate of landed work.

## 8 · Open items (highest first)

1. **The CLAP catalogue case fails** (§5) — the socket half of row 79 is not yet drivable for
   `format: "clap"`. One of the two contract decisions above closes it; the test then needs the
   one-line follow-up.
2. `plugins/ClapEffect/ClapHost.cpp` 953 → 1158 lines (tracked growth): split the note path and
   the `Impl` note state into their own TU — **not** a re-anchor.
3. The inherited complexity (57) and file-length (11 of 12) lines the fix-up list already
   carries.
4. The inherited LuaBridge abort (`ScriptDawBindings.cpp:307`) that ends the
   `ReversibilityContractTest` binary after six passing slots.
5. `tools/mcp-zene-control/zene_control/commands_snapshot.json` is derived and was NOT
   regenerated (that is a live-instance job at a merge): expect `ControlCommandsSnapshot` red.

## 9 · The single next action

Decide the CLAP `id` key (fix 1 of §5), apply it to `src/core/ControlDeviceCatalogue.cpp` +
the test's `plugin.load` argument + the release-notes sentence, and re-run from `build/tests`:
`./ControlDeviceCatalogueTest` (expect `10 passed`) and `ctest -R 'ClapHostTest|ControlDeviceCatalogueTest'`.
