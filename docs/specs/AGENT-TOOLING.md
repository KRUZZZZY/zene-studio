<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-13).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, ableton-gap/AGENT-TOOLING.md
    sha256   : c71ffea13b7634c4cb8ba9e50664951af76650b9769fb67e69d2866022cb40c0
    bytes    : 32394
    why this file: DOC-5 names it; cited by 9 product source files (the command-id/error-set/schema conventions the registry implements)
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# AGENT-TOOLING: the agent surface contract — Zene Studio

> **Binding companion to `SPEC-zene-studio.md` §5 and §7 (A11–A15).** Owner instruction
> 2026-09-11: *"I want this app to be fully workable for AI."* This document is the contract, the
> per-feature command table, the build ladder and the acceptance gates. If a wave ships a capability
> that is not in this table, the table is wrong and gets fixed in the same commit.

---

## 1. What an agent can do today (verified 2026-09-11)

| route | what it covers | evidence / limit |
|---|---|---|
| **CLI, one process per call** | `render`, `rendertracks`, `dump`, `compress`, `upgrade`, `makebundle`, `--import`, `--geometry` | verified: `-v` prints `LMMS 0.1.0-alpha`; every call **starts a new process** |
| **Lua 5.4 via `--run-script <file>`** (help text `src/core/main.cpp:207`; arg parse `:288` / `:664`; `scriptFile` set from argv `:673`; `ScriptEngine::runFile` `:794`) | scripted model edits, then exit | **reachable from outside** — verified: exit 0 with two Lua log lines on a real run. But it is run-and-exit only: still **no way to talk to a running instance** (corrected 2026-09-11; an earlier draft of this doc said there was no external entry point, which was wrong) |
| **`lmms-lab` MCP server** (outside the app, `mcp-lmms-lab/`) | build · ctest · render · measure · plugin-dlopen proof · worktrees · jobs · `lmms_compare` · `lmms_render_matrix` · `lmms_features` · `lmms_plugins` · `lmms_script` | **17 tools as of 2026-09-11**, all verified: ctest 25/25 at the release commit; `lmms_features` independently reports vst3/clap *wanted but not shipped*; `lmms_compare` reproduces the 35.767 s divergence frame from §5-I of the test plan. **A test harness, not a DAW remote control** |
| **`computer_use` / cua-driver** | the GUI itself | verified working on X11 `:0`, but coordinate+vision driven (Qt exposes a thin AT-SPI tree) and needs a display |

**The gap, stated plainly:** nothing lets an agent drive a *running* DAW. A session is a black box
once it is open; the only automation surface dies with the process that used it. Features that exist
only in a menu are, to an agent, features that do not exist.

## 2. What "fully workable for AI" requires

1. **A live control channel** into a running instance (open a project, play, edit, render, save).
2. **Addressable state** — stable IDs for tracks, clips, devices, parameters, scenes, chains. No
   screen coordinates, no name matching.
3. **Read-back** — every family exposes `*.get_state` returning the serialization structure.
4. **Audio truth** — a headless render/export so an agent can verify what it did, not just what it
   set (this is the same rule the QA gates already use).
5. **Headless parity** — no display, no audio device, no human.
6. **An anti-drift gate** — mechanically ensuring the surface keeps up with the product.

## 3. Target architecture

Binding decisions live in `SPEC-zene-studio.md` §7 (A11–A15). In one line each:

- **A11 one action, one implementation** — a single command registry behind menus, shortcuts, Lua
  and the agent surface.
- **A12 JSON-RPC over a local UNIX socket**, opt-in at launch; **MCP lives outside the app** as a
  thin bridge (`mcp-zene-control`).
- **A13 headless parity**, with `requires: {display|device|human}` declared where a command truly
  needs one.
- **A14 read-back + audio truth** (`*.get_state`, headless render/export).
- **A15 the `agent_surface` ctest gate** — every menu action must have a command ID, every declared
  command must be reachable headless; a new action without a command fails the build.

Message shape (line-delimited JSON, one request/response per line):

