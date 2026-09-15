# WAVE-5 INTEGRATION TRAIN — state at the merged tip

**Train:** the eight pending lane branches merged into `release/0.3.0` in `zene-030` with VERIFIED
UNION merges, the section-5a Off-mode test fixture FIXED AND RUN (the gate is now proved, not merely
in place), the parent's Gate-6 ruling applied, the two FEATURE-LIST resolutions folded, the A16
histogram constant re-measured on the merged tree, the MCP commands snapshot regenerated once from a
live instance, every newly registered ctest run — including the golden-audio record decision — and
the gate suite re-run.

**Tip:** `git log --oneline -1`. **Start tip:** `a74749d15`. **Log dir:** `.merge-logs-030w5/` (every
command's output, unpiped exit codes included; untracked working material, `.gitignore:
/.merge-logs-*/`). **Build:** `build/` in this worktree, incremental, `make -j2`, `BUILD_EXIT=0`
twice (`build-w5-1.log`, `build-w5-2.log`); the binary is `build/zene` and needs
`LD_LIBRARY_PATH=<root>/third_party/wasmtime/lib`.

## 1 · The merges, in order (one `--no-ff` merge commit each)

| # | branch | tip merged | merge commit | conflicted files |
|---|---|---|---|---|
| 1 | `030/session-completion` | `d10193318` | `96e490938` | 5 |
| 2 | `030/doc5-specs` | `f60624bf0` | `f38790b8f` | 1 |
| 3 | `030/row65-placement` | `47ffa0a6c` | `940325b33` | 1 |
| 4 | `030/crash-enable` | `ef10366fd` | `e584bc242` | 3 |
| 5 | `030/clap-windows` | `81e42d4c8` | `879d12c2c` | 4 |
| 6 | `030/golden-audio` | `0d8b766f0` | `ae8578034` | 3 |
| 7 | `030/repo4-hygiene` | `10d4d07df` | `3c1db9358` | 2 |
| 8 | `030/midi-reconnect` | `7bdbfd208` | `9fd99d2ee` | 11 |

Every conflicted append-only registry was resolved as a **union of entries** by
`.merge-logs-030w5/union-resolve.py` (rules: entry-union, ledger-union, section-union, block-union,
scope-union, join-union, keep-ours-for-a-measurement, `theirs` for a recorded resolution, and two new
rules for the REPO-4 move), then checked by `verify-merge.py` (no added line from either side missing
from the merged tree, on the STAGED tree) plus a marker / duplicate / trailing-newline sweep. Per-file
before → after counts are in `counts-<lane>-{before,merged}.txt` and in each merge commit message.
Every merge: `git-merge EXIT=1` (conflicts by design), `resolver EXIT=0`, `no-silent-loss EXIT=0`,
`git-commit EXIT=0`.

The parent's two recorded resolutions, as specified:

* **merge 3** — `docs/FEATURE-LIST-0.3.0.md` taken whole from `030/row65-placement`
  (`sha256 a81b7125b075…`, verified equal to the branch's blob before the later row folds).
* **merge 7** — the REPO-4 rename is KEPT: `LANE-STATE.md` stays at
  `docs/reports/LANE-STATE.md`, and the sibling lanes' content written at the old root path is
  carried into it as its own section (`lane-state-move-union`, 921 lines after the union). The root
  path stays gone. `LANE-STATE-CLAP-WINDOWS.md` (added by merge 5, after REPO-4 forked) was moved
  there in the same step, per the brief's "moved on sight" rule.
* **merge 8** — `LANE-STATE.md` again: this lane's state is appended to
  `docs/reports/LANE-STATE.md` as its own section (`lane-state-append-to-reports`).

## 2 · The mandated fixes

### (a) SECTION 5a — PROVED, not merely in place · `823124529`, `d22f1de08`

`cd build/tests && LD_LIBRARY_PATH=<root>/third_party/wasmtime/lib ctest -R
'^SampleAccurateAutomationTest$' --output-on-failure` → **`EXIT=8`** (one slot of eight still fails,
see §4 item 1) — but the 5a slot itself, and the four other fixture-dependent slots, now RUN AND
PASS:

```
AUTOMATION_EVIDENCE sample-mode  frames=2048 max_deviation=0 off_curve=0 move_frame=20
AUTOMATION_EVIDENCE block-mode   frames=2048 max_deviation=55.3125 off_curve=2015
AUTOMATION_EVIDENCE off-mode-ramp  periods=4 with_ramp=0 max_move_inside_block=0
AUTOMATION_EVIDENCE read-mode-ramp periods=4 with_ramp=4 max_move_inside_block=55.5102
PASS : anOffControlDoesNotFollowItsSampleAccurateRamp()
```

was `0 passed, 6 failed` at the wave-4 tip; it is **7 passed, 1 failed** now. Three fixture defects,
each fixed at its cause — **no assertion relaxed, no expectation edited to match the code:**

1. **The device id was a literal.** `channelGainParameter()` compared `pluginId == "fx-0"`, but
   `030/stable-ids` slice 2 (`03c2a5206`) made `Effect::m_id = ProjectIds::allocate()`
   (`src/core/Effect.cpp:56`) — a project-scoped counter shared with the channel, track, clip and note
   ids. The helper now takes the device the fixture put on the channel's chain and derives the id with
   `control::effectIdOf()`, pinning the model to that device's own children.
2. **A rendered block was not a new period.** `AutomatableModel::incrementPeriodCounter()` is the
   ENGINE's per-render-stage call (`src/core/AudioEngine.cpp:467`) and the fixture stops the device
   thread, so nothing made it: `automationRamp()` — "was a ramp published TO THIS PERIOD" — reported a
   STALE ramp as current, and the Off half read 4-of-4 for a ramp that was never published. Both
   helpers now make that call, as `PhaseDMixerTestSupport.h:146` and `VcaGroupTest.cpp:168` already
   do. This is what makes the Off half load-bearing: 4-of-4 → 0-of-4, while Read stays 4-of-4.
3. **The block had no tick boundary to carry.** A block is 256 frames, a tick is `55125/BPM`; at the
   shipped 140 BPM a block spans 0.65 of a tick, so the old curve (nodes on ticks 0/24/48/72) was FLAT
   inside every measured block — the measurement would have passed while proving nothing and its own
   control half could not fail it. The fixture now runs at 200 BPM with a node per tick, which also
   gives the ramp slot knots=4 where it could not reach 3 before.

### (b) GATE 6 — GREEN (was: 1 violation, that one the artefact the parent ruled on) · `76d5a251b`

`bash tests/no-upstream-regression-gate.sh` → **EXIT=0**, "PASS: every change to upstream-inherited
code since `01148947ea4d…` is declared" (`gate6-after-rename.log`). **The gate is untouched; the
files moved.** Three classes were reported as *"undeclared change to upstream-inherited code"*:

1. **`docs/lua-api-surface.txt` → `docs/LUA-API-SURFACE.md`** (the parent's ruling (i)), with every
   reader updated in the same commit: `tests/CMakeLists.txt` (the `--manifest` argument, 2 refs),
   `tests/lua-api-surface.py` (docstring ×3 and the default manifest path), `docs/LUA-API-STABILISATION.md`,
   `docs/LUA-COMPATIBILITY-POLICY.md` (×2), `docs/RELEASE-NOTES-v0.3.0-alpha.md`,
   `src/core/ScriptDawBindings.cpp` (its header comment; a wrong `tools/lua-api-surface.py` in the
   same sentence was corrected to `tests/`), `tests/upstream-modifications.txt` (the reason string).
   The `LuaApiSurface` ctest still passes: **EXIT=0**.
2. **`docs/SESSION-ARRANGEMENT-RECORD-MEASURED.txt` → `.md`** — same class, introduced by merge 1 of
   this train, no reader anywhere (tree-wide grep).
3. **`.merge-logs-030w4/*.py|*.sh|*.txt`** (14 files, committed by the wave-4 train's report commit
   *after* that train's own gate run, so its report never saw them). The RECORD moved to
   `docs/reports/MERGE-TRAIN-030w4.md` with a Location note (the REPO-4 convention); the working logs
   are untracked (`git rm --cached`, **files kept on disk**, `/.merge-logs-*/` ignored) exactly as the
   w1/w2/w3 trains' directories always were. Relocating the tooling under `tools/` was rejected:
   `tools/` is a measured scope (gates 4, 7, 8) and `union-resolve.py` is 656 lines, i.e. it would
   register a NEW file-length regression to fix a classification one.

### (c) A16 histogram — re-measured on the final merged tree · `c34f3a64b`

`bash .merge-logs-030w5/measure-a16.sh` → `histogram EXIT=0`, printed

```
MEASURED rows=334 true_inverse=158 snapshot=32 irreversible=10 not_mutating=134
```

(ZENE_TELEMETRY_ENABLED on, `-DLMMS_HAVE_WASM=1`, `WANT_STEM_SPLIT` off — this build's own
configuration), so the base constant is that measurement minus the guards' own additions
(−2/−2 telemetry, −6/−3/−3 wasm) = **326 / 158 / 29 / 10 / 129**, and 158+29+10+129 = 326 exactly.

| | rows | true_inverse | snapshot | irreversible | not_mutating |
|---|---|---|---|---|---|
| wave-4 constant | 314 | 156 | 27 | 10 | 121 |
| **wave-5 constant (measured)** | **326** | **158** | **29** | **10** | **129** |

Corroborated by the built test: `ReversibilityContractTest::theTableHistogramIsTheDocumentedOne()`
**PASSES** (`ctest-ReversibilityContractTest.log`) — a measurement, never a union.

### (d) MCP commands snapshot — regenerated once, from a live instance · `ab97099ce`

`bash .merge-logs-030w5/regen-snapshot-w5.sh` (offscreen instance on `/tmp/zene-w5-snapshot.sock`,
`snapshot_commands.py --socket …`, `EXIT=0`, instance stopped with `control.quit` and reaped by exact
PID):

* **before:** 322 commands, `captured_at 2026-09-15T20:50:04Z` (wave-4)
* **after:** **334 commands**, `captured_at 2026-09-15T22:03:26Z`, 52 groups,
  `ids_sha256 7dc75a5ff846671889fdbfc18d35640271771784ad557cbce223997c81f2bab2`, from `build/zene`
  `sha256 c870d693999a07b1…` (the merged tree's own binary)
* proof of the regeneration: the ctest's own three-way comparison reports **`0 missing, 0 extra`**
  between the live binary, the bridge's generated tool list and the committed snapshot
  (`ctest-ControlCommandsSnapshot.log`).

The +12 ids are the eight branches' groups: `session.follow_get_state`/`follow_set`/
`arrangement_record_arm`/`arrangement_record_land`/`arrangement_record_status`,
`crash.enable`/`crash.disable`, `midi.reconnect_arm`/`reconnect_set`/`reconnect_status`.

### (e) The newly registered ctests — all run from `build/tests`, exit codes unpiped

Enumerated by diffing `tests/CMakeLists.txt` against `a74749d15`
(`.merge-logs-030w5/new-tests.txt`). **`ControlGoldenAudio` ran FIRST, against the COMMITTED record.**

| ctest | exit | verdict |
|---|---|---|
| `ControlGoldenAudio` | **0** | PASS against the committed record — **the record stands, no re-measure** |
| `GoldenAudioSelfTest` | 0 | PASS |
| `SessionFollowTest` | 0 | PASS |
| `SessionArrangementRecordTest` | 0 | PASS |
| `CrashReporterArmTest` | 0 | PASS |
| `MidiReconnectTest` | 0 | PASS |
| `ClapLoaderErrorTest` | 0 | PASS |
| `ControlMidiReconnect` | 0 | PASS |
| `LuaApiSurface` | 0 | PASS (renamed manifest path) |
| `ControlCommandsSnapshot` | 8 | FAIL — **pre-existing** (also red at the wave-4 tip): the live-binary comparison is clean (0 missing / 0 extra), but the ctest passes `--compiled-in wasm.` and the guard correctly refuses a prefix the snapshot already carries ("the flag would excuse real drift"). Cause: the snapshot is regenerated from *this* box's build, which HAS wasmtime, so it now carries the eight `wasm.*` ids the flag exists to excuse. Honest fixes: (i) register the flag only when the committed snapshot is known not to carry the prefix, or (ii) capture the snapshot from a no-wasm build (needs a second build tree — out of budget). |
| `ReversibilityContractTest` | 8 | All 6 slots PASS **including the histogram**; the binary then ABORTS in `irreversibleUndoFailsTypedAndDoesNotUndoAnOlderStep` (`luabridge::LuaException: No writable member 'apiSurface'`, SIGABRT) — **pre-existing**, named in the wave-4 failure list |

**Full suite** (gate 1's own run): **204 tests registered**, 42 failed, `ctest EXIT=8`, 536 s.

### (f) Gate suite — `bash tests/run-all-gates.sh`, `RESULT: FAIL`, exit 1 · `.merge-logs-030w5/gates-w5.log`

| gate | wave-5 | vs wave-4 | |
|---|---|---|---|
| 1 ctest | **FAIL** | 42 of 204 failed (was 41 of 196) | fix-up pass's material; `failed-tests.txt` has the list |
| 2 coverage | SKIP | same | needs `--with-coverage` |
| 3 no-tautology | PASS | same | |
| 4 complexity | **FAIL** | still red | fork scope: 5820 functions, 72 over CCN 10, **52 new over target**; tools scope: 570 functions, 13 over. **Named, not fixed** (the parent's ruling) |
| 5 mutation | PASS | same | kill score **88.5 %** ≥ 80 % (30 mutants selected) |
| 6 upstream-regression | **PASS** | was FAIL | **fixed by (b)** — 0 violations |
| 7 file-length | **FAIL** | 10 regressions (was 7) | **named, not fixed** — fork scope 623 files, 20 over 500. **Three are this train's**: `include/ControlRegistryGroups.h` (671), `include/ControlReversibility.h` (518) and `src/core/ControlRegistryGroups.h`'s tag-file growth; the rest are inherited (`Vst3Host.cpp` 800→891, `ControlCommandsNotes.cpp` 512, `ControlCommandsProject.cpp` 510, `ControlCommandsWarpEdit.cpp` 510, `ControlReversibilityTablePassive.cpp` 518, `tests/control-stable-ids-slice2.py` 549, `tests/src/core/ControlRegistryTest.cpp` 505). Tools scope: 40 files, 2 over → PASS |
| 8 duplication | PASS | same | 0.00 % |
| 9 fork-sources | PASS | same | |
| 10 unregistered-tests | PASS | same | |
| 11 evidence | PASS | same | 6589 files scanned, 0 refused |

**`git status` after the suite: clean** — the mutation gate restored every mutant it wrote (the
wave-4 warning did not repeat). Nothing was run concurrently with a build.

### Golden-audio record decision

`ControlGoldenAudio` ran FIRST against the committed `tests/golden-audio-record.tsv` and **PASSED**
(23.97 s, exit 0) — every term (max |delta| LSB and dBFS, the level delta, the per-window envelope,
the run-to-run floor) held, and its negative control still fails a deliberate `mixer.set_volume`
move. **No merge moved the audio, so the record was NOT re-measured and NOT rewritten.** That is the
programme's own rule: a record rewritten while green is a disabled test.

## 3 · Defects this train found and fixed while merging (each its own commit)

| commit | what |
|---|---|
| `8d69bc574` | the FEATURE-LIST row 54 fold: ids cell 4 → the registry's six `crash.*` ids, the "no `crash.enable`/no uninstall" clause deleted, the proof cell names `CrashReporterArmTest` (folded from `docs/CRASH-REPORTER.md` §7, at the parent's ruling) |
| `5032d6ac1` | `tests/src/core/ControlVerbInverseTest.cpp`: the block-union had APPENDED repo4's two re-pointed comment lines to the file's TAIL, after `.moc` — invalid C++. Ours' text stands (DOC-5 had already removed that citation) and the tail copy is deleted. Same commit: `LANE-STATE-CLAP-WINDOWS.md` follows the REPO-4 move out of the root |
| `2798407c0` | FEATURE-LIST row 57's status follows the move the merge actually made: "partial — the root still carries the lane reports" → "in the tree", with the measured root listing and the deliberate absence of a gate |
| `76d5a251b` | the Gate-6 class fix (§2b) |
| `c34f3a64b` | the A16 constant re-measured |
| `823124529`, `d22f1de08` | the section-5a fixture (§2a) |
| `ab97099ce` | the snapshot regenerated |

Two files needed a **hand** resolution (the "ours moved the function" class, recorded by the resolver
and in verify-merge's declared-skip set, both in merge 8's own commit):

* `src/core/ControlRegistry.cpp` — ours verbatim; the lane's two registration calls carried into
  `src/core/ControlRegistryRegistrations.cpp`, where wave-4 moved `registerControlCommands()`.
* `src/core/ControlReversibilityTableAction.cpp` — ours, with the lane's join carried in: the
  statement now loops over `{chain, structure, midiReconnect}` instead of `{chain, structure}`.

## 4 · What is left (precise)

1. **`SampleAccurateAutomationTest` — one slot of eight.** The 5a gate is proved; the remaining
   failure is a distinct product defect this run found: `theSurfaceDrivesAndReversesTheMode` reads back
   `automation.ramp_get reported '<absent>'`. `automationRampGet()`
   (`src/core/ControlCommandsAutomationRamp.cpp:207`) walks `Engine::getSong()->tracks()` for
   Automation/HiddenAutomation tracks, but the clip that drives a MIXER CHANNEL parameter lives on the
   song's HIDDEN global automation track (`Song::m_globalAutomationTrack`), which is not in `tracks()`;
   and for an Automation-type track the loop's `resolveControlTarget()` refuses ("carries no device
   chain"), so the enumeration cannot yield an entry at all. Minimal fix: walk the clip's own
   `objects()` (`m_globalAutomationTrack` included) instead of resolving each holder track as a control
   target. Not patched here: it is an engine change to a command handler and this train's mandate for
   this file was the 5a fixture.
2. **Gate 1 — 42 of 204.** `.merge-logs-030w5/failed-tests.txt` has every name: assertion mismatches
   in the merged tests, 6 subprocess aborts (the `Script*` family + `ReversibilityContractTest`), 1
   SEGFAULT (`SafeStartLoadPathTest`), and pre-existing control-surface transcript reds
   (`ControlCrashReporter`, `ControlUndoStructuralTranscript`, `ControlVcaCommands`,
   `ControlPdcCommands`, `ControlBusCommands`, `ControlSocketIntegration`). All were red at the
   wave-4 tip or are the fix-up pass's material.
3. **`ControlCommandsSnapshot`** — the `--compiled-in wasm.` flag vs a snapshot that now carries the
   prefix (§2e); the snapshot itself is verified clean.
4. **Gates 4 and 7** — named, not fixed (the parent's ruling). Gate 7 has **10** regressions to the
   wave-4 list's 7, and three of them are this train's: `include/ControlRegistryGroups.h` grew to
   **671 lines** and `include/ControlReversibility.h` to **518**, because the brief requires every lane
   to declare its group in `ControlRegistryGroups.h` rather than in the capped `ControlRegistry.h`, and
   the eight merges unioned those declarations into the one tag file. The honest next move is the one
   this project has already used three times: split the declarations into satellite headers
   (`ControlRegistryGroups<Area>.h`, exactly as `ControlReversibilityTable<Area>.cpp` split the row
   tables) and join them with one include — never a blanket `--reanchor`.
5. **The A16 paragraph in `docs/RELEASE-NOTES-v0.3.0-alpha.md`** still carries the wave-3
   branch-local narrative (284 rows, 152/21/7/104). The test's comment block names the exact
   replacement figures (334 live, 326 base).
6. **Whole-tree scope** (`--whole-tree`) and **coverage** were not measured by this run, and no CI
   evidence exists for this tip (`release/0.3.0` is local-only and unpushed).

## 5 · One more finding, for the next train

`tests/upstream-modifications.txt` holds **457 keys** and grows with every merge; each lane's merge
appends a clause to the same paths' reasons. It is at 673 lines and its `tests/CMakeLists.txt` entry's
reason is now ~6 000 characters of concatenated lane clauses. It still reproduces and the gate reads
it, but the next train should consider a per-wave companion file rather than one ever-growing clause.

## 6 · The single next action

**Fix `automation.ramp_get`'s enumeration (item 1 in §4) and re-run `SampleAccurateAutomationTest`** —
it is the only newly-reachable proof in this train that still fails on a defect the run exposed, it is
one function, and it unblocks the last slot of the section-5a file. After that, gate 1 is the fix-up
pass's own work-list and gates 4/7 need the splits named in §4.
