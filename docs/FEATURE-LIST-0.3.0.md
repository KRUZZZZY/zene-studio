# Feature list — Zene Studio 0.3.0

The single list of **every feature 0.3.0 adds to LMMS**. It is a commitment, not a wish list: the owner
decision of 2026-09-13 (D12) is that **0.3.0 takes all the engine work** — everything that can exist in the
backend and be driven through the control surface, unless it is gated by something architectural. So a row
here is either already in the tree, or it is work this release owes. Nothing is implied by omission: the
features that are **out of scope**, the features that are **in the tree but not drivable**, and the commands
that exist only to refuse are each their own section below.

Read this with `docs/COVERAGE-MATRIX-2026-09-13.md` (the measurement of the surface) and
`V0.3-V0.5-RELEASE-LADDER.md` (which items the directive moved in).

## How to read this file

**The scope contract** (charter, `NEXT-0.3.0-AGENT-PROMPT.md` §3.1). A feature is in 0.3.0 only if all four
hold: the engine work is in the tree; it has a control-surface command group (ids, argument and result
schemas, A16 reversibility metadata); it has a proof (a registered ctest, or a control-surface transcript
committed as evidence); and its UI absence is written down. If it cannot be driven or observed through the
socket, it is not in this release.

**Status vocabulary**, exactly three states.

- **in the tree** — engine present, command group present, registered proof present. The proof is named.
- **partial** — present in part; what is missing is named.
- **to build** — not in the tree; the recorded dependency is named, or "none" where the sources record none.

**Base of record.** Status is stated against the audit's tree, `ddf5f171d` on `release/0.3.0`
(`docs/COVERAGE-MATRIX-2026-09-13.md`, branch `030/audit`). Rows the scope ledger records as landed **after**
that measurement are marked *(landed since the audit)*; their groups were re-verified in the release tree at
`334790219` and the proof given is the one registered there. A feature is called **in the tree** only where
the audit's §6.2, or a registered ctest, says so.

**Item numbers.** Every number in this file is from the **owner's 31-item list** (`BACKLOG.md`, the
assessment of 2026-09-12) and is written `OWNER-31 item N`. Nothing here uses the STATUS.md Bar-2 `1–45`
numbering; where that list is meant it is written `STATUS item N`. The two collide — see
`ITEM-NUMBERING-CROSSWALK-2026-09-13.md`. Statuses also cite the wave numbering (`W1–W7`,
`ableton-gap/PLAN-zene-studio.md`), and decisions `D11`/`D12` (`MASTER-PLAN.md` §3).

**The surface these features are driven through.** At the audit tip: **28 command groups · 150 command ids**,
each with schemas and reversibility metadata. At the release tree (`334790219`): **31 groups · 170 ids** —
the new `freeze`, `bounce`, `groove` and `record` groups and the three `transport.punch_*` ids are the
difference.

---