```
→ {"id":1,"cmd":"session.launch_clip","args":{"track":"trk-3","slot":1},"proto":1}
← {"id":1,"ok":true,"result":{"playing":true,"quantised_to":"bar"}}
→ {"id":2,"cmd":"project.get_state","args":{},"proto":1}
← {"id":2,"ok":true,"result":{"tracks":[{"id":"trk-3","clips":[…]}],"tempo":120}}
→ {"id":3,"cmd":"render.render","args":{"out":"/tmp/a.wav"},"proto":1}
← {"id":3,"ok":true,"result":{"path":"/tmp/a.wav","frames":4425728,"sha256":"…"}}
```

## 4. Naming and schema conventions

- Command IDs are `group.verb` (`transport.play`, `clip.trim`, `rack.add_chain`); groups match the
  table in §5 so an agent can discover by prefix.
- Every command declares a JSON schema for `args` and for its result; the MCP bridge generates its
  tools **from these schemas** — no hand-written tool definitions to drift.
- IDs are stable strings (`trk-<n>`, `clip-<n>`, `dev-<n>`, `slot-<t>-<s>`), assigned at creation and
  persisted in the project file; they are part of the serialization contract.
  - **Status 2026-09-12: TRUE FOR `trk-<n>`; the other five families are still index-derived.**
    Tracks now carry a creation-assigned number in an `id` **attribute** on their own element plus one
    `next-id` counter on the root (one definition in `include/ControlVocabulary.h`), assigned to a
    legacy file in memory on load and reported as `ids_assigned` in `project.open`'s typed result;
    `trk-<n>` survives `project.save` -> fresh instance -> load and the transactions' inverses are
    addressed by it. `clip-<n>`, `note-<n>`, `ch-<n>`, `fx-<n>` and `dev-<n>` remain index-derived —
    that is slice 2 of the design, with `<note>` deliberately last. The
    design that closes it - the measured problem with file:line derivation sites, the scheme, the XML
    shape and the backward-compatibility rules - is `SPEC-stable-ids.md`, and the implementation is
    boarded as its own task. Two of its findings are binding on any implementation: element TAG names
    are already version-unstable here (so only attributes can carry identity), and the engine's own
    `jo_id_t` is a journal-order routing int absent on `Note` - it is not the identity mechanism.
- Destructive commands (`*.delete`, `project.save` over an existing file) support `"dry_run":true` for
  a preview, and **must be reversible** (SPEC A16): the registry records the inverse, `control.undo`
  applies it, and no confirmation prompt gates the call.
- Every response carries `ok`, and on failure `error.kind` from a closed set
  (`not_found | requires | invalid_args | busy | refused`).
- **Headless start recipe (verified 2026-09-11 — do this or every command answers `busy`).** The
  engine needs an audio device that actually opens. The recipe the implementation's own integration
  test uses:
  `QT_QPA_PLATFORM=offscreen` plus a minimal config file passed with `--config` whose
  `<audioengine audiodev="Dummy (no sound output)"/>` string matches `AudioDummy::name()` exactly, and
  `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME` pointed at a temp directory so a run cannot disturb the
  user's real settings. Then poll `control.ping` until `engine_ready` is true.
