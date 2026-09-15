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
  stands unchanged — **only the pitch axis is applied by playback**; pressure and timbre are stored and editable
  and reach no instrument.
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
- **And there is no way to choose an automation mode — added 2026-09-13.** The sentence above is true of the
  *engine*; it is not true of the product. The modes exist in the model and a test covers them
  (`include/AutomatableModel.h:342-384`, `src/core/AutomatableModel.cpp:780-910`,
  `tests/src/core/AutomationModesTest.cpp`, registered at `tests/CMakeLists.txt:15`), but nothing can
  **select** or **persist** one: `setAutomationMode()` is called only from that test, no save/load path stores
  the mode, and the agent command `automation.mode_set` **refuses every call**
  (`STATUS-CORRECTION-2026-09-13.md` §8 — the automation-modes ruling: engine and tests exist, selection and
  persistence do not). Treat Read as what the engine runs, not as something you can switch to. The refusal's
  own justification still cites a `docs/KNOWN-LIMITATIONS.md:84` line reading "No automation modes", which this
  page does not contain at this tip — a stale comment in the code, reported here rather than silently
  harmonised.
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
- **MPE applies pitch only.** Per-note expression is captured from MPE input, stored on the note and editable;
  **pitch is applied on playback, while pressure and timbre are captured, stored and readable but not applied.**
  There is no per-note expression editor.
  Verified in the tree: `docs/MPE.md` §0 is the implementing lane's verdict — expression is captured from
  MPE-style input, stored backwards-compatibly on the note as `mpepitch` / `mpepressure` / `mpetimbre`, readable
  and editable through a headless API, with pitch applied by the playback path (`src/core/NotePlayHandle.cpp`)
  and pressure/timbre captured, stored and readable but not applied; the document names the exact lines that
  block a pressure/timbre path rather than inventing one. `src/core/midi/MpeExpression.cpp` is in the tree, and
  the lane `post-alpha/mpe` is an ancestor of this tip.
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
- **The routing graph is readable, not editable, and there is no patcher — added 2026-09-14.** The graph a
  signal is actually processed through (the effect chain's `RoutingGraph`: its nodes, connections, cached
  topological order and output node, plus a mixer channel's rack graph) is drivable through
  `--control-socket` with `routing.get_state`, and the mixer's routing is settable through `mixer.route_to` /
  `mixer.send_to` / `mixer.sidechain_to` / `mixer.route_remove` — but **nothing in `src/gui/` draws a patch
  bay, a cable, a node or a port**, and **no command edits a `RoutingGraph`**: the class's own threading
  contract (`include/RoutingGraph.h`) says topology edits are control-thread operations that must not run
  concurrently with `process()`, and the atomic plan swap that would make live edits safe is deliberately not
  implemented (see `PATCHER-MVP.md`); on top of that a chain's graph is DERIVED — `EffectChain::
  rebuildRoutingGraph()` clears and re-wires it from the effect list on every change, so a hand-wired edge
  would be discarded by the next `plugin.load` / `plugin.unload`. **The read is narrower than the name
  sounds, and this is measured rather than estimated:** a chain whose devices HAVE audio-ports models keeps
  the plain effect loop (`EffectChain::rebuildRoutingGraph` returns early for it,
  `src/core/EffectChain.cpp:89`), and every built-in device in this tree is `AudioPlugin`-derived
  (`DefaultEffect`, `include/AudioPlugin.h:462`), so a track's or a channel's chain graph is normally EMPTY
  with `routes_through_graph: false`; the graph with live prepared nodes is the **rack's**, which
  `routing.get_state` also reports and `tests/control-routing-commands.py` measures (two added chains = five
  nodes, six connections, the sum node as the output node, prepared at the engine's own block size). The
  patcher GUI is out of scope for this
  release, exactly as feature row 28 records.
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
  bytes (`re-run the action that crashed`; the discarded report's content is not recoverable). There is also no
  `crash.enable` / `crash.disable` — `main()` installs the reporter before the control socket exists and the
  module has no uninstall — and the module is a documented no-op on Windows, where the read reports no
  directory and the writers refuse, typed. The engine keeps its proof (`tests/src/core/CrashReporterTest.cpp`)
  and the **surface** is proven by `tests/control-crash-reporter.py`.

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
