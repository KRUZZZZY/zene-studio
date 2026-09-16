# Zene Studio 0.2.1-alpha: known limitations

What this alpha does **not** do, in the order you are most likely to hit it. If something here surprises you,
that is this page's fault — report it and it gets added.

> **Version.** This page ships with the 0.2.1-alpha re-cut of the 0.2.0-alpha release; the feature set is
> unchanged and the number moved because the `v0.2.0-alpha` tag's build failed 7 of 7 jobs and a `v*` tag is
> never re-pointed. References below to `0.2.0` as the release that was prepared, and to the `v0.2.0-alpha`
> tag, are to that superseded tag and stay as written; the `0.1.0-alpha` references are shipped history.

> **Verification convention.** `[VERIFY AT FREEZE]` marks a claim that must be re-checked against the built
> artefact before it ships; nothing carrying it goes out unverified, and no unverified claim goes out without
> one. This page's path is `docs/KNOWN-LIMITATIONS.md` — there is no version-suffixed 0.2.1 limitations file.
> Applied verbatim from `drafts/KNOWN-LIMITATIONS-v0.2.0-alpha-DRAFT.md` (reviewed) on
> `post-alpha/release-prep` (base `post-alpha/integration` @ `34c1f4f86`) except for the marker resolutions
> noted inline; the resolution table is `docs/RELEASE-PREP-0.2.0.md` §3.
>
> **2026-09-13 check.** Three claims on this page were re-checked against the tip (`post-alpha/integration` @
> `5565b4b1b`) for the four-audit verification
> (`projects/lmms-fl-research/STATUS-CORRECTION-2026-09-13.md`) and corrected in place, each carrying its own
> date: the automation-mode wording, the clip-editing-gestures bullet and the fork-scope count in the coverage
> bullet. The rest of the page was left as written.

## Before you download

- **This is an alpha.** Parts of it are unfinished and crashes are possible.
- **Keep backups.** Files saved by this build may not open in a later build, an older build, or in LMMS. Copy
  the project folder or use **File > Save As** before you open anything here, and keep the original.
- **The builds are unsigned.** Windows SmartScreen and macOS Gatekeeper will warn about an unknown developer.
  Do not answer either warning by turning protection off.
  **The pointer that used to sit here — "the one-time steps are in the release notes" — is deleted, not
  repaired.** The 0.2.1 release notes carry no per-platform first-run steps, and neither does this page, so the
  pointer led nowhere. The 0.1.0 page's "Getting it running" section (Linux: FUSE 2 /
  `--appimage-extract-and-run`, no menu entry; Windows: More info → Run anyway; macOS: Privacy & Security →
  Open Anyway, and the macOS 15 note that right-click-Open no longer works) was preserved as
  `docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md` when this page replaced it. It is **not** copied forward here: every
  step in it describes how a *packaged* artefact behaves when it is launched (AppImage FUSE, SmartScreen,
  Gatekeeper), and no artefact of this release exists on this machine to re-check the steps against. Restoring
  that section is a decision for whoever builds and ships the packages, and it is a copy, not a verification.
- **Packages come from the release page, and only from there.** A build job uploads a package for a tag build
  or a manual CI run, never for an ordinary push, so a push that is neither produces no package at all; and
  only the platforms whose build job is green have packages. Check the release page for the current set.
  Verified in the tree: six of the workflow's seven `upload-artifact` steps carry
  `if: startsWith(github.ref, 'refs/tags/') || github.event_name == 'workflow_dispatch'`
  (`.github/workflows/build.yml:147`, `:278`, `:420`, `:545`, `:717`, `:825`); the seventh (`:688`) uploads the
  ctest log on failure and is commented as deliberately exempt "because this is evidence, not a package". The
  workflow's own triggers are `push`, `pull_request` and `workflow_dispatch` (`:8-10`), so an ordinary push
  runs the jobs but uploads no package.
  *This bullet is the one piece of an incoming 0.1.0-era rewrite of this page that had no counterpart here;
  everything else in that edit is either already carried above in a newer form or deliberately excluded — see
  `tests/integration-logs-3f/`.*
- **Every save writes a `.bak` next to your project.** Verified in the tree: the backup is
  `<project file>.bak`, beside the project — `src/core/DataFile.cpp` composes `fullName + ".bak"` and moves the
  current file there before the new one is renamed into place. It is not written when the `app/disablebackup`
  setting is on.

## Files, formats and older builds — read this before you trust a project file

- **The version of the application that wrote a project is stamped into the file** (`creatorversion`), and that
  is what drives the behaviour below. Verified in the tree: `creatorversion` is an attribute of the project
  root, written as the writing build's version string (`LMMS_VERSION`, set wherever `src/core/DataFile.cpp`
  creates the root) and read back on load, where it drives both the "Version difference" notice and the choice
  of upgrade routine to run (`DataFile::legacyFileVersion()`).
  The behaviour half is checked at source level, which is the strongest check available here: the elements an
  older reader has no code path for, it cannot keep. `git show origin/master:src/core/Song.cpp` dispatches the
  song container's child element names `controllers`, `keymaps`, `scales`, `track` and `trackcontainer` — the
  same file also compares node names against the GUI editors' panels (`controllerRackView`, `pianoRoll`,
  `automationEditor`, `projectNotes`, the timeline), so "**and nothing else**" is *not* true of the file and
  is not claimed here; `git grep -c prefader origin/master -- src/` and
  `git grep -l sidechain-send origin/master -- src/` both return **no matches**;
  `git show origin/master:src/core/Note.cpp | grep -c slide` returns **0**. What none of that establishes —
  and this page does not claim it — is a *run* of the LMMS 1.3.0-alpha binary, which is not present on this
  machine. The claim is therefore about what the older reader's source can keep, not about behaviour anyone
  observed. This tree's own half is proved by a test rather than by argument:
  `ProjectOpenIntegrityTest::currentBuildRoundTripsBusSidechainAndPrefaderSends()` writes a bus, a pre-fader
  send and a sidechain send, asserts the written XML contains them, loads it back and asserts they survived;
  `slide` is pinned by `SlideNotesTest::slideNoteRoundTrip()` (`docs/SAVELOAD-INTEGRITY.md` §1 D6, §5).
- **An older 1.3-alpha build will OPEN a 0.2 project and silently drop parts of it** — clip and take-lane
  content, `<bus>` and `<sidechain-send>` routing, `prefader` flags, slide data. Silently is the operative
  word: you get a project that looks fine and is missing content. **Do not open a 0.2 project in an older
  build.** If you must, open a *copy* and compare.
- **The Session View is in the 0.3.0-alpha builds, and there is still no way to operate it from the
  interface.** From 0.3.0 `WANT_SESSION_VIEW` **defaults ON**, so the data layer, the launch scheduler and the
  `session.*` control-surface group are in the release builds. The honest limits, because the option's value is
  not the claim: there is **no clip launcher, no scene launcher and no clip grid** — no UI at all — and a
  launched session slot **does not render audio**, because this tree has no session-clip playback path
  (`src/core/SessionClip.cpp` is serialisation only). An agent can build a grid, launch a clip or a whole
  scene, and read the launch back through `--control-socket` (`session.get_state`, `session.set_slot`,
  `session.launch_scene`, ...); a user cannot see or hear any of it. A project containing `<session>` data is
  also **preserved across a round trip**, which was already fixed in 0.2.1 and is unchanged.
- **UI absence — one line: the clip-launch grid is drivable through the socket, not from the
  interface.** The grid, its scenes and its slots ARE the `session.*` command group's objects
  (`session.set_grid`, `session.set_scene`, `session.set_slot`, `session.launch_scene`,
  `session.get_state`), and the interface draws none of them:
  `grep -rniIE "session\.(set_|launch_|stop_|clear|get_state|follow_|arrangement_|back_to_)" src/gui/`
  returns **zero** matches, and so does the same grep for the session classes (`SessionView`,
  `SessionModel`, `SessionClip`, `SessionScheduler`, `SessionFollow`) — the only `session` spellings
  in `src/gui/` are `MainWindow`'s crash-recovery `SessionState` and one telemetry-consent string.
  The clip-launch grid UI itself (#598) is **out of 0.3.0**.
- **Follow Actions, Arrangement Record and the Back-to-Arrangement switch are drivable through the
  socket, not from the interface.** The chain a slot has carried since #594 is now EVALUATED (all ten
  action types, chance weighting, linked/unlinked timing) and the performance can be recorded into the
  arrangement ring and landed as timeline clips — through `session.follow_set` /
  `session.follow_get_state` / `session.arrangement_record_arm` / `_status` / `_land` /
  `session.back_to_arrangement`, and through nothing else: there is **no Follow Action editor, no
  Arrangement Record button, no take lane and no Back-to-Arrangement light**, and the clip-launch grid
  (#598) is **out of 0.3.0**. A landed clip carries the recorded **position and length**, not the
  session slot's notes (`pattern` reports the reference it names), so the "rendered audio matches the
  session playback" half of the feature's acceptance is **unmet in this tree** — a launched session slot
  does not render audio at all (the bullet above). `docs/RELEASE-NOTES-v0.3.0-alpha.md` states the same
  bounds.
- **A failed save is now reported rather than silent.** If a project cannot be moved aside on save (an existing
  file the platform refuses to rename over), the save is refused **and you are told**, rather than reporting
  success.
  Verified in the tree: the two renames that publish a saved project are now checked and their failures
  propagated — `DataFile::writeFile()` in `src/core/DataFile.cpp` is the checked sequence the pre-fix code
  replaced with an unconditional `true`, the rename into place and its rollback are each failure-checked, and
  `writeFile()` returns `false` on every failure path. `docs/SAVELOAD-INTEGRITY.md`
  §1 (D3) is the lane's report and names the tests that cover it; the lane is an ancestor of this tip.

  Related and **fixed in this release too**: a failed *open* used to leave modified-tracking, undo journalling
  and autosave off until a restart, and that bullet has been removed from this page rather than kept as a stale
  limitation. The failure branch now restores both flags before returning — `src/core/Song.cpp:1141-1163`, whose
  comment records the old behaviour ("carried on with autosave, undo journalling and modified-tracking all
  disabled until a restart, with nothing said") — and `docs/SAVELOAD-INTEGRITY.md` §1 lists it as D2, *fixed*.
- The project file is now `<zene-project creator="Zene Studio">`. The reader still accepts the old root, so
  older files open — but the writer only emits the new one.

## What this alpha cannot do at all

- **No CLAP hosting on Windows.** CLAP hosting ships on Linux and macOS in 0.2.1-alpha; the Windows builds are
  configured with `-DWANT_CLAP=OFF` because the host loads its plugins through `dlopen`/`dlsym` and neither
  MSVC nor MinGW provides `<dlfcn.h>`. VST3 effect hosting and VST3 instrument hosting **do** work on Windows.
  `tests/advertised-features.tsv` carries this claim per platform and the release-honesty guard checks it in
  both directions (present on Linux/macOS, asserted absent on Windows), so a future build that turns it on
  without updating this page fails the release job.

- **No instrument editor.** You can load a VST3 instrument and play it, but the plugin's own GUI **does not
  open**. What you get instead is the host's generated control grid, and we have **run it** rather than assumed
  it: with a VST3 instrument track loaded, the instrument window opens and lists the plugin's controls (verified
  against our own test instrument, whose `Level` knob appears as expected). A third-party instrument's grid may
  be larger or less tidy than that one — that is untested, not claimed.
  Verified against this tree, for the "no editor" half: `grep -rn IPlugView src/ include/ plugins/Vst3Effect/
  plugins/ClapEffect/` returns **0** hits, so the plug-in's own editor is not implemented in the host and the
  parameters can only surface as the generated grid (`docs/INSTRUMENT-HOSTING-SPEC.md` §0). **The 0 is over
  those four paths, which is why they are named**: the string also occurs in one product file,
  `plugins/Vst3Instrument/Vst3InstrumentView.h` (a comment recording that the interface is not implemented),
  and in the vendored Carla copy of the VST3 SDK headers
  (`plugins/CarlaBase/carla/source/includes/vst3sdk/...`).
  The "window opens and lists the controls" half is verified too, and out of band rather than by CI:
  `docs/INSTRUMENT-VIEW-SAFETY.md` §3 drove the shipped binary under Xvfb against a project carrying a VST3
  instrument track on the "Bass" track — pre-fix and post-fix the window opens, the process stays alive, and
  the window shows *"Controls for Zene VST3 Test Instrument"*, a `Level` knob and the disclosure line. The
  suite that guards the entry point sits behind the `WANT_VST3_TEST_INSTRUMENT` option
  (`tests/CMakeLists.txt`, default `OFF`); since 2026-09-13 the `linux-x86_64` CI job passes
  `-DWANT_VST3_TEST_INSTRUMENT=ON`, so that job builds and runs it on every push, and the other six keep the
  default. The guard was proven by running it out of
  band, green with it and `SIGSEGV` exit 139 at address `0x8` without it (§4).
- **Instrument hosting is new and narrow.** One instrument per track, MIDI in to audio out. **No third-party
  VST3 instrument has been tested by us** — the only instrument this release has been proven against is a
  purpose-built test instrument we ship in the source tree
  (`tests/data/vst3-test-instrument/`, an MIT VST3 fixture, proven by the SDK's own validator and by the
  `Vst3InstrumentFixtureProbe` probe — `docs/VST3-INSTRUMENT-FIXTURE.md` §0, §5). There is no multi-out, no
  preset management, no instrument latency compensation, and no out-of-process hosting. **No instrument hosting
  in CLAP.**
- **No VCA groups in the interface.** Mix-and-edit groups exist, are tested, and are saved with the project —
  and since 2026-09-14 the **whole group is drivable through `--control-socket`, which is still the only way
  to reach one: nothing in the interface creates a group, names one, assigns a member, locks it or edits
  through it.**
  Verified in the tree: the group is a real entity (`src/core/VcaGroup.cpp`, `include/VcaGroup.h`, owned by
  the mixer via `Mixer::createVcaGroup`, `src/core/Mixer.cpp:720`), its gain is applied on the audio path
  (`:528-545`), and the save/load element it is written as is `vcagroup` with a `vca` child (`:1893`,
  `:1898`, `:2022`, `:2037`). `docs/VCA-GROUPS.md` is the implementing lane's report; the lane is an ancestor
  of this tip. *Updated 2026-09-14 (OWNER-31 item 11, lane `030/vca-editgroups`): the UI half of this bullet
  is still exactly true — nothing in `src/gui/` creates a group, adds a member or toggles the phase lock —
  but the reachability half is no longer. The `vca.*` control group (14 ids: `vca.create`, `vca.remove`,
  `vca.list`, `vca.get_state`, `vca.rename`, `vca.set_gain`, `vca.set_mute`, `vca.set_solo`, `vca.assign`,
  `vca.unassign`, `vca.set_phase_lock`, `vca.track_add`, `vca.track_remove`, `vca.edit_move`) makes the
  whole group drivable through the socket and the MCP bridge, including the **edit** half the group's name
  promises: an edit set of tracks (`vca.track_add`) and a phase lock (`vca.set_phase_lock`, ON by default)
  under which `vca.edit_move` moves a named clip and every other member's clips that overlap its
  pre-command span by the same delta, so a take recorded across several inputs slides as one object and
  stays sample-aligned. The edit set is persisted on the group's own `<vcagroup>` element (`locked`, plus
  one `<edittrack track="n"/>` per member). `docs/VCA-EDIT-GROUPS.md` §1-§4 is that half's report, and
  the three limits that remain are named there and in `docs/RELEASE-NOTES-v0.3.0-alpha.md`: **one** media
  edit is propagated (a clip move — trim, slip, split and fades are not), a track deleted while it is in an
  edit set stays in the set (reported as `missing_tracks` / `skipped_tracks` until `vca.track_remove`), and
  `vca.set_solo`'s undo does not restore the transient `MixerChannel::m_muteBeforeSolo`. There is still no
  Lua binding for any of it, and a group's audibility is proved by `VcaGroupTest`'s rendered dB delta, not
  by the socket transcript.*
- **No racks in the interface, and no scripting access.** Parallel chains and a chain selector exist and are
  saved with the project, but a user can only load a project that already contains a `<rack>`; there is no UI
  and no binding. Switching chains is not crossfaded, so it can click.
  Verified in the tree: a rack is saved as the `rack` element inside a `<mixerchannel>`
  (`src/core/Rack.cpp:48`, `RACK_ELEMENT`; the chains and the selector are built in `Rack.cpp` /
  `RackNodes.cpp`), `docs/RACKS.md` §0 is the implementing lane's report, and the lane is an ancestor of this
  tip. *Corrected 2026-09-13: the UI half of this bullet is still exactly true — nothing in `src/gui/` creates
  a rack, adds a chain or moves the selector — but the reachability half is no longer: the `rack.*` control
  group (`rack.get_state`, `rack.add_chain`, `rack.remove_chain`, `rack.set_selected`) makes the whole rack
  drivable through `--control-socket` and the MCP bridge, which is `docs/RACK-MACROS.md` §1. There is still no
  Lua binding for it.*
