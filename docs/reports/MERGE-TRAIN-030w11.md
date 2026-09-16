# MERGE TRAIN — wave 11 (`release/0.3.0`, 2026-09-16)

The last internal integration train before the parent pushes to CI. Base `49a40b30c` (the wave-10
train's tip). **Nothing was pushed.** The train owns only `zene-030` (`release/0.3.0`) and merged the
five finished branches **by their pinned SHAs**, never entering another worktree.

| # | lane | pinned SHA | forecast vs HEAD | result |
|---|---|---|---|---|
| 1 | `030/fixup-clap` | `095633267` | clean (EXIT=0) | clean merge `f4b49b5e0` |
| 2 | `030/fixup-hygiene` | `d9885e37c` | clean (EXIT=0) | clean merge `163a5cc02` (`fork-sources.txt` auto-unioned) |
| 3 | `030/ci-prep` | `f8aa0fc60` | clean (EXIT=0) | clean merge `ab19e0d81` (the harness auto-merged) |
| 4 | `030/fixup-engine` | `a81cf57d6` | clean (EXIT=0) | clean merge `9727f0723` |
| 5 | `030/out-of-process` | `c9fc2027e` | **TWO conflicts** | both resolved by hand; `ce3fda2be` |

Plus `a3854af61` (the derived snapshot) and this record. Every merge was forecast with
`git merge-tree --write-tree HEAD <sha>` **before** it ran, and each merged diff's hunks were read
before the commit.

## 1 · The two conflicts, and how each was resolved

**`tests/fork-sources.txt` (comment block, union).** Both lanes appended a dated paragraph at the same
point in the recipe's header. Resolution: **both paragraphs kept**, hygiene's first (it is the earlier
2026-09-16 entry), then the oop lane's. Nothing else in the file needed a hand: the oop lane's awk
KEEP-list change, the three hygiene pathspec lines and every entry line auto-merged, the entry list is
C-sorted and the counts are exactly the lanes' (`643` fork-NEW at the base -> `+4` the CLAP split ->
`+3` the hygiene modules -> `+10` the out-of-process group = **660**); `bash tests/fork-sources-gate.sh`
→ **EXIT=0**, `PASS: every tracked source in scope is registered (660 fork-NEW, 1104 inherited, 40
tooling)`, `0 stale entry(ies)`.

**`tests/upstream-modifications.txt` (one ledger line, append-union).** Both sides appended a reason to
the same `tests/CMakeLists.txt` line. Resolution: our line plus **their suffix** — the union is
byte-exact: `git show :2:` and `:3:` both start with the `:1:` (base) line, so `ours + theirs[len(base):]`
is the two-reason line. The other nine inherited paths the oop lane touched auto-merged their reasons.
The file still **ends with a newline**, holds **460** entries, has no note without a leading `#`, and
`bash tests/all-sources-reproduce.sh` → **EXIT=0**, `REPRODUCES`.

## 2 · A16 — re-measured at the merged tip, and the block needed no digit change

`docs/RELEASE-NOTES-v0.3.0-alpha.md` entered the train at **340** rows (the oop lane had moved the
335-row wave-10 block by its own five). The merged tip re-measured it with the card's own command:

```
$ bash tools/dawproject-proof.sh            # part 1 EXIT=0, part 2 EXIT=0
MEASURED rows=340 true_inverse=158 snapshot=32 irreversible=13 not_mutating=137
DECLARED rows=340 entries=340 duplicates=0
```

Identical to the block's own `measured:` line, so **the re-take changed no digit** - which is exactly
what the block being self-checking is for. The prose beside it was corrected in one sentence (it read
as if the 335-row figure had included row 80's five; the arithmetic that this block states is
335 + 5 = 340) and the markers are untouched.

```
$ cd build/tests && ./ReversibilityContractTest theTableHistogramIsTheDocumentedOne
HIST_SLOT_EXIT=0      Totals: 3 passed, 0 failed
$ QT_QPA_PLATFORM=offscreen ./ReversibilityContractTest
REV_FULL_EXIT=0       Totals: 8 passed, 0 failed, 0 skipped
```

**The whole A16 binary is green for the first time**: at the wave-10 tip it ended in
`Received signal 6 (SIGABRT)` / `what(): No writable member 'apiSurface'` after six slots
(`REV_FULL_EXIT=134`). The engine lane's raw-write fix closed it, and no merge disturbed it.

## 3 · The snapshot, regenerated from a LIVE instance of the merged tip

