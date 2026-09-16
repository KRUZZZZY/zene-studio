# Coverage-matrix closure — the wave-9 measurement (board task #680)

**Lane:** `030/coverage-closure` (worktree `zene-030/wcov2`) · **Tip measured:** `a36e8b0d4` ·
**Base of the card:** `docs/COVERAGE-MATRIX-2026-09-13.md` (tip-pinned at `ddf5f171d`, re-measured by
`030/test-gaps` at `0ee78abed`) · **Date:** 2026-09-16.

**What this file is.** The card's own reconciliation and its delivered table: every gap the 2026-09-13
coverage matrix measured, re-measured at this tip, with the artefact each still-open gap needed — and the
per-feature verdict table for the sixteen "in the tree, not drivable through the socket" rows (the matrix's
Table B). Every number below came from a command run against this tree; nothing is inferred from another
document. The tracker (`scripts/zene-feature-tracker.py`) was used **read-only** (`--release-wt`), never
`--write`.

**Verdict vocabulary**, as the card defines it:

| verdict | meaning |
|---|---|
| **drivable** | a registered command group drives it AND a registered test artefact exercises it |
| **disarmed-documented** | in the tree, not drivable, and a document says so in as many words |
| **DISARMED-SILENT** | in the scope, not drivable, and no document records the absence — **a defect** |
| **unproven** | reachable, but no registered proof asserts it and the measurement cannot settle it |

---

## 1. The census at this tip (the denominator every count below uses)

```
$ git ls-files 'src/core/ControlCommands*.cpp' | xargs grep -h -o '\.id = QStringLiteral("[^"]*"' | wc -l
323                       # canonical id assignments, 50 id prefixes
$ python3 - <<'EOF' ... EOF     # union with the committed commands_snapshot.json (334 ids)
union ids: 341  artefacts: 222
```

The two sources agree on everything except two explained sets, so **341** is the id set this pass measured
(the tracker's own figure), and **323/50** is what the source grep alone sees:

| direction | ids | why |
|---|---|---|
| snapshot − source grep | 18 | `crash.*` ×6, `safestart.*` ×4 and `wasm.*` ×8 are helper-built (`cmd.id = <const>` / `cmd.id = QStringLiteral("wasm.") + verb`), which the canonical regex does not match |
| source grep − snapshot | 7 | the `stem.*` ids, compiled out of the instance the snapshot was captured from (`WANT_STEM_SPLIT` defaults OFF) |

**Id set provenance, measured after this lane's change:** **0 of 341 ids** have no registered-test-artefact
reference (the matrix's own method: `tree ids − {ids named by any registered test artefact}`, the artefact
set being `tests/src/**/*.cpp` ∪ the `tests/*.py` named in `tests/CMakeLists.txt` — 222 artefacts here).
Before this lane the same measurement returned **22**: 16 ids in six groups the card names, plus the four
`safestart.*` ids and the two `wasm.*` ids the sandbox grew after its transcript was written.

---

## 2. The card's four named gaps, reconciled one by one

The card was written against the 2026-09-13 matrix. Two of its four gaps were **already closed** by
`030/test-gaps` (`0ee78abed`) and later lanes before this lane opened; two needed work, and the *measured*
form of one of them had moved.

| # | gap as the card names it | state at `a36e8b0d4` | what this lane did |
|---|---|---|---|
| 1 | "a registered reference for the **9 ids**" | **CLOSED, and the queue had moved.** The matrix's nine (`export.*` ×3, `session.*` ×5, `midi.learn_toggle`) are all referenced now — by `ControlExportSettings`, `ControlSessionLifecycleTranscript` and `control-socket-integration.py`. Re-running the same method at this tip returned a **different** 16 ids in six groups that arrived after the audit | added **`tests/src/core/ControlSurfaceReferenceTest.cpp`** (registered) and then the four `safestart.*` ids it also found; measured after: **0 of 341** |
| 2 | "a registered ctest for the **export** group" | **ALREADY CLOSED** — `ControlExportSettings` is registered at `tests/CMakeLists.txt:1882` (drives `export.get_settings` / `export.set_dither` / `export.set_src_quality`, asserts defaults, the set, the A16 record, the `control.undo` inverse and the typed refusal) | nothing to add; verified present, named here so the next reader does not re-open it |
| 3 | "registration for the **unregistered test file**" | **the audit's instance was already registered** (`tests/control-export-settings.py`, §3.3's example). The class at this tip: five unregistered **entry points**, of which two are tests whose output runs nowhere | registered **`ControlStableIds`** and **`ControlReversibilityTranscript`**; the other three are dispositioned in §4 |
| 4 | "a `ProjectOpenIntegrityTest`-style **fixture** that ships a project with a `<modulation-layer>` element and asserts the round trip (create → save → clear → open → identical)" | **half existed**: `tests/data/modulation-layer-fixture.mmp` (the shipped project) and `ProjectOpenIntegrityTest`'s two cases — which open the fixture, assert its declared values and re-save it. The **create-modulators-first** direction was missing | added **`tests/src/core/ModulationLayerProjectRoundTripTest.cpp`** (registered): both modulators are authored through `modulator.create` / `rate_set` / `target_set`, written by `project.save`, the session cleared, read back by `project.open`, and asserted identical — every authored value and the serialised `<modulation-layer>` document |