- **Macros and key/velocity zones are socket-only, and the zones do not route yet — added 2026-09-13.** A rack
  macro is a named, persisted scalar that drives existing parameters through range windows
  (`rack.macro_add` / `rack.macro_target_add` / `rack.macro_set`, `docs/RACK-MACROS.md` §2) and a key/velocity
  zone is a persisted, validated key-and-velocity range mapped to one of the rack's chains
  (`rack.zone_add` / `rack.zone_remove` / `rack.zone_resolve`, §3), both saved as children of the channel's
  existing `<rack>` element. **Rack macros and key/velocity zones are drivable through the socket, not from
  the interface**: there is no macro knob, no zone editor and no key map to draw, so a user cannot create,
  see or move either one — `rack.get_state` reports them and nothing in `src/gui/` draws them. **And the zone
  half is a model plus a lookup, not note routing:** the rack renders one stereo block and has no per-note
  input, so no note path consults a zone in this build — `rack.zone_resolve` answers which zone a note would
  fall into, and nothing acts on that answer. `docs/RACK-MACROS.md` §4 states the same limit and what is
  needed to close it.
- **Folder tracks are in the engine and on the socket, and there is no interface for them —
  added 2026-09-13.** A folder is a **real container**: a track of its own type (`track.add`
  `type=folder`) that holds other tracks, in two modes — `group` (organisation only, every child
  keeping its own mixer channel, the default) and `routing` (the folder takes one regular mixer
  channel of its own and every child's output is summed through it) — plus a persisted `collapsed`
  flag, a persisted `pinned` flag and named, project-saved **visibility sets**. The engine half is
  `include/TrackFolder.h` / `src/tracks/TrackFolder.cpp` and the container's visibility-set store;
  the decisions, the ownership rule (a folder **references** its children and never owns them, so it
  can never double-free one) and the latency-compensation answer are in
  **`docs/TRACK-FOLDER-DESIGN.md`**; the proofs are `tests/src/core/TrackFolderTest.cpp` and the
  registered socket transcript `tests/control-track-folder.py`.
  **Folder tracks, their two modes, pinning and the named visibility sets are drivable through the
  socket, not from the interface**: nothing in `src/gui/` creates a folder, indents a child, collapses
  a row, draws the relation, shows a pin or offers a set switcher — a folder's row is an ordinary
  `TrackView`, so a user without a socket client cannot make one and the collapsed/pinned flags this
  release persists have **no affordance reading them yet**. Two further limits belong to this bullet
  rather than in a bug report: a child's mixer-channel **index** can change across a
  routing-off/routing-on cycle (`Mixer::deleteChannel` renumbers channels) while the routing relation
  itself is preserved; and an **older build** reading the folder's `type` hits `Track::create`'s
  `default: break` and **drops the row** — a dropped track rather than a degrading one, which
  `docs/TRACK-FOLDER-DESIGN.md` §4.4 states as the forward-compatibility cost of the enumerator
  approach.
- **No clip fade, crossfade or clip-gain gestures.** The clip model is in (an authored window that survives
  playback and is saved with the project) and so, since 2026-09-13, is the fade/gain model — but there are
  **no trim, slip, fade, crossfade or clip-gain tools** in the interface, and **fades, crossfades and clip gain
  are drivable through the socket, not from the interface**.
  *Corrected 2026-09-13: "trim" here means the **source window**, and the same word names a feature that does
  exist — a clip's **length** is changed today by dragging its edge and by the agent command `clip.resize`
  (`STATUS-CORRECTION-2026-09-13.md` §3, "Clip-length resize"; the source-window trim/slip has a model —
  `include/SampleWindow.h`, `srcin`/`srcout` — and registered tests but no authoring gesture, §3 "Clip
  source-window trim / slip"). Second correction, same day: fades, crossfades and clip gain have an ENGINE and
  an agent surface now — `include/ClipEdits.h` and `src/core/ClipEdits.cpp` hold a per-clip gain and
  fade-in/fade-out ramp (`gain`/`fadein`/`fadeout`/`fadeinshape`/`fadeoutshape` on the `<sampleclip>` element,
  each written only when it differs from the neutral default), `src/core/SamplePlayHandle.cpp` multiplies the
  envelope into the frames the play handle renders (never in `Sample::render`, which the browser preview and
  the metronome share), and `clip.set_gain`, `clip.set_fade` and `clip.crossfade` are registered commands
  (`src/core/ControlCommandsClipEdits.cpp`, reversibility rows in `src/core/ControlReversibilityTable.cpp`),
  proved by `tests/src/core/ClipEditsTest.cpp` and `tests/src/core/ClipFadesRenderTest.cpp`. What this bullet
  still means, and what is still absent, is a slip tool, every authoring gesture for those three edits, a
  linked crossfade object, and any fade or gain drawn on the waveform.*
  *The same correction states this feature's own limits, so they are not read as bugs: fades and clip gain are
  applied to **audio clips only** — a MIDI clip can carry the fields but nothing renders them, so
  `clip.set_fade` / `clip.set_gain` refuse a MIDI clip with a typed error rather than writing state that would
  do nothing — and a crossfade is a pair of independent fades rather than a linked object, so moving or
  resizing one clip afterwards breaks the pairing silently.*
- **Take lanes and comping are in the engine and on the socket, and there is no interface and no playback of a
  composite.** *This bullet used to read: "No take lanes and no comping. Verified as an absence in this tree:
  `grep -rniI "takelane\|take lane\|comping" src/ include/` returns 0 hits." That claim is now FALSE and the bullet
  is amended rather than deleted, because a reader who meets the old sentence in an older copy of this page has to
  be able to see what replaced it.* What is in the tree: take lanes on a track (`comp.lane_add`, `comp.lane_remove`,
  `comp.lane_list`) with a lane tag on each take clip (`clip`'s `lane` attribute), the assignment of an audio take to
  a lane (`comp.assign`), and the non-destructive composite — per-segment selection, rebuild and a state query
  (`comp.select`, `comp.rebuild`, `comp.get_state`) — over `include/TakeLane.h` / `src/core/TakeLane.cpp`, with A16
  rows in `src/core/ControlReversibilityTable.cpp` and `...Passive.cpp` and the proof split across the two
  comping test files, `tests/src/core/TakeLaneTest.cpp` (the lanes, the take audio and the project file) and
  `tests/src/core/TakeLaneCompTest.cpp` (the composite and the `comp.*` surface); the decisions are recorded
  in **`docs/COMPING.md`**.
  *What this bullet still means, and what is still absent: **nothing renders a composite** — no playback path reads
  it, so a comp sounds exactly like the track's clips as they lie and the per-segment `srcpos` slip is recorded but
  not applied; there is **no lane geometry, no lane handle, no comping gesture and no waveform drawing** anywhere in
  `src/gui/`; there is **no audition and no flatten** (the destructive bounce a comp can end in); and **MIDI
  comping is out** — `comp.assign` refuses a MIDI clip with a typed error, because the lane tag rides the clip
  attribute helper only `SampleClip` calls in this release.*
- **The modulation layer is in the engine and on the socket, and there is no interface for it.**
  Modulators (`modulator.*`, ten ids with `note.expression.*`) drive device parameters in a mixer channel's rack
  chains by a relative depth, on the audio path, once per block — `include/ModulationLayer.h`,
  `src/core/ModulationLayer.cpp`, the decisions and the honest limits in **`docs/MODULATION.md`**, the proofs in
  `tests/src/core/ModulationLayerTest.cpp` and `tests/src/core/ControlModulatorCommandsTest.cpp`.
  *What this bullet still means, and what is still absent: **nothing in `src/gui/` creates, draws or edits a
  modulator or a note's per-note expression** — the reachable path is the control surface, so a user without a
  socket client still cannot make a modulator; modulation is applied **once per audio block**, not
  sample-accurately; the source is an **LFO only** (there is no envelope follower); a route can name a **device
  parameter inside a mixer channel's rack chains** and not the Song's own master gain or an instrument's own
  parameters; and while a modulator is active the parameter's own control is taken over, so a fader shows the
  base it is modulated around rather than the modulated value.*
  Per-note expression was **not** a new store: `note.expression_set` / `get` / `clear` read and write the fields
  task `#601` already put on a `Note` (`mpepitch` / `mpepressure` / `mpetimbre`), so `docs/MPE.md` §4's limit
  is now lifted — **all three axes reach playback**: pitch as a frequency ratio, pressure and timbre as MIDI
  events on the note's own member channel (task #649). There is still no per-note expression editor.
- **No plugin-scanning interface worth the name.** A scan cache and a quarantine list exist; the user-facing
  surface is thin or absent. Verified against this tree: the cache is JSON on disk and the documented way to
  quarantine a plugin is a `{"path": …, "reason": …}` entry in that file; `docs/PLUGIN-SCAN-CACHE.md` §5
  records that there is **no GUI for it yet**.
- **Two features are compiled out of these builds**: the **WASM DSP sandbox** (needs the wasmtime C API) and
  **offline HTDemucs stem separation** (opt-in at configure time). Both report `OFF` in the binary's own build
  options, re-stated from the build this page was applied against:
  `WANT_WASM='OFF'`, `WANT_STEM_SPLIT='OFF'` (`build/lmmsversion.h`, the same text the binary prints on its
  `Build options:` line). **What enforces "everything the release notes document as present is in this build"
  is `tests/release-honesty-gate.sh`, and on this release's configuration it passes every row.** The guard's
  mechanism is the checkable half: it fails a documented-present feature when the build under test does not
  report that option `ON` (`AUTO` is not `ON` — the script's own header says so), it fails a documented-absent
  feature that reports `ON`, it exits 1 on any mismatch, and the six build jobs in
  `.github/workflows/build.yml` run it against the binary they have just built. **Measured on this release's
  configuration: 6 of 6 rows match** — the three plugin hosts `ON matches ON` and named as modules
  (`libvst3effect.so`, `libvst3instrument.so`, `libclapeffect.so`), and session view, the WASM sandbox and
  stem separation `OFF matches OFF` (`RESULT: PASS — all 6 documented feature(s) are what this build contains`,
  `tests/integration-logs-release-verify/honesty-guard.log`). Run against the two older, non-release build
  directories that happen to sit on this box it does not pass (3 of 6 rows on `build-coverage/zene`, 1 of 6 on
  `build/zene`) — those directories are configured against the manifest, and the guard is right to fail them;
  the release run above is this claim's evidence. The two features above are deliberately absent either way.
  **Changed in 0.3.0-alpha:** the `session-view` row is no longer one of them — the option defaults ON and the
  row now requires ON (see the Session View bullet above for what that does and does not include).

- **The WASM DSP sandbox is present and runnable when built with the wasmtime C API, it is drivable through
  the socket, and the RELEASE builds still compile it out — added 2026-09-13 (`#614`), revised by
  `030/w21-wasm-run`.** Three facts, and they are not in contradiction:
  - **The release builds compile it out, and the manifest row is still TRUE.** `WANT_WASM` defaults ON
    (`CMakeLists.txt:140`) and degrades to OFF when the wasmtime C API is absent from the find path
    (`CMakeLists.txt:957-963`). **None of the seven CI jobs provisions wasmtime**, so every released binary
    is built with `WANT_WASM=OFF` and `tests/advertised-features.tsv`'s `wasm-sandbox WANT_WASM OFF - *` row
    stays truthful; its platform column stays honest too. Nothing about this change touches that row, and
    flipping it to ON would fail `tests/release-honesty-gate.sh` — which is the gate working.
  - **On a box that has the C API, the sandbox is not a documentation-only item any more.** This box does:
    `zene-remote/third_party/wasmtime` holds the pinned v48.0.1 prebuilt C API, and
    `cmake -DWANT_QT6=ON -DWANT_WASM=ON -DWASMTIME_ROOT=<that prefix>` configures with
    `WANT_WASM:BOOL=ON` and a build-options line reading `WASM DSP sandbox : Enabled`. In that configuration
    the sandbox, the `wasm_effect` plugin, `wasm-wat2wasm` and both sandbox test targets all exist and run.
    **Note which prefix:** the pinned tree also carries a `min/` variant, and `min/lib/libwasmtime.a` does
    **not** export `wasmtime_module_new` or `wasmtime_wat2wasm`, so it cannot link this sandbox at all — the
    full `include/` + `lib/` prefix is the one that works.
  - **The sandbox is drivable through `--control-socket`** (below). That is the contract leg this item was
    missing.

  `docs/WASM-EFFECT-ABI.md` states the ABI a module must implement against the host — the exports and their
  signatures, the imports, the planar memory layout and how buffers are passed, how parameters, the sample
  rate and the block size reach a module, and the error/return conventions — derived statement-by-statement
  from `src/wasm/WasmSandbox.cpp`, `src/wasm/WasmWorker.cpp` and `src/wasm/WasmAbi.h`, with everything not
  determinable from that source listed as **UNKNOWN** rather than guessed. A conformance suite
  (`WasmAbiConformanceTest`) and one example effect written from that document alone
  (`tests/data/wasm-effect-abi/softclip.wat`) are committed and registered under the same `if(WANT_WASM)`
  guard as `WasmSandboxTest`.

- **The `wasm.*` command group, and what it does NOT reach — added 2026-09-13.** Six commands
  (`wasm.list`, `wasm.get_state`, `wasm.load`, `wasm.unload`, `wasm.set_param`, `wasm.process`), declared,
  defined and registered only under `#ifdef LMMS_HAVE_WASM`, so a build without wasmtime neither compiles
  them, nor registers their ids, nor carries their six SPEC A16 rows. `wasm.list` reports what the sandbox
  can host (the ABI surface and the module files in a directory), `wasm.load` / `wasm.unload` host and drop
  a module, `wasm.get_state` reports what is hosted, `wasm.set_param` writes one of the 16 parameter slots
  and `wasm.process` runs one block through the module and reports the outcome — a trap included, reported
  rather than thrown. The proof is the committed control-surface transcript `tests/control-wasm-sandbox.py`
  (registered as the `ControlWasmSandbox` ctest, WASM-ON builds only).
  **UI absence — one line:** these six commands are the only way to host, inspect, drive or parameterise a
  module **headlessly**; the interface's only route is the `wasm_effect` plugin's modal module chooser, which
  needs a display.
  **And the limit, stated plainly: the group drives the HOST's own sandbox, not a device's.**
  `wasm.load` does not put a module into an effect's audio path and nothing here is heard:
  `wasm_effect` instances are still given a module through that dialog, and their 8 parameter models are
  project state reachable with `plugin.param_*` on a `wasm_effect` device. A module hosted by `wasm.load` is
  not in any chain. `docs/WASM-EFFECT-ABI.md` §13 says the same.

- **What would still close `docs/INDEPENDENT-NOTES-READ.md` §B5:** provisioning the wasmtime C API **in the
  seven CI jobs**, so that `wasm-sandbox OFF` becomes a choice rather than an absence-by-dependency. The C
  API being present on *this box* (above) does not close it — CI is what builds the release — and
  `tests/advertised-features.tsv` and `tests/release-honesty-gate.sh` would have to be reconciled in the
  same commit that did it. Until then the row states the truth and the gate enforces it.

- **There is no undo-history UI — added 2026-09-13.** The undo stack is now bounded and its drags are
  grouped: `control.undo_depth` reports the depth, the count cap and the byte budget it is kept within,
  the bytes it retains and how many steps a bound has evicted, `control.set_undo_depth` sets the two
  caps, and `control.set_undo_coalescing` sets the window inside which a run of the same command on the
  same target is one undo step (a 200-call drag is one Ctrl+Z) — **the depth, the caps and the
  coalescing window are drivable through the socket, not from the interface**: there is no undo-history
  panel, no depth setting in any dialog and no gesture setting to change, and nothing in `src/gui/` draws
  or configures any of them. The one thing the interface does have is Edit ▸ Undo / Redo (Ctrl+Z), which
  is the *same* `ProjectJournal::undo()` the socket drives. What the bound cannot do is recover a step
  it evicted: `control.undo` refuses, typed, when a record's step has fallen off the stack. The two
  decisions and their values are in `docs/UNDO-BOUNDS.md`.

- **An agent session's MCP tool list can be a stale copy, and re-pointing the bridge is a deployment act — added 2026-09-13.** The `zene-control` bridge generates one MCP tool per command id it reads from a live instance, and a registered ctest now fails on any registered id the bridge offers no tool for (live, offline, and against a planted stale cache), so the *bridge in this tree* covers all **173** ids — but the entry in `~/.hermes/config.yaml` points at a **scratch copy** of the bridge (`projects/lmms-fl-research/mcp-zene-control`, git-ignored on purpose) whose offline list is the 0.1.0-alpha **70**, so a Hermes session still sees 70 ids until that entry is re-pointed at this tree's `tools/mcp-zene-control` or an instance answers at the scratch copy's socket (`docs/COVERAGE-MATRIX-2026-09-13.md` §4.2–4.4). No commit can fix a path outside the repository.

- **The offline tool list is stale whenever no instance is running, and the bridge can only *say so* while one is — added 2026-09-15.** Serving a shorter list is a limitation of the design, not a bug: with no instance at the socket the bridge has nothing to compare against and answers from the last-known copy. What changed is that the copy is now checkable — every bundle records the surface it describes (`id_count`, `group_count`, `ids_sha256`), and whenever **an instance IS answering** `zene_status` and `zene_commands` carry an `offline_drift` block naming each offline copy, whether it is stale, and which ids it is missing. Proven against a live binary of the integration tip: **265 ids / 43 groups live against this tree's 144-id snapshot**, the flag fired, and the ten groups feature-list row 49 measured as tool-free (`browser`, `comp`, `export`, `link`, `modulator`, `rack`, `session`, `telemetry`, `warp`, `wasm`) are driven end to end by the registered ctest `ControlMcpGroupCoverage` — nine against the real binary, `wasm.` excused by a both-directions `--compiled-out` flag because **no build on this machine compiles the sandbox in** (`Wasmtime_LIBRARY-NOTFOUND` in every configured build; the vendored C API under `zene-030/whost/third_party/wasmtime` is wired into nothing). A wasm-enabled build gets no flag and must drive the group for real; the bridge's own half of that case is `tools/mcp-zene-control/tests/test_declared_surface.py`. **The limit, stated plainly:** nothing checks the surface of an offline copy *while it is being served* with no instance up, and a copy can still be older than the tree it ships with — it is now loudly stale, not silently short.

## Where the quality bars are not met yet

- **Renders are reproducible — with two exceptions.** Exports now render on a single thread, so for **7 of the
  nine projects the determinism sweep covers two renders are byte-identical** (the tree ships **68**
  `.mmp`/`.mmpz` files; nine is the sweep's sample, not the repository's count —
  `docs/RENDER-DETERMINISM.md`). **Two are not**: `Root84` and `StrictProduction` differ
  even with the CPU pinned, ASLR disabled, `rand()` fixed and the clock frozen, and for `Root84` bypassing all
  twelve of its effect chains changes nothing — the cause is inside those instruments, not the renderer, and it
  is named in our notes. In practice: treat a render as reproducible for most projects, and verify rather than
  assume for those two. (Stock LMMS 1.3.0-alpha.2 is non-reproducible for the same demos, so this is a fix we
  carry that upstream does not.) The two, named as the tree ships them:
  `data/projects/demos/StrictProduction-DearJonDoe.mmp` and `data/projects/shorties/Root84-TrancyLoop.mmpz` —
  `docs/RENDER-DETERMINISM.md` §9 and §10.
- **No measured crash-free rate.** The crash reporter is new in this release; until there is a body of reports
  the "how often does it crash" number does not exist. That number is the point of shipping an alpha.
- **Our own test coverage, measured on this release tree: 87.21 % of the lines this configuration instruments
  (13,770/15,790), over the 165 of the 242 fork-scope entries that produced a record in that capture.** The
  **baseline scope** — the gate's own per-file baseline, 67 files — is at **85.95 %** (4,614/5,368), above our
  85 % aspiration; the headline is lower because a fuller configuration instruments more files. The other 77
  entries are sources this configuration does not compile, headers no translation unit instantiates, and
  tooling, and **the gate prints that split itself**, so the number is a claim about the 165 files it names and
  not about the whole scope. Measured at `3ef822eaf`; every commit after it is documentation only.
  How it was measured, so you can repeat it: `tests/run-coverage.sh build-coverage` on this tree with the pinned
  VST3 SDK and CLAP headers provisioned and **`-DWANT_VST3_TEST_INSTRUMENT=ON`**, so the plugin modules are
  instrumented *and* their integration suites actually run. An earlier capture of the same tree with the fixture
  off measured **75.06 %** — the two numbers the *earlier* capture reported over the 119 files it named (81.60 %
  with the fixture, 75.06 % without) are a genuine before/after pair because they share a denominator, and the
  gap is eight plugin files that go from 0 % to covered once those suites run, which is why the fuller
  configuration is the honest one. **Today's 87.21 % is not comparable with that pair**: it is a rate over the
  165 files that produced a record in the current capture, and the merge trains grew the fork scope from 175
  entries to 242 in between, so the denominator moved with the tree.
  *Corrected 2026-09-13: `242` and the 165-file rate are correctly attributed to the capture commit, and the
  rates are unchanged. What this page got wrong is "every commit after it is documentation only" — false at
  this tip: `cbbaf315f` and `e6050eed9` both changed `tests/fork-sources.txt`, and at `5565b4b1b` the ledger
  holds **244** non-comment entries, which is the figure the four-audit verification records
  (`STATUS-CORRECTION-2026-09-13.md` §3, last bullet). Re-run at this tip the universe behind that rate is two
  entries larger.*
- **Our own coverage gate fails on that same build, and we are telling you rather than exempting it away.**
  `tests/coverage-gate.sh --check` reports **15 new files below its 50 % entry floor**. **Ten** are dialogs,
  views and plugin-browser code that **cannot be constructed in a headless test binary** — a `Knob` needs
  `getGUI()`, which is null under a render-only engine init, the same wall we hit and documented while fixing
  the instrument window. **Two** are the telemetry transport and its consent dialog, **inert by design**
  (there is no server to send to). **Three are genuinely untested rather than untestable — including the VST3
  *effect* module's own class, which no test instantiates because the fixture we built is an *instrument*.**
  We are **not** writing exemptions for those fifteen at this release: an entry-floor exemption is a per-file
  decision with a written reason, and making fifteen of them at the tag is how a gate stops meaning anything —
  the same reasoning that left the whole-tree ratchets red rather than grandfathered. The plan is on the record:
  build an effect fixture and instantiate the effect module, then decide the headless-untestable and
  deliberately-inert files individually, with their reasons.
- **Automation is not sample-accurate.** Modes work (Read / Touch / Latch / Write) and riding a control in Read
  cannot destroy written automation, but automation is evaluated once per tick, so it lands on a tick boundary
  rather than a sample. Verified in the tree via the automation lane's own record: `docs/AUTOMATION-MODES.md`
  states that automation is evaluated once per tick and names the line that stands between that and a
  per-frame read.
- **Automation modes are drivable through the socket but not from the interface — added 2026-09-15.**
  `automation.mode_set` (off / read / touch / latch / write) and `automation.record_mode_set` (per-clip record
  flag) are registered, have schemas, are reported back per parameter (`automation.get_state` carries each
  parameter's `mode` and each clip's `recording` flag, so a set is observable and not only issuable), and are
  proven (`AutomationModesTest` pins the no-destruction property: riding a control in Read cannot alter written
  automation, with a sensitivity control so the comparison cannot pass by being blind;
  `ControlAutomationModesTest::readRideThroughTheSocketCannotTouchTheRecordedAutomation` repeats it through
  the command surface with a write-mode leg that must change the clip). `off` is a mode of its own and not a
  second name for `read`: an off control ignores its written curve — the manual value stands — and writes
  nothing. The mode is runtime state: it is not persisted in the project file and is not journalled, so a reload
  resets every control to Read with no trim and a mode change has no undo. Only the mixer fader is wired to a
  touch gesture; pan, sends and plugin-parameter knobs would each need widget hooks, so Touch and Latch have
  nothing to take hold of through the socket yet (Write needs no gesture and is fully drivable). Write mode does
  not erase the un-passed remainder of the clip — it overwrites where the playhead reaches and leaves the
  automation ahead of it untouched.
- **Warping changes pitch.** The warp engine attaches markers and lets a clip follow or lead the project tempo,
  but the time-stretch is done by resampling: a 2× stretch is an octave up. Pitch-preserving stretch is not
  built.
  Verified in the tree: `docs/WARP.md` §0 is the implementing lane's report — `WarpMarkers` is a child element of
  `<sampleclip>`, markers are pinned to source frames so a trim moves `sourceIn`/`sourceOut` and the markers stay
  on the audio, the map is monotonic and exact at every marker, with no markers it is the pre-warp arithmetic bit
  for bit, and a headless render puts a source whose transients are at 0/1/2/3 s at 0/0.5/1.0/1.5 s under a
  marker pair declaring 2× — while the base binary renders that project as if the `<warp>` element were absent.
  The lane `post-alpha/warp` is an ancestor of this tip.
- **And there is no warp UI at all — added 2026-09-13.** The sentence above is true of the *engine*; the
  product can author markers only through the control surface. `warp.list` / `warp.add` / `warp.move` /
  `warp.remove` / `warp.set` are registered commands (argument and result schemas, A16 reversibility records)
  and a registered ctest plus a committed transcript cover them — but nothing in `src/gui/` draws a marker,
  drags one, snaps one to the grid or places the first one, so an interface-only user cannot warp a clip at
  all. Warp marker editing is drivable through the socket, not from the interface.
- **MPE: all three axes reach playback.** Per-note expression is captured from MPE input, stored on the note
  and editable; **pitch, pressure and timbre are all applied on playback** — pitch as a frequency ratio,
  pressure and timbre as MIDI events on the note's own member channel (task #649). There is still no per-note
  expression editor.
  Verified in the tree: `docs/MPE.md` §0 is the implementing lane's verdict — expression is captured from
  MPE-style input, stored backwards-compatibly on the note as `mpepitch` / `mpepressure` / `mpetimbre`, readable
  and editable through a headless API, with all three axes reaching the instrument through the playback path
  (`src/core/NotePlayHandle.cpp`). `src/core/midi/MpeExpression.cpp` is in the tree, and the lane
  `post-alpha/mpe` is an ancestor of this tip. **What consumes the two new axes, stated plainly:** no built-in
  synthesiser does — they are driven by a note's frequency and volume and never see MIDI — so a hosted
  instrument (Vestige / LV2 / CLAP / Carla) or a MIDI output port is where a musician hears pressure and timbre,
  and the *proof* applies them to an in-tree MIT test instrument
  (`tests/src/plugins/MpeTestConsumer.cpp`, built from `tests/`, never installed), which is the vehicle of the
  registered ctest `MpePlaybackTest`. That test is a measured comparison, not a smoke test: one audio block
  rendered with the expression against the same block without it, and the level has to move by the ratio the
  expression asks for.
- **Recording is a two-track prototype.** Two input channels captured into two tracks, with the capture path
  hardware-verified. Arbitrary input counts and input monitoring are not implemented, and the default Linux
  ALSA backend has **no capture path at all** — recording needs JACK or SDL.
- **Shutdown waits rather than aborts, and that is deliberate.** This release fixes a crash where the
  application could die on exit (`QThread: Destroyed while thread is still running`) because the engine gave up
  waiting for an audio worker. The fix makes that wait unbounded: if a job ever failed to return, shutdown
  would **block until it did** instead of aborting. We chose a hang over a crash.
  Verified in the tree: `docs/TEST-HYGIENE.md` §0 is the lane's own report — 31 of 184 engine-test runs aborted
  at load ~27 before the fix, 0 of 30 full-suite runs after, and §9 names the half of the fix whose necessity
  the lane's own measurement did **not** establish rather than glossing it. The fix, the assertion and the test
  are all present here: the bounded re-check in the worker's wait
  (`src/core/AudioEngineWorkerThread.cpp:204`) and `tests/src/core/AudioEngineTeardownTest.cpp` (asserting
  `stranded == 0`, `:135-148`, `:170-179`). The lane `post-alpha/test-hygiene` is an ancestor of this tip.

- **Export dither and the SRC quality have no interface — added 2026-09-13.** The engine is in and both are
  drivable through `--control-socket` (`export.get_settings`, `export.set_dither`, `export.set_src_quality`)
  and the CLI (`--dither`, `--src-quality`), but **neither is on any dialog**: drivable through the socket,
  not from the interface. The dither is OFF by default and the SRC quality defaults to the converter the
  engine has always used, so a render that asks for nothing is byte-for-byte what it was. It is implemented
  for **WAV only** — FLAC, OGG and MP3 do not take the dither yet — and 32-bit float is deliberately never
  dithered (a float format has no quantisation step to dither against). See `docs/EXPORT-SRC-DITHER.md`.

- **Browser tag/metadata search and the waveform peak cache have no interface — added 2026-09-13.**
  The browser's items can be queried by name, by tag and by what the audio file itself says it is (sample
  rate, channels, length and the embedded title/artist/album/comment/genre tags), and a file's waveform
  peaks are read through a bounded cache — all of it drivable through `--control-socket`
  (`browser.roots`, `browser.query`, `browser.tags`, `browser.peaks`, `browser.tag.add`,
  `browser.tag.remove`). **Nothing in the interface can do any of it**: the browser's filter box still
  matches file names only, there is no tag column, no tag editor and no query UI, and the browser does not
  draw a waveform — drivable through the socket, not from the interface. The tags are persisted in the
  user's config directory (`browser-tags.json`), not in the project file, so they are a property of the
  user's library rather than of a project; `docs/RELEASE-NOTES-v0.3.0-alpha.md` carries the same sentence.
- **Tempo and time-signature changes have no editor — added 2026-09-13.** The engine is in (a persisted,
  ordered set of tempo and time-signature events the timeline obeys, with the ticks-to-time conversion reading
  it — `docs/TEMPO-MAP.md`) and it is drivable through `--control-socket`
  (`transport.tempo_map_get` / `tempo_map_add` / `tempo_map_remove` / `tempo_map_clear` /
  `tempo_map_set_active`), but **nothing in `src/gui/` draws, edits or reads a tempo map**: drivable through
  the socket, not from the interface. With an empty or inactive map the tempo is the single project value it
  has always been, so a project that never used one renders byte-for-byte what it did.
- **Standard MIDI File tempo-map interchange has no interface — added 2026-09-15.** The tempo map can be
  written as a conductor track in a Standard MIDI File another DAW reads, and a file's tempo and
  time-signature events can be read back and imported, through `--control-socket`
  (`interchange.smf_convention` / `smf_export` / `smf_read` / `smf_import`) — but **nothing in `src/gui/` writes
  or reads one**: drivable through the socket, not from the interface, and the File menu's "Export MIDI" is
  the pre-existing note export, neither changed by nor wired to these ids. `docs/SMF-INTERCHANGE.md` records
  the tick/PPQ and time-signature convention and the stated limits — events are steps, so no tempo curve is
  written, and only the conductor track is (no notes, clips or automation).
- **DAWproject import / export has no interface — added 2026-09-15.** Tracks, clips, notes, the tempo map
  and mixer strips can be written as a DAWproject container another DAW reads, and a file can be read back
  and imported, through `--control-socket` (`dawproject.convention` / `dawproject.export` / `dawproject.read` /
  `dawproject.import`) — but **nothing in `src/gui/` writes or reads one**: drivable through the socket, not
  from the interface. `docs/DAWPROJECT-INTERCHANGE.md` records the format version (1.0), the eleven stated
  losses (audio clips, automation, device state, sends, fades, loop points, scenes, folder nesting, mixer
  routing and sharing) and the time convention (beats, 48 ticks per quarter).
- **Session sync has no interface, and it is not Ableton Link — added 2026-09-13.** Two Zene instances on
  one machine (or one network segment, over UDP multicast on `224.76.78.75:20808`) can join one session and
  agree on a tempo and a shared beat phase — drivable through `--control-socket`
  (`link.get_state`, `link.set_enabled`, `link.set_quantum`, `link.set_start_stop_sync`,
  `link.set_session_tempo`), with one instance's tempo reaching an ordinary `transport.set_tempo` too — but
  **nothing in `src/gui/` draws, edits or reads a session tempo, a peer list or a beat phase**: drivable
  through the socket, not from the interface. The model is this project's own (`zene-link-style`, the
  semantics of Ableton Link without its library, which this build does not vendor — the licence finding that
  says vendoring it is *permitted* is `docs/LINK-SYNC.md` §1), so a Link-enabled third-party application
  cannot join this session. Also, a peer's newer tempo sets this instance's tempo, but nothing here starts or
  stops another instance's transport (`link.set_start_stop_sync` is announced and reported, never acted on),
  and the play head is not moved onto the session grid — the shared phase and this engine's phase are
  reported together, with the error between them, and `docs/LINK-SYNC.md` §5 lists every stated limit.
- **Freeze and bounce-in-place have no interface — added 2026-09-13.** A track's own output (its devices,
  fader, pan and sends) can be rendered to a WAV and the track made to play that render instead of its clips —
  drivable through `--control-socket` (`bounce.in_place`, `freeze.track`, `freeze.region`, `freeze.unfreeze`),
  and the frozen state is saved with the project and one `control.undo` takes it off — but **nothing in
  `src/gui/` renders a track, marks it frozen or plays a take**: drivable through the socket, not from the
  interface. Stated limits: a **region** freeze mutes the clips that *start* inside the region, so a clip that
  begins before the region and runs into it is left alone and sounds twice inside the region (the command names
  it in `overlapping_clips`); the render is 44.1 kHz and the take is re-sampled to the session's device rate,
  so a frozen render is not bit-identical to the same track unfrozen; a range's frame window is derived from
  the project tempo, so a project that also uses a tempo map can have its region offset read from the wrong
  tempo; a frozen track's take bypasses the
  track's own device chain and fader (they are already baked into the render), so those controls are inert
  until `freeze.unfreeze`; and a take whose WAV has moved since it was frozen still reports `frozen: true` but
  has nothing to play, which `track.get_state` reports as `frozen_audio_ready: false`.
- **The groove pool and quantise have no interface — added 2026-09-13.** A note pattern's timing and
  velocity feel can be captured into a named groove, re-applied to another clip with a strength, and notes
  can be quantised with a strength and a humanise amount — drivable through `--control-socket`
  (`groove.list`, `groove.extract`, `groove.set`, `groove.apply`, `groove.quantize`, `groove.remove`,
  `groove.rename`), with a registered ctest driving the real binary over the socket — but **nothing in
  `src/gui/` creates, shows, edits or applies a groove**: there is no groove list, no template browser, no
  drag-to-apply and no quantise dialog. The groove pool and quantise are drivable through the socket, not
  from the interface. `docs/GROOVE-POOL.md` also records the engine's own stated limits (a groove's
  resolution is the slot, a sample clip is refused because there are no notes to move, a groove is
  applied once rather than played live, and a re-quantise with no humanise puts the positions back on
  the grid but restores no velocity — a humanised take is reversed with `control.undo`, not by
  re-quantising).