`python3 tools/mcp-zene-control/snapshot_commands.py --socket <sock>` against a real `build/zene`
spawned through `tests/control_socket_harness.py` (offscreen platform, throwaway HOME). Never
hand-edited; the provenance records the build it came from.

```
wrote tools/mcp-zene-control/zene_control/commands_snapshot.json: 340 commands, proto 1,
      version 0.2.1-alpha.545+ce3fda2, lane head ce3fda2be
surface: 340 id(s) across 53 group(s), ids_sha256 a9c06f9a3f0c79b34144a985a3152b7344b0adf50bb092e2da968f3c662f94db
```

| change | ids |
|---|---|
| added (5) | `oop.get_state`, `oop.list_families`, `oop.reset_crashes`, `oop.restart`, `oop.set_mode` |
| added (1) | `plugin.host_notes` — the wave-10 CLAP row the wave-10 train could not re-take (no live capture at a merge) |
| description-only | `mixer.remove_channel` ("master ch-0 is refused" → "the master channel is refused"), `plugin.list` (the clap/`clap_id` + VST3 shapes), `scale.list` (the Major/Lydian example) — the engine lane's three corrected strings |
| removed | none |

`334 → 340` ids, `52 → 53` groups. `ControlCommandsSnapshot` now reports **0 drifted command ids**;
its one remaining finding is **inherited and not this train's** (§6.2).

## 4 · Build — the full tree, the kept build directory

The build tree `zene-030/build` was KEPT (the fix-up continuation reuses it), now **42 GB**. It is
configured `RelWithDebInfo / WANT_QT6=ON / WANT_CLAP=ON / WANT_VST3=ON + WANT_VST3_TEST_INSTRUMENT=ON /
WANT_WASM=ON (wasmtime found) / ZENE_TELEMETRY=ON / LMMS_HAVE_SESSION_VIEW / USE_WERROR=ON /
USE_COMPILE_CACHE=ON`, `LMMS_CLAP_PATH=build/clap`, VST3 SDK 3.8 from `build/vst3sdk`.

```
$ cmake -S . -B build                          CONFIGURE_EXIT=0
$ cmake --build build -j4                      BUILD_EXIT=0     # the WHOLE tree, 0 errors
$ cd build/tests && ctest                      CTEST_EXIT=8     # see §5
```

The three CLAP TUs the clap lane split out built in every target that compiles the host
(`zene`, `clapeffect`, `clapinstrument` and the three CLAP test targets), the two new oop test binaries
linked, and `ReversibilityContractTest` links at 499 lines.

## 5 · FULL ctest — 25 of 213, and NOTHING new

```
$ cd build/tests && ctest > /tmp/w11-ctest.log 2>&1; echo EXIT=$?
88% tests passed, 25 tests failed out of 213      CTEST_EXIT=8
```

```
NAME-LEVEL DIFF (wave-10 tip 44/211  ->  wave-11 merged tip 25/213)
  NEW failures : (none)
  FIXED        : 19  [ReversibilityContractTest, ControlReversibilityTranscript, ScriptDawBindingTest,
                      ScriptMemoryBudgetTest, ScriptStabilisationTest, ModulationLayerTest,
                      ModulationLayerProjectRoundTripTest, ControlModulatorCommandsTest,
                      ControlNoteExpressionCommandsTest, RackMacrosTest, RackZonesTest,
                      SmfInterchangeTest, SmfInterchangeRoundTripTest, ControlRegistryTest,
                      ControlProjectArchiveTest, ControlNoteScaleVerbsTest, ControlPdcCommands,
                      ControlBusCommands, ControlDeviceCatalogueTest]
  tests added  : OutOfProcessHostTest, OutOfProcessHostClientLoopTest (both PASS)
  CLASS CHANGES: (none — every remaining red keeps Failed / SEGFAULT / transcript FAIL)
```