---

## 3. What was added, and where it is registered

| artefact | registration |
|---|---|
| `tests/src/core/ControlSurfaceReferenceTest.cpp` | `tests/CMakeLists.txt` `LMMS_TESTS` (next to `ControlRegistryTest`) · manifest `tests/fork-sources.txt` |
| `tests/src/core/ModulationLayerProjectRoundTripTest.cpp` | `tests/CMakeLists.txt` `LMMS_TESTS` + the `QT_QPA_PLATFORM=offscreen` property list · `tests/fork-sources.txt` |
| `ctest ControlStableIds` → `tests/control-stable-ids.py` | `tests/CMakeLists.txt`, in the `PYTHON3 AND CONTROL_SUITE_AVAILABLE` block beside `ControlStableIdsSlice2` |
| `ctest ControlReversibilityTranscript` → `tests/control-reversibility-transcript.py` | same block; `SKIP_RETURN_CODE 77`, `TIMEOUT 420`, offscreen platform like its siblings |

No gate, baseline, manifest or workflow step was weakened, no `--reanchor` was used, and no failing test was
deleted. Two new `.cpp` sources were added to a manifest as fork-NEW; nothing was added to
`tests/upstream-modifications.txt` (no inherited file was edited — `tests/CMakeLists.txt`,
`tests/fork-sources.txt` and the two new sources are fork files).