- **Render/export presets have no interface, and a ranged render is socket-only — added 2026-09-15.**
  A named render preset (a sample rate, a bit depth and a stereo mode) can be saved, listed, applied
  and removed, drivable through `--control-socket` (`export.preset_add`, `export.preset_list`,
  `export.preset_apply`, `export.preset_remove`), and the applied preset is what the NEXT
  `render.render` is started with — but **nothing in `src/gui/` creates, shows, edits or applies a
  render preset** (a grep for `renderpreset` / `render preset` over `src/gui/` returns zero hits: the
  export dialog keeps its own per-render controls and has no preset list, no "save as preset" action
  and no apply control). Nor does the interface render a SELECTION to audio through this surface:
  `render.render` takes `start_ticks` / `end_ticks` and renders exactly that span (the CLI it drives
  gained `--range-start` / `--range-end`), while the dialog's "export between loop markers" checkbox
  is its own, pre-existing path and is neither changed by nor wired to these ids. Stated limits: a
  preset carries the **three settings only** — bitrate, compression level, the loudness report, dither
  and the SRC-quality choice are **not** in a preset, because the render CLI has no flag for them; an
  applied preset governs **`render.render` only**, while `render.stems` keeps its own fixed 44100 Hz
  and its own one-bar tail; the applied selection is **process-wide, not project state**, so it is not
  saved with the project and a new instance starts on the defaults; the **stereo mode is read only by
  the MP3 encoder** (`src/core/audio/AudioFileMP3.cpp:112`), so a WAV render stores it, passes it and
  is unaffected by it; the store is **per-user, not per-project** (`<userPresets>/renderpresets/`, one
  JSON document per preset), so two machines with the same project can hold different presets; the
  range is rendered EXACTLY, with no tail bar, so a selection render is not the same file as the
  matching span of a whole-project render once that render's own tail is counted; and a render already
  performed is **not** undone by undoing the apply that influenced it — its file stays where it was
  written, so the fallback is to apply the right preset and render again.