## 1. Session and arrangement

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 1 | Session View — engine: clip/scene model (#594), launch semantics (#595), Follow Actions + Arrangement Record (#596) | `session.*`, 11 ids: `session.get_state`, `session.set_grid`, `session.set_quantisation`, `session.set_scene`, `session.set_slot`, `session.clear`, `session.clear_slot`, `session.launch_slot`, `session.launch_scene`, `session.stop_slot`, `session.stop_all` | **partial** — the model and launch engine are in the tree and the group is registered behind `LMMS_HAVE_SESSION_VIEW`. At the audit tip only 6 of 11 ids were proved (`ControlSessionLaunch`, `control-session-m1.py`); the other five gained a registered reference in the `030/test-gaps` lane. **Missing:** Follow Actions and Arrangement Record register **no ids at all** — `FollowAction::Type` exists in `SessionModelCore` but nothing drives it and there is no arrangement-record path; and the release-configuration flip (`WANT_SESSION_VIEW` and the `session-view` row of `tests/advertised-features.tsv`, in one commit) is not made | charter In §3.2 (W1); audit §6.2, Table A, §3.3 |
| 2 | Clip fades, crossfades and clip gain — this is the whole of "clip/object effects" | `clip.set_fade`, `clip.set_gain`, `clip.crossfade` (of the `clip.*` 10 ids) | **in the tree** — proof `ClipEditsTest` and `ClipFadesRenderTest` (registered, and the render proof asserts the equal-power identity and hashes every render as `AB_EVIDENCE`). Stated limits: audio clips only (a MIDI clip is refused, typed), and a crossfade is a pair of independent fades, not a linked object | charter In §3.2 (engine gaps); ladder row for OWNER-31 item 12; audit §6.2 |
| 3 | Comping — take lanes and a non-destructive composite (W4) | `comp.*`, 7 ids: `comp.lane_add`, `comp.lane_remove`, `comp.lane_list`, `comp.assign`, `comp.select`, `comp.rebuild`, `comp.get_state` | **in the tree** — proof `TakeLaneCompTest` (7/7) and `TakeLaneTest`; the proof is a byte-identity pair (take files and buffers sha256-identical after every command and after save/reload). Stated limit: no playback path consumes the composite, so a comp does not sound different yet | charter In §3.2 (W4); audit §6.2 |
| 4 | Phase-locked multitrack edit groups | none yet | **partial** — the group *entity* landed (`include/VcaGroup.h`, `Mixer::createVcaGroup`, `VcaGroupTest`) but there is no `vca.*` group to drive it and the edit-group half is to build; a group can only be created by editing the project file | ladder row for OWNER-31 item 11; audit Table B #4 |
| 5 | Folder tracks | none yet | **to build** — dependency: **none** (OWNER-31 items 3/20/21, "depends: nothing"). Lane `030/folder-tracks` is dispatched, recovering the unmerged `next/trackfolder` rather than rebuilding. The layout/workspace-presets half of the same item stays on 0.5.0 by its own record | ladder row for OWNER-31 items 3/20/21; ledger "Dispatched to close…"; audit §6.1 |
| 6 | Linked / smart clips | none yet | **to build** — dependency: OWNER-31 items 8/22 name item 11 / #611 ("editing must exist first"). Carried here because D12's prose names linked clips among the items the ladder moves in — see *Reconciliation*, which records that no ladder table row covers items 8/22 | D12 (`MASTER-PLAN.md` §3); master list, OWNER-31 items 8/22 |

## 2. Automation and modulation

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 7 | Modulation layer (#602) and per-note expression | `modulator.*`, 7 ids: `modulator.get_state`, `modulator.create`, `modulator.remove`, `modulator.rate_set`, `modulator.target_set`, `modulator.depth_set`, `modulator.target_remove`; plus `note.expression_*`, 3 ids: `note.expression_set`, `note.expression_get`, `note.expression_clear` | **in the tree** — proof `ModulationLayerValueTest`, `ModulationLayerTest` (which measures **0 allocations over 64 blocks**), `ControlModulatorCommandsTest`, `ControlNoteExpressionCommandsTest`. Stated limits: applied once per audio block, not sample-accurately; LFO source only; targets are device parameters inside a mixer channel's rack chains | charter In §3.2 (W5); audit §6.2 |
| 8 | MPE capture, storage and edit (#601) | `note.expression_*` — the same 3 ids as row 7 | **partial** — the fields are stored on the `Note` and serialized, and the group is drivable, but **only the pitch axis reaches playback**: pressure and timbre reach no instrument. Drivable but inert | charter In §3.2 (W5); audit §6.2 and Table B #12 |
| 9 | Sample-accurate automation | none yet | **to build** — dependency: engine work, not architectural (the ledger states it exactly that way). The tree's own header describes the current behaviour as non-sample-accurate (`include/AudioEngine.h:304`) | charter In §3.2 (engine gaps); ledger, absent item 1; audit §6.1 |
| 10 | Automation modes | `automation.*`, 5 ids | **partial** — the group is behavioural (5/5 exercised by `ControlAutomationScriptTest` and by `control-socket-integration.py`), but `automation.mode_set` is a **registered command that always refuses** ("this build has no automation modes") | audit Table A and §5 (stubs) |
| 11 | Note random, note transform and slide notes | none yet | **partial** — engine and registered tests are in the tree (`NoteRandomTest`, `NoteTransformTest`, `SlideNotesTest`, `MidiProbabilityPersistenceTest`) but there is no command group; `note.*` covers add / move / remove / resize / select / velocity_set / expression_* only | audit Table B #11 |

## 3. Recording and capture

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 12 | Punch in / out | `transport.punch_set`, `transport.punch_get_state`, `transport.punch_clear` | **in the tree** *(landed since the audit)* — proof `ControlPunchTranscript` (registered ctest: the GATE flips with the transport position on both sides of both boundaries, and the region survives `project.save` / `project.open`, read out of the saved file). Stated limit: the audio-side gate is deferred and named in `KNOWN-LIMITATIONS.md` | charter In §3.2 (engine gaps); ledger "Landed since that measurement" |
| 13 | Recording crash recovery | `record.*`, 6 ids: `record.journal_begin`, `record.journal_update`, `record.journal_finish`, `record.recovery_get_state`, `record.recovery_restore`, `record.recovery_discard` | **in the tree** *(landed since the audit, and upgraded)* — proof `ControlRecordingRecovery` (registered ctest: it journals a capture, **SIGKILLs** the instance — a real abnormal exit, asserted as exit −9 — then starts a second instance that finds and recovers the take). The ledger records this as an upgrade from the partial `project.restore_revision` | charter In §3.2 (engine gaps); ledger "upgraded from a partial" |
| 14 | Multi-track recorder | none yet | **partial** — a real 2-track recorder is in the tree with tests (`MultiTrackRecorderTest`, `TwoTrackRecordingHarness`, `TwoTrackAlsaCaptureProbe`, registered in `src/core/CMakeLists.txt`) but no `record.*` group drives the recorder; and the id that should cover it, **`track.set_arm`, is a registered refusal stub** — the feature and the command contradict each other | audit Table B #8 and §5 |
| 15 | Retrospective MIDI capture | none yet | **to build** — dependency: **none** ("days-weeks, no dependency", owner-lifted 2026-09-12). Lane `030/retro-capture` is dispatched, recovering `next/midi-retro` / `next/midi-retro-impl` | ladder row for OWNER-31 item 14; ledger "Dispatched" |
| 16 | Retrospective audio capture | none yet | **to build** — dependency: #611 (input count + the ALSA capture path), which is in this line; the remainder is a hardware caveat, named rather than hidden | ladder row for OWNER-31 item 15 |

## 4. MIDI and controllers

*(Added to the reader's order the brief gives: the sources carry MIDI-input and controller items that no
other area covers.)*

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 17 | MIDI learn | `midi.learn_toggle` (of the `midi.*` 2 ids) | **partial** — the learn path landed and the id is registered, but at the audit tip it appeared **only** in `tests/upstream-modifications.txt`, a manifest, and Table A scored the group 1/2. The `030/test-gaps` lane added a registered reference | master list ("MIDI learn landed"); audit Table A and §3.3 |
| 18 | MIDI controller auto-reconnection | none yet | **to build** — dependency: **none** ("buildable now, backend by backend"); which backends expose hotplug notice is recorded as needing research | ladder row for OWNER-31 item 7; audit §6.1 |
| 19 | Controller soft-takeover, LED feedback, mapping templates | none yet | **to build** — dependency: **none for the engine half** ("the engine half has no gate"). OSC does not come with it: OSC is Bar 3 and stays out (see *Out of scope*) | ladder row for OWNER-31 item 24; audit §6.1 |

## 5. Audio engine and DSP

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 20 | Freeze / bounce-in-place | `freeze.track`, `freeze.region`, `freeze.unfreeze`, `bounce.in_place` | **in the tree** *(landed since the audit)* — proof `ControlFreezeCommandsTranscript` (registered ctest, end to end through `--control-socket`: it builds an audible session, renders the take and proves the take plays; it reports ctest *Skipped*, never *Passed*, when the build has no loadable instrument) | charter In §3.2 (engine gaps); ledger "Landed since that measurement" |
| 21 | Export dither and explicit SRC quality | `export.*`, 3 ids: `export.get_settings`, `export.set_dither`, `export.set_src_quality` | **in the tree** — proof `ExportDitherTest` (TPDF moments, decorrelation, determinism, off-by-default on real WAVs), `AudioResamplerRatioTest` (the converter ratio convention), and `ControlExportSettings` (registered by the `030/test-gaps` lane, which closes the audit's finding that this was the one group with **no test at all**). Stated limits: dither is off unless asked for, and the default SRC stays the historical converter | charter In §3.2 (engine gaps); audit §6.2 and §3.3 |
| 22 | Warp markers and the clip warp tempo mode (W2) | `warp.*`, 5 ids: `warp.list`, `warp.add`, `warp.move`, `warp.remove`, `warp.set` | **in the tree** — proof `ControlWarpCommandsTest` (5/5). Stated limit: the stretch is resampling, so a warp changes pitch (there is no pitch-preserving stretch — row 30) | charter In §3.2 (W2); audit §6.2 |
| 23 | WASM DSP sandbox, and the documented WASM effect ABI (#614) | `wasm.*`, 6 ids behind `#ifdef LMMS_HAVE_WASM` | **partial** — in the tree and drivable with a registered end-to-end proof (`ControlWasmSandbox`, `control-wasm-sandbox.py` 6/6, registered only when `WANT_WASM`), **but compiled out of every release build** — no CI job provisions the wasmtime C API — and `wasm.load` hosts a module in the host sandbox, not in any device chain, so nothing is heard | charter In §3.2 (#614); audit §6.2 and Table A |
| 24 | LUFS / loudness metering | none yet | **partial** — engine and tests are in the tree (`LufsMeterTest`, `LoudnessReportTest`, `MasteringTest`); there is no `lufs.` or `meter.` group and `export.get_settings` does not expose it | audit Table B #3 |
| 25 | Mastering chain / auto-mastering | none yet | **partial** — in the tree (`src/core/MasteringChain.cpp`, `MasteringJob.cpp`, `MasteringTest`); no `mastering.*` group. Auto-mastering wave 1 (#610: candidate generation and objective scoring) is boarded; wave 3's learned ranker is **out** (it needs pick-logs) | audit Table B #2; master list (#610); charter Out §3.3 |
| 26 | Stem separation | none yet | **partial** — offline HTDemucs over ONNX Runtime is in the tree with five registered tests (`OnnxRuntimeStemSeparatorTest`, `StemExportTest`, `StemJobManagerTest`, `StemModelStoreTest`, `StemSplitPipelineTest`); no `stem.*` group, and the only route is `stem_split_cli.py`, outside the socket. The release-honesty row is `WANT_STEM_SPLIT OFF` | audit Table B #1 |
| 27 | PDC and sidechain | none yet | **partial** — in the tree with registered tests (`PdcMixerTest`, `PhaseDSidechainTest`, `MixerRoutingBackwardCompatTest`); latency compensation is neither readable nor settable through the socket | audit Table B #5 |
| 28 | Routing graph | none yet | **partial** — in the tree with registered tests (`RoutingGraphTest`, `RoutingGraphLiveTest`, `RackTest`) and live in the audio path; no command group. The patcher GUI is missing and is out of scope (§ *Out of scope*) | audit Table B #6 |
| 29 | Audio ports / `AudioBus` | none yet | **partial** — in the tree with five registered tests (`AudioPortsTest`, `AudioPortsModelTest`, `AudioBusTest`, `AudioBusHandleTest`, `PluginAudioPortsTest`); pin and bus topology are reachable only from C++ | audit Table B #7 |
| 30 | Pitch-preserving time-stretch | none yet | **to build** — dependency: "a DSP project" — a size judgement, which the ladder states is not a gate | ladder row for OWNER-31 item 9; audit §6.1 |

## 6. Tempo, meter and groove

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 31 | Tempo map — tempo and time-signature changes | `transport.tempo_map_*`, 5 ids: `transport.tempo_map_get`, `transport.tempo_map_add`, `transport.tempo_map_remove`, `transport.tempo_map_clear`, `transport.tempo_map_set_active` | **in the tree** — proof `TempoMapTest` (the map's arithmetic, the verbatim empty-map path measured as a byte-identical round trip, the play head retiming) and `ControlTempoMapCommandsTest` (schemas, typed refusals, the inverse of every mutating command). Stated limits: events are steps, no tempo curves; a time-signature event changes bar/beat arithmetic, not the tick-to-frame rate | charter In §3.2 (engine gaps); ladder row for OWNER-31 item 26 (the keystone); audit §6.2 |
| 32 | Groove pool and quantise | `groove.*`, 7 ids: `groove.list`, `groove.extract`, `groove.apply`, `groove.quantize`, `groove.set`, `groove.rename`, `groove.remove` | **in the tree** *(landed since the audit)* — proof `GrooveTemplateTest`, `ControlGrooveCommandsTest`, and the end-to-end `ControlGrooveCommands` ctest, which captures a feel, applies it to another clip, quantises with a strength and a humanise amount, undoes each and **reads every number back off the wire** | charter In §3.2 (engine gaps); ledger "Landed since that measurement" |
| 33 | Tempo-map export / SMF cross-DAW interchange | none yet | **to build** — dependency: OWNER-31 item 26 (the tempo map) — **now satisfied**, so the ladder un-gates it. `grep -rniI` for the standard-MIDI-file vocabulary (`smf`, `standard midi file`) over `src include` → 0 hits | ladder row for OWNER-31 item 5; audit §6.1 |
| 34 | Transient / BPM / key detection on import | none yet | **to build** — dependency: OWNER-31 items 26 and 16 — **both now satisfied** | ladder row for OWNER-31 item 10; audit §6.1 |
| 35 | Chord track, chord detection, progression tools, generators | none yet | **to build** — dependency: the scale machinery already exists; no architectural gate | ladder row for OWNER-31 item 25; audit §6.1 |

## 7. Sync and interchange

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 36 | Ableton-Link session sync (W6) | `link.*`, 5 ids: `link.get_state`, `link.set_enabled`, `link.set_quantum`, `link.set_start_stop_sync`, `link.set_session_tempo` | **in the tree** — proof `ControlLinkCommandsTest` and `ControlLinkSync` — **two real binaries, one session, one driving the other's tempo and beat phase through `--control-socket`**, with the phase compared against elapsed wall time; registered `RUN_SERIAL` and reports *Skipped* when the host cannot carry multicast. Stated limit: `zene-link-style` semantics without the Ableton Link library, so a Link-enabled third-party application cannot join yet | charter In §3.2 (W6); audit §6.2 |
| 37 | DAWproject import / export | none yet | **to build** — dependency: OWNER-31 items 26 and 5 plus item 11; `grep -rniI 'dawproject'` across `src include tests tools docs` → 0 hits | ladder row for OWNER-31 item 19; audit §6.1 |
| 38 | Project collection / archive, hashing, relink | none yet | **to build** — dependency: **none** ("detection buildable now"); the portable-bundle half is Bar 3 | ladder row for OWNER-31 item 18; audit §6.1 |

## 8. Project and files

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 39 | Bounded, coalescing undo | `control.undo_depth`, `control.set_undo_depth`, `control.set_undo_coalescing` (with `control.undo` / `control.redo`) | **in the tree** — proof `UndoBoundsTest` (the count cap and the byte budget measured through the socket; a 200-call drag asserted to be one journal step and one record; the window-at-0 negative control) and `ReversibilityUndoTest` | charter In §3.2 (engine gaps); audit §6.2 |
| 40 | Autosave / project recovery | `project.restore_revision` (of the `project.*` 4 ids) | **partial** — in the tree and drivable, and the one in-tree *recovery* feature that is, but it is **referenced by no behavioural test** (Table A scores the group 3/4) | audit Table B #14 and Table A |
| 41 | Plugin chains as reusable presets | none yet | **to build** — dependency: **none** ("buildable now — `EffectChain` already saves and loads"); `grep -rliIE` for `chain.?preset` or `EffectChainPreset` → 0 hits | ladder row for OWNER-31 item 2; audit §6.1 |
| 42 | mmpz-git depth (#612) | none yet | **to build** — dependency: the mmpz-git tooling itself is DONE (15/15); the depth is #612 (3-way merge, conflict presentation, large assets, an audible-diff CLI, CI render recipes) | charter In §3.2 (#612); master list #612 |

## 9. Browser and content

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 43 | Browser tag/metadata search and the waveform peak cache (W7) | `browser.*`, 6 ids: `browser.query`, `browser.roots`, `browser.tags`, `browser.tag.add`, `browser.tag.remove`, `browser.peaks` | **in the tree** — proof `BrowserCatalogTest` (the engine: a byte-written RIFF/WAVE probe, the tag store's round trip, the peak cache's hit/miss behaviour) and `ControlBrowserCommandsTest` (6/6 — it drives `browser.tag.*` through `control.undo` and then reads the store file back) | charter In §3.2 (W7); audit §6.2 |

## 10. Plugin hosting

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 44 | Rack macros and key/velocity zones (W3) | `rack.*`, 12 ids: `rack.get_state`, `rack.add_chain`, `rack.remove_chain`, `rack.set_selected`, `rack.macro_add`, `rack.macro_remove`, `rack.macro_target_add`, `rack.macro_target_remove`, `rack.macro_set`, `rack.zone_add`, `rack.zone_remove`, `rack.zone_resolve` | **in the tree** — proof `RackMacrosTest` (12/12) and `RackZonesTest` (4/12). Stated limit: **no note path consults a zone in this build** — `rack.zone_resolve` reports which zone a note *would* fall into and nothing acts on that answer | charter In §3.2 (W3); audit §6.2 |
| 45 | CLAP hosting on Windows | none yet | **to build** — dependency: none named. The engine change is `LoadLibraryW` / `GetProcAddress` with the honesty manifest back to `*`, and the proof named is the three Windows CI jobs | charter In §3.2 (Hosting) |
| 46 | Plugin scan cache and quarantine | none yet | **partial** — in the tree with a registered `PluginScanCacheTest`; no command group, and the documented quarantine route is hand-editing a JSON file | audit Table B #9 |

## 11. The agent / control surface

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 47 | The control surface itself: `--control-socket` and the in-app command registry | 28 groups · 150 ids at the audit tip; 31 groups · 170 ids at the release tree | **in the tree** — every id carries argument and result schemas and A16 reversibility metadata; at the audit tip 141 of 150 ids were referenced by a registered test artefact and **149 of 150 were swept to a typed reply** by the registered `agent_surface` ctest (`telemetry.consent` is the one documented allowlist entry) | charter §3.1 and In §3.2 (A11–A15); audit §1–§3 |
| 48 | `ARCH-2` — the control registry as a `zene::api` boundary | none yet | **to build** — dependency: none (0.3.0 by D11). The proof named is that the boundary compiles headless with no Qt widget includes; `grep -rniI` for `zene::api` or `namespace zene` over `src include` → 0 hits at the audit tip (the registry is `lmms::ControlRegistry`) | charter In §3.2 (Architecture); ledger, absent item 4; audit §6.1 |
| 49 | MCP bridge coverage of the tree's surface | none yet | **to build** — dependency: none. The measured gap at the audit tip: **10 groups with no MCP tool** (`browser`, `comp`, `export`, `link`, `modulator`, `rack`, `session`, `telemetry`, `warp`, `wasm`) and **74 ids** invisible, because the registered bridge serves a stale 70-id 0.1.0-alpha cache against the tree's 144-id snapshot; lane `030/mcp-coverage` is dispatched. The mechanism needs no per-feature bridge work — a live instance at the configured socket closes the whole gap | audit §4 and §8; ladder "Wave 2 queue" |
| 50 | Lua API stabilisation (#613) | `script.*`, 2 ids: `script.list`, `script.run` | **partial** — drivable, with `ScriptBindingsTest`, `ScriptEngineTest` and `ScriptStabilisationTest` behind it, but the binding deliberately reaches **no** mixer channel, effect chain, plugin, send, PDC, automation clip, controller or settings object — a pattern-editing API, not a DAW-control API | charter In §3.2 (#613); audit Table B #13 |
| 51 | Stable-ID contract, slice 2 | none yet | **partial** — half-delivered: only `trk-<n>` is persistent today; five id families are still index-derived | master list (`ableton-gap/AGENT-TOOLING.md` §5); charter §3.1 |

## 12. Engineering and process

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 52 | Real-time-safety whole-tree verification programme | n/a (a programme, not a command) | **to build** — dependency: "a programme; nothing in the tree". No test, gate or tool implements it — `grep -rliI 'real-time safety' tests docs tools` finds prose only; allocation probes exist per feature, not as a sweeping gate | charter In §3.2 (verification programmes); ledger, absent item 2; audit §6.1 |
| 53 | Golden-audio integration programme | n/a (a programme, not a command) | **to build** — dependency: "a programme; nothing in the tree". `grep -rliI 'golden'` across the whole tree returns **one** file, a pinned 0.1.0-alpha snapshot | charter In §3.2 (verification programmes); ledger, absent item 3; audit §6.1 |
| 54 | Crash reporter | none yet | **partial** — in the tree with a registered `CrashReporterTest`; no command group | audit Table B #10 |
| 55 | `REL-2` — a release job that cannot run from a red commit | n/a (a gate) | **to build** — dependency: none (NEW). The window is live: 0.2.0 and 0.2.1 were both tagged from red commits | charter In §3.2 (Process); master list `REL-2` |
| 56 | `REPO-2` — a gate refusing evidence file types and oversized files | n/a (a gate) | **to build** — dependency: the same decision as `REPO-1` (`CP-1`, still open) | charter In §3.2 (Process); master list `REPO-2` |
| 57 | `REPO-4` — move lane reports and transcripts out of the repository root | n/a (a move) | **partial** — the naming decision and the audit's own matrix are under `docs/`; the repository root still carries the lane reports | charter In §3.2 (Process); master list `REPO-4` |
| 58 | The whole-tree ratchet decision | n/a (a decision, executed) | **in the tree** — 49 recorded per-path `--reanchor-file <path> "<reason>"` invocations on the two `-all` baselines; after them the whole-tree complexity, file-length and duplication gates each exit 0, and regenerating `tests/all-sources.txt` from its own command found a file no whole-tree gate measured | charter In §3.2 (Process); `docs/CONVENTIONS.md` |
| 59 | `DOC-5` — commit or remove the specifications the docs and code cite | none yet | **to build** — dependency: none named; it is also a decision about this workspace's files, so it is the owner's call | charter In §3.2 (Process); master list `DOC-5` |

---

## Out of scope for 0.3.0

Verbatim in substance from the charter (§3.3) and the ladder. None of these is a gap; each is a decision.

**Taste and curation — Bar 3.** `34` modern stock devices (≈30 instruments and effects, null-tested,
denormal-safe), `35` factory content (GBs of licence-cleared samples, 500+ presets), `36` the design system
(a component library plus a retrofit of every Qt widget). Bar 3 is 0% and years, and stays where D11 put it.

**Every UX/UI item.** The clip-launch grid (#598), the rack and modulation UIs, the marker editors, the
browser's audition and drag-and-drop, the comping gestures, `ARCH-6` (the new UI), the design-system
retrofit, accessibility and localisation. Stated plainly, because it is the release's own promise: the
interface stays deliberately minimal, and **almost nothing on this list is operable from it**.

**Hardware- or ear-bound, so not verifiable here.** Controller surfaces, input monitoring, the real-interface
half of arbitrary input count, MIDI-learn by hand, the nightly plugin-compatibility matrix, and the learned
ranker (`#610` wave 3 needs pick-logs). An engine half may land — e.g. the retrospective-capture engine in
rows 15 and 16 — but it ships as **"unverified on real hardware"**, and that sentence goes in the notes.

**Licence-gated.** ARA2 hosting: it needs an ARA2 SDK decision and a real plugin.

**Architectural, deliberately separate — 0.4.0.** A new document model (`ARCH-4`: scenes, lanes and warp as
native concepts, removing `WANT_SESSION_VIEW`) and **multicore graph scheduling** (a scheduler over the
`RoutingGraph`), plus the decision-shaped rows `ARCH-5` (the upstream-divergence ledger), `ARCH-7` (the
time-boxed Tracktion Engine evaluation) and `ARCH-8`. Each can destabilise everything above it, and none is a
feature a tester can tick off. `ARCH-2` is **not** in this group — D11 included it in 0.3.0 (row 48).

**The ladder places no taste, UI or UX.** It is a ladder of *engine* rungs; it neither schedules nor forbids
the UI phase, which the owner's stated intent makes a phase of its own once the engine work is in. The
directive also does not change the *content* of any item — it re-scopes placement only.

**The two recorded exclusions the ladder does not overturn.** They are exclusions, not deferred work; either
one would be a new owner decision. **OWNER-31 item 17** — project preview renders, recorded as "in NEITHER
bar → deliberate exclusion". **OSC** — Bar 3, and it does not come in with the controller work in row 19.

**Deferred by the charter's own words.** ML sound-similarity browser search (the W7 similarity half): the
charter's In-list carries tag/metadata search and says similarity is "engine-only or deferred (needs a
model)". It is not committed here.

**Also 0.5.0, not 0.3.0** — the layout/workspace-presets half of OWNER-31 items 3/20/21, anything found
incomplete during the 0.3.0 or 0.4.0 verification passes, and the release-readiness items that need real
users or machines: the beta period and bug backlog, the nightly plugin-compatibility matrix, the measured
crash-free rate, the project-format stability guarantee, the manual and localisation, and support capacity.

**Deliberately not planned at all** (not a 0.3.0 question): notation/score, surround/immersive, expression
maps/articulation switching, and the ecosystem "noise" list. The register of record is
`PLANNED-WORK-MASTER-LIST-2026-09-13.md` §"Roadmap-gap register — deliberate exclusions".

---

## In the tree but not drivable through the socket — 16 features

The owner's directive covers these: **if it cannot be driven through the socket it is not in this release**,
so each is either made drivable or it is out. They are listed by name exactly as the audit's Table B counts
them; the status column gives where each one stands on the feature table above.

| # | Feature (tree anchor) | Its tests | Why it is not drivable | Row above |
|---|---|---|---|---|
| 1 | Stem separation — offline HTDemucs via ONNX Runtime | `OnnxRuntimeStemSeparatorTest`, `StemExportTest`, `StemJobManagerTest`, `StemModelStoreTest`, `StemSplitPipelineTest` | no `stem.*` group; the only route is `stem_split_cli.py`, outside the socket | 26 |
| 2 | Mastering chain / auto-mastering | `MasteringTest` | no `mastering.*` group | 25 |
| 3 | LUFS / loudness metering | `LufsMeterTest`, `LoudnessReportTest`, `MasteringTest` | no `lufs.` or `meter.` group; `export.get_settings` does not expose it | 24 |
| 4 | VCA / edit groups | `VcaGroupTest` | no `vca.*` group; a group can only be created by editing the project file | 4 |
| 5 | PDC + sidechain | `PdcMixerTest`, `PhaseDSidechainTest`, `MixerRoutingBackwardCompatTest` | no command group; latency compensation is not readable or settable | 27 |
| 6 | Routing graph | `RoutingGraphTest`, `RoutingGraphLiveTest`, `RackTest` | no command group | 28 |
| 7 | Audio ports / `AudioBus` | `AudioPortsTest`, `AudioPortsModelTest`, `AudioBusTest`, `AudioBusHandleTest`, `PluginAudioPortsTest` | no command group; pin/bus topology is reachable only from C++ | 29 |
| 8 | Multi-track recorder | `MultiTrackRecorderTest`, `TwoTrackRecordingHarness`, `TwoTrackAlsaCaptureProbe` | no `record.*` group — and the id that should cover it, `track.set_arm`, is a refusal stub, so the feature and the command contradict each other | 14 |
| 9 | Plugin scan cache + quarantine | `PluginScanCacheTest` | no command group; the documented quarantine route is hand-editing a JSON file | 46 |
| 10 | Crash reporter | `CrashReporterTest` | no command group | 54 |
| 11 | Note random / transform / slide notes | `NoteRandomTest`, `NoteTransformTest`, `SlideNotesTest`, `MidiProbabilityPersistenceTest` | no command group; `note.*` does not cover them | 11 |
| 12 | MPE pressure + timbre | `MpeExpressionTest`, `MpeNoteStorageTest`, `MpeInputPathTest` | drivable but **inert**: only the pitch axis reaches playback | 8 |
| 13 | Lua API surface beyond the bound objects | `ScriptBindingsTest`, `ScriptEngineTest`, `ScriptStabilisationTest` | `script.run` / `script.list` are drivable, but the binding deliberately reaches no mixer channel, effect chain, plugin, send, PDC, automation clip, controller or settings object | 50 |
| 14 | Autosave / project recovery | `ProjectRecoveryTest`, `ProjectOpenIntegrityTest` | drivable, but `project.restore_revision` is referenced by no behavioural test | 40 |
| 15 | Real-time-safety whole-tree verification programme | none — no test, gate or tool implements it | not in the tree at all | 52 |
| 16 | Golden-audio integration programme | none | not in the tree at all | 53 |

## The three commands that exist only to refuse

Each is a declared, schema'd, reversibility-classified command that can never succeed. A caller sees a
command; the feature is absent. These are not features of 0.3.0 — they are the false positives the socket
would otherwise produce, and each is listed because the directive covers it.

| id | the refusal | what it contradicts |
|---|---|---|
| `track.set_arm` | "this tree has no record-arm on a song track" | `src/core/audio/MultiTrackRecorder.cpp`, a real 2-track recorder (Table B #8 / row 14) |
| `mixer.set_pan` | "this tree has no pan on a mixer channel" | `mixer.*` is otherwise behavioural 5/5 |
| `automation.mode_set` | "this build has no automation modes" | `automation.*` is otherwise behavioural 5/5 (row 10) |

---

## What this means

**59 features are on this list.** Counted from the tables above: **16 are in the tree** with a named proof,
**20 are partial** — the engine is in or partly in, and the control-surface half, the registered proof or the
routing is what is missing — and **23 are to build**. That is the whole commitment on one page: a third of it
is proved today, a third needs its socket surface or its test, and the last third is not written yet.

**The remainder is scheduled, not optional.** Every row marked *partial* or *to build* is a commitment of
0.3.0 under the owner's directive, and a row leaves this list only by an owner decision that says so — the
two recorded exclusions and the out-of-scope sections above are the only places that has happened.

---

## Reconciliation — what I could not settle

1. **The ladder's fourteen rows versus D12's prose.** The ladder's re-scope table moves **fourteen rows** in:
   OWNER-31 items 2, 5, 7, 9, 10, 11, 12, 14, 15, 18, 19, 24, 25 and 3/20/21. D12's own prose calls them
   "fourteen *functional* items" but then **enumerates fifteen names**, adding **linked clips** (OWNER-31
   items 8/22), which corresponds to no row in the ladder table. I could not determine which the owner
   intends, so linked clips is carried as **row 6, to build**, with both readings stated there. If it is not
   intended, the list is 58 features and 22 to build.
2. **"15 absent" versus "19 not in the tree".** The scope ledger says "Still ABSENT from the tree, with NO
   LANE — 15"; audit Table C §6.1 lists **19** ladder items not in the tree. They reconcile as
   19 − 3 (freeze/bounce-in-place, groove pool + quantise and punch in/out, which the ledger records as
   landed since the measurement) − 1 (folder tracks, which the ledger records as dispatched) = 15. Both
   figures are correct against their own base; neither is wrong.
3. **Audit findings the release tree has since closed, kept distinct.** The audit's §3.3 ("9 ids with no
   registered-test reference"), its §8 ("1 group with no test at all" — `export`) and its Table A `midi` 1/2
   are true of `ddf5f171d` and no longer of the release tip: the `030/test-gaps` lane registered
   `ControlExportSettings`, a session reference for all 11 `session.*` ids and a reference for
   `midi.learn_toggle`. Rows 17 and 21 say so rather than repeating the stale finding.
4. **The A16 row count is still three numbers.** The audit could not settle whether `139` (`RELEASE-NOTES`,
   line 300), `142` (`ReversibilityContractTest.cpp:87`) or `127` (the same test's docstring) is the figure
   the notes should quote; the `150` the test reduces to is the number two independent tree sources
   corroborate. Unchanged here — it needs a build, and this lane does not build.
5. **`stem.` `mastering.` etc. are absent as a class, not per feature.** Table B's grep is
   `QStringLiteral\("(stem|mastering|lufs|record|meter|vca|freeze|scan)\.` over the registry — it returned
   nothing at the audit tip. `record` and `freeze` now have groups (rows 13, 20), so that grep no longer
   holds as written; the remaining six prefixes still return nothing. That is a change in the tree, not a
   contradiction in the audit.

## Suspected misses, and where they stand

Named because a list like this is only trustworthy if it says what it decided to leave out and why.

- **MIDI clock / MTC.** A Bar-2 review-pass gap in the master list ("unboarded gap, no code"). It is engine
  work with no architectural gate, so by D12's own rule it would belong in 0.3.0 — but **no 0.3.0 document
  places it**: not the charter's In-list, not the ladder's re-scope table, not the ledger. Not carried as a
  row. If the directive means "all engine work" literally, this is the first thing it is missing.
- **Third-party instrument hosting (`STATUS item 18`) and out-of-process plugin hosting / crash isolation.**
  Both are Bar-2 items in the master list; neither the charter, the ladder nor the ledger places either in
  0.3.0. Not counted.
- **Plugin state save/restore.** The master list calls it "unboarded gap, no code", but `plugin.state_save`,
  `plugin.state_load` and the four `plugin.preset_*` ids are registered, schema'd and behavioural (11/11) at
  both bases. The master-list row is stale; it is not a 0.3.0 addition, so it has no row here.
- **Soak testing / performance at scale.** Needs users and machines, which the ladder's own scraps
  definition sends to 0.5.0. Not a 0.3.0 commitment.
- **The five index-derived stable-ID families.** Carried as row 51 rather than left implicit.
- **Bar-3-convenience and UI-only items** (custom shortcuts and the command palette, OWNER-31 items 1 and
  23; accessibility, item 29). UI, therefore out of scope by the charter — see *Out of scope*.