- **Readiness is part of the contract, not an implementation detail.** The socket is available before
  the engine is initialised, and that is deliberate: it is how a client finds out *why* an instance is
  not usable. **The order a client follows is:**

  1. **connect** to the `--control-socket` path (it exists as soon as the instance starts; connecting
     early is supported, and is the only way to observe a startup problem);
  2. **poll `control.ping`** — it always answers, even before the engine exists;
  3. **wait for `engine_ready: true`** — while it is `false`, the reply carries a `reason` object
     `{code, message}` (never a bare false), and `audio` carries the device state;
  4. **issue engine commands** only from there on. Before that, every engine command answers the typed
     `busy` error and its message repeats the same reason, so a client that skipped step 2 still gets
     told what is wrong instead of guessing.

  ```json
  → {"id":1,"cmd":"control.ping","args":{},"proto":1}
  ← {"id":1,"ok":true,"result":{
       "pong":true,"engine_ready":false,
       "reason":{"code":"engine_starting","message":"the engine is still starting up (no song/mixer yet); …"},
       "audio":{"state":"ok","device":"Dummy (no sound output)","requested":"Dummy (no sound output)",
                "start_failed":false,"sound_output":true},
       "version":"0.2.0-alpha","proto":1}}
  → {"id":2,"cmd":"mixer.get_state","args":{},"proto":1}
  ← {"id":2,"ok":false,"error":{"kind":"busy","message":"the engine is not addressable yet [engine_starting]: …"}}
  ```

  Reason codes are a closed set: `engine_starting` (poll again), `audio_device_failed` (see below),
  `engine_missing` (fatal: startup finished without an engine). The `audio` object is present in every
  `control.ping` answer, ready or not.

  **Why this shape and not the alternatives (task #626).** Three other designs were considered and
  rejected, and the reason is the same in each case: they all hide the not-ready window instead of
  reporting it.
  1. *Create the socket only once the engine is ready.* Then an instance that never becomes ready —
     the no-audio-device case — is indistinguishable from an instance that is not running at all: the
     client gets ENOENT and no diagnosis, and it cannot tell "start again" from "fix your config". The
     socket exists early precisely so the failure has a voice.
  2. *A readiness event/notification.* The client must already be connected to receive it, so the
     connect-early problem stays; and a subscriber that connects after the event has fired waits
     forever for a notification it missed. A poll of `control.ping` has no lost-event race.
  3. *A typed error per command while not ready.* This is kept as a **belt as well as braces** — every
     engine command answers the typed `busy` error whose message repeats the reason — but it is not the
     primary channel, because the agent has to guess a command to discover the problem, and a
     read-only probe should not be needed to diagnose readiness. `control.ping` always answers, before
     the engine exists, and is the documented first call.
  The contract is therefore: **connect -> poll `control.ping` -> wait for `engine_ready: true` -> issue
  engine commands**, with `reason` on every not-ready answer.
- **No usable audio device (task #626, fixed 2026-09-11).** With a configured backend that cannot open
  (measured: SDL → `Playback open error: Host is down`) the audio layer falls back to the dummy device.
  The engine is *addressable* either way — render, edit and save all work — so the product no longer
  blocks in the modal "Audio device setup failed" box (it used to block before `app->exec()`, leaving
  `engine_ready` false and every command `busy` for 92 s and counting). Instead it **says so**:
  `control.ping` reports
  `audio: {state:"dummy_fallback", requested:"SDL", device:"Dummy (no sound output)", start_failed:true,
  sound_output:false, message:"the configured audio device 'SDL' could not be opened; …"}`, the same
  sentence goes to stderr, and the one command that exists to make sound — `transport.play` — refuses
  with the typed `requires` error naming the backend. Nothing else refuses: a device-less instance is
  still a usable instance.
- **`control.quit` runs the normal shutdown** (task #626, fixed 2026-09-11): it answers the GUI's own
  "project was modified, save it?" question from the caller's stated intent instead of opening a dialog
  nobody can click, so `app->exec()` returns, the engine is destroyed, the autosave recovery file is
  cleaned up and the socket file is unlinked — exit code 0. `control.quit` takes `{"save": true|false}`
  (default `false`, i.e. discard unsaved changes; the reply reports `project_modified` and
  `unsaved_changes`). `save:true` on a project with no file is refused, typed, because saving would open
  the interactive Save-As dialog. A request that arrives before the instance is ready is remembered and
  applied once it is. The pre-fix `control.quit` needed a 2 s watchdog and printed
  `control.quit: the event loop did not stop`; **that line is now a defect signature** — if a test or a
  log shows it, the shutdown path is broken again.

## 5. Per-feature command table (the obligation, by area and wave)

**Measured baseline (2026-09-11, `AGENT-SURFACE-INVENTORY.md` — 441 capabilities, anchors machine-verified): 90 reachable by an agent today (20.4%), 351 not (79.6%).**

| group | callable / total | | group | callable / total |
|---|---|---|---|---|
| Transport and playback | 12 / 27 | | Recording and capture | **0 / 14** |
| Song / arrangement editing | 8 / 61 | | Project and file I/O | 27 / 45 |
| Piano roll / note editing | 6 / 46 | | Settings and lifecycle | 5 / 38 |
| Mixer, buses, routing, PDC | **0 / 34** | | Scripting and AI DSP | 25 / 34 |
| Plugins and hosting | 2 / 40 | | Planned waves + boarded gaps | 3 / 68 |
| Automation | 2 / 34 | | | |

Four findings from that inventory that change the build order:

1. **The mixer is the largest block of `NONE` in the tree — 34 capabilities, 0 callable.** It goes first in S2, not last: channels, sends, sidechains, buses, effect chains and PDC are C++ + Qt only.
2. **Lua is a pattern-editing API, not a DAW-control API.** It sets tempo, master volume and track gain and adds/removes notes in Beat/Bassline patterns — it cannot add a mixer channel, load a plugin, move a Song-Editor clip, write automation or start a recording. It is not a substitute for the registry.
3. **`rendertracks` already works** (`src/core/main.cpp:282`, `:775`) while the roadmap-gap register files per-track / stem export as not-HAVE — reconcile before that gap is sized.
4. **`Song::record()` is a stub** (`src/core/Song.cpp:522`, `// TODO: Implement`) and the two-track recorder is not wired to the engine's record path — recording depth is genuinely missing, not just untooled.

**Already-existing product capability (must be tooled now — see §7)**

| area | commands |
|---|---|
| transport | `transport.play` `stop` `pause` `seek` `set_tempo` `set_time_signature` `loop_set` `metronome_set` `get_state` |
| song/arrangement | `track.add` `remove` `rename` `mute` `solo` `arm` `reorder` · `clip.add` `move` `resize` `split` `delete` `duplicate` `select` `get_state` · `timeline.get_state` |
| piano roll / notes | `note.add` `remove` `move` `resize` `velocity_set` `select` · `clip.quantize` · `clip.humanize` · `roll.get_state` |
| mixer / routing | `mixer.add_channel` `remove` `set_volume` `set_pan` `route_to` `send_to` · `bus.create` · `pdc.report` · `mixer.get_state` |
| plugins | `plugin.list` `load` `unload` `bypass` `param_get` `param_set` `state_save` `state_load` · `plugin.preset_list` `preset_load` `preset_save` |
| built-in devices / AI DSP | `dsp.rnnoise_enable` · `dsp.nam_load_model` · `dsp.get_state` (device params ride the generic `plugin.param_*`) |
| recording | `record.arm` `monitor_set` `start` `stop` · `take.list` `take.select` |
| project / file I/O | `project.new` `open` `save` `save_as` `bundle` `upgrade` `compress` `dump` · `import.midi` `import.sample` · `export.midi` `render.render` `render.per_track` |
| automation | `automation.add_point` `remove_point` `set_value` `mode_set` `get_state` |
| settings / lifecycle | `settings.get` `settings.set` · `audio.device_list` `audio.device_set` · `midi.device_list` · `app.version` `app.quit` |
| scripting | `script.run` `script.list` |
| crash / recovery | `crash.list_reports` · `recovery.list` `recovery.restore` (autosave `recover.mmp` — confirmed present in the shipped binary) |

**Planned waves (each lands with its own commands — SPEC §4, PLAN waves)**

| wave | commands |
|---|---|
| W1 Session View | `session.slot_create` `slot_fill` `slot_clear` · `session.launch_clip` `stop_clip` `stop_all` `scene_launch` `scene_capture` · `session.quantise_set` `legato_set` · `follow_action.set` · `arrangement.record_session` · `clip.loop_set` `clip.gain_set` `clip.transpose_set` |
| W2 Warp | `warp.enable` `warp.mode_set` `warp.marker_add` `marker_remove` `marker_auto_detect` · `clip.tempo_leader_set` · `warp.slice_to_midi` (v1) |
| W3 Racks | `rack.create` `rack.remove` · `rack.add_chain` `remove_chain` · `chain.zone_set` (`key`/`velocity`/`chain_select`) · `macro.map` `unmap` `set` |
| W4 Comping | `take.lane_create` · `comp.region_select` `comp.audition` `comp.flatten` |
| W5 MPE + modulation | `note.expression_set` (pitch/slide/pressure/release) · `modulator.create` `remove` `rate_set` `depth_set` `target_set` |
| W6 Link | `link.enable` `disable` `peers` `start_stop_sync_set` · `link.audio_peer_connect` (v1) |
| W7 Browser | `browser.search` `tag` `filter` `preview` `import_to_track` · `browser.similarity_search` |

**Delivered 2026-09-12 (verified by the orchestrator's own probes):** the notes/clips/tracks group and
the plugin + settings/lifecycle group are live.

- *Notes/clips/tracks* (21 commands): `note.add/remove/move/resize/velocity_set/select`,
  `roll.get_state`, `clip.add/move/resize/split/delete/duplicate/select`, `track.add/remove/rename/
  set_mute/set_solo/set_arm`, `arrangement.get_state`. My probe composed music over the socket -
  track → clip → two notes → move/resize/velocity → `note.remove` → **`control.undo` restored the
  note (note_count 1 → 2)** - and every typed error fired correctly (`key: 300 is above the maximum
  127`, `'position' 288 is not strictly inside the clip (0..192)`, `no clip clip-1 (the song has 1)`).
  15 of the 21 are `reversible:true` via a real `ProjectJournal` checkpoint, each proven by an actual
  `control.undo` in the tests; `track.add`/`track.remove` are `reversible:false` because upstream has
  the track checkpoints commented out, and `clip.select`/`note.select` because selection is view
  state the project file does not carry. Honest limits: **`track.set_arm` is a typed `refused` in this
  build** - arm lives on the prototype `MultiTrackRecorder`, not on the song model (the §5 table
  previously listed it as a plain command; correct to a refusal); and a freshly added instrument track
  has **no instrument**, so an agent's arrangement is real model state that renders silence until
  something calls `plugin.load`.
- *Plugins + settings/lifecycle* (18 commands): the plugin + settings/lifecycle
  groups are live - `plugin.list` (333 devices: 53 built-in, 280 LADSPA, 258 loadable; by kind 267 effect
/ 52 instrument), `plugin.load`/`unload`/`bypass`/`param_get`/`param_set`/`state_save`/`state_load`/
`preset_list`/`preset_load`/`preset_save`, `dsp.get_state`, `settings.get`/`set`, `audio.device_list`/
`device_set` (honestly `applies on next start`), `midi.device_list`, `app.version`. `plugin.param_set`
is reversed by `control.undo` through `ProjectJournal` (measured 100 -> 150 -> undo -> 100); the rest
are `reversible:false` with a recorded inverse or a bounded snapshot, as A16 requires. Known gap this
exposed: **LV2 hosting is compiled in and 27 LV2 bundles are installed, but `plugin.list` does not
enumerate LV2 - an agent cannot load an LV2 plugin through this surface yet.**

**Live status 2026-09-12 (measured on the merged integration branch, 43/43 ctest green): 87
commands across 22 groups are implemented and reachable through the bridge — `control` (8),
`transport` (5), `track` (8), `clip` (7), `note` (6), `roll` (1), `mixer` (5), `plugin` (11), `dsp` (1),
`settings` (2), `audio` (2), `midi` (1), `project` (3), `app` (1), `render` (1), `arrangement` (1).
`automation` (5) and `script` (2) joined on 2026-09-12, verified with
rendered audio (three automation shapes render to three distinct files) and with `script.run`
executing Lua **in the running instance** and refusing a runaway loop by name.
`record` (7), `import` (2) and `export` (1) joined on 2026-09-12 with the file-I/O group — including a
**real microphone capture** verified on this box (132,352 frames from a Razer Seiren Mini through SDL,
non-silent) and the **ALSA negative control** (0 frames), which is the alpha's limitations-page claim
measured rather than quoted.
Group-by-group in the tables below, the only rows still NOT delivered are the per-wave groups W1–W7
(S5 — they land with their waves). Separately, the id contract is half-delivered: `trk-<n>` is
persistent, the other five families remain index-derived (slice 2, scoped in `SPEC-stable-ids.md`). **Merging this work into the product programme is a real reconciliation, not a formality** — the two
integration branches are 178 commits apart in one direction and 25 in the other off the same base, and a
`git merge-tree` dry run conflicts in nine paths, four of them C++ startup files both programmes changed
for different reasons. The situation, the resolution policy and what accepting involves are written up
in **`HANDOFF-agent-surface-merge.md`**, and a scratch branch
(`post-alpha/agent-surface-onto-integration`) is prepared so the decision is accept-or-reject.

**Both halves of that gap are scoped from measurement**: `SCOPE-io-commands.md` (the CLI semantics of
each I/O action, the A16 class of each proposed command, and the two unknowns the lane must settle
first) and `SCOPE-recording-commands.md` (the recorder API a `record.*` group would wrap, the four
unknowns, and the measured fact that this box *does* have a capture device — a Razer Seiren Mini — so
the alpha's "ALSA records silence" claim is testable here rather than only quotable). Both should land
after #623, whose file-revision policy their file-level commands must reuse rather than reinvent, and
both follow the refusal pattern this surface already uses four times for features that do not exist. `automation.mode_set` is delivered as a typed REFUSAL: this tree has no automation
modes (its own known-limitations page says so), and `automation.set_progression` — the one thing the
GUI's automation editor can do that this group cannot — is named as a gap rather than faked. Everything the rows name as
`plugin.*`, `note.*`, `clip.*`, `track.*`, `mixer.*`, `transport.*`, `settings.*`, `audio.*`,
`midi.*`, `dsp.*`, `roll.*`, `arrangement.*`, `render.render`, `project.open/save/get_state` and
`app.version` is live today.

**Boarded gaps / "don't have" list** (each needs commands the day it lands): `clip.trim` `slip`
`fade_set` `crossfade_set` `clip_gain_set` · `record.punch_set` · `plugin.scan` `plugin.rescan`
(alpha ships no scanner) · `mixer.lufs_read` · `render.freeze` `render.bounce_in_place` `render.stems`
· `automation.record_mode_set` · `midi.learn_*` · `groove.*` · `scale.root_set` `scale.set` ·
`note.probability_set` · `patcher.instantiate` `patcher.node_add` `node_connect` · `device.mpe_set` ·
`scheduler.multicore_set` · `telemetry.consent_set`.

## 6. Build ladder

| slice | deliverable | acceptance |
|---|---|---|
| **S1** | lab-side tools (outside the app) | `lmms_compare`, `lmms_render_matrix`, `lmms_features`, `lmms_script`, `lmms_plugins` land with tests and are run for real |
| **S2** | in-app command registry + JSON-RPC socket (`--control-socket <path>`, opt-in) with `control.ping` `control.version` `control.commands_list` and the **existing-capability** groups from §5's first table | an external client opens a project, edits, renders and saves without a display; every command returns typed schema-valid results |
| | **DONE 2026-09-11 (first slice) — VERIFIED.** Lane `zene-pa-agentctl`, branch `post-alpha/agent-control-surface`, head `6b01b98eb` off base `0c23587d2`; +3317 lines / 19 files; 23 registered commands; ctest 27/27; binary reports `0.2.0-alpha`; a live instance driven end to end over the socket by an external client. Accepted limits: render serialises-then-shells-out to the CLI, `mixer.set_pan` is a typed refusal (no pan on `MixerChannel`), ids are index-based and not persisted, `project.open` can hang on a modal error box, and `control.quit` needs a watchdog because the event loop can survive quit. Follow-ups: #626 (readiness + shutdown), #625 (headless load path). | |
| | **DONE 2026-09-11 (#626 hardening) — VERIFIED.** Lane `zene-pa-ctrlhard`, branch `post-alpha/control-hardening`, off `6b01b98eb`. Both shutdown reproductions were reproduced and root-caused with stacks (their causes are different, and neither is "the event loop survives quit"): (a) `QCoreApplication::quit()` -> Qt6 application termination -> `QApplication::closeAllWindows()` -> `MainWindow::closeEvent` -> `mayChangeProject()` -> the modal "Project not saved" box, because `ProjectJournal::undo/redo` call `Song::setModified()`; (b) `MainWindow::finalize()`'s modal "Audio device setup failed" box blocking *before* `app->exec()`, so `setReady(true)` was never reached and every command answered `busy`. The fix answers the quit prompt from the caller's stated intent and asks no dialogs in a headless run; `control.ping` now carries the readiness reason and the audio device state, and `transport.play` is the one command that refuses (`requires`, naming the backend) when the device failed instead of silently making no sound. The 2 s watchdog is gone - `control.quit` runs the normal shutdown (engine destroyed, autosave cleaned up, socket unlinked, exit 0). Four new ctest tests: both reproductions, the readiness order, the no-audio report, and the negative controls proving each can fail. | |
| **S3** | **DONE 2026-09-11, verified by the orchestrator's own injection.** `agent_surface` is ctest #28: reflection over the live menu/toolbar surface (`control.surface_report`), a **measured 42-entry baseline** of pre-existing unregistered actions, a shrink-only ratchet with `--reanchor "reason"`, reverse completeness, and a headless sweep of every non-allowlisted command under a bounded timeout (9.5 s of a 120 s budget). My runs: `--self-test` → 15/15 rules bite; clean tree → 1/1 pass; **injected an unregistered action → exit 8, naming `menu:File/Orchestrator probe` while the 42 grandfathered entries stayed quiet**; reverted → exit 0, tree clean. 3 exemptions, each with a one-line reason; the fixture is hash-guarded so the sweep cannot write to it. Blinds spots documented: a declaration is not proof of routing (Qt cannot report slot connections), lazily-built menus, and non-standard toolbar containers. | the gate fails when a menu action lacks a command — **proven both by the lane and by my own red/green injection**; the full surface runs under `QT_QPA_PLATFORM=offscreen` with no audio device |
| **S4** | **DONE 2026-09-11, verified live.** `mcp-zene-control`: 25 tools (23 generated from the DAW's `control.commands_list` + `zene_status` + `zene_commands`), stdio MCP, resources `zene://project/state` and `zene://instance/status`, typed `no_instance`/`stale_socket`/`disconnected`/`timeout`/proto-mismatch, bounded calls, and offline command discovery from a snapshot when nothing is running. 53 unit + 11 e2e tests green. Driven by the orchestrator from a **live Hermes session** against a real headless instance: the full flow plus set→undo→redo (0.25 → 1 → 0.25 via `lmms::ProjectJournal`). Registered in `~/.hermes/config.yaml`; docs in `MCP-ZENE-CONTROL.md`. Limits: only `mixer.set_volume`/`transport.set_tempo` have true inverses (the bridge forwards `reversible:false` verbatim), and long renders need an explicit larger timeout. | an agent drives a live instance end-to-end through MCP only; the tool list matches `control.commands_list` exactly — **both verified** |
| **S5** | per-wave command groups land with W1–W7 (PLAN.md updated per wave) | no wave passes its milestone demo without its commands present and exercised by the demo script |

### Sequencing — what lands now, what rides a wave, what waits for the release

Decided 2026-09-11, because the alternative (build the whole surface later) costs more: the waves are
landing *now*, and A11's "one action, one implementation" is a design constraint on how each action is
written, not a coat of paint. A feature built without the registry has to be reopened to be tooled, so
the registry has to exist before the features do.

| when | what | why that timing |
|---|---|---|
| **Now (spine)** | the S4 bridge, the S3 gate, and the #625/#626 hardening fixes; plus the command groups for whatever the next wave touches | the bridge is pure Python with **no build and no disk cost**, and it is what makes the surface reachable by an agent at all; the gate is what stops drift; the hardening bugs get worse with every feature added on top |
| **With each wave (never deferred)** | that wave's command group (`session.*`, `warp.*`, `rack.*`, `comp.*`, `note.expression.*`/`modulator.*`, `link.*`, `browser.*`) | PLAN rule 8: a wave that ships user-facing capability without its commands is not done. The S3 gate makes this mechanical rather than aspirational |
| **Batched into the 0.2.0 window (the tail)** | the remaining **existing-capability** groups the next waves do not touch — notes/piano roll, automation, plugin load/params/state, recording, settings and audio-device selection, the rest of file I/O, `script.run` | wide but shallow, and none of it is on a wave's critical path; batching it keeps the lane budget for features |

**Both release conditions are now met (2026-09-11): S4 landed** (verified from a live agent session)
and **S3 landed** (verified by red/green injection), so the 23-command surface is reachable by any
MCP-attached agent *and* mechanically guarded against drift. What remains before the surface can be
called done: the command-group tail (`cmdnotes`, `cmdplugins` in flight; the rest batched into the
0.2.0 window), the hardening pair (#625/#626), and the merge into the post-alpha stream.

**Load note (measured 2026-09-11):** on this box the full suite can exit 8 because `PdcMixerTest`
subprocess-aborts (SIGABRT) under heavy parallel load; it passes 1/1 on an isolated re-run. Same class
as the `TwoTrackRecordingHarness` fixed-`/tmp` flake another lane reported — the suite is not safe to
run in parallel with a dozen lane builds until that is fixed.

## 7. Immediate build list (what is missing *right now*)

1. **S1 tools — DONE 2026-09-11.** `lmms_compare`, `lmms_render_matrix`, `lmms_features`, `lmms_script`, `lmms_plugins` are implemented, registered, unit-tested (24 new tests, all passing) and were each run for real. Caveat: a live Hermes connection keeps serving the 12 pre-existing tools until the MCP server process is restarted.
2. **S2 registry + socket** — the single highest-value item in the whole document: today an agent
   cannot touch a running session at all. It needs a design spike first (where the registry lives,
   how handlers queue to the UI thread, how the socket is secured).
3. **`agent_surface` gate (S3)** — cheap and mechanical once S2 exists; it is what stops the drift.
4. **Coverage of existing capability**: the §5 first table is ~70 commands over functionality the
   product already ships — none of which any agent can call today.

## 8. Acceptance gates (repeatable)

- `control.commands_list` ⊇ every action in every menu/toolbar (reflection, not manual list).
- Every command runs with `QT_QPA_PLATFORM=offscreen` and no audio device, or declares `requires`.
- Every command family has a `*.get_state` whose output is the same structure the project file
  serializes (compare with `dump`).
- The render path is callable headlessly and returns a hash, so an agent can prove audio changed.
- A deliberate violation (a new menu action, no command) fails the build.

## 9. Owner decisions (taken 2026-09-11)

1. **Consent model — OPT-IN.** The control socket is off unless the instance is launched with
   `--control-socket <path>`; the socket file is mode `0600` in the working directory and nothing ever
   listens on the network.
2. **Release bar — 0.2.0.** The agent surface ships as **v0.2.0**, not a `0.1.x` patch. Numbering rule
   and worked examples: `VERSIONING.md`.
3. **Destructive commands — no confirmation gate; reversibility instead.** `danger` is handled by
   inversion, not by a prompt: see SPEC **A16** — every mutating command records a transaction
   (before-state + inverse) that `control.undo`/`control.redo` can reverse, sharing the history with the
   GUI's own undo. `dry_run` stays available for previewing a change, but nothing blocks on a
   `confirm:true`. Boarded as task **#623** (the general reversibility contract).