- **Plugin-chain presets have no interface, and their store is per-user rather than per-project — added
  2026-09-13.** A track's effect chain (its ordered devices together with each device's own settings)
  can be captured as a named preset and applied to another track, drivable through `--control-socket`
  (`chain.list`, `chain.get_state`, `chain.save`, `chain.apply`, `chain.rename`, `chain.remove`), with a
  registered ctest driving the real binary over the socket and asserting the applied device order and
  parameter values — but **nothing in `src/gui/` creates, shows, edits or applies a chain preset**:
  there is no preset list, no "save chain as preset" action and no apply control. Plugin-chain presets
  are drivable through the socket, not from the interface. The store is also **per-user, not
  per-project**: it lives in the user preset tree (`<userPresets>/chainpresets/`, one `.zcp` document per
  preset), which is exactly what makes a preset usable in another project — and what means a preset is
  not carried inside a project file, not shared with one and not versioned with it, so two machines with
  the same project can hold different presets. A preset carries effects only: the track's instrument and
  the rack's parallel chains (`rack.*`) are not part of it, and a preset naming a device this build
  cannot load is refused, typed, with the target's chain left untouched.
- **Punch in/out is the region and its gate, and there is no interface for it — added 2026-09-13.**
  A punch region (a tick range that capture is gated to, plus an arm flag) lives on the transport, is written
  with the project and survives a save/load — drivable through `--control-socket` (`transport.punch_set`,
  `transport.punch_clear`, `transport.punch_get_state`), and one `control.undo` takes a region back off through
  the timeline's own checkpoint — but **nothing in `src/gui/` draws a punch ruler, a region handle or a punch
  toggle, and the capture path does not consult the gate yet**: the region and
  `Timeline::punchCapturesAt()` are real and proved, and **wiring the audio-side capture gate is deferred** —
  this build has no capture path to gate (ALSA records nothing and the two-track prototype is fed by tests), so
  a gate here would be a change no test could exercise. Drivable through the socket, not from the interface.
