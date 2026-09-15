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

**Base of record.** Status was originally stated against the audit's tree, `ddf5f171d` on `release/0.3.0`
(`docs/COVERAGE-MATRIX-2026-09-13.md`, branch `030/audit`). Rows the scope ledger records as landed **after**
that measurement are marked *(landed since the audit)*; their groups were re-verified in the release tree at
`334790219` and the proof given is the one registered there. A feature is called **in the tree** only where
the audit's §6.2, or a registered ctest, says so.

**Re-measured 2026-09-14 at the release tip `3956ef589`** — `docs/COVERAGE-REMEASURE-2026-09-14.md` on this
branch is the measurement of record for the surface; where a status below is corrected, the row carries a
`measured at 3956ef589:` line naming the command and its output. The one live reading this lane took used the
built binary at `zene-030/build/zene` (one instance, reaped by explicit PID — `pgrep -a zene` → `EXIT=1`);
that binary is built at `571016ff8`, the tip's parent, and the tip changes **no** `src/core/` file
(`git diff --name-only 571016ff8..3956ef589 -- src/core/` → empty), so its registry is the tip's registry.

**Item numbers.** Every number in this file is from the **owner's 31-item list** (`BACKLOG.md`, the
assessment of 2026-09-12) and is written `OWNER-31 item N`. Nothing here uses the STATUS.md Bar-2 `1–45`
numbering; where that list is meant it is written `STATUS item N`. The two collide — see
`ITEM-NUMBERING-CROSSWALK-2026-09-13.md`. Statuses also cite the wave numbering (`W1–W7`,
`ableton-gap/PLAN-zene-studio.md`), and decisions `D11`/`D12` (`MASTER-PLAN.md` §3).

**The surface these features are driven through.** Re-measured at the release tip `3956ef589`
(`docs/COVERAGE-REMEASURE-2026-09-14.md`): **33 command groups · 185 command ids**, each with schemas and
reversibility metadata. That is the registry source and the live instance agreeing exactly:

```
$ grep -c '\.id = QStringLiteral' src/core/ControlCommands*.cpp        # per-file, summed
185
$ python3 verification/ctl.py --socket <own dir>/audit.sock commands    # one instance at zene-030/build/zene
# 185 command(s)
$ pgrep -a zene ; kill <PID> ; pgrep -a zene ; echo EXIT=$?
EXIT=1                                                                  # reaped by explicit PID
```

The registry **source** carries **34 group names** — the 33 above plus the helper-built `wasm` group
(5 ids), which is behind `#ifdef LMMS_HAVE_WASM` and absent from a build that does not define it
(the live reading has no `wasm.*`), i.e. **190 ids** in the source over 34 groups. The measuring
instrument (`scripts/zene-feature-tracker.py`) reports **192 ids / 35 groups**, which is 2 more than
the source really has: its part-B regex matches `QStringLiteral("a.b")` anywhere, and at
`src/core/ControlCommandsBrowserTags.cpp:175` and `:202` those are `cmd.verb = QStringLiteral("tag.add")`
and `("tag.remove")` — verb fields of the registered `browser.tag.add` / `browser.tag.remove`, not ids.
See `docs/COVERAGE-REMEASURE-2026-09-14.md` §4. The figures this paragraph carried before (170 ids /
31 prefixes, measured at `334790219` and `01b99753a`) are superseded; see *Reconciliation* 6.

**Row numbers, and the consistency pass of 2026-09-13.** Rows **1–59** keep the numbers they were first
given here, so that every citation of "row N" in this project still resolves. The candidates the KB sweep
found (`V0.3-COMPLETENESS-VERDICT.md`) were added on 2026-09-13 as rows **60–89**; they are not in numerical
order within a section for exactly that reason.

## How this list relates to the other documents

**This file is the single source of truth for what 0.3.0 adds.** Every other list — `MASTER-PLAN.md` §3
**D12**, `V0.3-V0.5-RELEASE-LADDER.md`, `V0.3-SCOPE-LEDGER.md`, `V0.3-SCOPE-CORRECTIONS.md`,
`V0.3-ALPHA-PLAN.md` §0c, `BACKLOG.md`, `PLANNED-WORK-MASTER-LIST-2026-09-13.md` and
`ITEM-NUMBERING-CROSSWALK-2026-09-13.md` — is a **decision** about placement, a **ledger** of what is
missing against the tree, or an **inventory** of planned work. None of them is the scope. Where one of them
and this file disagree, this file is the list of record and the other is the record of a decision or a
measurement — and the disagreement is written down under *Reconciliation* rather than settled by whichever
document a reader happens to open first.

**The rule that classifies every row** is D12's, in the ladder's own narrow terms: an item is in 0.3.0
**unless it is gated by something architectural**. Gated means **`ARCH-4`** (a new document model) or
**multicore graph scheduling**, plus the decision-shaped rows `ARCH-5`/`ARCH-7`/`ARCH-8`. **A dependency is
not a gate** — an item whose only dependency is engine work this line is already doing is in. Taste, UI and
UX are not placed by the ladder at all, and Bar 3 (`34` devices / `35` content / `36` design system), ARA2
and the hardware- or ear-bound items are out.

---