Against the dispatch's expected candidates: `ClipLinkTest`, `ClipSerialisationTest` and
`ControlChainPresetTest` are **still red** (inherited); **`ControlRegistryTest` is FIXED** (the engine
lane's `NotFound`/`Refused` decision). The engine lane's claims all hold at the merged tip: the
LuaBridge cluster (5 aborts) is green, the four named ctests are green, and the modulator / mixer /
scale / SMF / tempo slots are green.

**What the remaining 25 are, by class** (all red at the wave-10 tip too):

1. **Serialisation-attribute drift** — `ClipSerialisationTest`, `MpeNoteStorageTest`,
   `MidiProbabilityPersistenceTest`, `SlideNotesTest` ("Compared lists have different sizes": the
   project-file attribute sets the 0.3.0 features added).
2. **The id-prediction class** — `ClipLinkTest` (`no note note-1 (the clip has 2)`),
   `ReversibilityUndoTest` (`note-0`; `mixer.set_volume` on `survivor`), `ControlVcaCommandsTest`
   (`channelId(1)`), `ControlSocketIntegration` (`expected error.kind='refused'`, got
   `no mixer channel ch-0 (the mixer has 1)`), and the three python transcripts
   (`ControlUndoStructuralTranscript`, `ControlVcaCommands`, `ControlCrashReporter`). The engine lane
   named this class as open, and named these exact files.
3. **Byte-identity / round-trip values** — `StableTrackIdsTest`, `TempoMapPersistenceTest`,
   `DawProjectInterchangeRoundTripTest`, `ClipLinkPersistenceTest`, `ClipWarpPersistenceTest`,
   `ControlAutomationModesTest`, `ControlAutomationScriptTest`.
4. **`SafeStartLoadPathTest` SEGFAULT** in `theLoadPathReallySkipsAThirdPartyInstance` (a real
   `signal 11`, exit `0x8`); `SafeStartTest` red beside it.
5. **`PatcherCommandsTest`** — 8 slots fail on `the engine is not addressable yet [engine_starting]`
   (the engine never becomes ready for that binary).
6. **Script expectation drift** — `ScriptBindingsTest::lmmsNamespace`;
   `ScriptEngineTest::testVersionHeader(v0.2-too-new)` and `testHelloScript` (it asserts the log says
   `Zene Studio Lua API 0.1`; the tree says `0.2` — the Lua API version bump).
7. **`ControlCommandsSnapshot`** — see §6.2 (configuration, not drift).

## 6 · Gates (`bash tests/run-all-gates.sh --no-mutation`)

Per-gate numbers unpiped, then the suite (`/tmp/w11-gates.log`):

```
gate   name                     result
1      ctest                    FAIL      4      complexity               FAIL
2      coverage                 SKIP      5      mutation                 SKIP
3      no-tautology             PASS      6      upstream-regression      PASS
7      file-length              FAIL      8      duplication              PASS
9      fork-sources             PASS     10      unregistered-tests       PASS
11     evidence                 PASS     12      rt-safety                PASS
scope: fork (tests/fork-sources.txt) + tools — the enforced scope, the same one CI's static-gates job runs
RESULT: FAIL — see the failing gate above            SUITE_EXIT=1
```

`SUITE_EXIT=1` is gate 1 (25 inherited tests) plus the four new lines below; gates 2/5 skip by flag
(`--no-mutation`, no `--with-coverage`), exactly as the hygiene lane's `EXIT=3` did without a build
tree. **Gate 4 went from the wave-10 tip's 57 regression lines to TWO; gate 7's fork scope to TWO.**

### 6.1 · The four NEW gate lines, named and NOT re-anchored

| gate | line | comes from |
|---|---|---|
| 4 | `check_baseline@162-193@tests/control-pdc-commands.py (CCN 11)` | 030/fixup-engine's master-id fix in that driver |
| 4 | `OutOfProcessHostClientLoopTest::aRealClientIsHostedKilledNoticedAndRefused@113-230 (CCN 11)` | 030/out-of-process's real-client loop test |
| 7 | `include/ControlRegistryGroups.h grew 684 -> 696 lines` | 030/out-of-process's five `oop.*` declarations |
| 7 | `include/ControlReversibility.h grew 528 -> 536 lines` | 030/out-of-process's `reversibilityRowTable` join |

No `--reanchor-file` and no `--reanchor` was taken: each is a *shape* decision (split the file or the
function, or accept the growth) for the fix-up continuation, and no ratchet move can make gate 1 green
at this tip. The whole table, with the exact commands, is in `tests/QA-GATES.md` → "The wave-11 merged
tip: four new line(s), NAMED and not re-anchored".

**Whole-tree (advisory) deltas, measured this run:** gate 4 → `3` regression lines
(`instrument_process@tests/data/clap-test-plugin/clap-test-instrument.c` CCN 23 — the wave-10 CLAP
fixture; `main@tools/dawproject-a16-histogram.cpp` CCN 11 -> 13; the same oop test);
gate 7 → `6` lines (`1693 all-scope sources measured; 138 exceed 500`), the two headers above plus
`plugins/ZynAddSubFx/ZynAddSubFx.cpp` 757 -> 784, `src/core/RemotePlugin.cpp` 601 -> 640 (both the oop
lane's declared inherited edits) and two new over-500 fixture/test files
(`tests/data/clap-test-plugin/clap-test-instrument.c` 564, `tests/src/plugins/ClapHostTest.cpp` 705).

### 6.2 · The one inherited red the train touched and did NOT close

`ControlCommandsSnapshot`: **0 drifted ids** (the regeneration closed the drift half) but one finding
remains — `--compiled-in wasm. was declared, but the snapshot ALREADY carries ids with that prefix`.
This box's build has wasmtime on the find path, so the live list and the captured snapshot both carry
the eight `wasm.*` ids the flag exists to excuse. It is **not new**: the wave-5 train diagnosed it
(`docs/reports/MERGE-TRAIN-030w5.md` §"ControlCommandsSnapshot") with the same two honest fixes —
(i) pass the flag only when the committed snapshot is known not to carry the prefix, or (ii) capture
the snapshot from a no-wasm build (needs a second build tree). **Neither is a merge train's call**:
(i) edits a registered test's registration and (ii) needs a build. Named for the fix-up continuation,
together with the fact that a CI build (which never fetches wasmtime) is the no-wasm configuration the
flag describes — so this red is CI-visible and worth deciding before the push.

## 7 · Sweep — other unmerged `030/*` branches (report only, nothing merged)

Two branches carry commits that are not in the merged tip; every other `030/*` branch is an ancestor:

* **`030/audit` (`2d6f83f5b`)** — 3 commits: `fa24b1889` + `5b9d9088f` fold the wave-9 lanes' rows and
  reconcile the row accounting in `docs/FEATURE-LIST-0.3.0.md` (109 lines), plus
  `docs/reports/RECONCILE-ROW-ACCOUNTING-030-audit.md` and `WAVE9-FOLD-030-audit.md`. That worktree is
  the parent's; a merge train does not take it.
* **`030/w20-wasm-abi` (`e292eb694`)** — 1 commit whose work is **already in the tip** under other
  SHAs (`docs/WASM-EFFECT-ABI.md`, `tests/src/wasm/WasmAbiConformanceTest.cpp` both exist here); same
  finding as the wave-10 sweep. Nothing to merge.

**Observed while reviewing `docs/FEATURE-LIST-0.3.0.md`:** row 79 (CLAP instrument hosting) still reads
`to build — there is no CLAP instrument hosting at all`, although the feature landed in wave 10 and its
proof is green here; row 80 is folded, rows 79/81 are not. The row folding is the `030/audit` lane's
work, so the train left it alone — named for the parent.

## 8 · Free disk, processes, hotspots

* **Disk:** `/home` at **51 GB free (82% used)**; the kept `build/` is 42 GB, and `build/tests` holds
  the ~260 MB test binaries. One stray `build/zene` from a failed harness script was killed by exact
  PID (`kill -TERM 2261104`); `pgrep -x cc1plus` is 0 now. No `pkill -f` was ever used.
* `hotspot: include/ControlRegistryGroups.h` (696) and `include/ControlReversibility.h` (536) — both at
  their caps before this train, both grown by the oop lane; the next group to declare here needs a new
  header.
* `hotspot: src/core/ScriptDawBindings.cpp` — exactly 500 lines after the engine lane's fixes.
* `hotspot: tests/src/core/ReversibilityContractTest.cpp` — 499/500 and it now reads the published
  figure, so it must not be edited for a number again.
* `hotspot: tools/mcp-zene-control/zene_control/commands_snapshot.json` — derived; re-take it at any
  merge that adds an id, and read §6.2 before deciding the `wasm.` flag question.
* `hotspot: the CLAP ledger reason` — the `tests/upstream-modifications.txt` line the clap lane added
  spells the split TUs as `ClapHost{Load,Notes,Params}.cpp`; the files on disk are
  `ClapHostLifecycle.cpp`, `ClapHostNotes.cpp`, `ClapHostParams.cpp`. A reason string, not a gate
  input — but the next editor of that line should correct the name.

## 9 · The single next action

**Decide the `--compiled-in wasm.` question (§6.2) and re-capture the snapshot from a no-wasm build**
— that is the only red in this train that a CI run cannot explain by itself, and the parent is about
to push: every other remaining red is one of the 25 inherited ctests and the four named ratchet lines.
Concretely, in this worktree: `cmake -S . -B build-nowasm -DWANT_WASM=OFF …`, build `zene` there, run
`tools/mcp-zene-control/snapshot_commands.py --socket <that instance>` and commit the snapshot; then
`cd build/tests && ctest -R ControlCommandsSnapshot` must report 0 findings.