- **Recording crash recovery is journalling and recovery, not an import — added 2026-09-13.** A capture in
  progress is journalled to a side file beside its take (`<take>.rec-journal`), a clean stop retires it, and an
  abnormal exit leaves it — so the next start can find the interrupted take and hand the material back, drivable
  through `--control-socket` (`record.journal_begin` / `journal_update` / `journal_finish`,
  `record.recovery_get_state` / `recovery_restore` / `recovery_discard`) — but **nothing in `src/gui/` offers a
  recovery prompt, and `record.recovery_restore` does not put the recovered take into the session**: 0.3.0 has
  no command that imports an audio file onto a track as a clip, so restore resolves the offer and hands back the
  material untouched (`audio_untouched: true`), which its result says in `next_step`. The bound is stated rather
  than implied: **guaranteed recoverable is `min(frames the journal recorded, frames the take's file holds)**;
  NOT recoverable is the audio written after the journal's last update (the journal lags by at most one second
  of audio) and up to **65536 frames** that were still in the recorder's ring buffer when the process died —
  audio that never reached a file. `include/RecordingJournal.h` states it, `record.recovery_get_state` reports
  it per take, and `docs/RELEASE-NOTES-v0.3.0-alpha.md` records the test that measures it. Drivable through the
  socket, not from the interface.
- **MIDI clock is the engine and the socket, and there is no interface for it — added 2026-09-13.** The DAW runs
  as a MIDI clock **master** (24 pulses to the quarter note, START/STOP/CONTINUE and a Song Position Pointer on
  the transport's own edges, emitted from the audio thread through the engine's existing MIDI output) and as a
  clock **slave** (it follows an incoming clock, measures its tempo over one quarter note of pulses, and writes
  that tempo to the song when told to follow) — drivable through `--control-socket` (`clock.get_state`,
  `clock.master_set`, `clock.slave_set`), with a registered ctest driving the real binary over the socket and a
  registered QTest proving the rate arithmetic — but **nothing in `src/gui/` offers a clock port selector, an
  external-sync toggle or a lock indicator**: `grep -rniI 'MidiClock' src/gui/` returns **0** hits against
  **36** for `MidiLearn` in the same directory, so the clock is drivable through the socket and not from the
  interface. The bound is stated rather than implied:
  **the bytes reach a MIDI device only where a real backend is open** — a headless run reports what the engine
  PRODUCED (the message counters and the bounded monitor `clock.get_state` returns, asserted by
  `tests/control-clock-commands.py`), NOT what an external instrument received, which no test on a box with no
  instrument can measure. **MIDI time code (MTC) is not generated at all**: a full-frame timecode master needs a
  frame rate, a drop-frame flag and a SMPTE start offset, and this engine's time model is ticks-per-bar with
  neither, so `clock.get_state` reports `mtc: "absent"` rather than a timecode it cannot produce (quarter-frame
  `0xF1` and song-position `0xF2` are decoded and counted on the INPUT side, and the note is what a slave can
  report without acting on it). Two further bounds: real-time messages are decoded and counted by the slave but
  **an incoming START/STOP/CONTINUE/SONG POSITION does not move the transport** in this release (the transport is
  the user's, and a MIDI thread moving it is a behavioural change this lane did not take), and the follower's
  measured tempo is accurate to **at most `tempo × 2 × 5 ms / window`** — the engine reports that number as
  `slave.tempo_error_bound_bpm` — because a pulse is timestamped when the MIDI client's reader thread observes it,
  and the tree's ALSA Raw reader polls after a 5 ms sleep (`src/core/midi/MidiAlsaRaw.cpp`). Drivable through the
  socket, not from the interface.

- **Retrospective MIDI capture keeps the last 8192 events, not a recording, and has no interface — added
  2026-09-13.** Arming the mode makes the engine keep a rolling window of the MIDI it receives, so what you
  just played can be written into a new clip AFTER the fact — drivable through `--control-socket`
  (`midi.retro_capture_arm`, `midi.retro_capture_status`, `midi.retro_capture_to_clip`), with a registered
  ctest playing REAL MIDI into the running engine and recovering it — but **nothing in `src/gui/` shows the
  window, how long it is or what it holds**: there is no view of it, no "you played something" prompt, and
  no keyboard shortcut. Retrospective MIDI capture is drivable through the socket, not from the interface.
  The bound is stated rather than implied: **8192 events (128 KiB), the most recent ones, per open MIDI
  client** — a **memory bound, not a time bound**, because the ring is written from the MIDI input thread and
  that path may not allocate, lock or call out, so its storage is allocated once and never resized. In the
  case the feature exists for (a human playing, 10–20 events a second) that is **roughly 7–13 minutes** — the
  release notes say *minutes, not hours* — and a dense controller stream fills the same window in under a
  minute; nothing guarantees a length in minutes. The policy is drop-OLDEST with the loss counted
  (`overwritten`, `paused_dropped`). The window is **per MIDI client, not per project or per track**: every
  channel and source port share it, arming and disarming do not clear it, choosing another MIDI backend
  discards it, and `project.save` writes none of it. It is not a recording, a SysEx is stored as a flagged
  placeholder rather than its bytes, clock/start/stop bytes are not stored, and a window that starts
  mid-phrase is reported as truncated (`unmatched_ons`/`unmatched_offs`) rather than tidied.
  `docs/MIDI-RETRO-CAPTURE-BOUNDS.md` is the decision record, and the registered ctest
  (`ControlRetroCapture`) asserts the capacity the build reports equals the figure that page states.
  **Owner's-31 item 15, retrospective AUDIO capture, is NOT in this release** — it needs the same rolling
  window applied to audio frames and this build has no capture path to apply it to (ALSA records nothing,
  and the two-track recorder prototype is fed by tests), so a window built now could not be filled and no
  bound stated for it could be measured by a registered test. Item 14's own recorded dependency is *none*;
  item 15's is the capture path itself.

- **Plugin delay compensation is readable, not settable, and has no interface — added 2026-09-14.** The
  mixer's own PDC graph (#605: the total latency from a source to the master output, every channel's
  alignment point, the latency a channel's effect chain adds, and the compensation the mixer applies at every
  send) is drivable through `--control-socket` with `pdc.report`, and the same report says whether **sidechain
  routing** exists and lists every sidechain send with its tap point — but **nothing in `src/gui/` shows a
  latency figure, a compensation value or a per-channel PDC table**, and no command SETS a compensation:
  `Mixer::updateLatencyCompensation()` recomputes every edge's delay from the routing graph once per period and
  publishes it, so a command that wrote one would be overwritten by the next period — the settable thing that
  changes PDC is the topology (`mixer.route_to` / `mixer.send_to` / `mixer.sidechain_to` / `bus.create`).
  **The number this build publishes is 0 unless a device reports latency**, because
  `Effect::latencyFrames()` defaults to 0 and in this tree only the WASM effect overrides it (and only with a
  module loaded) — so the arithmetic of a nonzero delay is proven in process by the registered
  `tests/src/core/PdcMixerTest.cpp` (sample alignment, the `LatencyCompensation::MaxFrames` clamp, the
  bit-identical zero-delay bypass) and the registered transcript `tests/control-pdc-commands.py` proves the
  SURFACE: the numbers are on the wire and they follow the routing. Stated as the bound it is, not hidden.
- **The routing graph is readable, and the patcher group edits it; there is no patcher GUI — added 2026-09-14,
  extended 2026-09-15.** The graph a
  signal is actually processed through (the effect chain's `RoutingGraph`: its nodes, connections, cached
  topological order and output node, plus a mixer channel's rack graph) is drivable through
  `--control-socket` with `routing.get_state`, and the mixer's routing is settable through `mixer.route_to` /
  `mixer.send_to` / `mixer.sidechain_to` / `mixer.route_remove` — but **nothing in `src/gui/` draws a patch
  bay, a cable, a node or a port**, and there is **no patcher canvas**: the `patcher.*` group (added
  2026-09-15, row 69) reads a chain's graph in patch terms and RE-WIRES it, but the node SET is the effect
  list's — a node can be neither added to nor removed from a chain's graph, and no verb sets a node's own
  parameters. Until 2026-09-15 no command edited a `RoutingGraph`, on two recorded grounds: the class's
  threading contract (`include/RoutingGraph.h`) says topology edits are control-thread operations that must
  not run concurrently with `process()`, and a chain's graph is DERIVED — `EffectChain::rebuildRoutingGraph()`
  clears and re-wires it from the effect list on every change, so a hand-wired edge would have been discarded
  by the next `plugin.load` / `plugin.unload`. `patcher.set_wiring` answers both (see its own bullet below);
  the **lock-free pending-change plan swap** of `PATCHER-MVP.md` section 4 Part C 3 /
  `mixer/SPEC-dynamic-routing.md` section 5.4 is still **not implemented**. **The read is narrower than the
  name sounds, and this is measured rather than estimated:** a chain whose devices HAVE audio-ports models
  keeps the plain effect loop (`EffectChain::rebuildRoutingGraph` returns early for it,
  `src/core/EffectChainPatcher.cpp:121`), and the built-in devices this tree ships are `AudioPlugin`-derived
  (`DefaultEffect`, `include/AudioPlugin.h:462`), so a track's or a channel's chain graph is normally EMPTY
  with `routes_through_graph: false` and `patcher.set_wiring` refuses it, typed
  (`error.kind: "refused"`); the graph with live prepared nodes is the **rack's**, which
  `routing.get_state` also reports and `tests/control-routing-commands.py` measures (two added chains = five
  nodes, six connections, the sum node as the output node, prepared at the engine's own block size). Devices
  that are NOT `AudioPlugin`-derived (a legacy `Effect`) do make a chain graph live, which is what
  `tests/src/core/PatcherCommandsTest.cpp` builds to prove the edit. The
  patcher GUI is out of scope for this
  release, exactly as feature row 28 records.
- **The patcher's edit is guarded, session-scoped and blocking — added 2026-09-15.** `patcher.get_state` /
  `patcher.set_wiring` (feature row 69) let an agent re-wire a target's effect chain through
  `--control-socket`: the wiring is addressed by ROLE (`"input"`, `"effect:<index>"`), it is re-applied by
  every derived rebuild, and `control.undo` restores the previous wiring as ONE step (a chain that was on its
  derived wiring comes back AS the derivation). Three bounds, stated rather than hidden: (1) **no patcher
  GUI** — nothing in `src/gui/` shows the wiring the way the socket does; (2) **the wiring is session state**
  — no `<routinggraph>` element is written into `<fxchain>`, so a patch does not survive a save/load, exactly
  as the pre-existing re-wired session did not (`docs/ROUTING-GRAPH-LIVE.md` section 7); (3) **the edit is not
  lock-free** — the new graph is built off the audio thread and published under the engine's model-change
  guard, so it is never concurrent with a render period, but it BLOCKS the audio thread for the rebuild's
  duration (every other topology edit in this tree, `appendEffect` / `moveUp` / `clear`, pays the same cost).
- **Buses are topology only, and `bus.remove` is not undoable — added 2026-09-14.** A parallel bus
  (`Mixer::createBusChannel`: a mixer channel that never receives instrument output and whose incoming sends
  default to pre-fader) is drivable through `--control-socket` with `bus.list` / `bus.create` / `bus.remove`
  — but **nothing in `src/gui/` offers "add bus" or draws a bus channel differently**, and there is no
  `bus.set_*`: a bus IS a mixer channel, so its fader and its routing are reached through `mixer.set_volume`
  and the routing verbs, deliberately rather than by a second set of commands. `bus.create` is ONE undoable
  step (the recorded action deletes the bus it created); **`bus.remove` is not reversible** and says so — it
  records the bus's full state but nothing in this engine recreates a channel WITH state, so `control.undo`
  fails with the typed `irreversible` kind and names the fallback (`mixer.remove_channel` has the same class
  for the same reason). A bus's audio behaviour (pre-fader send default, no instrument input) is proven in
  process by the registered `tests/src/core/AudioBusTest.cpp`; the topology is proven by
  `tests/control-bus-commands.py`.
- **Audio ports: the pin matrix is drivable, but only a device that HAS one, and there is no pin connector —
  added 2026-09-14.** A device's audio-ports model (its input/output pin matrices, their channel counts and
  names, and the engine's own used-channel caches) is readable through `--control-socket` with
  `port.get_state`, and one pin is writable with `port.set_pin` — the write is the PinConnector view's own
  call (`AudioPortsModel::Matrix::setPin`) and `control.undo` re-dispatches the recorded inverse command —
  but **nothing in `src/gui/` creates a `PinConnector` from the socket path, and the socket does not open a
  device editor**: the pin matrix belongs to an **AudioPlugin-derived** device (the CLAP and VST3 hosts and
  the analyser effects), and `Effect::audioPortsModel()` is `nullptr` for every built-in effect, so a build
  that ships no such device answers `port.get_state` with a typed `not_found` NAMING that fact. The
  registered transcript `tests/control-ports-commands.py` proves the typed answers and every malformed
  request's refusal in every build, and **reports ctest *Skipped* (never *Passed*) when the pin WRITE could
  not be measured** because this build has no device with an audio-ports model — a test that cannot make its
  measurement must not report that it did. The pin write's engine half is proven in process by the registered
  `tests/src/core/AudioPortsModelTest.cpp`.
- **The plugin scan cache and its quarantine list are drivable, and there is no interface for either —
  added 2026-09-14.** The cache the plugin scan fills (one record per candidate file: its path, the size and
  mtime it had, its status and, for a plugin, the descriptor metadata the scan resolved) and the quarantine
  list that hides files from discovery are both readable through `--control-socket` with
  `plugin.scan_cache_get_state` / `plugin.scan_cache_list` / `plugin.scan_cache_lookup`, and the quarantine is
  **operable** with `plugin.scan_cache_quarantine_add` / `plugin.scan_cache_quarantine_remove` — with
  `plugin.rescan` to apply an edit — but **nothing in `src/gui/` shows a scan record, a cache hit or a
  quarantine entry, and no view offers to add one**: before this group the only route to un-quarantining a
  plugin was hand-editing `plugin-scan-cache.json`, which is the defect the audit's row 46 names and the
  reason the group exists. Two bounds are stated rather than implied: the cache's CONTENTS became reachable
  only with this group (`PluginScanCache::records()` / `record(path)` were added for it, since the class could
  previously answer a count and a fingerprinted `lookup()` and nothing else), and **the enumeration is derived
  state, not project state** — nothing here is saved with the project, and a missing, corrupt or
  wrongly-versioned cache file degrades to a full scan by the engine's own contract (`include/PluginScanCache.h`).
  The engine layer keeps its proof (`tests/src/core/PluginScanCacheTest.cpp`, extended with the two enumeration
  cases) and the **surface** is proven by `tests/control-plugin-scan-commands.py`, which reads the cache file
  off disk as well as off the wire.
- **The crash reporter is drivable, and there is no way to see or send a report from the interface — added
  2026-09-14.** The reporter's state (whether it is installed, its report directory, every report it holds with
  its size and last-written time, whether one is still pending an offer, the `offered` sentinel, whether a
  session marker says the previous run exited uncleanly, its two hard bounds) is readable through
  `--control-socket` with `crash.list_reports`, and its two operations are `crash.acknowledge_report` (writes
  the sentinel, keeps the report) and `crash.discard_report` (deletes the report and the sentinel) — but
  **nothing in `src/gui/` shows a report, its state or its directory**, and **there is no way to send one**:
  `crash.upload_report` is registered and REFUSES every call by name, because this build has no upload and no
  network code of any kind in the reporter — a design property `include/CrashReporter.h` states in as many
  words, and a product decision that would have to be taken deliberately rather than assumed. Both writers are
  `irreversible` and name their fallback: nothing in the module removes the `offered` sentinel (delete the file
  and the report is pending again; the report itself is untouched), and nothing writes a report from a caller's
  bytes (`re-run the action that crashed`; the discarded report's content is not recoverable). **The reporter can
  be armed and disarmed through the socket since 2026-09-15** — `crash.enable` (with no arguments it arms the
  report directory the reporter remembers; an explicit `directory` arms that one) and `crash.disable` (which
  restores the default dispositions and DELETES NOTHING: the report, its directory and the session marker all
  survive, and `crash.list_reports` still names them while disarmed). Both report `armed`, read back from the
  kernel's own signal dispositions rather than from a flag, alongside the module's `installed` flag and `agree`;
  both are `snapshot` rows whose recorded inverse is the paired command, so `control.undo` takes either back.
  **Nothing in `src/gui/` arms or disarms the reporter** — there is no interface switch, no indicator that it is
  disarmed, and no dialog that offers the choice — and the module is a documented no-op on Windows, where the read reports no
  directory and the writers refuse, typed. The engine keeps its proof (`tests/src/core/CrashReporterTest.cpp`)
  and the **surface** is proven by `tests/control-crash-reporter.py`.
- **Safe-start mode is drivable, and there is no way to see, offer, accept or clear it from the interface —
  added 2026-09-15.** After a session that did not exit cleanly, the next launch writes a crash marker
  (`zene-safe-start.marker`, in the working directory, beside the crash reporter's) and loads the project with
  **third-party plugin instances skipped** — each one replaced by the engine's own `DummyPlugin`, the same
  substitute a missing plugin gets — because the thing that killed the last session is usually loaded during
  start-up. The state is readable through `--control-socket` with `safestart.get_state` (the marker, the
  acknowledgement, the crashed session's own record, this session's skipped instances, the directories the
  third-party classification treats as this build's own, and the offer), the offer is accepted with
  `safestart.acknowledge` (the NEXT launch loads the plugins; the acknowledgement is consumed by that launch),
  the marker is dropped now with `safestart.clear`, and the session-scoped half of the predicate is
  `safestart.set_skip` — but **nothing in `src/gui/` shows the mode, offers the normal start, accepts it or
  clears it**: the offer is printed on stderr by `main()` and held on the control surface, and there is no
  dialog, banner, menu item or toolbar button for any of it. All three writers are `irreversible` and each
  names its fallback (the acknowledgement's file, the report the crash reporter still holds, or loading the
  project again with `safestart.set_skip` off); `safestart.set_skip` is session-scoped process state and no
  `JournallingObject` checkpoint describes it. "Third-party" is a definition rather than a guess — a module
  file this build does not ship (see `safestart.get_state`'s `own_plugin_directories`) — the engine proofs are
 `tests/src/core/SafeStartTest.cpp`, which raises a real signal in a forked child before asserting the next
 launch, and `tests/src/core/SafeStartLoadPathTest.cpp`, which drives the real `Plugin::instantiate()`
 against a third-party module copy — and the surface half is the `safestart.*` group in the first of them.

## Telemetry and privacy

- **Telemetry is off unless you turn it on**, and the consent screen shows you the exact payload before you
  decide. The payload is built from a **closed allowlist of 24 fields** (platform and hardware summary, plugin
  counts, crash counters) and **cannot** carry a project name, a file path, a plugin name, an email address, an
  IP address or an installation ID — that is enforced in code and tested, and the bytes you preview are the
  bytes produced. It cannot be turned on by a default, and a distribution can build it out entirely.
  Verified in the tree: `docs/TELEMETRY-V1.md` is the implementing lane's report — the allowlist is
  **24 keys and closed** (§3), the mutator refuses anything outside it, the consent state defaults to all-false,
  and the preview renders the exact bytes produced. That report's §5 records the first defect the kill switch
  caught and is true of the moment it records; the configuration was broken again later by a merge and has
  since been repaired, and **the OFF configuration is now built and measured for this release**. The packager
  kill switch is `option(ZENE_TELEMETRY … ON)`
  (`CMakeLists.txt:140`), whose `OFF` compiles the client, its consent screen and its networking code out —
  **the whole client, so a debug-stripped binary carries no `telemetry` symbol and no `telemetry` string, and
  the `telemetry.*` commands are absent from the registry**. Measured both ways on this release:
  `-DZENE_TELEMETRY=OFF -DUSE_WERROR=ON` compiles and the suite passes 86/86, and against the ON build the
  `nm` telemetry symbols read **99 → 0**, the debug-stripped `strings` count **156 → 0**, and the registry
  **74 → 72 commands**, the diff being exactly `telemetry.consent` and `telemetry.status`. (An unstripped
  binary's only `telemetry` string hits are this build directory's own absolute path in the DWARF strings,
  which stripping removes — the raw count is not zero and is not claimed to be.) Verified in the tree:
  `docs/TELEMETRY-KILL-SWITCH.md` is the repair's report — both configurations measured, the ON object
  byte-identical and every binary section identical except 3 `.rodata` bytes (a Qt resource timestamp), ctest
  86/86, the render sha256 unchanged — and `tests/telemetry-off-build.sh` re-runs the
  OFF build and the two counts, so the switch cannot rot again in silence. Evidence:
  `tests/integration-logs-telemetry-off/`. The lane
  `post-alpha/telemetry` is an ancestor of this tip.
- **Telemetry v1 is inert: there is no server to send to yet.** The client is complete and refuses to open a
  connection; even switched on, **nothing leaves your machine**. That is stated plainly because a privacy
  control that appears to do nothing is worth less than one you can see working.

## The name, honestly

This release renames the product to Zene Studio: the application name, the packaging, the desktop entry and man
page, the configuration and project paths (migrated from the old ones, so your settings are adopted rather than
orphaned), the MIME types, the names other audio software sees us by, and the plugin logo.
Verified in the tree: the migration is real code, not the sentence's assumption —
`ConfigMigration::adoptConfigFile` and `adoptWorkingDir` (`src/core/ConfigManager.cpp`, defined in
`ConfigMigration` and called from `ConfigManager`'s constructor) rename the legacy config file and working
directory into the new names where that is possible, copy where it is not, and as a last resort keep reading the
legacy path so nothing is orphaned; they are exercised by `tests/src/core/ConfigMigrationTest.cpp` and declared
in `include/ConfigManager.h` (commit `6c1ff660c`,
lane `post-alpha/rename-complete`, an ancestor of this tip). **This sentence used to carry a marker saying it was
contradicted by the tree, and at the release-prep base it was:** at `34c1f4f86` there was no migration code,
`docs/WAVE-R-RENAME.md` §6 ("User state") recorded that `~/.lmmsrc.xml`, `~/Documents/lmms/` and the
`lmms-workspace` marker were deliberately left alone, and the honest phrasing was that 0.2.0 read the same files
0.1.0 did. The migration commit has since merged, so the sentence is true as written and the marker is deleted
rather than satisfied by keeping the stale half. One residue of the old state remains and is **stale**: the
same bullet in `docs/WAVE-R-RENAME.md` §6 still gives "renaming would orphan an existing install" as the reason
the paths were left alone. On this tree that reason no longer holds; **reported rather than silently harmonised**,
because that is the rename lane's record of its own finding and it needs a decision, not an over-write. (An
earlier version of this paragraph named the release notes' first headline for the same error; the notes' residue
list says user state is *migrated* rather than kept, so that half of the charge was stale and is removed.)

Two things deliberately keep the old name, and neither is an oversight:

- **The licence notices and the "derived from LMMS" attribution.** This is a derivative work under
  GPL-2.0-or-later; retaining upstream notices is a condition of the licence, and removing them would be
  misattribution, not a rename.
- **Internal code identifiers** — the `lmms::` namespace, `LMMS_*` macros and file names such as
  `lmmsconfig.h`. No user can see these, and renaming them is a large code-wide change rather than an identity
  change. Related and more important: the **plugin entry symbol is unchanged, so existing native plugins still
  load**. If that symbol is ever renamed it becomes a deliberate ABI break, and it will be announced as one.

**The identity artwork in this release is a placeholder, and we would rather say so than let you infer it.** An
audit found that **41 shipped identity images were still upstream LMMS artwork** — 39 of them byte-identical to
upstream, 2 identical in drawing data with only their metadata changed, and **not one had already been
replaced**. All 41 are now replaced with hand-authored placeholders (a plain note and neutral glyphs), drawn
from nothing and rasterised at the upstream files' exact pixel sizes through the product's own SVG engine, so
there is no licence or attribution obligation attached to them. They are labelled as placeholders in their own
metadata. **The scope is identity art, and the boundary is worth stating**: the theme's **UI icons** (arrows,
knobs, gear, speaker) and the **per-plugin artwork** are still upstream's drawings. They carry no Zene branding
and no product name — that is what makes them a different question from the logo — and they remain under the
upstream licence with its credit intact. What you are looking at is our identity, standing in for the mark the
owner has yet to choose. **The final mark is not in this release** — it is the owner's to choose, and the placeholder exists so
that the product stops shipping someone else's identity in the meantime. (The upstream artwork and its CC0
credit remain intact in the project's history, where they belong.)
Verified in the tree, including the runtime half the marker for this paragraph demanded: `docs/BRAND-PLACEHOLDERS.md`
§0/§1 hold the audit (41 files; `identical_same_path 24`, `identical_renamed 15`, `art_only 2`, `differs 0` — and
0 of the 39 byte-identical files had been replaced by anyone), §3c proves 0 of the 41 remain byte-identical to
upstream and that every raster keeps its upstream pixel dimensions, and §3a runs the rename lane's plugin-logo
resource test **in both directions** — the green run reports the placeholder resolving to a real **48×48** pixmap
(not the `1×1` fallback `PixmapLoader` returns on a miss), and the red control moves the file away and the
assertion fails. §3b's resource sweep — `python3 tests/brand-resource-sweep.py`, exit 0 — scans **993** call
sites, leaves 3 unresolved (all three pinned as pre-existing: `arp_down_on`, `arp_up_on`, `logo`), resolves
`zene-plugin-logo` to `data/themes/default/zene-plugin-logo.svg`, and reports **0 NEW** unresolved names. The
command is quoted and exits 0 on this tree, so the figures can be re-run; the earlier text here said 989, the
count before the control-surface merge added its call sites. The marking is in the artefacts
themselves: the SVGs carry `<dc:title>… (placeholder)`, `<dc:description>placeholder - pending the product mark`
and a `<dc:rights>` stating they contain no third-party artwork, and all 34 generated PNGs carry the
`Description` chunk. The lane `post-alpha/brand-placeholders` is an ancestor of this tip.
**The upstream artwork sentence that used to sit here is deleted, not softened** — it read "the plugin logo's
artwork is currently the upstream artwork, which is CC0-licensed and credited", which was true at the
release-prep base and is false at this one.

## The 0.3.0 verb wave: `clip.trim`, `clip.slip`, `note.probability_set`, `render.stems`

Four ids whose engines were already in the tree and whose command surface was not. All four
are **drivable through the socket, not from the interface**, and this section is the honest half.

- **UI absence — one line: `clip.trim` is drivable through the socket, not from the interface.**
  The song editor does have the gesture the command implements — the left-edge drag in
  `src/gui/clips/ClipView.cpp`, whose three-line rule (position, length and source offset move
  together) `clip.trim` reproduces exactly — but no menu item, action or keybinding reaches the
  *command*: the GUI path is the drag, an agent's path is the id. Nothing in `src/gui/` invokes
  `clip.trim`.
- **UI absence — one line: `clip.slip` is drivable through the socket, not from the interface.**
  There is no slip gesture anywhere in the product: a case-insensitive grep for `slip` over `src/`
  and `include/` returns seven hits and every one of them is a comment or a doc-string. The
  nearest existing concept is the comp take's `srcpos`, which is recorded and reported but
  deliberately **not applied** (`docs/COMPING.md`). `clip.slip` is the first implementation of the
  verb and it is socket-only.
- **UI absence — one line: `note.probability_set` is drivable through the socket, not from the
  interface.** `docs/MIDI-DEPTH.md` already states it — "Probability/velocity jitter is not exposed
  in the GUI editor (no drag handle, no right-click entry). The values are editable only by
  file/API today, so the feature is reachable by the render path and by tests, not yet by mouse" —
  and a grep for `probability` over `src/gui/` returns zero matches. This release adds the
  control-surface id and **no** UI control.
- **UI absence — one line: `render.stems` is drivable through the socket, not from the interface.**
  Stem export exists as the CLI subcommand `lmms exportstems` and as
  `RenderManager::exportStems()`. The File menu's **"Export Tracks..."** action is a *different,
  pre-existing* code path (`renderTracks()`, which trims each stem to its own track's length and
  does not align them); this id neither changes it nor is wired to it, and there is no
  "export stems" menu entry, dialog or action that reaches `render.stems`.

**Stated limits.**

- **`clip.trim` and `clip.slip` do not author the sample window.** `SampleClip`'s authored window
  (`srcin`/`srcout`) is written only when it is not the whole buffer, and is applied on load only
  `if (_this.hasAttribute("srcin") || _this.hasAttribute("srcout"))` — there is **no
  reset-on-absence** for it, so a Clip checkpoint captured before a *first* window edit could not
  take that edit back. Both verbs therefore write only attributes their clip type serialises
  **unconditionally** (`pos`, `len`, `off`, `autoresize`), and `setSampleWindow` is not reachable
  from this surface in 0.3.0; an agent that wants to move which part of a source plays uses
  `clip.slip`'s tick offset. A frame-domain trim is a later feature and is not claimed here.
- **`clip.trim` does not reduce a pattern clip's offset modulo the pattern length.** The song
  editor does that as a GUI overflow guard; the value this command sets is the value the model
  holds, and the engine moduluses at use time.
- **`render.stems` renders PER TRACK, post-fader, not per bus.** `docs/STEM-EXPORT.md`'s "No
  bus-level stems" states it: a "bus" is a `MixerChannel`, not a `Track`, and the render path
  isolates tracks by muting, so `exportStems` selects tracks. The tail is one bar past the project
  end by default (`stemTailBars`, settable through `tail_bars`), which is the whole-project
  render's own convention. Bus-level stems are not claimed.
- **`render.stems` carries the DECLARED BOUND every render-running command carries.** The export
  runs in a child process and blocks the dispatch thread on `waitForFinished(600000)` (worst case
  630 s with the 30 s start bound), so the control surface does not answer — `control.ping`
  included — until the export finishes. This is the **same** defect `docs/RENDER-CHILD-WAIT.md`
  records for `render.render` and the three bounce/freeze commands, and the deferred-reply fix that
  document designs is **not built in this release**. `render.stems` does not pretend to a timeout
  knob it does not have: the bound is stated in the command's own description and in its A16
  contract row, and the ctest that exercises it (`ControlStemExportVerb`) gives the call its own
  declared per-command budget rather than raising a socket timeout to hide the wait.
**Auto-mastering (wave 1) is drivable, and it does not rank anything.** `mastering.run` renders the open
session **once** and writes N measured candidates into a directory the caller names (`mastering.list_candidates`
publishes the set the candidates are generated from, `mastering.get_state` reads the last run back); it **does
not rank them and does not claim a best**, because no validated preference scorer exists for master variants of
one song. Candidate verdicts are against named, cited targets — EBU R 128 with its published ±0.5 LU, and a
−14 LUFS-I streaming **convention** with a tolerance this project chose and states. The run is a **child
process on a serialised copy of the session**, so the session is not modified and the running instance's audio
path is untouched — but that also means the candidate files themselves are the only artefact, `control.undo`
takes them back by **removing what the run created** and writing back the revisions the directory already held
(bounded at 64 MiB of pre-existing wav files; beyond that the run is **refused** rather than performed without
an inverse), and **there is no redo half**: `control.redo` cannot re-create a candidate set, only a re-issue of
`mastering.run` can. Renders in this tree are **not bit-reproducible** run to run, so two runs of the same
master are equal only to the meter's tolerance (≤ 0.05 LU / 0.01 dB), never byte for byte; within **one** run
all candidates branch off the same render, so their metrics ARE comparable with each other. **`wav` only**, no
per-candidate parallelism, no reference-matching arm, **no level-matched A/B** (the candidates differ in
loudness by design, which is the target axis) and **no pick-log** — which is exactly why the learned ranker
(wave 3) is not here: it is gated on real user pick-logs, which do not exist yet. Likewise the engine is
drivable through the socket and **nothing in the interface masters anything**: there is no Export-dialog
mastering mode, no candidate list panel and no A/B player.

**Note randomisation, note transforms, slide notes, the `scale.*` group and `device.mpe_set` are drivable
through the socket and absent from the interface** (board task #648; feature-list rows 11, 66 and 81). One
line each, because the scope contract asks for one each:

- **Note randomisation, transforms and slide notes are drivable through the socket, not from the interface.**
  `note.randomize` (the seeded roll), `note.random_seed_get` / `note.random_seed_set` (the project's MIDI
  seed), `note.transpose` / `note.velocity_offset` / `note.velocity_scale` and `note.slide_set` /
  `note.slide_clear` have no action, menu entry, shortcut or view: the piano roll's velocity edits are per
  note and per drag, its transpose is an interactive drag, and there is no slide-note action and no marker
  for one — a slide note sounds like a portamento and looks like any other note. The seed is persisted in the
  project header (`midiseed`) and **no interface shows or edits it**.
- **`note.randomize` rolls on top of what is already there.** The velocity roll is multiplicative on the
  note's current velocity and the position roll is drawn from the note's identity at entry, so applying the
  command twice is not a no-op and the inverse is the clip's checkpoint, never a re-run.
- **`note.random_seed_set` accepts 0..2147483647.** The engine's seed is a `uint32_t` and the schema subset's
  integer is signed, so a project carrying a larger seed can be **read** exactly (`note.random_seed_get`
  reports a number) but not re-set to that value through this surface.
- **The scale group's context is not the piano roll's key/scale selector, in either direction.**
  `scale.list` / `scale.get_state` read the engine's own vocabulary (`ChordTable`) and the group's own
  context; `scale.root_set` / `scale.set` write that context, which is **process state and deliberately not
  serialized** (the `MpeExpression::isEnabled()` precedent: a project never changes meaning because of a
  control-surface setting); `scale.snap_notes` is the one verb that edits a clip. The piano roll's key and
  scale combo boxes are unchanged and are neither read nor written by any of these ids, so an interface-only
  user can neither see nor set what the group resolves against. `scale.snap_notes` **refuses** rather than
  guessing a scale when the context holds none.
- **Scale-aware root-note highlighting is deferred to the interface phase — it is not in 0.3.0** (feature row
  65, recorded 2026-09-15). Its deliverable is the interface's: the **root note drawn distinctly** from the
  other in-scale degrees, and that root colour exposed as a **theme value** (`BACKLOG` OWNER-31 item 6;
  `ui-research/UI-DIRECTION-RECONCILED.md` item 5). What exists is a single-colour highlight — one draw loop,
  `src/gui/editors/PianoRoll.cpp:3659-3670`, painting every marked semitone with `m_markedSemitoneColor`
  (themed by `qproperty-markedSemitoneColor` in `data/themes/*/style.css`) — over the piano-roll **window's**
  own state (`m_keyModel` / `m_scaleModel` / `m_markedSemiTones`, written by `PianoRollWindow::saveSettings`),
  so none of it is drivable or observable through the socket. No engine half is owed by this row either: the
  socket-side scale and root-note facts are the `scale.*` group's (row 66, already in the tree), and the
  scale-aware edit operation `docs/MIDI-DEPTH.md` §1.1 asked for is that group's `scale.snap_notes`. The
  decision is also stated in `docs/FEATURE-LIST-0.3.0.md` row 65 and in `docs/RELEASE-NOTES-v0.3.0-alpha.md`.
- **`device.mpe_set` is drivable through the socket, not from the interface.** No checkbox, menu entry or
  setting reaches the MPE input switch and none shows its state; the per-note expression editor
  `docs/MPE.md` names is still absent (this page's MPE entry above stands). What it gates, exactly: while it
  is off the MIDI input path is what it was before MPE existed; while it is on, a bend / pressure / CC74 on a
  note's own member channel is that note's expression instead of a channel-wide bend. **All three axes reach
  playback** (pitch as a frequency ratio, pressure and timbre as MIDI events on the note's own member channel,
  task #649); pressure and timbre no longer stay stored-only, and the measured proof is the registered ctest
  `MpePlaybackTest` (one block with the expression against the same block without it) against the in-tree test
  instrument `tests/src/plugins/MpeTestConsumer.cpp` — no built-in synthesiser consumes either axis, so the
  fixture is the vehicle. Switching the flag off does **not** clear
  expression already stored on notes — `note.expression_clear` is the verb for that. The master channel and
  the bend range are per-MIDI-stream **instance** settings with no object the control surface can reach, so
  `device.mpe_get_state` reports the engine's defaults rather than writing a copy nothing reads.
## Offline stem separation (`stem.*`, feature row 26) — socket-only, opt-in, and it needs the model

The engine (HTDemucs over ONNX Runtime, one job at a time) was already in the tree with five registered
tests; what was missing was any way to drive it. Seven ids now do — `stem.get_state`, `stem.job_start`,
`stem.job_status`, `stem.job_result`, `stem.job_cancel`, `stem.model_get_state`, `stem.model_download` —
and this is the honest half.

- **UI absence — one line: the whole `stem.*` group is drivable through the socket, not from the
  interface.** The only user-visible gesture that reaches this engine is the sample clip's **"Split to
  stems"** context action, a *different, pre-existing* path (`src/gui/clips/SampleClipView.cpp:124` →
  `StemSplitController::splitClipToStems`): it takes its mix from the clip a human selected and exists
  only when a display does. Nothing in the interface shows an agent's job, its progress or its error,
  nothing lists or fetches models, and no menu item, toolbar button or keybinding reaches a `stem.*` id.
- **The feature is OFF in the default release configuration.** `WANT_STEM_SPLIT` defaults to OFF
  (`CMakeLists.txt:120`), so a default build compiles none of this engine and registers none of these
  ids — poll `control.commands` and they are simply not there, which is the truth rather than a bug. The
  A16 rows are guarded by the same macro, so the contract table and the registry cannot disagree. This
  is the same shape as the `telemetry.*` / `session.*` / `wasm.*` rows above.
- **It needs the model present, and models are never bundled.** A build with the option ON can still
  refuse every job: the default spec is deliberately unpinned in v1 (no URL, no SHA-256, no size), so
  `stem.model_download` refuses to fetch it and names the model card instead; `stem.get_state` reports
  the path it looked in, whether the file is there, and the reason when it is not. Place the file by
  hand (or pin a spec with `url` + `sha256` + `size_bytes`) and the group works.
- **No live mode is claimed, and none exists.** HTDemucs is a hybrid transformer that needs the whole
  7.8 s segment (343980 frames at 44100 Hz) as context, so no chunk size fits an audio buffer: this is an
  **offline job only**. `stem.get_state` reports `realtime: false` and the lookahead; the group offers no
  monitoring, no streaming and no realtime variant.
- **44100 Hz only.** The input must already be at the model's own rate — there is no resampler
  (SPEC-stem-split.md OQ-1) — and the refusal names the rate it got. A file `render.render` wrote is
  already in that format, which is why "bounce the session, then split the bounce" is the composable flow.
- **The source is a file, not a clip or a bus.** The socket has no way to hand the engine a clip's audio
  buffer; `stem.job_start` names an absolute path, and the mix is decoded from it.
- **`stem.job_result` writes files, not tracks.** It produces `<stem>.wav` (drums, bass, other, vocals)
  as float32 RIFF/WAVE with a sha256 per file, and it does **not** materialise `SampleTrack`s: the GUI's
  `StemTrackBuilder` gesture is not reachable from the socket in 0.3.0.
- **Jobs are not project state.** They live in the instance's memory: a reload or a restart loses the id,
  its progress and its stems, and `control.transactions` shows no record because the whole group is
  `not_mutating` — a job is not a document and a written stem is an output.
- **`stem.model_download` carries a DECLARED BOUND.** A performing transfer (an explicitly pinned spec)
  runs on the control surface's own thread, so the surface does not answer — `control.ping` included —
  until it finishes or fails. This is the **same** defect `docs/RENDER-CHILD-WAIT.md:120-126` records for
  `render.render` and the bounce/freeze commands, and the deferred-reply fix that document designs is
  **not built in this release**. The refusal path (`download_allowed: false`) is all the default build
  ever reaches. The transfer's performing path is **not exercised by any registered proof**: CI has no
  pinned artefact to fetch. The proof that IS registered is the ctest `ControlStemCommands`
  (`tests/control-stem-commands.py`), which drives the real pipeline over `--control-socket` on the
  committed stub ONNX graph, and which **skips** (exit 77) on a host with no python onnxruntime rather
  than passing.
**Loudness metering is drivable and there is no meter in the interface — the socket is the only way to watch
or measure a level.** `meter.get_state` reads the PASSIVE tap on the live master (gated integrated loudness,
momentary, short-term, the loudest short-term window and true peak, fed one period per rendered period out of
`AudioEngine::renderStageMix()`), `meter.arm` arms or disarms it (arming starts a fresh measurement; a
disarmed tap is one relaxed atomic load per audio period and measures nothing), and `meter.measure_file`
measures a **rendered file** with the same BS.1770-4 meter the render path uses, with the EBU R 128 verdict.
The render path's own report — the `.loudness.txt` sidecar — is reachable as well now: `export.get_settings`
exposes `loudness_report` and `export.set_loudness_report` turns it on for the next render. What is **absent
from the interface**: there is **no loudness meter widget, no LUFS/true-peak readout, no meter bridge and no
loudness column** — `grep -rniI 'lufs\|loudness' src/gui/` finds only the export dialog's existing report
checkbox and its result label — so nothing in this release shows a level while the transport runs. Stated
limits: the live tap measures the **master mix only** (no per-track or per-bus loudness, no R128 momentary
history graph and no loudness range / LRA); the meter is **stereo** in the application (`DEFAULT_CHANNELS`),
while `meter.measure_file` accepts 1 to 6 channels and refuses more, typed; a reading is JSON `null` — never a
plausible number — while the meter has no measurement (silence, or a window that has not filled), and the
**EBU R 128 target (−23.0 LUFS-I ±0.5 LU, −1.0 dBTP) is the only thing graded** (the −14 LUFS-I streaming
figure is carried as an informative convention); and `meter.measure_file` measures a file **this** instance can
open — it does not fetch, decode or render one. `docs/METER-SURFACE.md` is the feature's own record.
## Linked / smart clips: two clips, one source (`clip.link_*`, feature row 6)

`clip.link_create` / `clip.link_remove` / `clip.link_get_state` / `clip.link_sync` are
**drivable through the socket, not from the interface**: a link group is created, inspected, repaired and
broken only through those four ids, and nothing in `src/gui/` draws a link badge, offers "edit shared
source", or colours a member of a group (`grep -rn "linkId\|clip.link_" src/gui/` returns nothing).
**UI absence — one line: content edits propagate and placement edits do not.** What a link shares is the
clip's note list, so `note.add` / `note.remove` / `note.move` / `note.resize` / `note.velocity_set` on ANY
member are seen by every member of the group (and `clip.link_sync` forces the group to agree when an edit
did not pass through those verbs — a piano-roll in-place gesture, a hand-edited project file); what it does
NOT share is where and how each member plays that content, so `clip.move`, `clip.resize`, `clip.trim`,
`clip.slip`, `clip.set_fade`, `clip.set_gain`, `clip.crossfade`, mute/solo, a member's name or colour and
its take lane are per-member and never propagate. **An audio clip cannot be a member**: the group's content
channel is a note list, so `clip.link_create` refuses a `SampleClip` typed (`invalid_args`) rather than
creating a group of one that would look linked and share nothing. The design decision (a persisted group id
plus a write-through mirror — not a shared content object, not copy-on-write), the edit-kind table and the
bounds are `docs/LINKED-CLIPS.md`; the proof is the registered ctest **ClipLinkTest** (the propagation, the
unlink, the A16 rows, and a save/reload round trip that shows the link is still there and still propagates
after `loadProject`).
**CODE-6/CODE-7/CODE-8 — the change-plan code rows: what each of them does NOT do.** Four absences,
each one line, because each is a bound rather than a bug:
- **The Lua memory budget has no interface.** `script.set_memory_budget` sets the cap and
  `script.run` reports what a run measured against it; **no dialog shows live Lua bytes and nothing in
  the window lets a user set a budget**. The default is 64 MiB
  (`ScriptEngine::DefaultMemoryBudgetBytes`), the surface accepts `[512 KiB, 1 GiB]` and refuses
  anything outside that rather than clamping it, and the budget bounds **Lua memory** — what the script
  asks the Lua allocator for — **not** the C++ heap the bindings allocate, and not the process's total
  footprint. A run already executing keeps the budget it opened its state with, so raising the cap does
  not rescue a script that is mid-allocation.
- **The telemetry transport's policy has no interface.** The consent screen describes **what** would be
  sent; it does not show the endpoint, the scheme or whether this build would post to it. That verdict
  is reachable only through `telemetry.status` (`transport_policy`, `transport_endpoint_allowed`,
  `transport_endpoint_reason`, `transport_blocking`). And the transport is **not wired to any code path
  that sends** — the endpoint ships empty in this release, no ingest service exists, so an
  https-only, non-blocking transport still delivers nothing. A reply that never arrives is an attempt
  that is never counted as a send: `send()` returning true means "queued", not "delivered".
- **The control-server shutdown hook has no interface.** It is process machinery on the exit path:
  nothing in the window shows it, no command reports it, and `runShutdownHooks()` is neither
  thread-safe nor meant to be (it is the UI thread's exit path). It is a process-lifetime store, so
  **nothing about it survives a `SIGKILL`** — the socket file is removed by the hook, by
  `ControlServer::~ControlServer()`, or by `main()`'s own `close()` on the normal route, and a process
  that dies without running any of them leaves the file for the next start's stale-socket handling
  (`docs/CONTROL-SOCKET-PATH-SAFETY.md`).
- **There is no `telemetry.consent_set`.** Row 85 of the 0.3.0 feature list is closed by a recorded
  decision, not by a command (`docs/TELEMETRY-V1.md` §2.6): `telemetry.consent` is the consent verb,
  it declares `requires: display, human` so no automated caller can consent, and a second
  agent-reachable consent setter would be a command that turns telemetry on on the user's behalf —
  which the feature's design forbids. Nothing was built, so there is nothing in the interface, and the
  decision is pinned by `ControlRegistryTest::noAutomatedCallerCanReachAConsentVerb` rather than by
  this sentence.

**The recording engine surface (0.3.0) — three absences, one line each.** `record.input_set` takes effect on
the **next start**: the backend opens its capture device at startup and each record route's selectable input
range is fixed when the engine is built, so the command reports `restart_required: true` instead of pretending
to swap a live device (the route COUNT, `MultiTrackRecorder::MaxRoutes`, never changes either). A retrospective
AUDIO take is **written to a file and never inserted into the session** — the same absence
`record.recovery_restore` states for a recovered take, for the same reason. And **the real-interface half is
hardware-bound and unverified on this box**: whether a sound card opens, how many channels it grants and
whether it delivers frames are *this machine's* answers and not properties of the feature, which is why they
are reported rather than assumed — `record.input_get_state` carries `capture_capable`, `capture_open`,
`capture_reason`, the granted channel count and rate, and the `bus_frames` / `wide_frames` /
`input_frames_staged` counters, and every one of them is **0 or false** on a build with no capture device.
**Pitch-preserving time-stretch is drivable, and it is not formant-preserving.** A warped clip renders
its rate change through a WSOLA stretcher instead of the resampler when it asks for it
(`warp.stretch` with `mode="preserve_pitch"`, or the `stretch="wsola"` attribute of the clip's own
`<warp>` element), and the pitch is measured where it was: on a 2x warp of a 440 Hz + 660 Hz source the
stretcher renders 0.4992 / 0.2990 of the two tones where the default resampling mode renders all of it
an octave up at 880 / 1320 Hz (`docs/PITCH-STRETCH.md`). Stated limits, all measured or structural:
**formants are not preserved** (WSOLA keeps the waveform's period, not a vowel's spectral envelope, so a
large stretch of a voice moves the formants *with* the pitch — pitch-preserving is not
formant-preserving); the alignment search reaches ±128 source frames (2.9 ms at 44.1 kHz), so content
**below ~345 Hz** cannot be aligned and is stretched as plain overlap-add; **transients are not
detected** (a grain straddling an onset smears it over up to one grain, 23 ms); the sample-rate
conversion on this path is **linear interpolation** between source frames — the engine's default
`Linear` class — so the export's `SincBest` quality does **not** reach it; **one rate per audio period**
is inherited from `Sample::play`, so a rate *change* takes effect at the next period boundary
(≤ 23 ms) while a marker pair covering a whole clip is exact everywhere; and the stretcher's parameters
(the grain length and the search radius — the quality/complexity dial, 17x realtime at the default
58.7 ms per second of stretched audio) are **not exposed** on the control surface. A clip that renders
linearly is never routed through the stretcher and `warp.stretch` **refuses** `preserve_pitch` for it;
a project with no warp, or with warps in the default mode, renders exactly as it did before this
feature. **UI absence — one line: the stretch mode is settable through the socket, not from the
interface** — `grep -rniI 'WarpStretchMode\|preserve_pitch\|warpStretch\|AudioStretcher' src/gui/`
returns **0** hits, there is no clip-context entry, no checkbox and no marker-drag gesture for it, and a
project file or `warp.stretch` are the only two ways to author it.

- **Structural undo is drivable and undoable through the socket, and the interface has no history panel —
  added 2026-09-15.** `track.add`, `track.remove`, `track.move`, `plugin.load` and `plugin.unload` are journalled
  as ONE stack (`ProjectJournal`, the same one the GUI's Ctrl+Z unwinds), and one `control.undo` restores each of
  them. A **deleted track comes back WITH its clips and their notes**, at the index it was removed from, because
  the inverse is captured **before** the delete: `~Track` destroys the clips and only then calls
  `TrackContainer::removeTrack`, so a checkpoint taken at the container restores a track with an **empty clip
  list**. Three limits, stated rather than discoverable: (1) `clip-<n>` is an **index-derived** ordinal of the
  clip in the whole song's arrangement order (feature row 51, unchanged by this work), so deleting a track SHIFTS
  every later clip's id and the undo shifts them back — a client must **re-read** `arrangement.get_state` after an
  undo rather than cache a clip id across one; the `trk-<n>` of the restored track **does** survive, because it is
  persisted on the track element and `Track::loadTrack` takes it back; (2) a track or device whose captured
  document is over the 64 KiB cap records **no** inverse and says so in its transaction — a truncated capture is a
  corrupt restore, so it is refused instead; (3) the captured document's size is charged to the undo stack's byte
  budget, so a long run of structural deletes is evicted like any other step. **`track.move` is new** (the
  arrangement's order had no command at all — the reorder existed only as a drag) and it is refused, not clamped,
  for an index outside the song. In the interface: the track ✕ button and a track drag record the **same** step the
  commands do (`control::journalTrackRemoval` / `TrackContainer::moveTrack`), so those two gestures are undoable
  with Ctrl+Z — but there is still **no undo-history panel**, nothing lists the structural steps, and there is no
  control that names or limits the capture bound.

**The chord track, its detection and its generators are drivable, and nothing in the interface
shows or plays them.** `chord.*` is the only way to reach any of it: there is no chord lane, no
chord ruler, no chord-edit popover and no generator panel in 0.3.0, and `grep -rniI
'ChordTrack\|chord-track' src/gui/` returns **0** hits. What IS there is the entity (a chord
track persisted as one `<chord-track>` element inside `<song>`, written only when it holds a
chord), the detection (what a clip's notes spell, against the vocabulary the piano roll's own
chord and scale selectors read — no second scale table exists in this fork) and two generators
that write notes into a clip. The bounds, stated rather than implied: the track holds at most 64
events; detection is from NOTES that sound together (not from audio, not over time — the key is
one estimate for the whole note list, and a slice whose nearest vocabulary entry is not an exact
match is reported with `exact: false` and the tones it misses and adds rather than rounded to a
name); the generator's seed only decides the VOICINGS, the timing nudges and the velocity nudges
that `variation` opens — with `variation` 0 the take is the progression itself and the seed
decides nothing; and a chord track does not sound on its own (it is harmony written down, and
`chord.track_write` is what turns it into notes). There is no roman-numeral analysis of arbitrary
chord sequences and no chord detection from audio in this release.

**Project assets can be inspected, hashed and relinked — they cannot be bundled (feature row 38).**
`project.missing_assets` lists every file a project file references (sample clips, AudioFileProcessor
and SF2 instruments, session-view audio slots) and which of those have nothing on disk at them;
`project.hash_assets` adds a sha256 and a size per reference that is on disk, plus one digest over the
reference set; `project.relink` points the references the caller names at the file that was found,
refusing unless that file hashes to an `expect_sha256` the caller names. All three work on **one
project FILE**, so they answer for a project that is not open and for one that cannot be loaded, and
`project.relink` is reversible through a recorded action checkpoint that restores the file's previous
bytes. The **portable-bundle half of the row is OUT (Bar 3)**: nothing copies media beside a project,
nothing rewrites a project into a self-contained bundle, and no verb in the group writes any file
except the one project file `project.relink` was given. Also absent: a **collection-wide sweep** (each
call names one project file — the collection is scanned one project at a time), **plugin and preset
paths as assets** (the scan covers the elements the project format carries a media path in; a plugin
binary or a preset file is not a reference it reports), and a **committed `--control-socket`
transcript** — the group's registered proof is the in-process ctest
`ControlProjectArchiveTest`, which drives the same three ids through `ControlRegistry::invoke` and
carries the negative control (an intact project reports no missing assets). Nothing in `src/gui/` lists
a project's references, offers a relink or reports a hash.

## The plugin hosts chunk, and the WASM pool renders offline — what is still not there (CODE-4, CODE-5)

**Chunking the plugin host paths has no interface.** `plugin.host_chunking` (read-only) reports the contract and
the live counters over `--control-socket`; **nothing in the interface shows them**, there is no per-device panel
and no per-device breakdown at all — the counters are process-wide, they are never reset except by exiting the
process, and they are read as a snapshot (the audio path increments them, the command reads them, so a request
in flight may or may not be in a given read). What the rule covers is the **host**: the frames the host hands the
plug-in and the buffers it hands them in. A plug-in that writes outside the frames it was asked for is still its
own bug and this release does not defend against it.

The proof is a **host-level unit test with an in-tree MIT fixture** (`Vst3ChunkProbeTest`, and
`ClapHostTest::testChunkedProcessing` against the CLAP fixture), because **no third-party VST3 or CLAP plug-in
can be installed on the build machine**: the chunk sequence, the "no call larger than the declared block" witness
and the audio equality between two prepared block sizes are all measured against fixtures this repository owns,
never against a real third-party plug-in. The prepared block size comes from the engine's period at the moment
the device is loaded, so a buffer-size change mid-session still reaches the host through the device's own
re-prepare path rather than through this feature.

**The WASM worker pool is bounded and its wake-up is a futex.** Lanes are `min(cores - 1, 8)` and the number is
fixed the first time the pool is used — there is **no configuration knob** and no per-module lane allocation.
A `submit()` from the audio thread takes **one futex wake when every lane is parked** (`wasm.pool`'s `wakeups`);
while a lane is awake it takes none (`wakes_suppressed`). That is a deliberate, measured trade and not a
lock-free guarantee: the queue hand-off itself is lock free, the wake is not.

**The offline render is not the live streaming path.** `wasm.render_offline` renders one block in flight on a
fresh worker, which is what makes the repeatability verdict meaningful; **live playback keeps the weaker
contract** and can still drop blocks when no slot or queue entry is free (`dropped_blocks` is reported per run,
and a run with a non-zero value is not a render to trust). The verdict is **within this build's own measured
run-to-run floor** — it is **not** bit-identity, **not** a promise across builds, machines or optimisation
levels, and it says nothing about a module that is itself non-deterministic (a module that seeds noise from the
clock can push the floor up and still be "deterministic within its own floor"; the honest reading is that the
floor, not the verdict, is what that case tells you). And the sandbox is still **not in any device's audio
path**: `wasm.render_offline` and the rest of the `wasm.*` group run the module in a sandbox of their own and
produce no audio the user hears — the limit `docs/WASM-EFFECT-ABI.md` section 13 records is unchanged.

**Stable ids — slice 2 (feature row 51).** `clip-<n>`, `note-<n>`, `ch-<n>` and `fx-<n>` are persistent across save/open, `dev-<n>` is a catalogue selector and is intentionally not in the project file. **Stable id inspection is drivable through the socket, not from the interface**: there is no id column in the track list, the clip list, the piano roll, the mixer or the rack; `control.id_contract` is the only way to read the contract and the counts.

**mmpz-git depth is drivable through the socket, not from the interface.** The merge driver, semantic diff, conflict reporter and audible-diff CLI are wrapped as `project.merge`, `project.diff`, `project.conflicts` and `project.audible_diff` on the control surface, but nothing in the GUI reaches them. The audible-diff command requires the built binary as its renderer; the merge driver operates on project files, not the running session.

## MIDI controller surfaces — soft-takeover, LED/feedback and mapping templates (feature row 19, board task #651)

**The controller surface is drivable through the socket, not from the interface.** There is no
soft-takeover toggle, no feedback switch and no template menu: `grep -rniI
'ControllerSurface\|softtakeover\|controller\.template_' src/gui/` returns **0** hits, and the
`controller.*` group (`controller.surface_state`, `controller.soft_takeover`, `controller.feedback`,
`controller.template_save`, `controller.template_list`, `controller.template_apply`,
`controller.template_delete`) is the only way to reach any of it.

What is bounded, stated rather than implied:

- **No hardware was attached when this was written, and none is required to run the proof.** Every
  claim in `ControllerSurfaceTest` is made by feeding a synthetic control-change through
  `MidiLearn::handleMidiEvent` / `MidiPort::processInEvent`, the same event the MIDI clients deliver.
  The LED half's strongest in-process claim is **a measured write to the output client**: the port's
  own counter (`MidiPort::outputEventsWritten()`) is read where `m_midiClient->processOutEvent()` is
  called. That is a write reaching the client, **not** a proof that a lamp lit — with the dummy client
  `sendByte()` is a no-op, and nothing here has been run against a controller that could confirm it.
- **OSC is out** — this is the MIDI controller surface only.
- **No motorised-fader return path is proved.** `controller.feedback` writes the value the project
  holds; whether a given device acts on it (moves a fader rather than lighting a ring) is a property of
  the device, and is unverified here.
- **Feedback writes are filtered by the port's output channel**, like every other out-event
  (`MidiPort::processOutEvent`). Enabling feedback points the output channel at the channel the control
  transmits on, which is what makes the write pass; a project that later sets a different output
  channel will filter it out again.
- **Soft-takeover's take-over point is the value the model holds** when it is enabled (or the explicit
  `target`). It is a scalar outside every journal checkpoint, so `control.undo` does not reverse it:
  the recorded inverse command does.
- **A mapping template is files outside the project** (`<userConfig>/controller-templates/<name>.json`).
  It is not carried in the project file, it is not shared by saving a project, and deleting one has no
  undo. A binding whose target model is absent is skipped and reported, never invented.

## The control socket on Windows is a named pipe, and that half's verdict is CI-only evidence (CODE-9, feature row 83)

The agent control surface (`--control-socket`) is the **same surface** on Windows: the same
line-delimited JSON-RPC framing (one request line in, one response line out), the same command ids,
the same typed refusal shapes, the same `control socket listening on <path>` start line. What differs
is the kernel object underneath. On POSIX it is an AF_UNIX socket, mode 0600, unlinked on exit. On
Windows it is a **named pipe** (`\\.\pipe\<name>`) created with `PIPE_REJECT_REMOTE_CLIENTS`, so a
client cannot reach it over `\\<host>\pipe\...` — the one route by which a named pipe could ever be
reached from off the machine (SPEC A12 / AGENT-TOOLING.md #9.1). Windows has **no `chmod` and no
inode here**: the pipe carries the process's default DACL (the creating user and local
administrators) rather than an explicitly owner-only descriptor, and the POSIX rule "unlink only the
socket THIS instance bound" has no counterpart — the pipe name simply ceases to exist with the
process, so a stale name can never be bound by mistake.

Two bounds, stated rather than implied. **One thread per accepted connection**, and a finished
thread's entry is retained until the listener closes: connections are served and closed promptly, but
an instance that accepts hundreds of connections in one session accumulates that many
joined-at-close thread entries (bounded by the number of connections, never by their size or
duration). If a client stops reading a reply, that connection's thread blocks on the pipe buffer
until the instance closes — the POSIX path queues the tail and keeps its event loop free instead.

**This half's verdict is CI-only evidence.** The lane that built it had no Windows toolchain: the
transport is compiled and exercised by the `msvc-x64` CI job's `ControlNamedPipeSmoke` ctest (which
starts the real binary on a pipe, drives the wire and checks the framing, the ids, the refusal shapes,
the 1 MiB line cap and the shutdown) and by nothing on a Linux box. What *is* proven locally is that
the POSIX transport is **untouched**: the three control translation units preprocess token-for-token
to the `release/0.3.0` revision, and the Windows TU preprocesses to nothing on POSIX. Both proofs and
their exact commands are in `docs/CONTROL-NAMED-PIPE.md`.

**UI absence — one line: the Windows transport is drivable through the socket, not from the
interface.** There is no pipe-name field, no control-surface page and no Windows-specific menu entry;
`grep -rniI 'control-socket\|ControlServer\|controlSocket' src/gui/` returns **2** hits, both
comments about *unattended* runs (`GuiApplication.cpp`, `MainWindow.cpp`) and neither a widget, a
dialog nor a menu entry.

**The Lua API's DAW-control binding is socket-and-file scripted, never a panel — added 2026-09-15
(feature row 50, task #674).** `zene.mixer()`, `Mixer`, `MixerChannel`, `EffectChain` and `Effect`
make the mixer, a channel's gain/mute/solo, its effect chain and a device's parameters drivable from
a Lua script (they are the same objects and the same `ch-<n>` / `fx-<n>` ids the `mixer.*` /
`plugin.*` command groups address), and `zene.apiSurface()` reports the API version and the live
surface. There is **no interface** for any of it: no Lua console pane, no script editor and no
widget that shows what a script changed (`grep -rniI 'luaConsole\|LuaConsole\|scriptEditor' src/
include/` returns 0 hits); the only GUI path is the pre-existing File > Run Lua Script file dialog
(`src/gui/MainWindow.cpp:942`), which runs a whole file and reports nothing back. Withheld on
purpose, one line each: channel **pan** (a `MixerChannel` has no pan control in this tree, which is
why `mixer.set_pan` refuses), channel/effect **removal** (no inverse exists for a deleted channel;
that stays the socket's `irreversible` `mixer.remove_channel`), **sends** (read-only — routing is
`mixer.route_*`'s job), **PDC** (not bound: derived per chain, reported by `dsp.get_state`),
**plugins beyond a chain** (no scan, no preset publishing from Lua), **automation clips and
controllers** (other groups own those objects), **settings** (only `script.set_memory_budget`), and
a channel **rename is journalless** (`MixerChannel::m_name` is a plain `QString`; no inverse is
claimed for it). The proof is the registered ctest `ScriptDawBindingTest`, which drives a real
mixer channel and the socket-addressable chain from Lua and reads the engine back.

- **The revision timeline is drivable, and it is not a semantic diff — added 2026-09-15.** A project's
  revisions are listed with their source and their UTC time by `revisions.list` (group `revisions`,
  feature-list row 76 / OWNER-31 item 30): the **keep-3 rotation** `project.save` performs
  (`<file>.rev0` .. `<file>.rev2`, 8 MiB each), the **`<file>.bak`** a save from the interface leaves,
  the **autosave** (`recover.mmp`, and `recover.mmp.bak` when it replaced one, each with its `.info`
  sidecar's recorded time) and the project's own **git history** where it lives in a repository (one
  bounded `git log`, 2500 ms, skipped entirely when no `git` is on the machine — the `git` object in the
  reply says which). **No new store and no format change**: every entry is an artefact the engine
  already writes. Stated limits, all structural or measured: **`revisions.compare` is a STRUCTURAL
  comparison** — each document's element count per tag and the tags that differ — and it is **not a
  semantic diff**; the musical diff of two project documents stays `tools/mmpz-git`'s (`mmpz-git diff`),
  outside this process; **a git entry carries no `sha256`** (hashing every listed commit would be one
  child process per entry — the commit sha is its identity) while its `bytes` IS measured, with one
  `git cat-file --batch-check` for the whole list; **`revisions.compare`'s `identical` is a byte
  comparison of the two artefacts**, so a `.mmpz` revision and the equivalent `.mmp` document are
  not "identical" while their element counts are the same (a `.mmpz` container is decompressed
  before it is counted, and reported unreadable only when it cannot be read at all); **`revisions.restore` does not reload the
  session** — it restores the FILE, and `project.open` is how a caller works on the restored bytes
  (the same sentence `project.restore_revision` carries); the restore is reversible only through the
  keep-3 set, so a live file over the policy's 8 MiB per-revision cap is **refused, typed, before
  anything is written** rather than restored without an inverse; and **`mmpz` documents are compared
  after `qUncompress`**, so a document over 64 MiB decompressed is reported unreadable rather than
  counted. Proof: the registered ctest `RevisionTimelineTest` (real documents, a real `.bak`/`.rev0`/
  autosave fixture, a real repository where git exists, all three ids driven through the registry).
  **UI absence — one line: the revision timeline is drivable through the socket, not from the
  interface** — `grep -rniI 'RevisionTimeline\|revisions\.list\|revisions\.restore\|RevisionEntry'
  src/gui/` returns **0** hits: there is no revision panel, no timeline strip and no "restore this
  revision" entry, and `revisions.list` is where a caller finds out what a project has.

**Import detection (`detect.*`) suggests a tempo and a key, and it is drivable only through the socket.**
`detect.analyze` reads one audio file and reports a tempo (BPM), the first transient it carries and a key;
`detect.apply` writes them into the project's own fields — the tempo as ONE integer-BPM tempo-map event at tick 0
with the map switched on, the key into the project's `<detected-key>` element under a name the pre-existing
ChordTable scale vocabulary already answers to. **Its accuracy is measured only on synthesised input with a known
answer** (a click track at 128 BPM and one at 90 BPM — 128.131 and 89.878 BPM measured; an A major scale over an
A bass — tonic A, scale Major, correlation 0.839, margin 0.077): **real-world detection accuracy is unverified on this box**,
no real-music corpus was analysed, and the confidence numbers the commands return are the detector's own scores
(the envelope's periodicity at the chosen lag; a rank margin for the key) — **not probabilities, and no accuracy
figure for real music is quoted anywhere in this release.** The bounds, stated because each one is a way the
answer can be wrong rather than a detail: the tempo band is 40..240 BPM and the choice of lag carries a
120 BPM-centred log-Gaussian prior, so a metrical relative can still be reported and the prior itself biases
toward the centre; the tempo map holds an INTEGER bpm, so an applied tempo is rounded to the nearest whole BPM
(the exact estimate is reported beside it) and a tempo outside the map's own 10..999 is refused rather than
clamped; at most 60 s (300 s hard maximum) from the START of the file is analysed; the analysis is mono; the
chroma is 12-tone equal-tempered by construction, so tuning, microtonality and other temperaments are outside it;
the scale vocabulary is exactly the ChordTable's own `isScale()` set deduplicated by mask (the first name the
table holds for a mask wins — `Aeolian` before `Minor`), and a detected key with no name in that table is
REFUSED rather than written. **Nothing in the interface detects, suggests or applies anything**: there is no
import hook that runs a detection, no suggestion panel and no accept button — the socket is the surface
(`grep -rniI 'detect\.' src/gui/` finds no call site of these commands) — and the piano roll's OWN key/scale
combo is **not** moved by this feature: it stays the per-window state it has always been.
`docs/IMPORT-DETECTION.md` is the full record.

## Sample-accurate automation (`automation.ramp_set`, `automation.ramp_get`) — feature row 9

**Automation now reaches the audio path per sample, and here is exactly where it cannot.** Each
automation clip carries its own `sample_accurate` flag (`automation.ramp_set` with `mode="sample"`, or the
`sample_accurate="1"` attribute of the clip's own element). With the flag on, `Song::buildAutomationRamps()`
publishes the clip's curve to every parameter it drives as a per-sample ramp at the start of every audio
block — `include/AutomationRamp.h`, one knot per tick boundary inside the block plus the block's two ends —
and the per-sample buffer the audio path multiplies with (`AutomatableModel::valueBuffer()`) carries the
curve at every frame instead of one value smeared over the whole block from the previous block's value
(`docs/SAMPLE-ACCURATE-AUTOMATION.md` is the design record; `automation.ramp_get` reports, per parameter,
the mode, the ramp the render thread built, and how many knots the fixed capacity refused). Stated limits,
all structural and all measurable through the socket:

* **a device that never reads a per-sample value keeps the block's single value.** The ramp lives in the
  model and the consumer has to ask for it (`AutomatableModel::valueBuffer()` is the door — the mixer
  channel fader, the instrument tracks and the fx chains are the consumers that use it), so a parameter
  whose own `process()` reads `value()` once per block sees the value at the block's start and no
  per-sample movement at all;
* `CubicHermite` (**tangent-edited**) curves are **approximated**: their stored shape inside one tick is a
  cubic and a ramp is piecewise linear, so such a clip is interpolated as one straight segment per tick.
  `Linear` and `Discrete` (the engine's default, and what `automation.add_point` writes) are reproduced
  exactly — the surface reports the progression type beside the mode rather than implying otherwise;
* a block that needs more knots than the ramp's fixed capacity holds (`MaxKnots` 32: about 400 BPM with a
  512-frame block at 44.1 kHz and 192 ticks per bar) is **refused the surplus knots and counts them**
  (`refused_knots` in `automation.ramp_get` — never an allocation), so the block interpolates over the
  knots it kept;
* a clip **in a pattern** cannot use it at all: `automation.ramp_set` **refuses** it (`Refused`) because a
  pattern's automation is re-read against the pattern's own tick grid and has no single block timeline;
* a transport **jump** (loop wrap or seek) that lands inside a block is read with the ramp built for that
  block's start, so the rest of that one block follows the old position's curve; the next block is exact;
* the mode is **off by default**, per clip, and a project that never turns it on renders byte-identically
  (`buildAutomationRamps()` returns at its first type test) — which also means an automation clip that was
  never opted in still carries the whole-block lag it always had.

**UI absence — one line: the mode is settable through the socket, not from the interface** —
`grep -rniI 'sampleAccurate\|sample_accurate\|ramp_set\|AutomationRamp' src/gui/` returns **0** hits, there
is no automation-editor toggle, no clip-context entry and no per-parameter gesture for it, and a project
file or `automation.ramp_set` are the only two ways to author it.

## CLAP hosting on Windows, and a load failure that says WHICH failure it was (task #667, advertised-features row `clap-hosting`)

**CLAP effect hosting is built on all three platforms in 0.3.0-alpha.** It could not be until now: the host
opened its plug-ins with `dlopen`/`dlsym` (`<dlfcn.h>`), which neither MSVC nor MinGW provides, so the three
Windows jobs were configured `-DWANT_CLAP=OFF` and `tests/advertised-features.tsv` scoped the row to
`linux,macos`. The loader now has a Windows half (`LoadLibraryW` / `GetProcAddress` / `FreeLibrary`, opened
through the wide path so an install directory with non-ASCII characters works) and that row says `*` again.
The guard asserts the **inverse** on any platform a row does not list, so the row and the jobs cannot drift
apart in either direction without the release job failing.

**A load failure now has a type, not only a sentence.** The loader
(`plugins/ClapEffect/ClapLoader.h`) answers with one of eleven `loader::Code` values — the file could not be
opened, the module exports no `clap_entry`, the module declares a CLAP version this host cannot host,
`clap_entry.init()` failed, the module has no plugin factory, the plug-in id is not in the module, the
plug-in could not be created or could not be initialized, a required extension is missing, the audio-ports
description is unusable — each with a stable machine token (`symbol-missing`, `version-unsupported`, …) that
a log reader or an agent can match on. Before this, four different failures arrived as the same prose and a
directory scan called three of them "'<path>' is not a usable CLAP module". The sentence a user sees is
**unchanged**, and the file that opens a module or looks up `clap_entry` is now exactly one:
`grep -rlI '"clap_entry"' plugins/ src/ include/` returns `plugins/ClapEffect/ClapLoader.cpp` and nothing else.

Two bounds, stated rather than implied.

- **The Windows half's verdict is CI-only evidence.** The lane that landed this port had no Windows
  toolchain: the `#ifdef _WIN32` branch has never been compiled on this box. It is compiled by all three
  Windows jobs of `.github/workflows/build.yml` (`mingw`/mingw64, `msvc`/msvc-x64, `msys2`/windows-arm64),
  all three of which pass `-DWANT_CLAP=ON`, and — of those three — **only msvc-x64 runs `ctest`**, so that is
  the job where a broken module is actually put through the loader on Windows; mingw64 and windows-arm64
  build the module and their release-honesty guard asserts it exists in their artifacts. Nothing on Linux
  compiles it. What IS proven locally is that the POSIX loader is **untouched**
  — the five lines that are the POSIX loader (`dlopen` with `RTLD_NOW | RTLD_LOCAL`, `dlsym`, `dlclose`,
  `dlerror`, `fromLocal8Bit`) are compared as whole lines against `release/0.3.0`, the Windows half
  contributes 0 lines to a POSIX build, and the typed path itself is driven by five real modules on Linux —
  `bash tests/prove-clap-loader-unchanged.sh`, and the per-claim output is in the lane's report.
- **No third-party CLAP plug-in has been loaded on Windows** — or on any platform. The only CLAP module this
  release has been proven against is our own MIT fixture (`tests/data/clap-test-plugin/`, built from the
  pinned CLAP 1.2.10 headers), which the msvc-x64 job loads under CI and which the four one-fault modules
  beside it deliberately break in one way each so that a wrong error code is a test failure. A third-party
  module that misbehaves in a way the fixtures do not model is untested, not claimed.

**UI absence — one line: a failed load is drivable and observable through the log and the device's error
path, not from the interface.** There is no plug-in-error dialog, no scan-report page and no CLAP-specific
preference entry: `grep -rniI 'lastLoadFailure|ClapLoader|ClapHost|loader::Code' src/gui/` returns **0** hits.

## The golden-audio integration programme (board task #679, lane `030/golden-audio`) — added 2026-09-15

**UI absence — one line: the golden-audio programme is a test, not a surface.** It registers **no**
command group (no `golden.*` id exists), nothing under `src/gui/` reaches it, and its release
evidence is the two registered ctests (`GoldenAudioSelfTest`, `ControlGoldenAudio`) plus the
committed record `tests/golden-audio-record.tsv`. It is not driven from the interface because it is
not part of the product's surface at all; the whole programme is `docs/GOLDEN-AUDIO.md`.

What it is bounded by, stated rather than measured-away (every number below was measured on
2026-09-15 on one 20-core Linux box, `build/zene` RelWithDebInfo/Qt6, binary sha256
`fd10f160…`; the record carries the floors and the provenance):

- **The tolerance is a measured same-build run-to-run floor, not bit-identity.** Renders here are
  not reproducible run to run (`docs/RENDER-DETERMINISM.md`), so no check anywhere may compare two
  renders by hash. The programme renders its fixture 5 times per path, compares every pair, and
  takes the worst value of each term as that term's floor; a verdict fails only when a term
  exceeds **twice its own floor**, floored at one LSB (16-bit) or 0.01 dB.
- **Three of the four measured paths were bit-reproducible on that build** (floor 0 LSB over 10
  pairs): a socket-built fixture's `render.render`, `render.stems` and `bounce.in_place`. On those
  the smallest gain change the programme distinguishes is **−0.001 dB**; −0.0001 dB (one LSB of
  sample delta) is **not** distinguishable and is not claimed to be.
- **The bundled project that is recorded as still not reproducible still is**: over 10 pairs of the
  same build, 96.7 % of `Root84-TrancyLoop`'s frames differed, by up to 13 275 LSB (40.5 % of full
  scale), while its **loudness moved by 0.0023 dB** — which is why the sample-level terms exist and
  why a level check alone would be worthless here. On that fixture the sample term's tolerance is
  81–95 % of full scale, so it can only catch a near-full-scale difference and **the level term is
  what carries the discrimination**; the smallest change the programme distinguishes there is
  **−0.1 dB**.
- **It says nothing about a module that is itself non-deterministic**, and nothing across builds,
  machines or optimisation levels: the floor is a property of one build on one box. A change whose
  samples move less than the floor is indistinguishable by construction, and a difference the
  programme does report is not attributed to a commit — that attribution is the reader's work.

- **MIDI controller auto-reconnection has no interface, and its reach is the client poll — added
  2026-09-14.** A controller assignment is remembered by IDENTITY (the MIDI client's NAME and the port's
  NAME, "<client name>:<port name>") and re-established without user action when the device comes back
  at a new sequencer address, drivable through `--control-socket` (`midi.reconnect_status`,
  `midi.clients_list`, `midi.reconnect_arm`, `midi.reconnect_set`) with a registered ctest that kills a
  real external ALSA-sequencer client and starts it again under the same name — but **nothing in
  `src/gui/` shows, arms or reports a controller re-connection**: MIDI controller auto-reconnection is
  drivable through the socket, not from the interface. There is no re-connection indicator, no binding
  list and no mode switch. The notice is **measured per backend on this box**, not read off the source
  (2026-09-15, the rebuilt binary, the config file's `audioengine/mididev` set to one client at a time and
  `midi.reconnect_status` read back): the ALSA-sequencer client reports `notice: "polled"` with its ports
  listed, and the registered transcript measures a real kill-and-return re-attachment through that poll;
  the dummy client reports `notice: "none"` and an empty port list. **The JACK, ALSA-raw, OSS and sndio
  clients cannot be reached on this host at all** — there is no `jackd`, no `sndiod`, no `/dev/midi*` and
  no `/dev/sequencer`, and configuring any of the four lands the engine on the dummy client (measured:
  `notice: "none"`, 0 ports) — so their own APIs' hotplug behaviour is **unverified-on-hardware**, not
  measured and not claimed; the WinMM and CoreMIDI clients are not compiled on this platform at all
  (`LMMS_HAVE_WINMM` undefined here) and are unverified-on-hardware for the same reason. What IS measured
  is that this build re-attaches only where a client publishes a port-list change and reports `notice`
  itself, so on any client that declares none the loss is recorded and nothing can re-attach
  automatically. Four further bounds are stated rather than left to be discovered: a re-connection is
  observed within the client's poll (about a second for the ALSA-sequencer client) and not at the instant
  the device returns; a device that comes back with the address it just freed inside one poll interval is
  never *seen* to leave, so no loss is recorded and there is nothing to re-attach (measured: the
  transcript's mode step has to wait for the recorded loss before it measures anything); a port whose
  track lives inside a Beat/Bassline container is reported by `midi.reconnect_status` as the port's own
  display name, which `midi.reconnect_set` cannot address, because a `trk-<n>` id resolves over the song
  container alone (`resolveTrack`, `src/core/ControlEditSupport.cpp`; measured: `'Jupiter' is not a track
  id of the form trk-<n>`); an assignment is remembered only while it is bound at least once with the
  device present, so a project naming a port that is absent at load time has no subscription to remember
  and is not re-attached; and a controller whose driver renames its sequencer client on every replug is a
  different identity, which is not re-attached.