**What `ControlSurfaceReferenceTest` claims, and what it does not.** Per id: registration; a `group.verb` id
whose group is its own prefix; both schemas and a description; the A16 row; the row's mutating verdict against
the command's own flag; and — for every id the table says does **not** mutate — a typed reply through the
registry (`ok` or a typed refusal, never an unknown command, never a hang). It is a **reachability and
contract** reference: a typed refusal passes, and the mutating ids are deliberately not invoked (a contract
test must not write the running user's state). Behaviour for these groups lives where it already lived, named
per group in §5.

---

## 4. Test files that exist and are registered as no ctest (the residual class)

Five `tests/*.py` entry points are still named by no `add_test`. Each is dispositioned, measured — not
assumed — by how the tree uses it:

| file | disposition |
|---|---|
| `tests/control-warp-commands-transcript.py` | **not a gap**: its printed transcript is committed as `docs/WARP-COMMANDS-TRANSCRIPT.md` (the charter's criterion 3 alternative), and `ControlWarpCommandsTest` is the registered behavioural proof |
| `tests/control-pitch-stretch-transcript.py` | **not a gap**, same shape: `docs/PITCH-STRETCH-TRANSCRIPT.md` is committed, and `AudioStretcherTest` / `SampleClipStretchTest` are registered |
| `tests/brand-resource-sweep.py` | a by-hand sweep whose findings are recorded in `docs/BRAND-PLACEHOLDERS.md`; it makes no pass/fail claim about the product |
| `tests/golden_audio_record.py` | the **record generator** of the golden-audio programme (`--write-record`), driven by the registered `ControlGoldenAudio` and `GoldenAudioSelfTest`; it is a tool of that programme, not a test beside it |
| `tests/data/**` (`loudness/`, `midi-depth/`, `oop-hosting/`, `vca-inject-group.py`) | fixture generators and probes for the registered tests that read their output |

Nine further unregistered `.py` files are **helper modules imported by a registered entry point** (measured
by their `import` sites): `agent_surface_lib.py`, `control_socket_flows.py`, `mastering_probe_lib.py`,
`stem_commands_lib.py`, `midi_reconnect_flows.py`, `control_instance_diagnosis.py`, `control_pipe_client.py`,
`mcp_stdio_session.py`, `link_sync_evidence.py` — plus `midi_reconnect_probe.py`, a separate process spawned
by the registered `ControlMidiReconnect`.

---

## 5. Table B — the sixteen "in the tree, not drivable" rows, verdicts at this tip

The matrix's Table B was measured at `ddf5f171d` and **not re-derived** by the later re-measurement. Here it
is re-derived. Every row now has a command group, a registered proof and a document line — **sixteen rows, and
fifteen are in the first two verdicts.**

| # | feature (matrix's row name) | group at this tip | registered proof | verdict |
|---|---|---|---|---|
| 1 | Stem separation | `stem.*` ×7 | `ControlStemCommands` (`control-stem-commands.py`) + `ReversibilityContractTest` (A16 rows) | **drivable — configuration-gated**: the group is compiled only under `WANT_STEM_SPLIT`, which defaults **OFF** and is documented as such (`KNOWN-LIMITATIONS.md:1044,1057`; `RELEASE-NOTES:747`) |
| 2 | Mastering chain / auto-mastering | `mastering.*` ×3 | `MasteringTest` + `ControlMasteringCommands` | **drivable** (KL:975, RN:920) |
| 3 | LUFS / loudness metering | `meter.*` ×3 | `MeterTapTest` + `ControlMeterCommands` | **drivable** (KL:1093, RN:883) |
| 4 | VCA / edit groups | `vca.*` ×14 | `ControlVcaCommandsTest`, `ControlVcaEditGroupsTest`, `ControlVcaCommands` | **drivable** (KL:167, RN:475) |
| 5 | PDC + sidechain | `pdc.report` (+ the mixer routing verbs) | `ControlPdcCommands` | **drivable** (KL:685, RN:535) |
| 6 | Routing graph | `routing.get_state` | `ControlRoutingCommands` | **drivable** (KL:701, RN:555) |
| 7 | Audio ports / `AudioBus` | `port.get_state` / `port.set_pin`, `bus.*` ×3 | `ControlPortsCommands`, `ControlBusCommands` | **drivable** (KL:738,748, RN:575,581) |
| 8 | Multi-track recorder | `record.*` ×15 | `ControlRecordInputs`, `RecordingInputPathTest` | **drivable** — and the id that contradicted it, `track.set_arm`, is a real reversible command now (`ControlReversibilityTableRecording.cpp:66`), not the refusal stub the matrix found (KL:620, RN:895) |
| 9 | Plugin scan cache + quarantine | `plugin.scan_cache_*` ×5 | `PluginScanCacheTest` + `ControlPluginScanCommands` | **drivable** (KL:766, RN:672) |
| 10 | Crash reporter | `crash.*` ×6 | `ControlCrashReporter` | **drivable**, one documented refusal: `crash.upload_report` refuses every call by name — this build has no upload — and says so (KL:783,786, RN:701) |
| 11 | Note random / transform / slide | `note.randomize|random_seed_get|random_seed_set|transpose|velocity_offset|velocity_scale|slide_set|slide_clear` | `ControlNoteScaleVerbsTest` (all eight ids), `MidiProbabilityPersistenceTest` | **drivable** (KL:1000, RN:1585,1602) |
| 12 | MPE pressure + timbre | `note.expression_set|clear|get` | `ControlNoteExpressionCommandsTest`; the audit's "drivable but inert" limit is **lifted** — all three axes reach playback (task #649) | **drivable** (KL:281,462, RN:1087) |
| 13 | Lua API beyond the bound objects | `script.list|run|set_memory_budget` | `LuaApiSurface` (the surface ratchet), `ControlAutomationScriptTest`, `ScriptMemoryBudgetTest` | **drivable**, with the binding's scope and the memory budget's UI absence documented (KL:1131,1359, RN:1817) |
| 14 | Autosave / project recovery (`project.restore_revision`) | `project.restore_revision`, `revisions.*` ×3 | `RevisionTimelineTest`, `ReversibilityUndoTest`, and (from this lane) `ControlReversibilityTranscript` | **drivable** — the matrix's "referenced by no behavioural test" is no longer true (KL:1379, RN:2413) |
| 15 | **Real-time-safety whole-tree verification programme** | — none — | — none — | **DISARMED-SILENT — see §6** |
| 16 | Golden-audio integration programme | (a programme, no `golden.*` id by design) | `GoldenAudioSelfTest`, `ControlGoldenAudio`, `docs/GOLDEN-AUDIO.md`, `tests/golden-audio-record.tsv` | **drivable/proved** — the matrix's "not in the tree at all" is no longer true (KL:1514, RN:2767) |

### 5.1 The three registered-but-always-refusing stubs the matrix counted

- **`track.set_arm`** — no longer a stub: `RC::Snapshot`, reversible, and `record.arm_track` is its sibling
  (`ControlReversibilityTableRecording.cpp:66`).
- **`automation.mode_set`** — no longer a stub: automation modes landed (`ControlAutomationModesTest`,
  `RELEASE-NOTES:1115`).
- **`mixer.set_pan`** — still refuses, and the refusal is stated as a limitation in the release documents
  (`KNOWN-LIMITATIONS.md:1369`: "this tree has no pan on a mixer channel") → **disarmed-documented**.

---

## 6. DISARMED-SILENT — the one case this pass found (to be filed, not fixed here)

**Title:** *The real-time-safety whole-tree verification programme is absent with no absence line in the
release documents (KNOWN-LIMITATIONS.md / RELEASE-NOTES-v0.3.0-alpha.md).*

**Measured evidence (all read-only):**

```
$ grep -c 'real-time-safety\|real-time safety' docs/KNOWN-LIMITATIONS.md
0
$ grep -n 'real-time' docs/RELEASE-NOTES-v0.3.0-alpha.md
2770: the 0.3.0 plan names (the other is the real-time-safety one). It answers "did this change alter ...
```

- The programme is a **0.3.0-scope item**: `V0.3-ALPHA-PLAN.md:53` lists it under "still to build";
  `:141` gives it its own wave ("W10 verification programmes"); `NEXT-0.3.0-AGENT-PROMPT.md:100` names it
  beside golden-audio as "the evidence the items above rest on".
- It is **not in the tree**: no test, gate or tool implements it (`docs/FEATURE-LIST-0.3.0.md:303`, row 52;
  `V0.3-SCOPE-LEDGER.md:34` — "a programme; nothing in the tree").
- **Its sibling programme did the paperwork and it did not**: the golden-audio programme has a
  `KNOWN-LIMITATIONS.md` section (`:1514`, with the "one line" UI absence at `:1516`) and a release-notes
  section (`:2767`). The real-time-safety programme has **no** limitation section and **one** incidental
  mention in the release notes — inside the golden-audio section's first sentence.
- The tracker's own verdict agrees: row 52 measures **0 % / NOT-STARTED, "limits: no group to look a line up
  by"**.
- Board state: task **#678** ("Real-time-safety whole-tree verification programme (row 52)") is `ready` — the
  *build* is tracked. **What is not tracked is the release-document half**: 0.3.0 ships without this
  programme and a reader of the release documents cannot learn that.

**Spec text for the new task** (the parent files this; this lane did not touch the board):

> The 0.3.0 scope names two verification programmes; only golden-audio landed. `docs/KNOWN-LIMITATIONS.md`
> contains no line saying the real-time-safety whole-tree programme is absent from this release, and
> `docs/RELEASE-NOTES-v0.3.0-alpha.md` mentions it only inside the golden-audio section's first sentence.
> Acceptance: one line in `docs/KNOWN-LIMITATIONS.md` stating, in the document's own convention, that the
> 0.3.0 release ships without the real-time-safety whole-tree verification programme and what that means for
> the reader (audio-thread allocation/locking claims rest on per-feature probes only, not on a sweeping
> check); the matching sentence in `docs/RELEASE-NOTES-v0.3.0-alpha.md`; no gate, baseline or manifest
> change. Evidence: `grep -c 'real-time-safety' docs/KNOWN-LIMITATIONS.md` → 0 before, ≥1 after;
> `docs/FEATURE-LIST-0.3.0.md:303`, `V0.3-SCOPE-LEDGER.md:34` and board task #678 are the sources.

---

## 7. What this lane could NOT verify

1. **Nothing here was compiled or executed.** The box had **4 `cc1plus` processes running** when this lane
   started (the wave-9 brief: two or more box-wide compiles → defer and report), so no build directory was
   created in this worktree. All four added/changed ctests are **registered but unexecuted**. The exact
   commands for whoever gets a build slot (unpiped, from the build tree's `tests/`):

   ```
   cmake --build build --target ControlSurfaceReferenceTest ModulationLayerProjectRoundTripTest -j2
   cmake --build build --target zene -j2
   cd build/tests && ctest -R 'ControlSurfaceReferenceTest|ModulationLayerProjectRoundTripTest|ControlStableIds|ControlReversibilityTranscript' --output-on-failure; echo EXIT=$?
   ```

2. The two transcripts (`ControlStableIds`, `ControlReversibilityTranscript`) were checked only as far as a
   no-argument run can check them: usage prints, exit 2 (their documented usage code), `py_compile` clean.
   Their assertions were **not** executed.
3. `ControlSurfaceReferenceTest`'s typed-reply leg is unproven for every id it invokes; the mutating ids
   (`controller.template_save` / `template_apply`, `safestart.acknowledge` / `clear` / `set_skip`) are
   contract-only by design.
4. The `KNOWN-LIMITATIONS.md` / `RELEASE-NOTES` "line present" checks in §5 are **term-presence** measurements
   with the matched lines read by hand; they are not a per-feature parsing of the document's own convention.

## 8. Gates run in this worktree (unpiped exit codes)

| command | exit | result |
|---|---|---|
| `bash tests/fork-sources-gate.sh` (Gate 9) | **0** | `PASS: every tracked source in scope is registered (626 fork-NEW, 1103 inherited, 40 tooling)` |
| `bash tests/unregistered-tests-gate.sh` (Gate 10) | **0** | `PASS: every test source under tests/ is registered, or declared with a reason` (169 scanned) |
| `bash tests/no-tautology-gate.sh` | **0** | `PASS: every registered test file has test slots and real assertions, no literal tautologies` |
| `bash tests/file-length-gate.sh` | **1** | **pre-existing red, not this lane's**: `src/core/ControlReversibilityTablePassive.cpp (518)`, `tests/control-stable-ids-slice2.py (549)`, `tests/src/core/ControlRegistryTest.cpp (505)` — all three untouched here, and none of the two new files (242 / 297 lines, both under the 500-line ratchet) appears |
| `scripts/zene-feature-tracker.py --release-wt …` (read-only) | **0** | `341 registered ids measured` |

## 9. Hotspots (likely collisions for the merge train)

- `hotspot: tests/CMakeLists.txt` — every lane registers here; this lane added two `LMMS_TESTS` entries and
  two `add_test(NAME …)` blocks.
- `hotspot: tests/fork-sources.txt` — the entry list on this branch is **already not reproducible from its own
  header recipe** (the recipe emits `tests/src/core/ModulationLayerTest.cpp` and others the list does not
  carry; they are registered in `tests/all-sources.txt` instead). This lane appended two entries in sorted
  position instead of re-generating, and said so in the file. **The merge should re-generate both manifests
  once, at the tip.**
- `hotspot: tools/mcp-zene-control/zene_control/commands_snapshot.json` — derived and stale by design
  (334 ids, no `stem.*`, no newest `safestart`/`wasm` generation); never hand-edited here. Expect
  `ControlCommandsSnapshot` red in this lane for exactly that reason.
- `hotspot: src/core/ControlReversibilityTable*.cpp` and `include/ControlRegistryGroups.h` — **not touched by
  this lane**: it registered no command and added no A16 row, so no per-area table file was needed.