## 1. Session and arrangement

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 1 | Session View — engine: clip/scene model (#594), launch semantics (#595), Follow Actions + Arrangement Record (#596) | `session.*`, 11 ids: `session.get_state`, `session.set_grid`, `session.set_quantisation`, `session.set_scene`, `session.set_slot`, `session.clear`, `session.clear_slot`, `session.launch_slot`, `session.launch_scene`, `session.stop_slot`, `session.stop_all` | **partial** — the model and launch engine are in the tree and the group is registered behind `LMMS_HAVE_SESSION_VIEW`. At the audit tip only 6 of 11 ids were proved (`ControlSessionLaunch`, `control-session-m1.py`); the other five gained a registered reference in the `030/test-gaps` lane. **Missing:** Follow Actions and Arrangement Record register **no ids at all** — `FollowAction::Type` exists in `SessionModelCore` but nothing drives it and there is no arrangement-record path; and the release-configuration flip (`WANT_SESSION_VIEW` and the `session-view` row of `tests/advertised-features.tsv`, in one commit) is not made | charter In §3.2 (W1); audit §6.2, Table A, §3.3 |
| 2 | Clip fades, crossfades and clip gain — this is the whole of "clip/object effects" | `clip.set_fade`, `clip.set_gain`, `clip.crossfade` (of the `clip.*` 10 ids) | **in the tree** — proof `ClipEditsTest` and `ClipFadesRenderTest` (registered, and the render proof asserts the equal-power identity and hashes every render as `AB_EVIDENCE`). Stated limits: audio clips only (a MIDI clip is refused, typed), and a crossfade is a pair of independent fades, not a linked object | charter In §3.2 (engine gaps); ladder row for OWNER-31 item 12; audit §6.2 |
| 3 | Comping — take lanes and a non-destructive composite (W4) | `comp.*`, 7 ids: `comp.lane_add`, `comp.lane_remove`, `comp.lane_list`, `comp.assign`, `comp.select`, `comp.rebuild`, `comp.get_state` | **in the tree** — proof `TakeLaneCompTest` (7/7) and `TakeLaneTest`; the proof is a byte-identity pair (take files and buffers sha256-identical after every command and after save/reload). Stated limit: no playback path consumes the composite, so a comp does not sound different yet | charter In §3.2 (W4); audit §6.2 |
| 4 | Phase-locked multitrack edit groups | `vca.*`, 14 ids: `vca.create`, `vca.remove`, `vca.list`, `vca.get_state`, `vca.rename`, `vca.set_gain`, `vca.set_mute`, `vca.set_solo`, `vca.assign`, `vca.unassign`, `vca.set_phase_lock`, `vca.track_add`, `vca.track_remove`, `vca.edit_move` | **in the tree** — the group entity (`include/VcaGroup.h`, `Mixer::createVcaGroup`, `VcaGroupTest`, task #622) is now driven by a registered group that also carries the EDIT half the row owed: an edit set of tracks by stable `trk-<n>` id and a phase lock (ON by default) under which `vca.edit_move` moves a named clip and every other member's clips that overlap its pre-command span by the SAME delta, so a multitrack take slides as one object and stays sample-aligned (one `control.undo` returns every moved clip). Proof `ControlVcaCommandsTest` + the registered ctest `ControlVcaCommands` (`tests/control-vca-commands.py`), which proves the edit set through a real save/open round trip; the entity half is exercised on a scratch `Mixer` in the same test (`tests/src/core/VcaGroupTest.cpp` is grandfathered at 1022 lines with `FILE_LINE_TOLERANCE=0`, so extending it would regress Gate 7 in the whole-tree scope). Stated limits: **one** media edit is propagated (a clip move — trim, slip, split and fades are not); a track deleted while it is in an edit set stays in it and is reported as `missing_tracks`/`skipped_tracks` until `vca.track_remove`; a clip id is index-derived, so `vca.edit_move`'s inverse is the checkpoint and not a replayed `clip-<n>`; `vca.set_solo`'s undo does not restore the transient `MixerChannel::m_muteBeforeSolo`. UI-absent: no strip, no group menu, no member list, no lock toggle, no Lua binding — the socket and the MCP bridge are the only way in | ladder row for OWNER-31 item 11; audit Table B #4; `docs/VCA-EDIT-GROUPS.md` |
| 5 | Folder tracks | `track.set_folder`, `track.folder_get_state`, `track.folder_set_collapsed`, `track.set_pinned` — the folder half of `track.*`'s 17 ids | **in the tree** — a folder is a **real engine container** (`include/TrackFolder.h`, `src/tracks/TrackFolder.cpp`): `track.add` takes `type=folder`, the child carries a `folder` attribute, and `group` (organisation, the default) and `routing` (the folder's own mixer channel sums the children) are both drivable. Proofs `TrackFolderTest` and the registered socket transcript `ControlTrackFolderTranscript`. Stated limit: drivable through the socket and **not** from the interface — `grep -rniI 'set_folder' src/gui/` → 0 hits, and the only `TrackFolder` in `src/gui/` is a comment at `src/gui/MixerView.cpp:24` (`docs/KNOWN-LIMITATIONS.md`:178). The **layout/workspace-presets** half the row used to leave to 0.5.0 is still not in the tree: `grep -rniI 'workspace preset' src include` → 0 hits, so that clause stands | ladder row for OWNER-31 items 3/20/21; ledger "Dispatched to close…"; audit §6.1 |
| 6 | Linked / smart clips | none yet | **to build** — dependency: OWNER-31 items 8/22 name item 11 / #611 ("editing must exist first"). Carried here because D12's prose names linked clips among the items the ladder moves in — see *Reconciliation*, which records that no ladder table row covers items 8/22 | D12 (`MASTER-PLAN.md` §3); master list, OWNER-31 items 8/22 |
| 60 | Clip trim | `clip.trim` | **in the tree** — the id is registered with an argument schema (`clip`, `start`, optional `end`), a result schema (`clip`, `position`, `length`, `end`, `offset`) and a `true_inverse` A16 row; the handler follows the song editor's own left-edge drag (`src/gui/clips/ClipView.cpp`) so the start, the length and the source offset move together and the audio stays at the same song position, which is what neither `clip.move` nor `clip.resize` can do alone. Proof: ctest `ControlVerbInverseTest` (invoke → read back → `control.undo` → read back, plus the A16 contract row). UI absence stated in `docs/KNOWN-LIMITATIONS.md` and the release notes: socket-only, no menu item/action/keybinding reaches the command. Dependency OWNER-31 item 12 / `#611` was a dependency, not a gate: the engine needed is `Clip`'s own serialized `pos`/`len`/`off`/`autoresize`, all present at the base. Declared limit: the verb does not author `SampleClip`'s `srcin`/`srcout` window (no reset-on-absence exists for it, so a pre-first-edit checkpoint could not undo it) — a frame-domain trim is a later feature | verdict Group A #6; `PLANNED-WORK-MASTER-LIST` :100, :387-390; branch `030/clip-note-stem-verbs` |
| 61 | Clip slip | `clip.slip` | **in the tree** — `clip.*` is now 12 ids and carries both clip-editing verbs. `clip.slip` moves the audio INSIDE a fixed clip rectangle: position and length do not move and the part of the source that plays at the clip's start becomes `offset` ticks into it. It is the FIRST implementation of the verb in the product — a case-insensitive grep for `slip` over `src/` + `include/` returns seven hits, every one a comment, and the nearest concept (the comp take's `srcpos`) is recorded but deliberately not applied (`docs/COMPING.md`) — so the design decision is recorded in `src/core/ControlCommandsClipTrim.cpp` and in the A16 row: the offset domain (`off`, serialised unconditionally) rather than the frame-domain window. `true_inverse` through the clip's own checkpoint. Proof: ctest `ControlVerbInverseTest`. UI absence in `docs/KNOWN-LIMITATIONS.md` + release notes | verdict Group A #6; `PLANNED-WORK-MASTER-LIST` :387-390; branch `030/clip-note-stem-verbs` |
| 62 | Folder tracks as a routing / mix group — the routing half of OWNER-31 item 21 | `track.set_routing` — the `routing` mode of a folder | **in the tree** *(landed since the audit)* — routing mode gives the folder **one regular mixer channel of its own** (`Mixer::createChannel()`, deliberately **not** `createBusChannel()`, which refuses instrument output) and points every child's mixer-channel binding at it, so the children's output is summed through the folder's own effect chain and sent to master like any other channel; the previous bindings are recorded in the folder's `prevch` attribute so a folder saved in routing mode can be switched back in a later session, and routing mode **refuses an empty folder, typed**. Dependency: row 5 — now satisfied. Proof `TrackFolderTest` plus the registered transcript `ControlTrackFolderTranscript`. Stated limits: drivable through the socket and not from the interface (a folder's row is an ordinary `TrackView`), and a child's mixer-channel **index** can change across a routing-off/on cycle while the relation itself is preserved (`Mixer::deleteChannel` renumbers channels) | verdict Group A #13; `BACKLOG` OWNER-31 item 21; `zene-next-trackfolder/docs/TRACK-FOLDER-DESIGN.md` §5-§7 |

**Rows 5 and 62, corrected 2026-09-14** (the row numbers, the group names, the proofs and the limits lines
above are all measured, not carried):

```
measured at 3956ef589: grep -n 'cmd.id = QStringLiteral("track.set_folder")' src/core/ControlCommandsTrackFolder.cpp -> 283:	cmd.id = QStringLiteral("track.set_folder");
measured at 3956ef589: grep -n 'cmd.id = QStringLiteral("track\.' src/core/ControlCommandsTrackFolder.cpp src/core/ControlCommandsTrackFolderSets.cpp -> 283 track.set_folder, 310 track.folder_set_collapsed, 333 track.set_routing, 361 track.set_pinned, 386 track.folder_get_state; 273 track.visibility_set_save, 298 track.visibility_set_apply, 322 track.visibility_set_remove, 344 track.visibility_set_list  (9 ids)
measured at 3956ef589: ls include/TrackFolder.h src/tracks/TrackFolder.cpp -> both present, EXIT=0
measured at 3956ef589: grep -oE '^add_test\([A-Za-z0-9_]+' build/tests/CTestTestfile.cmake -> TrackFolderTest, ControlTrackFolderTranscript  (both registered)
measured at 3956ef589: grep -n 'Folder tracks are in the engine' docs/KNOWN-LIMITATIONS.md -> 178
measured at 3956ef589: grep -rniI 'set_folder' src/gui/ -> 0 hits (EXIT=1)
measured at 3956ef589: grep -rniI 'workspace preset' src include -> 0 hits
```


## 2. Automation and modulation

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 7 | Modulation layer (#602) and per-note expression | `modulator.*`, 7 ids: `modulator.get_state`, `modulator.create`, `modulator.remove`, `modulator.rate_set`, `modulator.target_set`, `modulator.depth_set`, `modulator.target_remove`; plus `note.expression_*`, 3 ids: `note.expression_set`, `note.expression_get`, `note.expression_clear` | **in the tree** — proof `ModulationLayerValueTest`, `ModulationLayerTest` (which measures **0 allocations over 64 blocks**), `ControlModulatorCommandsTest`, `ControlNoteExpressionCommandsTest`. Stated limits: applied once per audio block, not sample-accurately; LFO source only; targets are device parameters inside a mixer channel's rack chains | charter In §3.2 (W5); audit §6.2 |
| 8 | MPE capture, storage and edit (#601) | `note.expression_*` — the same 3 ids as row 7 | **partial** — the fields are stored on the `Note` and serialized, and the group is drivable, but **only the pitch axis reaches playback**: pressure and timbre reach no instrument. Drivable but inert | charter In §3.2 (W5); audit §6.2 and Table B #12 |
| 9 | Sample-accurate automation | none yet | **to build** — dependency: engine work, not architectural (the ledger states it exactly that way). The tree's own header describes the current behaviour as non-sample-accurate (`include/AudioEngine.h:304`) | charter In §3.2 (engine gaps); ledger, absent item 1; audit §6.1 |
| 10 | Automation modes | `automation.*`, 5 ids | **partial** — the group is behavioural (5/5 exercised by `ControlAutomationScriptTest` and by `control-socket-integration.py`), but `automation.mode_set` is a **registered command that always refuses** ("this build has no automation modes") | audit Table A and §5 (stubs) |
| 11 | Note random, note transform and slide notes | none yet | **partial** — engine and registered tests are in the tree (`NoteRandomTest`, `NoteTransformTest`, `SlideNotesTest`, `MidiProbabilityPersistenceTest`) but there is no command group; `note.*` covers add / move / remove / resize / select / velocity_set / expression_* only | audit Table B #11 |
| 63 | `automation.record_mode_set` — the record-mode verb the boarded-gaps list names | none yet | **to build** — `automation.*` registers 5 ids and is behavioural 5/5, but the record-mode verb is not among them, and `automation.mode_set` is a registered refusal ("this build has no automation modes"). Dependency: row 10's automation-modes work | verdict Group A #14; `PLANNED-WORK-MASTER-LIST` :387-390 |

## 3. Recording and capture

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 12 | Punch in / out | `transport.punch_set`, `transport.punch_get_state`, `transport.punch_clear` | **in the tree** *(landed since the audit)* — proof `ControlPunchTranscript` (registered ctest: the GATE flips with the transport position on both sides of both boundaries, and the region survives `project.save` / `project.open`, read out of the saved file). Stated limit: the audio-side gate is deferred and named in `KNOWN-LIMITATIONS.md` | charter In §3.2 (engine gaps); ledger "Landed since that measurement" |
| 13 | Recording crash recovery | `record.*`, 6 ids: `record.journal_begin`, `record.journal_update`, `record.journal_finish`, `record.recovery_get_state`, `record.recovery_restore`, `record.recovery_discard` | **in the tree** *(landed since the audit, and upgraded)* — proof `ControlRecordingRecovery` (registered ctest: it journals a capture, **SIGKILLs** the instance — a real abnormal exit, asserted as exit −9 — then starts a second instance that finds and recovers the take). The ledger records this as an upgrade from the partial `project.restore_revision` | charter In §3.2 (engine gaps); ledger "upgraded from a partial" |
| 14 | Multi-track recorder | none yet | **partial** — a real 2-track recorder is in the tree with tests (`MultiTrackRecorderTest`, `TwoTrackRecordingHarness`, `TwoTrackAlsaCaptureProbe`, registered in `src/core/CMakeLists.txt`) but no `record.*` group drives the recorder; and the id that should cover it, **`track.set_arm`, is a registered refusal stub** — the feature and the command contradict each other | audit Table B #8 and §5 |
| 15 | Retrospective MIDI capture | `midi.retro_capture_arm`, `midi.retro_capture_status`, `midi.retro_capture_to_clip` — 3 of the `midi.*` 5 ids | **in the tree** *(landed since the audit)* — arming the mode keeps a rolling window of the MIDI the engine receives, per open client, so what was just played is written into a new clip **after** the fact; the engine half is `include/RetroMidiCapture.h` / `src/core/RetroMidiCapture.cpp`, and the proofs are `MidiRetroCaptureTest` and the registered socket transcript `ControlRetroCapture` (it plays real MIDI into the running engine and recovers it). Stated limit (`docs/KNOWN-LIMITATIONS.md`:578): **8192 events (128 KiB), the most recent, drop-oldest — a memory bound, not a time bound** (roughly 7–13 minutes for a human at 10–20 events a second; a dense controller stream fills the same window in under a minute), and nothing in `src/gui/` shows the window, its length or its contents (`grep -rniI 'RetroMidi' src/gui/` → 2 hits, an `#include "RetroMidiCaptureSettings.h"` at `src/gui/MainWindow.cpp:48` and a comment at `:422` — no view, no prompt, no shortcut) | ladder row for OWNER-31 item 14; ledger "Dispatched" |
| 16 | Retrospective audio capture | none yet | **to build** — dependency: #611 (input count + the ALSA capture path), which is in this line; the remainder is a hardware caveat, named rather than hidden | ladder row for OWNER-31 item 15 |
| 64 | Arbitrary input count / multiple simultaneous inputs | none yet | **to build** — dependency: engine work, not architectural (`#611`'s input-count row); the default Linux backend is playback-only today (`AudioAlsa` has no capture path). The real-interface half stays hardware-bound and unverified (see *Out of scope*) | verdict Group A #8; `PLANNED-WORK-MASTER-LIST` :103; `SURVEY-FEATURE-PRIORITY-SPEC` §2 |

**Row 15, corrected 2026-09-14, and rows 12, 13 and 20 verified unchanged:**

```
measured at 3956ef589: grep -n 'cmd.id = QStringLiteral("midi.retro' src/core/ControlCommandsMidi.cpp -> 163 midi.retro_capture_arm, 214 midi.retro_capture_status, 360 midi.retro_capture_to_clip  (3 ids)
measured at 3956ef589: ls include/RetroMidiCapture.h src/core/RetroMidiCapture.cpp -> both present, EXIT=0
measured at 3956ef589: grep -oE '^add_test\([A-Za-z0-9_]+' build/tests/CTestTestfile.cmake -> MidiRetroCaptureTest, ControlRetroCapture  (both registered)
measured at 3956ef589: grep -n 'Retrospective MIDI capture keeps the last 8192 events' docs/KNOWN-LIMITATIONS.md -> 578
measured at 3956ef589: grep -rniI 'RetroMidi' src/gui/ -> 2 hits: src/gui/MainWindow.cpp:48 (#include "RetroMidiCaptureSettings.h") and :422 (a comment); no view, no prompt, no shortcut
--- and the three rows left alone, each of which its own probe already satisfies ---
measured at 3956ef589: ctl.py commands -> transport.punch_set, transport.punch_get_state, transport.punch_clear all present (185 ids, live); grep '^add_test(' build/tests/CTestTestfile.cmake -> ControlPunchTranscript registered -> row 12 unchanged
measured at 3956ef589: ctl.py commands -> record.journal_begin/_update/_finish, record.recovery_get_state/_restore/_discard all present (6/6); grep '^add_test(' build/tests/CTestTestfile.cmake -> ControlRecordingRecovery registered -> row 13 unchanged
measured at 3956ef589: ctl.py commands -> freeze.track, freeze.region, freeze.unfreeze, bounce.in_place all present (4/4); grep '^add_test(' build/tests/CTestTestfile.cmake -> ControlFreezeCommandsTranscript registered -> row 20 unchanged
```


## 4. MIDI and controllers

*(Added to the reader's order the brief gives: the sources carry MIDI-input and controller items that no
other area covers.)*

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 17 | MIDI learn | `midi.learn_toggle` (of the `midi.*` 2 ids) | **partial** — the learn path landed and the id is registered, but at the audit tip it appeared **only** in `tests/upstream-modifications.txt`, a manifest, and Table A scored the group 1/2. The `030/test-gaps` lane added a registered reference | master list ("MIDI learn landed"); audit Table A and §3.3 |
| 18 | MIDI controller auto-reconnection | none yet | **to build** — dependency: **none** ("buildable now, backend by backend"); which backends expose hotplug notice is recorded as needing research | ladder row for OWNER-31 item 7; audit §6.1 |
| 19 | Controller soft-takeover, LED feedback, mapping templates | none yet | **to build** — dependency: **none for the engine half** ("the engine half has no gate"). OSC does not come with it: OSC is Bar 3 and stays out (see *Out of scope*) | ladder row for OWNER-31 item 24; audit §6.1 |
| 65 | Scale-aware root-note highlighting — the residual half of OWNER-31 item 6 | none — this row adds no group of its own (the scale.* group is row 66's, and it is in the tree) | **partial** — the scale machinery and the in-scale highlighting are pre-existing (`PianoRoll`'s scale selector built from `ChordTable`'s `isScale()` entries, `markSemiTone` / `MarkCurrentScale`, and key/scale/marked-semi-tone persistence); the **root note is not drawn distinctly** from the other in-scale degrees and neither colour is a theme value (checked against this tree on 2026-09-15: the **in-scale** colour IS one already — qproperty-markedSemitoneColor, data/themes/default/style.css:193 — so what does not exist is a distinct **root** colour and its theme value). Stated plainly: D12's rule places this row, but the drawing change itself is a piano-roll/UI change, which the ladder does not place. **RESOLVED 2026-09-15 (task #694): DEFERRED TO THE INTERFACE PHASE — no socket-observable half is left to land.** The one non-UI half this row could own (a scale/root-note fact a view could read and a socket could drive, with command ids, schemas, an A16 row and a registered proof) is row 66's scale.* group, and that group is **already in the release tree at this base**: landed in the note/scale/device wave at c1282f6c7, an ancestor of release/0.3.0, with its A16 rows in src/core/ControlReversibilityTableNoteScale.cpp and the registered ctest ControlNoteScaleVerbsTest as its proof — so landing it here would be row 66 a second time, and this row is the residual half, not that half. What is left is exactly what this row's own sources name (BACKLOG OWNER-31 item 6; ui-research/UI-DIRECTION-RECONCILED.md item 5): the **root note drawn distinctly** from the other in-scale degrees, and a **theme value** for that root colour. Both are the interface — the highlight is one draw loop in src/gui/editors/PianoRoll.cpp:3659-3670 painting every marked semitone with the single colour m_markedSemitoneColor (include/PianoRoll.h:82, :545), and the state behind it is the piano-roll **window's** own (m_keyModel / m_scaleModel / m_markedSemiTones, written by PianoRollWindow::saveSettings, not by the project) — and a colour and a style-sheet value can neither be driven nor observed through the socket. By the scope contract's own rule (CHARTER §3.1: if it cannot be driven or observed through the socket, it is not in this release) this row therefore leaves 0.3.0 for the **interface phase**, which the ladder does not place and which *Out of scope* below already calls a phase of its own. A placement, not a deletion: no gate, baseline, manifest, test or row was weakened or removed, no engine work is owed by this row, and the same decision is written in docs/RELEASE-NOTES-v0.3.0-alpha.md and docs/KNOWN-LIMITATIONS.md | verdict Group A #12; `BACKLOG` OWNER-31 item 6; `docs/MIDI-DEPTH.md` §1.1 |
| 66 | `scale.*` — the scale command group the boarded-gaps list names | none yet | **to build** — no `scale.` id exists at either base; the engine (the scale + key vocabulary) is pre-existing, so the work is the group and its proofs | verdict Group A #14; `PLANNED-WORK-MASTER-LIST` :387-390 |
| 67 | `note.probability_set` — the probability verb the boarded-gaps list names | `note.probability_set` | **in the tree** — the engine is `Note::probability()` / `setProbability(float)` (`include/Note.h:140-141`, clamped to [0, 1] at `src/core/Note.cpp:158-161`), persisted per note as the OPTIONAL `prob` attribute (`src/core/Note.cpp:282-285`), read back with a default of 1 (`:325-329`) and consumed at trigger time by `NoteRandom::passesProbability` (`src/tracks/InstrumentTrack.cpp:906-911`). The registered id adds an argument schema (`clip`, `note`, `probability`), a result schema and a `true_inverse` A16 row through the owning `MidiClip`'s checkpoint — the `note.velocity_set` shape, because a `Note` is a SerializingObject and not a JournallingObject. A probability outside [0, 1] is REFUSED typed before the checkpoint, never silently clamped (SPEC A11). Proof: ctest `ControlVerbInverseTest` — invoke → read back → `control.undo` → read back from the state the trap lives in (a note at the DEFAULT, which writes no `prob` attribute at all). UI absence is already stated by `docs/MIDI-DEPTH.md` and is repeated in `docs/KNOWN-LIMITATIONS.md` and the release notes | verdict Group A #14; `PLANNED-WORK-MASTER-LIST` :387-390; audit Table B #11; branch `030/clip-note-stem-verbs`  |

## 5. Audio engine and DSP

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 20 | Freeze / bounce-in-place | `freeze.track`, `freeze.region`, `freeze.unfreeze`, `bounce.in_place` | **in the tree** *(landed since the audit)* — proof `ControlFreezeCommandsTranscript` (registered ctest, end to end through `--control-socket`: it builds an audible session, renders the take and proves the take plays; it reports ctest *Skipped*, never *Passed*, when the build has no loadable instrument) | charter In §3.2 (engine gaps); ledger "Landed since that measurement" |
| 21 | Export dither and explicit SRC quality | `export.*`, 3 ids: `export.get_settings`, `export.set_dither`, `export.set_src_quality` | **in the tree** — proof `ExportDitherTest` (TPDF moments, decorrelation, determinism, off-by-default on real WAVs), `AudioResamplerRatioTest` (the converter ratio convention), and `ControlExportSettings` (registered by the `030/test-gaps` lane, which closes the audit's finding that this was the one group with **no test at all**). Stated limits: dither is off unless asked for, and the default SRC stays the historical converter | charter In §3.2 (engine gaps); audit §6.2 and §3.3 |
| 22 | Warp markers and the clip warp tempo mode (W2) | `warp.*`, 5 ids: `warp.list`, `warp.add`, `warp.move`, `warp.remove`, `warp.set` | **in the tree** — proof `ControlWarpCommandsTest` (5/5). Stated limit: the stretch is resampling, so a warp changes pitch (there is no pitch-preserving stretch — row 30) | charter In §3.2 (W2); audit §6.2 |
| 23 | WASM DSP sandbox, and the documented WASM effect ABI (#614) | `wasm.*`, 6 ids behind `#ifdef LMMS_HAVE_WASM` | **partial** — in the tree and drivable with a registered end-to-end proof (`ControlWasmSandbox`, `control-wasm-sandbox.py` 6/6, registered only when `WANT_WASM`), **but compiled out of every release build** — no CI job provisions the wasmtime C API — and `wasm.load` hosts a module in the host sandbox, not in any device chain, so nothing is heard | charter In §3.2 (#614); audit §6.2 and Table A |
| 24 | LUFS / loudness metering | none yet | **partial** — engine and tests are in the tree (`LufsMeterTest`, `LoudnessReportTest`, `MasteringTest`); there is no `lufs.` or `meter.` group and `export.get_settings` does not expose it | audit Table B #3 |
| 25 | Mastering chain / auto-mastering | `mastering.*`, 3 ids: `mastering.list_candidates`, `mastering.get_state`, `mastering.run` | **in the tree** *(landed since the audit)* — the engine was already there (`src/core/MasteringChain.cpp`, `src/core/MasteringJob.cpp`, `MasteringTest`) and is now **drivable**: `mastering.list_candidates` publishes the wave-1 candidate set the engine generates (`MasteringJob::defaultCandidates()`: five candidates varying target loudness, true-peak ceiling and the dynamics stage, each target with the document its numbers come from — EBU R 128's published −23 LUFS-I ± 0.5 LU and −1 dBTP, and a −14 LUFS-I streaming *convention* with a tolerance this project chose and states); `mastering.run` renders the mix **ONCE** (`render_count` is the engine's own `ProjectRenderer::renderCount()`), branches every candidate off that one render, writes one wav per candidate into a directory the caller names and measures each with the merged BS.1770-4 meter (LUFS-I, loudest 3 s window, measured dBTP, crest, the residual against that candidate's own target and its loudness and true-peak verdicts); `mastering.get_state` reads the last run back and hashes the files it wrote. The run is a **child process on a serialised copy** of the session (`render.render` / `bounce.in_place`'s rule), so the session is not modified. `mastering.run` is **`true_inverse`** through a recorded ACTION checkpoint: the output directory's `.wav` entries are captured before the first write (bounded at 64 MiB; beyond that the run is REFUSED rather than performed without an inverse), the recorded step removes what the run created and writes replaced revisions back byte for byte, and there is **no redo half** (stated in the row and in the record). Proof: `MasteringTest` (engine, incl. the report-document ↔ run agreement) and the registered ctest `ControlMasteringCommands` (`tests/control-mastering-commands.py`, 35 checks over `--control-socket`). **Stated limits:** nothing ranks the candidates and none is preferred; `wav` only; no per-candidate parallelism; renders are not bit-reproducible run to run, so two runs agree only to the meter's tolerance (≤ 0.05 LU / 0.01 dB); the UI absence is in `docs/KNOWN-LIMITATIONS.md` | audit Table B #2; master list (#610); charter Out §3.3 |
| 26 | Stem separation | none yet | **partial** — offline HTDemucs over ONNX Runtime is in the tree with five registered tests (`OnnxRuntimeStemSeparatorTest`, `StemExportTest`, `StemJobManagerTest`, `StemModelStoreTest`, `StemSplitPipelineTest`); no `stem.*` group, and the only route is `stem_split_cli.py`, outside the socket. The release-honesty row is `WANT_STEM_SPLIT OFF` | audit Table B #1 |
| 27 | PDC and sidechain | none yet | **in the tree** — with registered tests (`PdcMixerTest`, `PhaseDSidechainTest`, `MixerRoutingBackwardCompatTest`); **readable** through `--control-socket` with `pdc.report` (the mixer's published total latency, every channel's alignment point (`Mixer::channelInputLatency`) and chain latency, the compensation applied at every send, the direct track inputs, and whether sidechain routing exists with each route's tap point and deferred flag), and the sidechain half is settable with `mixer.sidechain_to` / `mixer.route_remove`; **not settable as a value** by decision — `Mixer::updateLatencyCompensation()` recomputes every edge's delay once per period, so a command that wrote one would be overwritten by the next period. Proof: `tests/control-pdc-commands.py` (ctest `ControlPdcCommands`, 15 checks). **Bound:** the published number is 0 unless a device reports latency (`Effect::latencyFrames()` defaults to 0; only the WASM effect overrides it), so the arithmetic of a nonzero delay is proven by `PdcMixerTest` and the surface by the transcript | audit Table B #5, and this row's own "neither readable nor settable" sentence |
| 28 | Routing graph | none yet | **in the tree** — with registered tests (`RoutingGraphTest`, `RoutingGraphLiveTest`, `RackTest`) and live in the audio path; the graph is **readable** through `--control-socket` with `routing.get_state` (nodes with the engine's own type names, connections, the cached topological processing order, the output node, `EffectChain::routesThroughGraph`, and a mixer channel's `Rack` graph). **No command EDITS a graph, by decision**: `include/RoutingGraph.h`'s threading contract forbids live topology edits and names the atomic plan swap it deliberately does not implement, and a chain's graph is DERIVED (`EffectChain::rebuildRoutingGraph` re-wires it from the effect list, so a hand-wired edge would be discarded by the next `plugin.load`). **Measured scope:** a chain whose devices HAVE audio-ports models keeps the plain loop (`EffectChain::rebuildRoutingGraph` returns early, `src/core/EffectChain.cpp:89`), and every built-in device is `AudioPlugin`-derived (`DefaultEffect`), so the chain graph is normally EMPTY and the graph with live prepared nodes is the **rack's** — `tests/control-routing-commands.py` (ctest `ControlRoutingCommands`, 30 checks) measures it: two added chains = five nodes, six connections, output node 1, prepared at the engine's block size. The settable topology in this release is the MIXER's: `mixer.route_to` / `mixer.send_to` / `mixer.sidechain_to` / `mixer.route_remove`. The patcher GUI is missing and is out of scope (§ *Out of scope*) | audit Table B #6 |
| 29 | Audio ports / `AudioBus` | none yet | **in the tree** — with five registered tests (`AudioPortsTest`, `AudioPortsModelTest`, `AudioBusTest`, `AudioBusHandleTest`, `PluginAudioPortsTest`); the **bus topology** is drivable with `bus.list` / `bus.create` / `bus.remove` (a bus is a `MixerChannel` with `is_bus`, so its fader and routing are `mixer.set_volume` and the routing verbs — there is no `bus.set_*`), and the **pin matrix** is drivable with `port.get_state` / `port.set_pin` (`AudioPortsModel::Matrix::setPin`, the PinConnector view's own write, validated before it writes). Proof: `tests/control-bus-commands.py` (ctest `ControlBusCommands`, 15 checks, which measures BOTH A16 answers: `bus.create` is one undoable step, `bus.remove` makes `control.undo` fail typed `irreversible`) and `tests/control-ports-commands.py` (ctest `ControlPortsCommands`, 11 checks — the pin write is MEASURED, not skipped). **Bounds:** `bus.remove` is not reversible (nothing recreates a channel With state); `port.set_pin` needs a device that HAS an `AudioPortsModel` — in this tree every built-in effect is `AudioPlugin`-derived (`DefaultEffect`) and so has one, and a build where none does reports the transcript's ctest **Skipped** (never Passed) | audit Table B #7 |
| 30 | Pitch-preserving time-stretch | none yet | **to build** — dependency: "a DSP project" — a size judgement, which the ladder states is not a gate | ladder row for OWNER-31 item 9; audit §6.1 |
| 68 | Stem export — per-track / per-bus, post-fader, tail convention, headless CLI (distinct from stem *separation*, row 26) | `render.stems` | **in the tree**, with three declared limits — the engine is `RenderManager::exportStems()` (`include/RenderManager.h:40-52` `StemExportOptions{tailBars=1, alignToProjectLength=true}`, `:76`) driven through the shipped `lmms exportstems` CLI (`src/core/main.cpp:381`, `:1002-1008`, `:1033-1036`) in a child process, exactly as `render.render` drives `lmms render` and for the same reason (`src/core/ControlCommandsProject.cpp:296-309`, `:327-333`). The registered id adds an argument schema (`out` absolute directory, `format` enum, `tail_bars`), a result schema (`directory`, `format`, `sample_rate`, `tail_bars`, `count`, `stems[]`) and a `not_mutating` A16 row. Proof: ctest `ControlStemExportVerb` — a REAL export over a live socket (two tracks, the documented `<index>_<name>.wav` naming, non-empty RIFF/WAVE files, and a second export into the same directory that must report the files it REWROTE rather than refuse an empty set). **Declared limits, all in `docs/KNOWN-LIMITATIONS.md`:** (a) **per track, not per bus** — a "bus" is a `MixerChannel`, not a `Track`, and the render path isolates tracks by muting (`docs/STEM-EXPORT.md`, "No bus-level stems"); (b) **per-fader and post-effects is what the engine does**, and the tail is one bar past the project end by default (`tail_bars` settable), which is the whole-project render's own convention; (c) **the render bound is declared, not fixed** — the child blocks the dispatch thread on `waitForFinished(600000)` so the control surface does not answer, `control.ping` included, until the export finishes (`docs/RENDER-CHILD-WAIT.md:120-126` records the defect and designs the deferred-reply fix; this lane does NOT build it), and the ctest gives the call its own declared per-command budget (`RENDER_TIMEOUT = 180.0`, the `tests/freeze_bounce_evidence.py:57` number) instead of raising a socket timeout. UI absence in `docs/KNOWN-LIMITATIONS.md` + release notes: the File menu's "Export Tracks..." action is the different, pre-existing `renderTracks()` path and is neither changed by nor wired to this id | verdict Group A #5 and #14; `docs/STEM-EXPORT.md`; `PLANNED-WORK-MASTER-LIST` :387-390; branch `030/clip-note-stem-verbs` |
| 69 | Patcher node-graph driving | `patcher.*` | **to build** — no `patcher.` id exists at either base; the routing-graph engine it would drive is in the tree (row 28) and the patcher GUI is out of scope | verdict Group A #7; `PLANNED-WORK-MASTER-LIST` :387; `lanes/W0-BRIEF.md` §7 |
| 70 | Render / export presets | none yet | **to build** — `OutputSettings` exists (bit depth, sample rate, stereo mode) and the export path is in the tree (row 21), but there is no saved preset list; `grep` for `export preset` / `RenderPreset` / `batch export` → nothing | verdict Group A #11; `BACKLOG` OWNER-31 item 27; gap register :126 |
| 71 | Selection-to-audio | none yet | **to build** — the render entry is whole-project; a time range the render path can take is the work | verdict Group A #11; `BACKLOG` OWNER-31 item 27 |
| 72 | Auto-mastering wave 1 (`#610`) — candidate generation + objective scoring | `mastering.*`, 3 ids: `mastering.list_candidates`, `mastering.get_state`, `mastering.run` | **in the tree** *(landed since the audit)* — both halves of wave 1 are built and drivable: **candidate generation** (`MasteringJob::defaultCandidates()`, published with each target's cited source by `mastering.list_candidates`) and **objective scoring** (`MasteringJob::measureCandidate()` over the merged BS.1770-4 meter, published per candidate by `mastering.run` and read back by `mastering.get_state`). Proof: `MasteringTest` (engine — incl. the new `theReportDocumentIsTheRunItself`, which holds the document the surface reads to the run's own reports field by field) and the registered ctest `ControlMasteringCommands` (35 checks: 5 candidates from 1 counted render, every candidate inside its own target's tolerance and at or below its ceiling, `control.undo` removes what a first run created and restores a re-run's replaced revision byte for byte, the 64 MiB capture bound refused typed). **What remains UNBUILT of wave 1, named rather than implied:** the *decision* half the feasibility study's step 5 names — **no pick-log** (no record of which candidate a user chose), and therefore **no learned ranker** (wave 3 is gated on real user pick-logs, which do not exist yet, and the study's own rank-correlation finding is why nothing here orders the candidates); **no reference-matching arm**; **no level-matched A/B** (the candidates differ in loudness by design — that is the target axis); **`wav` only**; the cost of `MasteringChain::process()` was never benchmarked. `docs/AUTO-MASTERING.md` §8 is the record; `docs/KNOWN-LIMITATIONS.md` and the release notes carry the same sentences | verdict Group A #16; `docs/AUTO-MASTERING.md`; master list (`#610`) |
| 73 | `CODE-5` — WASM worker: shared pool, real wake-ups, deterministic offline render | none yet | **to build** — it touches the render-determinism contract the roadmap already ships against; check that contract before building | verdict Group A #15; `BACKLOG` § Change-plan register, `CODE-5` |

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
| 74 | MIDI clock / MTC — the DAW as clock master or slave | `clock.*`, 3 ids: `clock.get_state`, `clock.master_set`, `clock.slave_set` | **partial** *(landed since the audit)* — the **MIDI clock** half is in the tree: the DAW runs as a clock **master** (24 pulses to the quarter note, START/STOP/CONTINUE and a Song Position Pointer emitted from the audio thread through the engine's own MIDI output) and as a clock **slave** (it follows an incoming clock, measures its tempo over one quarter note of pulses and writes that tempo to the song when told to follow); the engine half is `include/MidiClock.h` / `src/core/MidiClock.cpp`, and the proofs are `MidiClockTest` (the rate arithmetic) and the registered socket transcript `ControlClockCommands`. **MTC is absent, and stays recorded as absent**: `clock.get_state` reports `mtc: "absent"` and the group has no timecode command, because a full-frame timecode master needs a frame rate, a drop-frame flag and a SMPTE start offset and this engine's time model is ticks-per-bar with neither — `docs/KNOWN-LIMITATIONS.md`:566. Two further bounds, both stated in the same bullet: the bytes reach a MIDI device only where a real backend is open (a headless run measures what the engine PRODUCED, not what an instrument received), and an incoming START/STOP/CONTINUE/SONG POSITION does not move the transport in this release. Drivable through the socket, not from the interface (`grep -rniI 'MidiClock' src/gui/` → 0 hits). Dependency: **none**; pure engine work with no architectural gate. **No 0.3.0 document placed this before this list**: it was the corrections document's strongest candidate gap (unplaced by the charter's In-list, the ladder and the ledger), it stands here because D12's rule places it rather than because a decision did, and the group that landed does not change that provenance | verdict Group A #4; `PLANNED-WORK-MASTER-LIST` :160; `AGENT-SURFACE-INVENTORY` Group 11; `V0.3-SCOPE-CORRECTIONS.md` § "Candidate gaps" |

**Row 74, corrected 2026-09-14 to *partial*** — the clock group is measured present, and MTC is measured
absent and stays recorded as absent:

```
measured at 3956ef589: grep -n 'cmd.id = QStringLiteral("clock\.' src/core/ControlCommandsClock.cpp -> 209 clock.get_state, 240 clock.master_set, 268 clock.slave_set  (3 ids)
measured at 3956ef589: ls include/MidiClock.h src/core/MidiClock.cpp -> both present, EXIT=0
measured at 3956ef589: grep -oE '^add_test\([A-Za-z0-9_]+' build/tests/CTestTestfile.cmake -> MidiClockTest, ControlClockCommands  (both registered)
measured at 3956ef589: grep -n -i 'MTC' src/core/ControlCommandsClock.cpp -> 21: 'MTC, stated: clock.get_state reports mtc: "absent" and this group has no'; 219: 'Read-only. mtc reports "absent": this'
measured at 3956ef589: grep -n 'MIDI time code (MTC) is not generated at all' docs/KNOWN-LIMITATIONS.md -> 566
measured at 3956ef589: grep -rniI 'MidiClock' src/gui/ -> 0 hits (EXIT=1), against 36 hits for MidiLearn in the same directory
```


## 8. Project and files

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 39 | Bounded, coalescing undo | `control.undo_depth`, `control.set_undo_depth`, `control.set_undo_coalescing` (with `control.undo` / `control.redo`) | **in the tree** — proof `UndoBoundsTest` (the count cap and the byte budget measured through the socket; a 200-call drag asserted to be one journal step and one record; the window-at-0 negative control) and `ReversibilityUndoTest` | charter In §3.2 (engine gaps); audit §6.2 |
| 40 | Autosave / project recovery | `project.restore_revision` (of the `project.*` 4 ids) | **partial** — in the tree and drivable, and the one in-tree *recovery* feature that is, but it is **referenced by no behavioural test** (Table A scores the group 3/4) | audit Table B #14 and Table A |
| 41 | Plugin chains as reusable presets | `chain.*`, 6 ids: `chain.list`, `chain.get_state`, `chain.save`, `chain.apply`, `chain.rename`, `chain.remove` | **in the tree** *(landed since the audit)* — `chain.save` captures a target's effect chain as a named preset: every device in the chain's own order, each with the state document `plugin.state_save` writes for it, as **one** file in the store (`<userPresets>/chainpresets/<name>.zcp`, outside the project); `chain.apply` puts it on another track in another project, and `control.undo` restores the file a save replaced. Proofs `ControlChainPresets` (the registered socket transcript) and `ControlChainPresetTest`, both registered. Stated limit (`docs/KNOWN-LIMITATIONS.md`:516): the store is **per-user, not per-project**, and nothing in `src/gui/` creates, shows, edits or applies a preset — drivable through the socket, not from the interface. The `grep -rliIE 'chain.?preset'` this row used to cite is superseded by the registration itself | ladder row for OWNER-31 item 2; audit §6.1 |
| 42 | mmpz-git depth (#612) | none yet | **to build** — dependency: the mmpz-git tooling itself is DONE (15/15); the depth is #612 (3-way merge, conflict presentation, large assets, an audible-diff CLI, CI render recipes) | charter In §3.2 (#612); master list #612 |
| 75 | Undo robustness — structural-op journalling, and undo of a deleted track (OWNER-31 item 4) | none yet | **to build** — the journalling *pattern* exists (`ProjectJournal`, `CheckPointStack m_undoCheckPoints`, `MAX_UNDO_STATES = 100`), but `TrackContainer::removeTrack` erases the pointer and the destruction path has already deleted the track's clips, so a deletion is unrecoverable; add / remove / move track and add / remove effect are **not journalled** and several paths bypass `addJournalCheckPoint`. It is the mechanism A16 already obliges (#623) | verdict Group A #9; `PLANNED-WORK-MASTER-LIST` :151; `BACKLOG` OWNER-31 item 4 |
| 76 | In-app revision timeline (OWNER-31 item 30) | none yet | **to build** — the artefacts it would list already exist on disk (the `.bak` written on every save, the autosave sidecar, and `mmpz-git` when the project is in a repository); the panel over them is the work. Dependency: none — item 18's hashing only if revisions must be shareable | verdict Group A #10; `BACKLOG` OWNER-31 item 30 and §4 shortlist |
| 77 | Safe-start mode after a crash — launch with third-party plugins disabled (OWNER-31 item 31) | none yet | **to build** — its two prerequisites are already in the tree (the crash reporter, row 54, and the plugin scan cache + quarantine, row 46); the crash marker and the load-time "skip plugin instances" predicate are the work. Dependency: none architectural | `BACKLOG` OWNER-31 item 31; `PLANNED-WORK-MASTER-LIST` (crash reporter; plugin scan cache) |

**Row 41, corrected 2026-09-14** — this is the row the generated feature queue was still sending the fleet
at, and the `chain.*` group is measured present:

```
measured at 3956ef589: grep -n 'cmd.id = QStringLiteral("chain\.' src/core/ControlCommandsChain.cpp src/core/ControlCommandsChainEdit.cpp -> 198 chain.list, 248 chain.get_state, 282 chain.save, 147 chain.apply, 189 chain.rename, 266 chain.remove  (6 ids)
measured at 3956ef589: python3 verification/ctl.py --socket .../audit.sock commands -> chain.list, chain.get_state, chain.save, chain.apply, chain.rename, chain.remove all present in the live 185
measured at 3956ef589: grep -oE '^add_test\([A-Za-z0-9_]+' build/tests/CTestTestfile.cmake -> ControlChainPresets, ControlChainPresetTest  (both registered)
measured at 3956ef589: grep -n 'Plugin-chain presets have no interface' docs/KNOWN-LIMITATIONS.md -> 516
measured at 3956ef589: grep -n 'chain.save' docs/RELEASE-NOTES-v0.3.0-alpha.md -> 349
measured at 3956ef589: python3 verification/ctl.py --socket .../audit.sock describe chain.save -> "Capture a target's effect chain as a named preset ... Writes ONE file in the store (chainpresets/<name>.zcp), outside the project ... Reversible through a recorded action checkpoint that puts the file - or the revision it replaced - back."
```


## 9. Browser and content

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 43 | Browser tag/metadata search and the waveform peak cache (W7) | `browser.*`, 6 ids: `browser.query`, `browser.roots`, `browser.tags`, `browser.tag.add`, `browser.tag.remove`, `browser.peaks` | **in the tree** — proof `BrowserCatalogTest` (the engine: a byte-written RIFF/WAVE probe, the tag store's round trip, the peak cache's hit/miss behaviour) and `ControlBrowserCommandsTest` (6/6 — it drives `browser.tag.*` through `control.undo` and then reads the store file back) | charter In §3.2 (W7); audit §6.2 |

## 10. Plugin hosting

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 44 | Rack macros and key/velocity zones (W3) | `rack.*`, 12 ids: `rack.get_state`, `rack.add_chain`, `rack.remove_chain`, `rack.set_selected`, `rack.macro_add`, `rack.macro_remove`, `rack.macro_target_add`, `rack.macro_target_remove`, `rack.macro_set`, `rack.zone_add`, `rack.zone_remove`, `rack.zone_resolve` | **in the tree** — proof `RackMacrosTest` (12/12) and `RackZonesTest` (4/12). Stated limit: **no note path consults a zone in this build** — `rack.zone_resolve` reports which zone a note *would* fall into and nothing acts on that answer | charter In §3.2 (W3); audit §6.2 |
| 45 | CLAP hosting on Windows | none yet | **to build** — dependency: none named. The engine change is `LoadLibraryW` / `GetProcAddress` with the honesty manifest back to `*`, and the proof named is the three Windows CI jobs | charter In §3.2 (Hosting) |
| 46 | Plugin scan cache and quarantine | `plugin.*` scan group, 6 ids: `plugin.scan_cache_get_state`, `plugin.scan_cache_list`, `plugin.scan_cache_lookup`, `plugin.scan_cache_quarantine_add`, `plugin.scan_cache_quarantine_remove`, `plugin.rescan` | **in the tree** — engine `include/PluginScanCache.h` + `PluginFactory`'s scan, proof `PluginScanCacheTest` (extended with the two enumeration cases the group needed); the group is registered and proven by `ControlPluginScanCommands` — the cache FILE is read back off disk as well as off the wire, the recorded inverse restores a quarantined entry WITH its reason (and the negative control measures the loss a path-only inverse would cause), and a quarantined plugin really leaves `plugin.list` after `plugin.rescan`. The hand-edit route the audit named is retired: the two quarantine verbs write the list through the engine's own API. Stated limits: the cache's CONTENTS became reachable only with this group (`PluginScanCache::records()` / `record(path)` are new and read-only); nothing in `src/gui/` shows a scan record, a cache hit or a quarantine entry, nor offers to add one; a missing, corrupt or wrongly-versioned cache file still means a full scan, by the engine's own contract | audit Table B #9 |
| 78 | Third-party VST3 instrument hosting (`STATUS item 18`) | none yet | **partial** — the VST3-instrument path is PARTIAL (the descriptor audit found the three VST3 instrument tests never build and the VST3 effect tests unregistered) while the effect host is in the tree; the item is the instrument half plus its lifecycle. The three readings of the hosting evidence are disagreement 1 below | verdict Group A #3; `PLANNED-WORK-MASTER-LIST` :107; `zene-pa-instrview/docs/INSTRUMENT-HOSTING-SPEC.md` |
| 79 | CLAP instrument hosting | none yet | **to build** — there is **no CLAP instrument hosting at all**; CLAP hosting is effects-only and, on the Windows jobs, off (`-DWANT_CLAP=OFF`). Dependency: none named — the holder is the `LoadLibraryW` / `GetProcAddress` port (row 45) and the three Windows CI jobs | verdict Group A #3; `PLANNED-WORK-MASTER-LIST` :107 |
| 80 | Out-of-process plugin hosting / crash isolation | none yet | **partial** — landed on `post-alpha/oop-hosting`, **ZynAddSubFx only**; the ecosystem-gap analysis calls it blocking. Its three readings are disagreement 3 below | verdict Group A #2; `docs/OOP-HOSTING.md`; `STATUS-CORRECTION` §93; `ecosystem-gap/` §3.1 |
| 81 | `device.mpe_set` — the MPE device verb the boarded-gaps list names | none yet | **to build** — `note.expression_*` (3 ids) is the MPE surface that landed (row 8); the device-side verb was never delivered | verdict Group A #14; `PLANNED-WORK-MASTER-LIST` :387-390 |
| 82 | `CODE-4` — plugin hosts process in chunks instead of truncating or overrunning | none yet | **to build** — `ClapHost.cpp:747` clamps to `maxFrames`; `Vst3Host.cpp` sizes `silence` / `scratchOutput` to `maxBlockSize` with **no clamp**, and neither truncates correctly when the host asks for more than the prepared block | verdict Group A #15; `BACKLOG` § Change-plan register, `CODE-4` |
| 83 | `CODE-9` — Windows named-pipe control transport | none yet | **to build** — the only one of `CODE-9`'s three halves with no precedent in the tree (the VST3 instrument polling half is folded into row 78; the process-context atomics are ungated) | verdict Group A #15; `BACKLOG` § Change-plan register, `CODE-9` |

## 11. The agent / control surface

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 47 | The control surface itself: `--control-socket` and the in-app command registry | **33 command groups · 185 command ids** at `3956ef589`, each with schemas and reversibility metadata (34 group names in the registry source, the extra one being the helper-built `wasm` group that no release build compiles in) | **in the tree** — the id set is identical in the registry source and in a live `control.commands_list` (185 = 185, empty difference both ways), and at the audit tip 141 of 150 ids were referenced by a registered test artefact and **149 of 150 were swept to a typed reply** by the registered `agent_surface` ctest (`telemetry.consent` is the one documented allowlist entry). The figures this row carried before (28 groups · 150 ids at the audit tip; 31 groups · 170 ids at the release tree) are superseded | charter §3.1 and In §3.2 (A11–A15); audit §1–§3; `docs/COVERAGE-REMEASURE-2026-09-14.md` |
| 48 | `ARCH-2` — the control registry as a `zene::api` boundary | none yet | **to build** — dependency: none (0.3.0 by D11). The proof named is that the boundary compiles headless with no Qt widget includes; `grep -rniI` for `zene::api` or `namespace zene` over `src include` → 0 hits at the audit tip (the registry is `lmms::ControlRegistry`) | charter In §3.2 (Architecture); ledger, absent item 4; audit §6.1 |
| 49 | MCP bridge coverage of the tree's surface | none yet | **to build** — dependency: none. The measured gap at the audit tip: **10 groups with no MCP tool** (`browser`, `comp`, `export`, `link`, `modulator`, `rack`, `session`, `telemetry`, `warp`, `wasm`) and **74 ids** invisible, because the registered bridge serves a stale 70-id 0.1.0-alpha cache against the tree's 144-id snapshot; lane `030/mcp-coverage` is dispatched. The mechanism needs no per-feature bridge work — a live instance at the configured socket closes the whole gap | audit §4 and §8; ladder "Wave 2 queue" |
| 50 | Lua API stabilisation (#613) | `script.*`, 2 ids: `script.list`, `script.run` | **partial** — drivable, with `ScriptBindingsTest`, `ScriptEngineTest` and `ScriptStabilisationTest` behind it, but the binding deliberately reaches **no** mixer channel, effect chain, plugin, send, PDC, automation clip, controller or settings object — a pattern-editing API, not a DAW-control API | charter In §3.2 (#613); audit Table B #13 |
| 51 | Stable-ID contract, slice 2 | none yet | **partial** — half-delivered: only `trk-<n>` is persistent today; five id families are still index-derived | master list (`ableton-gap/AGENT-TOOLING.md` §5); charter §3.1 |
| 84 | Telemetry v1 (`#617`) — opt-in platform statistics and the `-DZENE_TELEMETRY=OFF` kill switch | `telemetry.*`, 2 ids: `telemetry.consent`, `telemetry.status` (behind `#ifdef ZENE_TELEMETRY_ENABLED`) | **in the tree** — proof `TelemetryTest` (2/2) and `ControlRegistryTest` (2/2), both registered; the group is absent from a build that does not define `ZENE_TELEMETRY_ENABLED`, and `telemetry.consent` is the **one** `agent-surface-allowlist.txt` entry the whole-tree sweep does not exercise (`requires: display, human`). Stated limit: nothing asserts the build-option string, and the release-build default is a release decision rather than a test | verdict Group A #1; `docs/TELEMETRY-V1.md`, `docs/TELEMETRY-KILL-SWITCH.md`; `STATUS-CORRECTION` §3 |
| 85 | `telemetry.consent_set` — the consent verb the boarded-gaps list names | none yet | **to build** — the tree registers `telemetry.consent` and `telemetry.status`; `consent_set` is a different id and does not exist at either base | verdict Group A #14; `PLANNED-WORK-MASTER-LIST` :387-390 |
| 86 | `CODE-6` — Lua: a memory budget beside the instruction budget | none yet | **to build** — `ScriptEngine.cpp:96` (`luaL_newstate`) has no allocator hook | verdict Group A #15; `BACKLOG` § Change-plan register, `CODE-6` |
| 87 | `CODE-7` — telemetry transport: https only, never block the caller | none yet | **to build** — the transport only; the consent model is on the change plan's "Keep" list and is not touched | verdict Group A #15; `BACKLOG` § Change-plan register, `CODE-7` |
| 88 | `CODE-8` — control-server shutdown hook must survive its owner | none yet | **to build** | verdict Group A #15; `BACKLOG` § Change-plan register, `CODE-8` |
| 89 | The A16 reversibility contract, and the row-count deliverable that goes with it | n/a (a contract over every registered id) | **partial** — the contract is in the tree: every registered id carries reversibility metadata, and `ReversibilityContractTest` is registered and green (3/3 at `bcf440d61`, with `ControlRegistryTest` and `ReversibilityUndoTest`), the table having been split into three TUs on 2026-09-13 to satisfy the file-length gate. **The deliverable that is not settled is the row count** — 139 / 142 / 127 / 150 / 155 / 157 are all in circulation; see disagreement 6. This lane does not settle it either: the figures it can measure are the **185 registered ids** and the reversibility table's own 155-row note at `docs/RELEASE-NOTES-v0.3.0-alpha.md`:430, which is not a run of the test | verdict Group A #17; `W13-A16-SPLIT-2026-09-13.md`; `ableton-gap/A16-STATUS-MEASURED.md` |

**Row 47, corrected 2026-09-14** — the surface figures it quoted were the two stale bases:

```
measured at 3956ef589: grep -h -oE '\.id = QStringLiteral\("[a-z0-9_.]+"\)' src/core/ControlCommands*.cpp | sed 's/.*("//;s/")//' | sort -u | wc -l -> 185
measured at 3956ef589: grep -h -oE '\.id = QStringLiteral\("[a-z0-9_.]+"\)' src/core/ControlCommands*.cpp | sed 's/.*("//;s/")//' | sed 's/\..*//' | sort -u | wc -l -> 33
measured at 3956ef589: python3 verification/ctl.py --socket .../audit.sock commands -> "# 185 command(s)", 33 distinct prefixes
measured at 3956ef589: python3 verification/ctl.py --socket .../audit.sock ping -> "version": "0.2.1-alpha.159+571016f"  (the built binary is the tip's parent; git diff --name-only 571016ff8..3956ef589 -- src/core/ is empty)
measured at 3956ef589: pgrep -a zene -> no match, EXIT=1  (the one instance was reaped by explicit PID)
```


## 12. Engineering and process

| # | Feature | Command group / ids | Status | List it comes from |
|---|---|---|---|---|
| 52 | Real-time-safety whole-tree verification programme | n/a (a programme, not a command) | **to build** — dependency: "a programme; nothing in the tree". No test, gate or tool implements it — `grep -rliI 'real-time safety' tests docs tools` finds prose only; allocation probes exist per feature, not as a sweeping gate | charter In §3.2 (verification programmes); ledger, absent item 2; audit §6.1 |
| 53 | Golden-audio integration programme | n/a (a programme, not a command) | **to build** — dependency: "a programme; nothing in the tree". `grep -rliI 'golden'` across the whole tree returns **one** file, a pinned 0.1.0-alpha snapshot | charter In §3.2 (verification programmes); ledger, absent item 3; audit §6.1 |
| 54 | Crash reporter | `crash.*`, **6 ids**: `crash.list_reports`, `crash.acknowledge_report`, `crash.discard_report`, `crash.upload_report`, `crash.enable`, `crash.disable` | **in the tree** — engine `include/CrashReporter.h`, proof `CrashReporterTest`, and since 2026-09-15 (board task #643) the arm pair's own proof `CrashReporterArmTest` (the engine, real signals, with the negative control) plus `tests/control-crash-reporter.py` on the socket; the group is registered and proven by `ControlCrashReporter` — the report and the `offered` sentinel are measured ON DISK as well as on the wire, and both writers are asserted to make `control.undo` fail typed `irreversible` with a named fallback. Stated limits: `crash.upload_report` is a REFUSAL by name (this build has no upload and no network code of any kind in the module); `crash.acknowledge_report` / `crash.discard_report` are `irreversible` with named fallbacks (nothing un-writes the sentinel, nothing writes a report from a caller's bytes); nothing in `src/gui/` shows a report, its state or its directory, and there is no way to send one | audit Table B #10 |
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
interface stays deliberately minimal, and **almost nothing on this list is operable from it.** **Row 65 is
placed here by name (2026-09-15, task #694):** scale-aware root-note highlighting — the residual half of
OWNER-31 item 6 — is **deferred to the interface phase**, because its non-UI half is row 66's `scale.*` group
and is already in the tree, and what remains (the root note drawn distinctly from the other in-scale degrees,
and a theme value for its colour) is the interface; no socket-observable work is left for it. The decision is
stated in full in that row, in `docs/RELEASE-NOTES-v0.3.0-alpha.md` and in `docs/KNOWN-LIMITATIONS.md`.

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

**The KB sweep's group B — named in the sources and placed, so they are decisions rather than gaps (13).**
Each was absent from this list before 2026-09-13; being named and excluded is the point, and none of them is
a 0.3.0 commitment. Source: `V0.3-COMPLETENESS-VERDICT.md` Group B, which cites each.

- **Link Audio peer streaming** — a `SPEC` §4.1–4.8 **v1 exclusion**. Later release.
- **Auto-tagging** — the same v1 exclusion, `SPEC` §4.1–4.8. Later release.
- **Slice-to-MIDI** — v1 exclusion, `SPEC` §4. Later release.
- **Drum-rack pad** — v1 exclusion, `SPEC` §4. Later release.
- **Macro variations** — v1 exclusion, `SPEC` §4. Later release.
- **Clip-level modulation** — v1 exclusion, `SPEC` §4. (Distinct from the modulation layer, row 7, which is
  in the tree.) Later release.
- **16 macros** — v1 exclusion, `SPEC` §4. The landed `rack.*` group is 12 ids. Later release.
- **Soak testing / performance at scale** — 200 tracks, 100 instances, 8-hour sessions. **Needs users and
  machines**, which the ladder's own "scraps" definition sends to **0.5.0**.
- **Accessibility + keyboard navigation** (`OWNER-31` item 29) — **UI**, and **the documents disagree**
  (disagreement 2 below): the register calls it a Bar-2 requirement on the 0.3 shortlist; the charter §3.3
  defers it.
- **Browser audition / waveform / hot-swap** (`OWNER-31` item 16) — **UI**. Audition, favourites and
  drag-and-drop have landed; the browser's own peak-cache half is row 43.
- **CRDT collaboration** — `collab/CRDT-VERDICT.md` is a research verdict, not a 0.3.0 commitment.
- **Microtuning** (`#5522`) — **already upstream**, so it is not work this line owes.
- **Plugin state save/restore** — **already in the tree** (`plugin.state_*` / `plugin.preset_*`, behavioural
  11/11), so it is not an addition. The master-list row that calls it "no code" is stale — disagreement 8.

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
| 4 | VCA / edit groups | `VcaGroupTest` | drivable through `--control-socket` with the `vca.*` group (14 ids, `vca.edit_move` included); what is absent is the INTERFACE - no strip, no group menu, no member list, no lock toggle, no Lua binding - `tests/control-vca-commands.py` | 4 |
| 5 | PDC + sidechain | `PdcMixerTest`, `PhaseDSidechainTest`, `MixerRoutingBackwardCompatTest` | readable with `pdc.report` (sidechain drivable with `mixer.sidechain_to`); the compensation is not settable as a value by decision (recomputed per period) — `tests/control-pdc-commands.py` | 27 |
| 6 | Routing graph | `RoutingGraphTest`, `RoutingGraphLiveTest`, `RackTest` | readable with `routing.get_state`; no graph EDIT by decision (threading contract + the graph is derived); the live graph is the rack's — `tests/control-routing-commands.py` | 28 |
| 7 | Audio ports / `AudioBus` | `AudioPortsTest`, `AudioPortsModelTest`, `AudioBusTest`, `AudioBusHandleTest`, `PluginAudioPortsTest` | drivable: `bus.list` / `bus.create` / `bus.remove`, `port.get_state` / `port.set_pin` — `tests/control-bus-commands.py`, `tests/control-ports-commands.py` | 29 |
| 8 | Multi-track recorder | `MultiTrackRecorderTest`, `TwoTrackRecordingHarness`, `TwoTrackAlsaCaptureProbe` | no `record.*` group — and the id that should cover it, `track.set_arm`, is a refusal stub, so the feature and the command contradict each other | 14 |
| 9 | Plugin scan cache + quarantine | `PluginScanCacheTest` | drivable through `--control-socket` with the `plugin.*` scan group (6 ids: the three reads, the two quarantine writes and `plugin.rescan`); the JSON hand-edit route is retired — `tests/control-plugin-scan-commands.py`. What is absent is the INTERFACE: no scan record, cache hit or quarantine entry is shown, and no view offers to add one | 46 |
| 10 | Crash reporter | `CrashReporterTest` | drivable through `--control-socket` with `crash.list_reports` and the module's two real operations (`crash.acknowledge_report`, `crash.discard_report`, both `irreversible` with named fallbacks); `crash.upload_report` is a typed REFUSAL because the module has no network code — `tests/control-crash-reporter.py`. What is absent is the INTERFACE: no report, state or directory is shown, and there is no way to send one | 54 |
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

**89 features are on this list.** Counted from the tables above by the measuring instrument's own
`doc_state_of` (89 rows, no duplicates): **21 are in the tree** with a named proof, **27 are partial** — the
engine is in or partly in, and the control-surface half, the registered proof or the routing is what is
missing — and **41 are to build**. Before the 2026-09-14 re-measurement the same count was 17 / 26 / 46: the
five rows whose status this pass corrects are **5** and **62** (folder tracks and their routing mode, to the
tree), **15** (retrospective MIDI capture, to the tree), **41** (plugin-chain presets, to the tree) and **74**
(MIDI clock, to partial — MTC is still absent). That is the whole commitment on one page: a quarter of it is
proved today, three tenths need their socket surface or their test, and the rest is not written yet.

The **30 rows numbered 60–89** are the candidates the KB sweep of 2026-09-13 found absent from the compiled
list (`V0.3-COMPLETENESS-VERDICT.md`): of the 30, **2 are in the tree** (rows 62 and 84), **7 are partial**
(rows 65, 68, 72, 74, 78, 80, 89) and **21 are to build**. Thirteen more candidates from the same sweep
are named and excluded in *Out of scope* above. *Reconciliation* 7 states how the sweep's headline "34"
relates to what is enumerated here.

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
6. **The surface figures are counted on two bases.** `V0.3-SCOPE-CORRECTIONS.md` and this file quoted the
   release tree as **31 groups / 170 ids** while the audit tip is **28 groups / 150 ids**. Re-measured by
   counting the registry source on 2026-09-13: at `334790219` and again at `01b99753a` on `release/0.3.0`,
   `src/core/ControlCommands*.cpp` carries **164** `.id = QStringLiteral` assignments and the helper builds
   **6** more (`wasm.*`), so the id count is **170** — but the **group** count is **31 id prefixes plus the
   helper-built `wasm` group, i.e. 32 group names**. The audit tip's **28 = 27 prefixes + `wasm`**, so the
   two group figures are one apart on their bases rather than a growth of three. Re-measured again on
   2026-09-14 at `3956ef589` (`docs/COVERAGE-REMEASURE-2026-09-14.md`): the source carries **185** `.id =
   QStringLiteral` assignments over **33 id prefixes**, the helper builds **5** more (`wasm.*`, now under
   `#ifdef LMMS_HAVE_WASM`), and a live `control.commands_list` from the built binary returns the same
   **185 ids over 33 groups** — so 190/34 is the registry source and 185/33 is the build. The 2026-09-13
   figures above are kept as the record of that base, not corrected; what is settled is that the two bases
   differ by **exactly** the compile-gated `wasm` group, which is the decision this item was waiting on.
   One further discrepancy is measured rather than carried: the instrument
   (`scripts/zene-feature-tracker.py`) reports **192 ids / 35 groups** on the same source, two more than it
   has, because its part-B regex also matches `cmd.verb = QStringLiteral("tag.add")` at
   `src/core/ControlCommandsBrowserTags.cpp:175` and `:202`. Nothing on this list declares `tag.add` or
   `tag.remove`, so no row's verdict is affected — see `docs/COVERAGE-REMEASURE-2026-09-14.md` §4.
7. **The sweep's "34 candidates" is a headline, not an enumeration.** `V0.3-COMPLETENESS-VERDICT.md` says
   "34 candidates absent … ~20 of them IN 0.3.0", but its **Group A table has 17 rows** and **Group B names
   13 features** — 30 entries between them. This file places all of them (30 rows, numbered 60–89, plus 13
   named in *Out of scope*), expanding the six boarded-gaps command groups and the six change-plan rows into
   separately deliverable rows. **The arithmetic between 34 and the verdict's own tables is not reconciled
   here**, because the verdict does not enumerate 34 and this file must not invent the missing four.

## The ten document disagreements — recorded, not resolved (2026-09-13)

Carried forward from `V0.3-COMPLETENESS-VERDICT.md` § "Disagreements to record, not resolve" and
`V0.3-CONSISTENCY-PASS-CHECKLIST.md` § "Disagreements already known". **Each is deliberately left open**:
picking a side is how the fourteen/fifteen error happened, and every one is either a measurement only a
build can settle or a scope call that is the owner's. Two (2 and 10) are scope questions and bear directly
on this list; the rest are records of the tree, the counts or the documents. **Awaiting an owner decision.**

1. **VST3/CLAP hosting evidence.** *Reading A:* `docs/STATUS.md` — "VST3 + CLAP DONE, effects only".
   *Reading B:* `POST-ALPHA-PLAN` — on every one of the 7 published jobs configure printed *hosting skipped*.
   *Reading C:* the descriptor audit — the VST3 effect tests are unregistered and the three VST3 instrument
   tests never build. Bears on rows 44, 45, 46, 78, 79.
2. **Accessibility (OWNER-31 item 29).** *Reading A:* `BACKLOG.md` / the roadmap-gap register — a **Bar-2
   requirement** on the 0.3 shortlist. *Reading B:* the charter §3.3 — **UI, deferred**. Named in
   *Out of scope* with both readings.
3. **Out-of-process hosting.** *Reading A:* `ecosystem-gap/` §3.1 calls it **blocking**. *Reading B:*
   `docs/OOP-HOSTING.md` — **landed, isolation proven**. *Reading C:* `V0.3-SCOPE-CORRECTIONS.md` —
   **unplaced by every 0.3.0 document**. This file places it as row 80 under D12's rule; which reading is
   true of the tree is not settled.
4. **"15 absent" (ledger) vs "19 not in the tree" (audit §6.1).** Both correct against their own base
   (19 − 3 landed − 1 dispatched = 15). Now stated in both, so a reader does not read it as a
   contradiction.
5. **"fourteen" vs "fifteen".** Corrected for the ladder in `1a001c8`; **D12's body was still saying
   fourteen and is fixed to fifteen in this pass** (`MASTER-PLAN.md` §3) — that was an explicit instruction,
   not a resolution of the disagreement by preference. What stays open is the *scope question* the mismatch
   exposed: **linked clips (OWNER-31 items 8/22) is named by the ladder's prose as one of the fifteen and has
   no table row of its own**, so a reader counting the ladder's table finds fourteen. The corrections
   document decides linked clips is IN (Correction 1); the ladder's table still does not show it.
6. **The A16 row count.** 139 / 142 / 127 / 150 / 155 / 157 are all in circulation; only a merged-tree run
   of `ReversibilityContractTest` settles it. Row 89 carries the deliverable.
7. **`docs/VERSIONING.md`'s own worked example** says 0.3.0 = Session View (W1) only, against this list's
   89 rows.
8. **Plugin state save/restore.** *Reading A:* the master-list row — "unboarded gap, no code". *Reading B:*
   the tree — `plugin.state_*` / `plugin.preset_*` registered, schema'd and behavioural 11/11 at both bases.
   Named in *Out of scope*; the master-list row is corrected in this pass.
9. **The coverage-matrix copies disagree** — 28/150 vs 30/154 vs 31/170. Re-measured here as **170 ids**
   (164 + 6) and **31 id prefixes + the helper-built `wasm` group**; see *Reconciliation* 6.
10. **Key/chord detection (OWNER-31 items 10/25).** *Reading A:* IN per the ladder. *Reading B:*
    OUT-until-needed per `BACKLOG.md` §5. Rows 34 and 35 carry both readings; the ladder's rule places them,
    and nothing else does.

## Suspected misses, and where they now stand

Named because a list like this is only trustworthy if it says what it decided to leave out and why. This
section is what the compiled 59-row list said **before** the KB sweep; the sweep changed the answer, and each
line now points at the row or the section that carries it.

- **MIDI clock / MTC** — was "not carried as a row". Now **row 74, to build**: engine work with no
  architectural gate, so D12's rule places it. Its history is kept rather than tidied: no 0.3.0 document
  placed it, and it is the one item this list now carries on the strength of the rule alone, with no separate
  owner decision behind it.
- **Third-party instrument hosting and out-of-process plugin hosting** — were "not counted". Now **rows 78,
  79 and 80**, with the hosting readings left open as disagreements 1 and 3.
- **Plugin state save/restore** — was "not a 0.3.0 addition; the master-list row is stale". Unchanged in
  substance, now also named in *Out of scope* so it cannot be re-boarded (disagreement 8).
- **Soak testing / performance at scale** — unchanged: needs users and machines, 0.5.0, named in
  *Out of scope* rather than left implicit.
- **The five index-derived stable-ID families** — unchanged, row 51.
- **Bar-3-convenience and UI-only items** (OWNER-31 items 1 and 23; accessibility, item 29) — unchanged in
  substance: UI. Accessibility is now also named in *Out of scope* with both readings (disagreement 2).
- **The change-plan engine rows and the boarded-gaps command groups** — were not mentioned here at all. Now
  rows **63** (`automation.record_mode_set`), **66** (`scale.*`), **67** (`note.probability_set`), **73**
  (`CODE-5`), **81** (`device.mpe_set`), **82** (`CODE-4`), **83** (`CODE-9`), **85** (`telemetry.consent_set`),
  **86** (`CODE-6`), **87** (`CODE-7`), **88** (`CODE-8`); `render.stems` is folded into row 68, the stem-export
  feature it belongs to.
