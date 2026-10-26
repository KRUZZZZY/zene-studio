# Zene Studio 0.3.0-alpha: known limitations

What this alpha does **not** do, in the order you are most likely to hit it. If something here surprises you,
that is this page's fault — report it and it gets added.

> **Version.** This page ships with **0.3.0-alpha** (`release/0.3.0`,
> `docs/RELEASE-NOTES-v0.3.0-alpha.md`). Every paragraph added for 0.3.0 carries its own date, and the
> 2026-09-19 corrections in this pass are marked *Corrected 2026-09-19 (task 691)*.
>
> **The release's promise, in one sentence, and the reason most of this page is a list of absences in the
> interface rather than absences in the product:** **everything is operable through `--control-socket` and the
> MCP bridge; almost nothing is operable from the interface.**
>
> **Version (0.2.1 history).** The page first shipped with the 0.2.1-alpha re-cut of the 0.2.0-alpha release; the feature set is
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
- **Packages come from the release page, and only from there.** A build job uploads a package **for a tag build only** — an ordinary push and a manual CI run both run
  the jobs but upload no package at all; and only the platforms whose build job is green have packages.
  Check the release page for the current set. Verified in the tree (2026-09-19): the six package
  `upload-artifact` steps carry `if: startsWith(github.ref, 'refs/tags/')` with **no** dispatch disjunct
  (`.github/workflows/build.yml:275`, `:493`, `:750`, `:1049`, `:1279`, `:1444`, each carrying a comment that the
  `|| github.event_name == 'workflow_dispatch'` disjunct used to sit there and was removed); a seventh
  upload (`:1191`, `ctest-log-msvc-x64`) runs on failure only and is commented as deliberately exempt
  "because this is evidence, not a package". The workflow's triggers remain `push`, `pull_request` and
  `workflow_dispatch` — a manual dispatch also uploads nothing. (Corrigendum from
  `docs/CAPABILITIES-0.3.0-VERIFICATION.md` §4.1: the previous text quoted the retired condition and
  stale line numbers.)
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
- **The Session View has no clip launcher, scene launcher or clip grid in the interface.** From 0.3.0
  `WANT_SESSION_VIEW` **defaults ON**, so the data layer, launch scheduler, MIDI and audio cell playback, and
  the `session.*` control-surface group are in the release builds. An agent can build a grid, launch a clip
  or a whole scene, and read the launch back through `--control-socket` (`session.get_state`,
  `session.set_slot`, `session.launch_scene`, ...); users can hear launched content but cannot see or operate
  the grid. A project containing `<session>` data is also **preserved across a round trip**, which was already
  fixed in 0.2.1 and is unchanged.
- **UI absence — one line: the clip-launch grid is drivable through the socket, not from the
  interface.** The grid, its scenes and its slots ARE the `session.*` command group's objects
  (`session.set_grid`, `session.set_scene`, `session.set_slot`, `session.launch_scene`,
  `session.get_state`), and the interface draws none of them:
  `grep -rniIE "session\.(set_|launch_|stop_|clear|get_state|follow_|arrangement_|back_to_)" src/gui/`
  returns **zero** matches, and so does the same grep for the session classes (`SessionView`,
  `SessionModel`, `SessionClip`, `SessionScheduler`, `SessionFollow`) — the only `session` spellings
  in `src/gui/` are `MainWindow`'s crash-recovery `SessionState` and one telemetry-consent string.
  The clip-launch grid UI itself (#598) is **out of 0.3.0**.
- *(Updated 2026-09-30.)* **The clip launcher (View ▸ Clip Launcher, the R5.3 grid) now has a row for these**:
  Follow Actions (the session-wide switch, `session.set_follow_actions`), Record to arrangement
  (`session.arrangement_record_arm`), Land the recording (`session.arrangement_record_land`) and Back to
  arrangement (`session.back_to_arrangement`), kept in step with the engine once a second
  (`SessionTransportBarTest`). Per-cell Follow Action chain editing is **not** socket-only: a cell's (and a
  scene's) right-click menu has **Follow Action Chain...** (`chainEntry` in `src/gui/SessionGridMenus.cpp`,
  WF-061), which opens the per-cell chain editor and writes the chain back through `session.set_slot` /
  `session.set_scene` `follow_actions`. That is separate from the session-wide switch in the grid's own menu
  (the **Follow Actions** row above, `session.set_follow_actions`), which only arms or clears evaluation for
  every cell. The bullet below is the earlier record.
- **Arrangement Record and the Back-to-Arrangement switch are drivable through the socket and through
  nothing else; Follow Actions themselves are NOT socket-only — the session-wide switch and per-cell
  chain editing are in the GUI (bullet above).** The chain a slot has carried since #594 is now EVALUATED
  (all ten action types, chance weighting, linked/unlinked timing) and the performance can be recorded
  into the arrangement ring and landed as timeline clips — through `session.follow_set` /
  `session.follow_get_state` / `session.arrangement_record_arm` / `_status` / `_land` /
  `session.back_to_arrangement` (2026-09-29, R5.2: and SCENE chains - `session.set_scene follow_actions`
  persists one, `session.scene_follow_set` arms it, and a launched row's chain moves the whole row and
  overrides its cells' chains; `session.set_slot follow_actions` is now how a cell's chain is persisted,
  which no socket path could do before): in the interface, the grid's **Follow Actions** row (the
  session-wide switch, `SessionTransportBar`) and every cell's/scene's **Follow Action Chain...** entry
  (`SessionGridMenus`, above) edit what these commands write, while **Arrangement Record** and
  **Back-to-Arrangement** stay socket-only (no Arrangement Record button, no take lane and no
  Back-to-Arrangement light), and the clip-launch grid (#598) is **out of 0.3.0**. A landed clip carries
  the recorded **position, length and copyable content**:
  MIDI notes are copied to instrument clips and audio cells to sample clips. Incompatible destinations and
  unreadable audio sources are refused without consuming the recorded performance. The release notes state
  the same bounds.
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

## Plug-in formats and libraries on Linux — what loads, and what we have tested

*(Added 2026-10-15, Q050-014.)* Format **loading** and third-party **compatibility** are different
claims, and this page keeps them apart. What this build does is load the formats it is compiled with;
what it has *tested* is a much smaller set. Read this before you expect a particular plug-in or sample
library to work.

- **Formats this Linux build loads** — from the interface and the agent surface alike: **VST3** effects and
  instruments, **CLAP** effects and instruments, **VST2** through Vestige, **LV2**, **LADSPA**,
  **SoundFont2** and GUS patches, plus the built-in instruments. Format availability varies by platform:
  this list is the Linux build's, and each platform's build enables its own set — CMake disables VST
  hosting on Apple builds, so a macOS build has no VST2/Vestige path. Discovery is Linux-native: plug-ins
  are found in each format's standard folders and in your own (`plugin.paths_get` /
  `plugin.paths_set`), and a scan records every candidate file.
- **SFZ instruments play in the built-in SFZ Player** (Q050-484; the sfizz engine, built into the module on every
  platform, with libsndfile as the only outside dependency). Velocity layers, round robins and key switches are
  tested; **only the Linux/GCC build has been built and run** - the other platforms' builds are by construction, and a
  region whose sample file is missing is silently dropped by sfizz. `docs/050-features/Q050-484.md` has the full list.
- **The GUI location for per-file scan status and quarantine controls is Project tools ▸ Plug-in
  Modules...** It lists every record the scan holds — its path, its per-file status, its name and the
  error it failed with — and each row offers **Quarantine...** (with a reason), **Release** and
  **Rescan**; **Edit ▸ Plugin Quarantine** adds and releases entries too. These are the *same*
  registrations an agent drives: `plugin.scan_cache_list`, `plugin.scan_cache_quarantine_add`,
  `plugin.scan_cache_quarantine_remove` and `plugin.rescan`.
- **Third-party compatibility is unverified except for what `docs/PLUGIN-COMPATIBILITY.md` measures.**
  That page is the measured matrix of named third-party VST3, CLAP and LV2 plug-ins (a pinned package and file
  hash each) run on one Linux system, with load, editor and state round-trip as separate outcomes and every
  failure listed; its in-tree fixtures section is a separate set. Beyond the fixtures in the source tree
  (`tests/data/vst3-test-instrument/` and the CLAP test modules) and those matrix rows, **no other third-party
  plug-in has been tested by us**. A file the scan finds and loads may still behave differently — that is
  untested, not claimed.
- **No verified bridge for Windows plug-ins.** There is no tested Wine/`yabridge` path: a Windows-only
  VST that needs a bridge has not been run here and is not supported.
- **Kontakt-scale libraries are external.** **SoundFont2** is a supported sampled-instrument format and
  is what a Linux user can rely on from this build; a **Kontakt** library is not. Kontakt needs Native
  Instruments' own installer and authorization, which live outside this project, and this build ships
  **no Kontakt runtime and has verified no Kontakt library**. A hosted plug-in that happens to load a
  library is the same unverified third-party case as above.

## What this alpha cannot do at all

- **CLAP hosting is in the release on every platform — this bullet said the opposite and is corrected.**
  *Corrected 2026-09-19 (task 691): re-measured against `.github/workflows/build.yml` and this tree.* All six
  build jobs (seven platform builds) pass `-DWANT_CLAP=ON`, the loader's Windows half is `LoadLibraryW` / `GetProcAddress` /
  `FreeLibrary` instead of `dlopen`/`dlsym`, and `tests/advertised-features.tsv`'s `clap-hosting` row is `*`
  again (every platform), which `tests/release-honesty-gate.sh` asserts in both directions. The 0.2.1 text
  (*"No CLAP hosting on Windows … the Windows builds are configured with `-DWANT_CLAP=OFF`"*) was true of that
  release and is not true of this one. What is *not* claimed stays claimed: the Windows half's verdict is
  CI-only evidence (the port's §"CLAP hosting on Windows" below), and **no third-party CLAP plug-in has been
  loaded anywhere** — the only modules proven are the MIT fixtures in the source tree.

- *(Corrected 2026-10-07, plugin-hosting lane.)* **Until now no VST3 or CLAP effect could be added to a
  chain.** The two effect host modules (`vst3effect`, `clapeffect`) never exported `lmms_plugin_main`, the
  entry point `PluginFactory` requires of every module, so the factory recorded both as "not a plug-in":
  `plugin.list` carried no VST3 or CLAP effect, the effect pickers offered none, and a project's saved one
  loaded as a placeholder. Every earlier proof of the effect hosts constructed the classes directly and never
  went through the factory. Both modules now export it. A class that does not load answers nothing, so the
  chain keeps a saved one as its placeholder, state intact, as before.
  `ControlDeviceCatalogueTest::hostedEffectsListAndLoadThroughTheSurface` lists and loads the in-tree VST3
  and CLAP effect fixtures through the surface. Still true: no third-party effect has been loaded by us.
- *(Added 2026-10-07, plugin-hosting lane, MX-094.)* **A VST3 effect's own window now opens at the device's
  zoom** (`plugin.options editor_scale`, 0.5 to 3), as a CLAP effect's already did. A view that scales itself
  (`IPlugViewContentScaleSupport`) is told the factor. Any other view that can resize is given the scaled size.
  A view that can do neither keeps its own size. Limits: the zoom applies when the window opens, not to a window
  already open. Instruments have no editor-scale option (it is an effect's device option). **LV2 is not
  covered**: this host does not open an LV2 plug-in's own UI at all (the generic controls only; the "Show GUI"
  button in `src/gui/Lv2ViewBase.cpp` sits behind `if (false)`), so there is no UI to send `ui:scaleFactor` to, and no
  LV2 UI fixture exists. Proved offscreen against the in-tree VST3 fixture only; no third-party editor has
  been zoomed on a real display.
- *(Updated 2026-09-30, R4.1/R4.2/R4.4.)* **A VST3 plug-in's own editor now opens** - in a window of its
  own, through `plugin.editor_open` (the host's `Vst3EditorSession`: createView, X11 embedding, resize,
  IPlugFrame and Linux::IRunLoop on the Qt loop). What is proved is the contract, offscreen, against the
  in-tree test instrument's view (attach, the view's size and minimum, its repaint timer, close and
  reopen); **no third-party plug-in's editor has been opened on a real X11 display**. The instrument window's
  control view carries a "Show plugin editor" button (plugin.editor_open through the registry), and so does
  a VST3 or CLAP effect's control dialog, naming the effect where it sits when clicked. CLAP
  editors embed too (R4.3: the clap.gui life cycle, X11 embedded only - a plug-in that can only float
  is refused rather than given a window this host does not manage), proved the same way against the
  in-tree CLAP fixture's gui; the host also answers timer- and fd-support. The
  paragraph below is the pre-R4 record, kept for its reasoning.
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
  `Vst3InstrumentFixtureProbe` probe — `docs/VST3-INSTRUMENT-FIXTURE.md` §0, §5). Since 2026-09-16 the same
  hosting is drivable through the socket as well as from the interface: `plugin.list` carries every VST3 class
  of the product's VST3 search directory as a `format: "vst3"` entry and `plugin.load` loads one onto an
  instrument track (the class, the bundle and the `(file, class)` key are the same ones the instrument browser
  uses; `ControlDeviceCatalogueTest` holds the fixture-backed proof). *(Corrected 2026-10-09, Q050-003.)*
  Output buses past the main one are routable (MX-114: `Vst3Instrument::enableOutputBus`,
  `plugins/Vst3Instrument/Vst3Instrument.cpp:239`; see "F2, instruments") and the instrument reports its
  latency to delay compensation (MX-157, `:262`; see "Hosted instrument latency"; the native CLAP, VST3 and LV2 instrument hosts report, built-in instruments are 0, and the out-of-process, VST2 (Vestige) and Carla paths report nothing to it - corrected 2026-10-19, Q050-169); the in-tree VST3 fixture has
  one output bus, so the routing of a real multi-output VST3 plug-in is unproven (`plugin.output_*` is measured
  against the stand-in `buses_test_instrument`, `Vst3BusMapTest::testOutputBusesKeepInactiveOnes` covers the bus
  map). There is still no read of the plug-in's own preset (program) lists and no out-of-process hosting. **CLAP instrument
  hosting is socket-only in the same sense** (feature row 79): a CLAP generator loads onto an instrument
  track through `plugin.load` — `plugin.list` carries the CLAP classes of the product's plug-in directory as
  `format: "clap"` entries — its notes reach it through the track's MIDI path, and
  `plugin.host_notes` reports the note ports and the audio-output configuration the host discovered. It is
  **drivable through the socket, not from the interface**: the CLAP instrument browser entries lead to a
  generated parameter view and there is no `clap.gui` support anywhere in this tree, so the instrument's own
  window does not open (the same limit the VST3 instrument has). *(Superseded 2026-09-30: R4.3 implements the
  clap.gui life cycle, X11 embedded, and the instrument view's "Show plugin editor" button opens it.)*
- *(Superseded 2026-09-29/30 (M3.7); the interface half corrected again 2026-10-18 (Q050-171).)*
  **A VCA group's mix half and its main edit-half gestures are in the interface, and
  `--control-socket` reaches the same registered commands for the verbs that have no menu, listed
  below.** The mixer draws a VCA strip per group - type badge, name, member count, Mute, Solo and
  the group's own fader - while the mix has any groups (`src/gui/VcaStripView.cpp`, built by `MixerView::refreshVcaStrips`,
  `src/gui/MixerView.cpp:403`, which hides the strip area when the last group is removed). The
  strip's right-click menu carries **Links** (Level, Mute, Solo and Suspend All, checkable,
  `vca.set_links`) and **Put Inside** (nesting through `vca.set_parent`; a parent that would close
  a loop or nest past `MaxGroupDepth` (8, `include/MixerGroups.h:37`) is offered disabled with the
  reason as its tooltip rather than refused unseen, `src/gui/VcaStripView.cpp:106`). A mixer
  channel's context menu has a **VCA group** submenu (`src/gui/MixerChannelView.cpp:347`): one
  checkable membership entry per group (`vca.assign` / `vca.unassign`) and **New VCA group**
  (`vca.create`, then `vca.assign` for that channel; `populateVcaMenu`,
  `src/gui/VcaStripView.cpp:168`) - two command invocations, so two recorded undo steps rather
  than one. **Project tools ▸ Groups...** lists the groups with Rename... (ask), Remove
  (confirmed), Add Track to Edit Set... and Remove Track from Edit Set... (ask), Phase Lock On and
  Phase Lock Off, and New Group... (`CommandLists::vcaGroups`, `src/gui/CommandLists.cpp:59`,
  behind `menuparts::list` in `src/gui/shell/ShellCommandMenus.cpp:526`); **Project tools ▸ Colour
  a Group's Channels...** runs `vca.color_members`
  (`src/gui/shell/ShellCommandMenus.cpp:519`); and the Inspector's **Clip** menu carries **Move
  With Edit Group...**, the `vca.edit_move` form prefilled with the clip
  (`src/gui/shell/ShellCommandMenus.cpp:327`, attached to the Clip section's title bar by
  `src/gui/shell/InspectorPanel.cpp:383`; the Inspector's channel menu has no VCA submenu). Every
  menu line runs a registered command the socket runs. Each **submitted mutating** action is one
  recorded undo step - the membership ticks, the Links and Put Inside choices, the Groups list's
  row actions and New Group..., Colour a Group's Channels..., the submitted Move With Edit Group...
  form and the strip's fader, Mute and Solo - with **New VCA group** above the one exception (two
  command invocations, two steps). The launchers themselves do not edit or add a step:
  **Groups...** opens a list whose read-only `vca.list` query writes nothing, and **Move With Edit
  Group...** (like **Colour a Group's Channels...**) opens a form that becomes one step when
  submitted. The strip's fader, Mute and Solo are the group's own models, so a strip edit, a menu
  edit and a socket edit move one state
  (`VcaStripViewTest::theStripsControlsAreTheGroupsOwnModels`,
  `theChannelMenuEditsMembershipThroughTheCommands`,
  `theStripsMenuChoosesLinksAndNestingThroughTheCommands`,
  `theGroupsListOffersCreateEditSetAndPhaseLock`, `theClipsMenuCarriesMoveWithEditGroup`).
  The entity is unchanged, with the citations that hold at this tip: a real object
  (`src/core/VcaGroup.cpp`, `include/VcaGroup.h`) created and owned by the mixer
  (`Mixer::createVcaGroup`, `src/core/Mixer.cpp:997`), its gain published to each member as
  `MixerChannel::m_vcaGain` and applied once per period on the audio path (`src/core/Mixer.cpp:762-768`),
  and saved as the `<vcagroup>` element with its `vca` child, its `locked` attribute and one
  `<edittrack track="n"/>` per edit-set member (`src/core/Mixer.cpp:2504-2544`), read back at
  `src/core/Mixer.cpp:2753-2825` - a pre-edit-half element loads as locked with an empty set. `vca.*`
  is 21 ids: the fourteen of `docs/VCA-EDIT-GROUPS.md` §3 plus `vca.color_members` (one colour for a
  group's channels), `vca.set_parent` and `vca.set_links` (the strip's Put Inside and Links menus),
  and the phase-locked `vca.edit_split`, `vca.edit_trim`, `vca.edit_slip` and `vca.edit_fade`; the
  whole group is registered by one function (`registerVcaCommands`,
  `src/core/ControlCommandsVca.cpp:468`), and the proofs are `VcaStripViewTest`,
  `VcaStripLifetimeTest`, `ControlVcaCommandsTest`, `ControlVcaEditGroupsTest`,
  `ControlVcaLockedEditsTest` and the registered ctest `ControlVcaCommands`
  (`tests/control-vca-commands.py`). `docs/VCA-GROUPS.md` (the mix lane) and
  `docs/VCA-EDIT-GROUPS.md` §1-§4 (the edit lane) are the implementing reports, both ancestors of this
  tip; each now carries its own dated note where its "no interface" lines went stale.
  What the interface still does not offer, so a reader can tell its limits from everyone's: **(1)** of
  the phase-locked edits only the move has a menu line - split, trim, slip and fade
  (`vca.edit_split` / `edit_trim` / `edit_slip` / `edit_fade`) are reachable from the socket or
  `zene.command()`, so an edge drag, a split or a fade made on a clip in the arranger affects that clip
  alone and never the edit set; **(2)** the channel's VCA menu never sends the shared-membership form
  `vca.assign` with `also: true`, so ticking a group for a channel another one already holds is
  refused (`heldElsewhere`, `src/core/ControlCommandsVcaShared.h:311`) and the menu reports nothing
  back; **(3)** the Groups... dialog cannot show which tracks an edit set holds - it can add and
  remove them but has no track column. And the limits that remain regardless of surface: a track
  deleted while it is in an edit set stays in the set, reported as `missing_tracks` by `vca.get_state`
  and `skipped_tracks` by an edit (`src/core/ControlCommandsVcaShared.h:177`, `:194`), until somebody says
  `vca.track_remove`; `vca.set_solo`'s undo does not restore the transient
  `MixerChannel::m_muteBeforeSolo`, re-derived on the next solo action
  (`src/core/ControlCommandsVcaMix.cpp:224`, the same limit `track.set_solo` states); a clip
  delete, a clip gain and a crossfade are not carried by the phase lock - `ControlVcaLockedEditsTest`
  holds the five edits that are, and `clip.delete`, `clip.set_gain` and `clip.crossfade` affect only
  the clip they name; and there is no dedicated Lua binding, though that is no longer the sentence to write: every
  `vca.*` id is scriptable through the generic `zene.command()` bridge, which the sandbox refusal
  lists do not name (`refusedPrefixes` / `refusedIds`, `src/core/ScriptCommandBridge.cpp:69` and
  `:80`; the bridge itself at `:272`), so what is missing is menus and read-backs, not reachability. A
  group's audibility is still proved by `VcaGroupTest`'s rendered dB delta rather than a socket
  transcript - `tests/control-vca-commands.py` reads state back, it does not re-render; a strip's
  fader drag undoes through the model's own journal checkpoint (`AutomatableModel` is a
  `JournallingObject`), like any channel fader.
- *(The pre-M3.7 record, kept so a reader who meets the old sentence in an older copy of this page can
  see what replaced it.)* **No VCA groups in the interface.** Mix-and-edit groups exist, are tested,
  and are saved with the project — and since 2026-09-14 the **whole group was drivable through
  `--control-socket`, which was then the only way to reach one: nothing in the interface created a
  group, named one, assigned a member, locked it or edited through it.** Its 2026-09-14 update named
  fourteen `vca.*` ids and the three limits of the day (one propagated media edit - the move; the
  deleted track still in the edit set; the `set_solo` undo); its engine citations (`Mixer.cpp:720`,
  `:528-545`, `:1893`, `:1898`, `:2022`, `:2037`) were stale by the next renumber and the corrected
  ones are in the bullet above.
- *(Updated 2026-09-30.)* **A mixer channel's context menu now has a Rack submenu**, read from `rack.get_state`:
  a line naming the chains and the routing; Route to (Parallel, or one chain; `rack.set_selected`); Add
  parallel chain and Remove chain (`rack.add_chain` / `rack.remove_chain`; chain 0 is the channel's own and is
  not offered); and the rack's macros - one item per macro showing its value, which sets it
  (`rack.macro_set`), Add macro... and Remove macro (`rack.macro_add` / `rack.macro_remove`) (`RackMenuTest`).
  *Since the same day each macro also has Bind <name> to (effect > parameter of the channel's own chain, full
  range; `rack.macro_target_add`) and Unbind <name> (`rack.macro_target_remove`).* *Corrected 2026-10-09 (Q050-047),
  against the tree:* putting effects into a parallel chain is **not** socket-only any more. The Device panel
  draws each parallel chain under a "Parallel chain N" heading with its devices as cards and its modulators
  (`src/gui/shell/DevicePanel.cpp:390-409`); a card's menu has **Insert Effect Before... / After...**, which name
  the card's own chain (`src/gui/EffectDeviceMenu.cpp:247-252`, `insertBeside`); **Wrap the Chain into a Rack Chain**
  (`rack.wrap`) moves the channel's devices into a new chain; and the Inspector's **Add Insert...** opens the
  `plugin.load` form, which has a `chain` field. **The first device of an EMPTY parallel chain**
  has no card to carry a menu, so its heading in the Device panel carries an **Add device...** button
  (`DevicePanel::addChannel`, `src/gui/shell/DevicePanel.cpp:392-408`), and the panel's own **Add device** button now
  loads into the channel's selected parallel chain (`rack.set_selected`; parallel or chain 0 selected means the
  channel's own chain; `addTargetChain`, `:171-179`). Both run `plugin.load` with a `chain` field - the call the
  socket takes, one undo step (`RackMenuTest::anEmptyParallelChainHasItsOwnAddDeviceButton`,
  `::addDeviceLoadsIntoTheSelectedChain`, `::anEmptyParallelChainTakesItsFirstDeviceFromTheInsertForm`, which
  keeps the Inspector's Add Insert... form route proven too). A track's Add device (a track has no rack) still loads
  into the track's own chain. Still socket-only: a binding
  narrower than the full range, a visual key map for the zones, and a Lua binding. The two bullets below are the
  pre-menu record.
- *(Superseded 2026-09-30 and 2026-10-09: a channel's Rack submenu creates chains, routes and removes them, and the
  Device panel shows and fills them; see the bullet above.)* **No racks in the interface, and no scripting access.**
  Parallel chains and a chain selector exist and are
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
- *(Corrected 2026-10-09, Q050-047.)* **Macros and zones in the interface.** Macros have a menu (Add macro..., set
  value, Bind to a parameter, Remove) and knobs on the Device panel (`MacroKnobs`, `DevicePanel.cpp:386`); zones have
  **Rack > Zones...** (a list over `rack.get_state` with Remove, `CommandLists::rackZones`,
  `src/gui/CommandLists.cpp:91-103`) and **Rack > Add Zone...** (the `rack.zone_add` form), both in a mixer
  channel's menu (`ShellCommandMenus::populateChannel`, `src/gui/shell/ShellCommandMenus.cpp:438-440`) and both over
  registered commands (`RackMenuTest::theZonesEntriesRunRegisteredCommands`). What is still absent is a zone
  editor that draws a key map: zones are rows and a form. The bullet below is the 2026-09-13 record.
- **Macros and key/velocity zones are socket-only, and the zones do not route yet — added 2026-09-13.** A rack
  macro is a named, persisted scalar that drives existing parameters through range windows
  (`rack.macro_add` / `rack.macro_target_add` / `rack.macro_set`, `docs/RACK-MACROS.md` §2) and a key/velocity
  zone is a persisted, validated key-and-velocity range mapped to one of the rack's chains
  (`rack.zone_add` / `rack.zone_remove` / `rack.zone_resolve`, §3), both saved as children of the channel's
  existing `<rack>` element. **Rack macros and key/velocity zones are drivable through the socket, not from
  the interface**: there is no macro knob, no zone editor and no key map to draw, so a user cannot create,
  see or move either one — `rack.get_state` reports them and nothing in `src/gui/` draws them. Since 0.5.0 (E20/E38)
  zones route: a note of an instrument track playing into the channel that a zone claims plays on that zone's
  chain only, and a zone whose sample loads plays the sample for the note. Notes of single-stream instruments
  (most hosted plug-ins) cannot be split, and a track with effects of its own or a compensation delay routes
  the old way; zoned notes skip the channel's trim, input mono and pre-FX taps.
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
  *(Updated 2026-09-30, M3.)* **Folders now have an interface**: the song editor's "Add folder track"
  makes one, every track's menu has a Folder submenu (Move to folder / No folder; on a folder,
  Collapsed, Pinned and Route children through the folder), all through the same `track.*` commands,
  and a collapsed folder's children leave the track list - from the menu or the socket
  (`TrackFolderMenuTest`), and a child's row is indented one step per folder level (its settings column
  keeping its width) with the membership as its accessible description. Since 0.5.0 (E21) a pinned
  folder stays on screen while the list scrolls (a strip above the scrolling tracks; TrackPinStrip.h). The named
  visibility sets have a switcher too - the song editor's Views menu: All tracks (the new
  `track.visibility_show_all`), each set (apply), Save visible tracks as..., Remove - and applying a
  set now actually hides the other rows (the view flag had never been read by the track list;
  `VisibilitySetMenuTest`). *(Updated 2026-10-07, ED-035.)* A folder's row draws its children's clips as
  **summary clips** - one per stretch where they hold clips (overlapping or touching clips make one), with a
  miniature of the clips inside - so a collapsed folder still shows where its music is. Dragging a summary
  clip moves every clip inside it (`track.folder_move` with its new optional `start`/`end` span), Ctrl+drag
  copies them there (`range.copy` + `range.paste`), and its menu duplicates it after itself or deletes it
  (`range.delete`); each edit is one undo step and the arranger's own range is put back after it
  (`FolderSummaryLaneTest`; `include/FolderSummaryLane.h`). Not done: a summary clip cannot be resized,
  split or selected together with ordinary clips, and has no keyboard route; the copy uses (and so replaces)
  the range clipboard; a delete follows the arranger's ripple mode. The paragraph below is the pre-M3 record.
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
- *(Updated 2026-09-30.)* **A sample clip's context menu now has Record into this clip** (`clip.set_record`,
  new): the clip is armed to take the input the next time the song plays with the song editor's **Record while
  playing**, and shows upstream's "Rec" badge until the take lands - the interface route to the recording path
  RecordingLatencyTest proves (latency-compensated, and since BUGS_FOUND 11.17 exact for a clip that starts
  inside a period) (`ClipEditsMenuTest`). The flag is not saved with the project, as upstream's never was.
- *(Updated 2026-09-30.)* **A sample clip's context menu now has Gain and fades**: Clip gain... (a dB value,
  `clip.set_gain`, the item naming the current gain) and Reset gain; Fade in and Fade out, each None / 1 beat /
  2 beats / 1 bar with the current length checked (`clip.set_fade`; a fade the clip cannot hold is refused);
  and Fade shape for both ramps - each change one `control.undo` step (`ClipEditsMenuTest`). These are menu
  items, not drag gestures: there are still no fade handles on the clip, and crossfades (`clip.crossfade`)
  and the source-window trim/slip stay socket-only. The bullet below is the pre-menu record.
  *(Updated 2026-10-07, 0.5.0 ED-176.)* Crossfades are no longer socket-only: an audio clip that another clip
  overlaps offers **Crossfade Editor...** (both sides' lengths and shapes, drawn, applied through `clip.crossfade`
  as one undo step; `CrossfadeEditorTest`). See "0.5.0 audio clips" at the end of this page for what it leaves out.
- *(Added 2026-10, Q050-125.)* **Clip gain, the gain envelope and polarity are now dragged on the waveform.**
  A sample clip draws its gain as a dashed line with a square handle on its left edge: drag the handle up or
  down and the clip's level follows the pointer, the drawn waveform growing or shrinking with it, and the
  release is one `clip.set_gain` step. Press the drawn level curve to add a gain-envelope point at the pointer
  and drag it in time and level (a plain press on a point moves it; points may be dragged past each other and are
  re-sorted, which is the order `clip.gain_envelope_set` requires). Alt and Ctrl keep their arranger meanings;
  the Smart tool's upper-half range gesture takes priority over the gain targets. The Gain and fades submenu
  adds **Invert polarity** (`clip.set_polarity`), which flips the clip's rendered sign and mirrors its drawn waveform while
  the SOURCE sample keeps every sample: the inversion is a signed stage on the clip's gain, written as `invert`
  only when set, so an older project loads non-inverted and renders bit for bit as before. The drawn amplitude
  is `clipGainCurveAt` (the one definition the audio path and the automation editor's ghost already share)
  times the polarity sign, sampled per drawn column. Proved by `ClipWaveformEditTest` (real mouse gestures on
  the real view, with the drawn image compared) and `ClipPolarityTest` / `ControlClipPolarity` (the render's
  sign, the source untouched, the commands and the file). What is still absent: a per-point delete gesture
  (remove points through the command), clip-filter and range-selection tools, destructive sample editing, MIDI
  clip edits and mixer-channel polarity.
- **No clip fade, crossfade or clip-gain gestures.** *(Stale in part: fade handles and the Crossfade Editor
  arrived in 0.5.0 L05 - ED-173/ED-176 - and the clip-gain handle, the envelope points and the waveform
  display in Q050-125, the bullet above. Kept as the record of what was absent on 2026-09-13.)* The clip model is in (an authored window that survives
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
  *What this bullet still means, and what is still absent (updated 2026-09-29): the composite **plays** (R3.1:
  each audio take sounds only where its lane is selected, with a short ramp at a switch) and **MIDI takes are comped**
  (R3.3: a note plays where the composite selects its clip's lane at the note's start), and `comp.audition` plays
  one lane whole. Since 0.5.0 (E2) the per-segment slip is applied on audio takes at a constant rate and their own pitch (warped, pitch-preserving and MIDI takes play unslipped); there is **no lane
  geometry, no lane handle, no comping gesture and no waveform drawing** anywhere in `src/gui/`; and there is **no
  flatten** (the destructive bounce a comp can end in). *(2026-09-29, R3.2: `clip.consolidate` is that flatten for
  a region of a sample track - every lane through the composite's gate, one clip back, one undo restores. A clip
  crossing the region's edge is refused rather than cut, and a MIDI region is not consolidated.)**
- **The Linux aarch64 build has no Rubber Band stretch mode — added 2026-10-01.** Ubuntu's aarch64
  librubberband allocates on the audio thread (`SampleClipRubberBandTest`: 16185 allocations in 32 periods
  on every hosted linux-arm64 run, 0 on x86_64 and on macOS arm64), which the realtime rule does not allow,
  so that one platform is built with `-DWANT_RUBBERBAND=OFF`: a warp there uses WSOLA, the default stretcher,
  and choosing Rubber Band is refused as unavailable (BUGS_FOUND 11.20 b).
- *(Updated 2026-09-30.)* **Edit ▸ Modulators...** (`window.modulators`, new) lists the layer's modulators,
  adds an LFO and removes one (`modulator.create` / `modulator.remove`), sets the selected one's shape and rate
  (`modulator.rate_set`), shows the parameters it drives with their depth, binds one from pickers over a mixer
  channel's effects and parameters (`modulator.target_set`) and unbinds it (`modulator.target_remove`)
  (`ModulatorPanelTest`). Still socket-only: the per-sample switch and editing a bound route's depth in place
  (`modulator.depth_set`); the layer still reaches mixer-channel racks only. The bullet below is the pre-panel
  record.
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
- *(Corrected 2026-10-15, Q050-014.)* **The plugin scan cache and the quarantine list now have an
  interface, and this bullet's "no interface" claim is corrected.** **Project tools ▸ Plug-in
  Modules...** lists every file the scan found with its per-file status (path, status, name, error) and
  offers **Quarantine...** (with a reason), **Release** and **Rescan** (`CommandLists::pluginModules`,
  over `plugin.scan_cache_list`); **Edit ▸ Plugin Quarantine** adds and releases entries as well. The
  documented way to quarantine a plugin is no longer hand-editing the JSON file. The pre-menu record
  that this replaced is kept in the "plugin scan cache" bullet further down this page.
- **One feature is compiled out of these builds: the WASM DSP sandbox** (it needs the wasmtime C API, which no
  CI job provisions, so the option's default `ON` degrades to `OFF`). This build reports `WANT_WASM='OFF'` in
  its own build options (`build/lmmsversion.h`, the same text the binary prints on its `Build options:` line).
  **Offline HTDemucs stem separation is the other configure-time feature, and it is the opposite case**
  *(corrected 2026-10-15, Q050-035)*: **it is built in and on by default.** Owner decision 14 made
  `WANT_STEM_SPLIT` default `ON` (`CMakeLists.txt:130`), the release-honesty manifest row requires `ON`, and
  this build reports `WANT_STEM_SPLIT='ON'` with the ONNX Runtime SDK found (`LMMS_HAVE_ONNXRUNTIME='1'`, so
  the in-process inference backend is enabled; without the SDK the external-process backend is used instead).
  It is **local and offline**: the separation runs on this machine and nothing is uploaded — the only network
  access is the one-time fetch of the pinned ~166 MB model on first use, which `LMMS_STEM_OFFLINE` (or an
  explicit `LMMS_STEM_MODEL` path) turns off. Text on this page and in the 0.3.0 release notes used to say
  stem separation was compiled out and reported `OFF`; that described the pre-owner-decision-14 configuration
  and is **not** true of the default build. The detailed limits of the feature (44.1 kHz OUTPUT, the
  offline job, socket-only UI) are in the `stem.*` section below, not here.
  **What enforces "everything the release notes document as present is in this build"
  is `tests/release-honesty-gate.sh`, and on this configuration it passes every row.** The guard's
  mechanism is the checkable half: it fails a documented-present feature when the build under test does not
  report that option `ON` (`AUTO` is not `ON` — the script's own header says so), it fails a documented-absent
  feature that reports `ON`, it exits 1 on any mismatch, and the build jobs in
  `.github/workflows/build.yml` run it against the binary they have just built. **Measured on this
  configuration: 5 of 5 rows match** — the three plugin hosts `ON matches ON` and named as modules
  (`libvst3effect.so`, `libvst3instrument.so`, `libclapeffect.so`), the WASM sandbox `OFF matches OFF`, and
  stem separation `ON matches ON` (`RESULT: PASS — all 5 documented feature(s) match this build on linux`;
  `tests/advertised-features.tsv` is the single list the gate reads).
  **Changed in 0.3.0-alpha:** the `session-view` row is gone — ARCH-4 removed `WANT_SESSION_VIEW` entirely
  (the Session View is native engine code), so there is no option for a row and a build to agree about
  (see the Session View bullet above for what the interface still does and does not include).

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
  Since 0.5.0 (E22) a `wasm_effect` device in any chain also takes a module headlessly through `wasm.device_load
  {target, plugin, path}` (all or nothing, saved, one undo step); otherwise instances are given a module through that dialog, and their 8 parameter models are
  project state reachable with `plugin.param_*` on a `wasm_effect` device. A module hosted by `wasm.load` is
  not in any chain. `docs/WASM-EFFECT-ABI.md` §13 says the same.

- **What would still close `docs/INDEPENDENT-NOTES-READ.md` §B5:** provisioning the wasmtime C API **in the
  seven CI jobs**, so that `wasm-sandbox OFF` becomes a choice rather than an absence-by-dependency. The C
  API being present on *this box* (above) does not close it — CI is what builds the release — and
  `tests/advertised-features.tsv` and `tests/release-honesty-gate.sh` would have to be reconciled in the
  same commit that did it. Until then the row states the truth and the gate enforces it.

- *(Added 2026-10-06, WF-033.)* **The undo history can be saved with the project, and is OFF by default.**
  A project opts in with `project.set_undo_history {setting: on}` or Edit ▸ Undo History's *Save with
  project* box; every project can by default (`undohistory/persist`, the panel's *All projects* box). The
  newest **20** steps (`undohistory/steps`) are saved as compressed whole-project snapshots under a **64 MB**
  cap (`undohistory/capmb`); over it the oldest go first, and a snapshot that alone exceeds the cap means
  nothing from that step back is saved - `project.save`'s `undo_history.warning` and a status message say
  so. Plug-in state is saved exactly as a normal save saves it. Limits: with persistence on, every edit
  serialises and compresses the whole project (the cost of the design, and why it is opt-in); steps made
  before it was turned on cannot be saved; the redo stack is not saved; undoing a loaded step reloads the
  project in place (playback stops); autosave/recovery files, templates, bundles, render temporaries and
  `project.save_version` copies do not carry it; a history whose project was edited by a build that does
  not read it is discarded on open, with the reason in `project.undo_history`'s `last_load`.
  `docs/UNDO-HISTORY-PERSISTENCE.md` is the record.
- *(Updated 2026-09-30.)* **Edit ▸ Undo History... now shows the stack** (`window.undo_history`,
  `UndoHistoryPanel`): depth, redo depth, the two caps, the bytes kept and the evicted count from
  `control.undo_depth`, Undo/Redo buttons through `control.undo`/`control.redo`, and the command record
  newest first. The record lists edits made through the registry only - an edit made directly in an
  editor is undone by Undo but not listed, and the panel says so. Its Limits row sets the two caps and
  the drag-merging window (`control.set_undo_depth`, `control.set_undo_coalescing`). The bullet below is
  the pre-panel record.
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

- **An agent session's MCP tool list can be a stale copy, and re-pointing the bridge is a deployment act — added 2026-09-13.** The `zene-control` bridge generates one MCP tool per command id it reads from a live instance, and a registered ctest now fails on any registered id the bridge offers no tool for (live, offline, and against a planted stale cache), so the *bridge in this tree* covers every id the binary registers — **340 ids in 53 groups for the reference configuration, 332 in 52 for the release configuration** (`Corrected 2026-09-19 (task 691)`: the 173 this line read was an earlier tip's count; re-measured with `tests/control-commands-snapshot.py <build>/zene --compiled-in wasm.`, which reports 340 live ids as 340 generated tools and the 332-id snapshot as 334 tools, **0 missing and 0 extra** in all three modes) — but the entry in `~/.hermes/config.yaml` points at a **scratch copy** of the bridge (`projects/lmms-fl-research/mcp-zene-control`, git-ignored on purpose) whose offline list is the 0.1.0-alpha **70** (*confirmed 2026-09-19: that copy's `commands_snapshot.json` holds 70 ids, captured 2026-09-12, version `0.1.0-alpha.15+a244564`*), so a Hermes session still sees 70 ids until that entry is re-pointed at this tree's `tools/mcp-zene-control` or an instance answers at the scratch copy's socket (`docs/COVERAGE-MATRIX-2026-09-13.md` §4.2–4.4). No commit can fix a path outside the repository.

- **The offline tool list is stale whenever no instance is running, and the bridge can only *say so* while one is — added 2026-09-15.** Serving a shorter list is a limitation of the design, not a bug: with no instance at the socket the bridge has nothing to compare against and answers from the last-known copy. What changed is that the copy is now checkable — every bundle records the surface it describes (`id_count`, `group_count`, `ids_sha256`), and whenever **an instance IS answering** `zene_status` and `zene_commands` carry an `offline_drift` block naming each offline copy, whether it is stale, and which ids it is missing. Proven against a live binary of the integration tip: **340 ids / 53 groups live against this tree's 332-id snapshot** (`Corrected 2026-09-19 (task 691)`: when this bullet was written the same probe read 265 / 43 against a 144-id snapshot; the re-take is `tests/control-commands-snapshot.py`, above), the flag fired, and the ten groups feature-list row 49 measured as tool-free (`browser`, `comp`, `export`, `link`, `modulator`, `rack`, `session`, `telemetry`, `warp`, `wasm`) are driven end to end by the registered ctest `ControlMcpGroupCoverage` — nine against the real binary, `wasm.` excused by a both-directions `--compiled-out` flag only in the builds whose configuration degrades the sandbox to OFF (`tests/CMakeLists.txt` passes the flag when `WANT_WASM` is off, which is what `Wasmtime_LIBRARY-NOTFOUND` produces). *Corrected 2026-09-19 (task 691): the clause "no build on this machine compiles the sandbox in" no longer holds — `zene-030/build` is configured `WANT_WASM='ON'` and its binary registers all eight `wasm.*` ids, so a run of this check against **that** build must drive the group for real and gets no flag.* A wasm-enabled build gets no flag and must drive the group for real; the bridge's own half of that case is `tools/mcp-zene-control/tests/test_declared_surface.py`. **The limit, stated plainly:** nothing checks the surface of an offline copy *while it is being served* with no instance up, and a copy can still be older than the tree it ships with — it is now loudly stale, not silently short.

- *(Updated 2026-09-30.)* **An effect's context menu now has Hosting**: a line with the device's state from
  `oop.get_state`, Run in a separate process (`oop.set_mode`, offered only where the family has a client and the
  plugin can switch - otherwise disabled, saying so) and Restart its process (`oop.restart`)
  (`EffectHostingMenuTest`). It is still ONE family of fifteen, and an instrument's hosting has no interface yet.
  The bullet below is the pre-menu record.
- **Out-of-process hosting is socket-only, and it is ONE family of fifteen — added 2026-09-16 (feature row
  80, board card #670).** The engine can host a plugin in a client process and now reports which families it
  can do that for (`oop.list_families`) and what the client processes have done this session
  (`oop.get_state`); **there is no page, dialog, column or toolbar button for any of it** — the hosting
  choice was and remains a checkbox in the ZynAddSubFx instrument view, and *everything else* — the family
  table, the crash counts, the refusal, the restart, the mode of any device — is **drivable through
  `--control-socket`, not from the interface**. In this build **one** family (`zynaddsubfx`) has an
  out-of-process path to choose and one more (`vestige`/`vsteffect`, VST2) is always out of process because
  it has no in-process path at all; the other thirteen families the table classifies (CLAP effect and
  instrument, VST3 effect and instrument, LADSPA, LV2 effect and instrument, SF2, GIG, the three Carla
  modules) ship **no client executable in this build** and are refused, by name and with that reason, rather
  than quietly run in-process. Three further limits are stated rather than left to be discovered: a client
  that dies is **not restarted automatically** (a slot stays silent until `oop.restart` asks for it, and the
  path REFUSES after 3 deaths in one session until `oop.reset_crashes`); the isolation proof kills the client
  from outside rather than crashing a plugin by its own bug (the mechanism a real crash trips, with an
  external trigger — `docs/OUT-OF-PROCESS-BEYOND-ZYN.md` §4); and the client executable has to be part of the
  build for the family to be hostable at all, which is what the table is for. Full document:
  `docs/OUT-OF-PROCESS-BEYOND-ZYN.md`.

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
  *Corrected 2026-09-19 (task 691): the coverage figures above are a **claim about their own capture**, not
  about this tip, and they cannot be re-derived from this tree — no coverage build or `coverage-fork.info`
  exists in it (`find . -name 'coverage-fork.info'` returns nothing; the capture named above was taken at
  `3ef822eaf`). What *is* measurable here is the ledger the rate is taken over: at this tip
  `tests/fork-sources.txt` holds **662** non-comment entries in 1189 lines (`grep -v '^\s*#' tests/fork-sources.txt
  | grep -v '^\s*$' | wc -l`), against the 242 the capture commit's rate names — so the 87.21 % is quoted
  figure-for-figure from that capture and is **not** a statement about the 0.3.0 tree's coverage. Re-measuring it
  is `tests/run-coverage.sh build-coverage` with the pinned VST3 SDK and CLAP headers and
  `-DWANT_VST3_TEST_INSTRUMENT=ON`, as above.*
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
- *(Updated 2026-09-30.)* **Every control's context menu now has an "Automation mode" submenu** (Off,
  Read, Touch, Latch, Write, the current one checked) that runs `automation.mode_set` for that parameter
  (`src/gui/AutomationModeMenu.cpp`, held by `AutomationModeMenuTest` against a real instrument). It
  appears only on a control the command surface can name; the song's tempo and the master fader, which
  the socket cannot address as parameters, get none. The per-clip record flag that
  `automation.record_mode_set` sets is the one the automation clip's own "Set/clear record" menu item
  has always toggled (upstream's), so it is reachable from the interface too. The bullet below is the
  pre-menu record.
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
- **The default warp render changes pitch, and two optional modes keep it — added 2026-09-13, corrected
  2026-10-19 (Q050-148).** The warp engine attaches markers and lets a clip follow or lead the project tempo;
  how the resulting rate change is *rendered* is a per-clip choice (`warp.stretch`, `WarpStretchMode` in
  `include/WarpMarkers.h`), and this file's earlier sentence "Pitch-preserving stretch is not built" no longer
  describes the engine (it was true of the warp lane alone, and feature-list row 30 landed it):
  - **`resample` (the default)** hands the rate to `AudioResampler` (libsamplerate): the pitch moves with the
    rate, so a 2× warp is an octave up and a 0.5× warp an octave down. This is varispeed, and every clip that
    does not ask otherwise renders through it.
  - **`preserve_pitch`** routes the same rate through `AudioStretcher` (WSOLA): the clip lasts as long as the
    mapping says and the pitch stays where it was. Limits: no formant preservation, no transient detection,
    content below ~345 Hz cannot be aligned, linear-interpolation SRC only — the per-algorithm caveats are
    listed in full in `docs/PITCH-STRETCH.md` §5.
  - **`rubberband`** routes the rate through Rubber Band 3's R3 engine for tonal and polyphonic material. The
    library is an **optional dependency** (`WANT_RUBBERBAND`, default ON): a build without it **refuses** the
    mode, and a project that asks for it loads and renders through WSOLA; even with the library, a pass for
    which both of the clip's two stretcher voices are busy falls back to WSOLA for that pass. `warp.list`
    reports `stretch_algorithm` (`"rubberband"` only when the library actually rendered).
  The refusals are honest: the pitch-preserving modes (`preserve_pitch`, `rubberband`) are refused for a
  clip that renders linearly (no markers, no source tempo — there is no rate change to preserve the pitch
  across), and `rubberband` is refused by a build without the library.
  Verified in the tree: `docs/WARP.md` §0 is the warp lane's report — `WarpMarkers` is a child element of
  `<sampleclip>`, markers are pinned to source frames so a trim moves `sourceIn`/`sourceOut` and the markers stay
  on the audio, the map is monotonic and exact at every marker, with no markers it is the pre-warp arithmetic bit
  for bit, and a headless render puts a source whose transients are at 0/1/2/3 s at 0/0.5/1.0/1.5 s under a
  marker pair declaring 2× — while the base binary renders that project as if the `<warp>` element were absent.
  The lane `post-alpha/warp` is an ancestor of this tip. `docs/PITCH-STRETCH.md` is the stretch lane's report
  (WSOLA measured at 440/660 Hz on a 2× warp, and the Rubber Band render, fallback and voice pool); the pitch
  numbers are measured by the registered ctests `SampleClipStretchTest` and `SampleClipRubberBandTest`, and the
  command path is driven end to end by `tests/control-pitch-stretch-transcript.py`.
- **And there is no warp UI at all — added 2026-09-13.** The sentence above is true of the *engine*; the
  product can author markers only through the control surface. `warp.list` / `warp.add` / `warp.move` /
  `warp.remove` / `warp.set` are registered commands (argument and result schemas, A16 reversibility records)
  and a registered ctest plus a committed transcript cover them — but nothing in `src/gui/` draws a marker,
  drags one, snaps one to the grid or places the first one, so an interface-only user cannot warp a clip at
  all. Warp marker editing is drivable through the socket, not from the interface.
- **MPE: all three axes reach playback.** Per-note expression is captured from MPE input, stored on the note
  and editable; **pitch, pressure and timbre are all applied on playback** — pitch as a frequency ratio,
  pressure   and timbre as MIDI events on the note's own member channel (task #649). Since Q050-016 each axis can also
  carry a **time-varying event stream** (ordered `(tick, value)` points per note, saved as the optional
  `mpepitchpts` / `mpepressurepts` / `mpetimbrepts` attributes and played at each point's own offset through
  `note.expression_stream_set` / `note.expression_stream_get`); there is still no per-note expression
  *editor* — drawing and dragging the streams in a curve view is the dependent unit Q050-137, and the streams
  are drivable through the socket only.
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
  *(Updated 2026-09-29, R2.1: input monitoring IS implemented - `track.set_monitor off|auto|in`, reported by
  `track.get_state`. An audio track monitors its record route's input channel through its own devices and
  fader; `auto` passes while the track is armed and not playing a clip. An instrument track takes `off` or
  `in` only - `auto` is refused, because the MIDI thread cannot read the route's arm without a lock - and its
  default stays `in`, the old behaviour; the plan's "MIDI Auto" default is not taken for that reason. The
  monitored signal is one engine period late plus the device's own latency. R2.2: a take recorded into a
  record-armed clip is shifted back by the recording round trip (`record.get_state` `latency_frames`: the
  device's two latencies, one capture block and one engine output period) and lands on the playback it was
  performed against (RecordingLatencyTest: within one frame through the Dummy loopback). Only JACK reports
  real device latencies; ALSA, PulseAudio, PortAudio and SDL report 0, so on those the device term is
  missing and a take lands late by the device's own buffering. Clip recording itself had recorded nothing
  since #611 (BUGS_FOUND 11.8), and the routed recorder (`record.arm_track`) writes a file, not a clip, so
  it is not compensated.)*
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

- *(Corrected 2026-09-30.)* **The export dialog has a Dither and resampling group** (`makeExportQualityControls`,
  `src/gui/ExportQualityControls.cpp`) that runs `export.set_dither` and `export.set_src_quality`, so the "no
  interface" below is no longer true; the bullet is the record of when it was.
- **Export dither and the SRC quality have no interface — added 2026-09-13.** The engine is in and both are
  drivable through `--control-socket` (`export.get_settings`, `export.set_dither`, `export.set_src_quality`)
  and the CLI (`--dither`, `--src-quality`), but **neither is on any dialog**: drivable through the socket,
  not from the interface. The dither is OFF by default and the SRC quality defaults to the converter the
  engine has always used, so a render that asks for nothing is byte-for-byte what it was. It is implemented
  for **WAV only** — FLAC, OGG and MP3 do not take the dither yet — and 32-bit float is deliberately never
  dithered (a float format has no quantisation step to dither against). See `docs/EXPORT-SRC-DITHER.md`.
  *Added 2026-09-29 (`040/dither`, owner decision 13):* the dither is now a **mode** — `off`, `tpdf`
  or `noise_shaped` (`export.set_dither {mode}`, `--dither-mode`; the boolean `dither` / `--dither` still
  mean TPDF). **Noise shaping applies to 16-bit WAV only**: the 24-bit path hands quantisation to
  libsndfile, so a 24-bit `noise_shaped` render is plain TPDF, byte for byte (`ExportWavDitherTest`).
  Measured on a -60 dBFS tone: low-band error energy -8.4 dB and high-band +9.0 dB against flat TPDF
  (`ExportNoiseShaperTest`). *(Updated 2026-09-30: the export dialog now has a Render quality group -
Dither (off / TPDF / noise-shaped) and Resampling (linear / sinc fastest, medium, best) - set through
`export.set_dither` / `export.set_src_quality`; `ExportQualityControlsTest`.)* No RPDF, by decision.

- *(Updated 2026-10-19.)* **The Search view searches by tag and - with the Search file metadata
  toggle - by embedded metadata now** (Q050-075): typing `tag:drums` finds the files tagged
  `drums` alongside the other expression fields (`kind:`, `name:`, `path:`, `ext:`), and the
  toggle sends `probe: true` to `browser.query` so a search matches what the audio files
  themselves say - the embedded title/artist/album/comment/genre and the sample rate, channels,
  frames and duration - opening at most 200 files per search (the command's `probe_limit`,
  `LibraryMenusTest`). The whole-library index takes no `probe` (it stores no embedded
  metadata), so the toggle is disabled while **Whole library** is on. `browser.query` re-reads
  the tag store file before matching, so a `tag:` search reflects tags written by another
  instance or earlier in the session. The waveform peak cache and peak drawing stay socket-only
  (`browser.peaks`): the Search view draws no waveforms. The bullets below are the earlier
  records.
- *(Updated 2026-09-30.)* **A file's context menu in the browser now has a Tags submenu**: every tag the
  library knows, the file's own checked - check to add (`browser.tag.add`), uncheck to remove
  (`browser.tag.remove`) - and New tag... (`BrowserTagMenuTest`). Searching by tag and by metadata is in
  the Search view now (the bullet above); the waveform peak cache alone is still socket-only. The bullet
  below is the pre-menu record.
- **Browser tag/metadata search and the waveform peak cache have no interface — added 2026-09-13.**
  *(Superseded for tag and metadata search: both are in the Search view - `tag:` alongside the other
  expression fields, embedded and numeric metadata behind the Search file metadata toggle. Only the
  waveform peak cache and peak drawing are still socket-only; see the bullet above. What follows is the
  record as of its date.)*
  The browser's items can be queried by name, by tag and by what the audio file itself says it is (sample
  rate, channels, length and the embedded title/artist/album/comment/genre tags), and a file's waveform
  peaks are read through a bounded cache — all of it drivable through `--control-socket`
  (`browser.roots`, `browser.query`, `browser.tags`, `browser.peaks`, `browser.tag.add`,
  `browser.tag.remove`). **Nothing in the interface can draw a waveform**: the browser's filter box
  matches file names only, there is no tag column or tag editor, and the browser does not draw a
  waveform — the peaks are drivable through the socket, not from the interface. (The filter box and
  the missing tag column and tag editor are pre-menu and pre-search-view details of this record; tags
  are edited from a file's Tags submenu and searched from the Search view, as the bullets above say.)
  The tags are persisted in the
  user's config directory (`browser-tags.json`), not in the project file, so they are a property of the
  user's library rather than of a project; `docs/RELEASE-NOTES-v0.3.0-alpha.md` carries the same sentence.
- **Tempo and time-signature changes have no editor — added 2026-09-13.** The engine is in (a persisted,
  ordered set of tempo and time-signature events the timeline obeys, with the ticks-to-time conversion reading
  it — `docs/TEMPO-MAP.md`) and it is drivable through `--control-socket`
  (`transport.tempo_map_get` / `tempo_map_add` / `tempo_map_remove` / `tempo_map_clear` /
  `tempo_map_set_active`), but **nothing in `src/gui/` draws, edits or reads a tempo map**: drivable through
  the socket, not from the interface. *(Updated 2026-09-30: Edit ▸ Tempo Map... now lists, adds at the
  playhead, removes, clears and activates it through those commands - `window.tempo_map`, `TempoMapPanelTest`;
  the timeline still does not draw it, and an event is added at the song's tempo and signature, not edited
  in place.)* With an empty or inactive map the tempo is the single project value it
  has always been, so a project that never used one renders byte-for-byte what it did.
- *(Updated 2026-09-30.)* **Edit ▸ Tempo Map... now has Export MIDI... and Import MIDI...**, which run
  `interchange.smf_export` (the map as a conductor track, replacing the picked file) and
  `interchange.smf_import` (the map replaced by a file's tempo and time-signature events, a refusal leaving
  it as it was) (`TempoMapPanelTest`). Reading a file without importing it (`interchange.smf_read`) is still
  socket-only. The bullet below is the pre-panel record.
- **Standard MIDI File tempo-map interchange has no interface — added 2026-09-15.** The tempo map can be
  written as a conductor track in a Standard MIDI File another DAW reads, and a file's tempo and
  time-signature events can be read back and imported, through `--control-socket`
  (`interchange.smf_convention` / `smf_export` / `smf_read` / `smf_import`) — but **nothing in `src/gui/` writes
  or reads one**: drivable through the socket, not from the interface, and the File menu's "Export MIDI" is
  the pre-existing note export, neither changed by nor wired to these ids. `docs/SMF-INTERCHANGE.md` records
  the tick/PPQ and time-signature convention and the stated limits — events are steps, so no tempo curve is
  written, and only the conductor track is (no notes, clips or automation).
- *(Updated 2026-09-30.)* **File ▸ Import DAWproject... / Export DAWproject...** now run `dawproject.import` /
  `dawproject.export` through a file dialog. The bullet below is the pre-menu record.
- *(Updated 2026-10-09, Q050-001.)* **DAWproject import reads DEFLATE now, and lists everything it could not take.**
  The reader accepts STORE and DEFLATE ZIP entries (what Bitwig, Studio One and Cubase write) under a 512 MiB
  per-entry and 1 GiB total decompressed-size bound, and `dawproject.read` / `dawproject.import` return a
  `deviations` array (corrupt, CRC-failing, unsupported-method, encrypted/ZIP64, oversized and truncated-directory
  entries; files `project.xml` references that the container lacks); the File menu shows the same list after an
  import. Still not supported: encryption, ZIP64, spanned archives and any method other than STORE and DEFLATE
  (each is reported, not skipped silently). The writer still stores its entries. See `docs/DAWPROJECT-INTERCHANGE.md`.
- **DAWproject import / export has no interface — added 2026-09-15.** Tracks, clips, notes, the tempo map
  and mixer strips can be written as a DAWproject container another DAW reads, and a file can be read back
  and imported, through `--control-socket` (`dawproject.convention` / `dawproject.export` / `dawproject.read` /
  `dawproject.import`) — but **nothing in `src/gui/` writes or reads one**: drivable through the socket, not
  from the interface. `docs/DAWPROJECT-INTERCHANGE.md` records the format version (1.0), the eleven stated
  losses (audio clips, automation, device state, sends, fades, loop points, scenes, folder nesting, mixer
  routing and sharing) and the time convention (beats, 48 ticks per quarter).
- *(Updated 2026-10-07, WF-051.)* **Session sync is real Ableton Link now.** Wherever the build carries the
  vendored library (`WANT_ABLETON_LINK`, default ON; Ableton Link 4.1, GPL-2.0-or-later, with standalone asio
  1.38.2, BSL-1.0, in `src/3rdparty/ableton-link`), the arranger toolbar's **Ableton Link** toggle and the `link.*`
  commands join the same session as Ableton Live, Bitwig, Reason and every other Link app on the network: tempo
  both ways (a peer's tempo is not an undo step), the play head held on the session's beat grid while playing, and
  start/stop both ways with `link.set_start_stop_sync`. The toggle shows the peer count and the session tempo; the
  quantum and start/stop sync are in Project > Sync and the socket, not on the toolbar. What is still limited, each in
  `docs/LINK-SYNC.md` §5: peers are counted, not named (Link exposes a count); no test runs against Ableton Live
  itself - the proof is Zene against the reference library in-process and two Zene binaries over real Link; output
  latency is one engine period plus the device's reported latency, with no user offset; the tempo map is not
  followed while a Link session plays; the Song's tempo is a whole number, so a fractional session tempo is shown
  rounded (the play head runs at the exact rate); a start counts in to the session's phase (up to one quantum) and
  a seek lands on the session grid (within half a quantum); a MinGW cross build cannot compile Link and gets the
  zene-link-style fallback, and the Windows and macOS builds of the library were not built in the lane that added
  it. `ZENE_LINK_BACKEND=zene-link-style` selects the fallback, which is what Zene 0.3/0.4 speak. The two bullets
  below are the pre-Link record.
- *(Updated 2026-09-30.)* **The song editor's transport now has a Sync toggle** (`link.set_enabled`) whose
  text shows the peer count and the session tempo while enabled (`SessionSyncAction`,
  `SessionSyncActionTest`). The quantum, start/stop sync and the peer list are still socket-only. The
  bullet below is the pre-toggle record.
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
- *(Updated 2026-09-30.)* **An instrument or sample track's menu now has Freeze track / Unfreeze track and
  Bounce in place** (`freeze.track`, `freeze.unfreeze`, `bounce.in_place`; `TrackRenderMenuTest`). Freezing a
  region (`freeze.region`) is still socket-only. The bullet below is the pre-menu record.
- **Freeze and bounce-in-place have no interface — added 2026-09-13.** A track's own output (its devices,
  fader, pan and sends) can be rendered to a WAV and the track made to play that render instead of its clips —
  drivable through `--control-socket` (`bounce.in_place`, `freeze.track`, `freeze.region`, `freeze.unfreeze`),
  and the frozen state is saved with the project and one `control.undo` takes it off — but **nothing in
  `src/gui/` renders a track, marks it frozen or plays a take**: drivable through the socket, not from the
  interface. Stated limits: a **region** freeze cuts a clip that crosses the region's edges at those
  edges (named in `overlapping_clips`; fixed 2026-10-16, it used to sound twice) and mutes only the pieces inside - a
  MIDI note held across an edge can still ring on; the render is 44.1 kHz and the take is re-sampled to the session's device rate,
  so a frozen render is not bit-identical to the same track unfrozen; a tempo map is followed (fixed 2026-10-16); a frozen track's take bypasses the
  track's own device chain and fader (they are already baked into the render), so those controls are inert
  until `freeze.unfreeze`; and a take whose WAV has moved since it was frozen still reports `frozen: true`, is listed by
  `project.missing_assets` (`track` / `frozenAudio`, repointed by `project.relink`), is reported with
  `frozen_audio_ready: false` plus the ways out, lets the source play on, and can be taken off with `freeze.unfreeze`
  (one `control.undo` puts it back).
- *(Updated 2026-09-30.)* **The piano roll's note tools now have a Groove menu**: Extract groove from this clip...
  (at the roll's grid, `groove.extract`), Apply groove ▸ each pooled groove at a chosen strength (`groove.apply`),
  and Quantize with strength... (`groove.quantize`) - `GrooveMenuTest`. Renaming and removing grooves, and the
  random/humanise amounts, are still socket-only. The bullet below is the pre-menu record.
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

- *(Updated 2026-09-30.)* **File ▸ Render Presets** is rebuilt from `export.preset_list` each time it opens:
  Default Settings and one item per stored preset with the applied one checked (`export.preset_apply`), Save
  Preset... (name, sample rate, bit depth, stereo mode; `export.preset_add`, asking before a name is replaced),
  a Delete submenu (`export.preset_remove`, after a confirmation), and Render Song with Preset... - the
  interface's first route to `render.render`, which is what a preset governs (`RenderPresetMenuTest`).
  *(Updated 2026-10-16, Q050-009.)* The **export dialog now carries a Render preset picker**: Default
  Settings and one item per stored preset with the applied one selected; choosing one applies it
  (`export.preset_apply`) and moves the rate/bit-depth/stereo rows, and Save Preset... stores the rows
  the dialog holds (`export.preset_add`) (`ExportProjectDialogPresetTest`). **Navigate ▸ Range** gains
  Render Selection... (`render.render` with the range's exact start/end ticks) and Export Sections...
  (`render.sections`, one correctly named, bounded file per named section) (`RenderRangeMenuTest`,
  `ControlRenderSections`). An applied preset now also governs **`render.stems`**, which renders at the
  selected rate and bit depth instead of a fixed 44100 Hz. The bullet below is the pre-menu record.
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
- *(Updated 2026-09-30.)* **An instrument or sample track's menu now has Effect chain presets**: Save chain
  as preset... (a name, then `chain.save`; an existing name asks before it is replaced), one Apply item per
  stored preset (`chain.apply` - one `control.undo` puts the previous chain back) and a Delete submenu
  (`chain.remove`, after a confirmation) (`ChainPresetMenuTest`). Renaming (`chain.rename`) and a mixer
  channel's chain (`ch-<n>`) are still socket-only. The bullet below is the pre-menu record.
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
- **Control-file read size limits (cluster new-p9-81).** Device-state and preset inputs are limited
  to 64 MiB per file. `project.relink` and `project.collect_media` accept project documents up to
  256 MiB, matching the existing `DataFile` document-recovery bound; a larger project document is
  refused before either command rewrites it.
- *(Resolved 2026-10-20, Q050-147.)* **The song editor's transport has Punch in/out and its own punch
  ruler.** The Song Editor's ruler draws the region in a band apart from the loop brace, with a draggable
  handle at each end; the toolbar's Punch in/out button arms it and disarms it while keeping its bounds, and
  Clear punch range forgets them (`src/gui/editors/TimeLineWidget.cpp`, `src/gui/editors/SongEditor.cpp`).
  Arming and dragging never read or write the loop markers, and every edit goes through `transport.punch_set`
  / `transport.punch_clear`, the same commands an agent uses. The capture path consults the armed gate
  frame-accurately since R2.4 (`AudioEngine::renderNextPeriod`'s punch window, `PunchWindowTest`,
  `RecordingLatencyTest`). The bullet below is the superseded record.
- **Punch in/out was the region and its gate with no interface — added 2026-09-13, superseded 2026-10-20.**
  This bullet's two claims are historical: the audio-side capture gate IS wired (R2.4) and the interface now
  has the ruler band, handles and arm/clear controls described above. Kept only so an old review reference
  finds the correction here.
- *(Updated 2026-09-30.)* **File ▸ Recover Recordings** is rebuilt from `record.recovery_get_state` each time it
  opens: a count, and per interrupted take (named with its recoverable length) **Place on a New Sample Track** -
  `record.recovery_restore`, then the interface loads the recovered file into a clip at bar 1 of a new sample
  track, the import the command itself does not make - and **Dismiss** (`record.recovery_discard`: the journal
  goes, the audio file stays) (`RecordingRecoveryMenuTest`). There is still no prompt at start-up; the menu is
  where a user looks. The placement is an interface action, not a registry command, so `control.undo` does not
  take the new track back. The bullet below is the pre-menu record.
- **Recording crash recovery is journalling and recovery, not an import — added 2026-09-13.** A capture in
  progress is journalled to a side file beside its take (`<take>.rec-journal`), a clean stop retires it, and an
  abnormal exit leaves it — so the next start can find the interrupted take and hand the material back, drivable
  through `--control-socket` (`record.journal_begin` / `journal_update` / `journal_finish`,
  `record.recovery_get_state` / `recovery_restore` / `recovery_discard`) — *(Superseded by Q050-046, 2026-10-12: startup now offers
  interrupted takes when `record.recovery_get_state` lists any, and `record.recovery_place` puts a take on a new
  sample track as one undo step; `record.recovery_restore` still only resolves the offer.)* Originally **nothing in
  `src/gui/` offered a recovery prompt, and `record.recovery_restore` did not put the recovered take into the
  session**, handing back the material untouched (`audio_untouched: true`). The bound is stated rather
  than implied: **guaranteed recoverable is `min(frames the journal recorded, frames the take's file holds)**;
  NOT recoverable is the audio written after the journal's last update and up to **65536 frames** that were
  still in the recorder's ring buffer when the process died — audio that never reached a file. *(Superseded by
  Q050-045, 2026-10-09: the journal now lags by a tenth of a second, not one second; see the next bullet.)* `include/RecordingJournal.h` states it, `record.recovery_get_state` reports
  it per take, and `docs/RELEASE-NOTES-v0.3.0-alpha.md` records the test that measures it. Drivable through the
  socket, not from the interface.
- *(Updated 2026-10-09, Q050-045.)* **The recording crash-loss window is now at most 0.185 s at 48 kHz, down from
  more than a second.** The disk-writer thread (never the audio thread) commits the take every
  `recordingjournal::updateIntervalFrames()` = a tenth of a second of audio: it rewrites the WAV header's data size
  (libsndfile only wrote it on close, so a SIGKILLed take used to claim zero frames however much was on disk), syncs
  the file, then rewrites the journal. **The stated bound is `lossBoundFrames()` = one commit interval + one
  4096-frame writer batch = 8896 frames = 0.185 s at 48 kHz** (`record.recovery_get_state` carries it in `bound`
  and `journal_lag_bound_frames`). `RecordingCrashLossTest` measured it with a real SIGKILL of a recording child at
  ten controlled points: the worst recovered take was 4096 frames (0.085 s) short of what the ring had accepted, the
  best 512. **What it does not cover:** a *stalled* writer (a disk that stops answering) can still leave up to
  65536 ring frames (1.37 s) unwritten, and the first 0.1 s of a take has no commit yet, so a crash inside it
  recovers nothing; the tail of a take is only committed once a full interval has accumulated, so a pause in the
  signal does not shorten the window. **I/O cost, measured:** about 10 commits per second per armed track, mean
  3.7 ms each (header rewrite + fsync + journal temp-file rename), i.e. about 4 % of one disk-writer thread, none of
  it on the audio thread; the audio-thread capture path still makes zero allocations
  (`captureStaysAllocationFreeWhileTheWriterCommits`, the AllocationProbe pattern). The old window was not
  re-measured on the new test; its figure is the one-second interval the code carried.
- *(Updated 2026-09-30; corrected 2026-10-20, Q050-308.)* **Edit ▸ MIDI Clock** switches the master
  (**Send Clock**, `clock.master_set`) and the slave (**Follow External Clock**, and **Follow Its Tempo**
  while it follows, `clock.slave_set`), and a line under them re-reads `clock.get_state` each time the
  submenu opens - not sending/sending, not following, following with no lock, or locked at the measured BPM;
  a refused switch (a master with no MIDI client to send through) stays unchecked and the line names the
  refusal (`MidiClockMenuTest`). The SOURCE PORT is chosen from this menu as well: **Edit ▸ MIDI Clock ▸
  Clock Source** offers **Any Input** plus one entry per readable client port, which is `midi.device_list`'s
  own vocabulary (the next bullet states that picker's bounds). The DRIFT BOUND is not an Edit-menu field:
  it is one argument of the same `clock.slave_set` command, and its form is **Project tools ▸ Sync ▸ Follow
  MIDI Clock...** (the bullet after the next one). Neither the port nor the bound is socket-only. The bullet
  below is the pre-menu record.
- *(Added 2026-10-16, Q050-026.)* **Edit ▸ MIDI Clock ▸ Clock Source** chooses which input drives the follower,
  through the same `clock.slave_set` the socket uses: **Any Input** plus one entry per readable client port as
  `midi.device_list` spells them, rebuilt each time the submenu opens so a plugged-in device is offered; picking
  one changes the source only and leaves the follower's mode alone (`MidiClockMenuTest::theSourceSubmenuDrivesTheSameCommand`).
  **A chosen source is now load-bearing** where before it was stored and never read: clock messages from any
  OTHER port are dropped and counted in `clock.get_state`'s `slave.ignored`, and switching the source forgets the
  measurement so the device moved off cannot keep writing its tempo
  (`MidiClockSourceTest::onlyTheConfiguredSourceCanDriveTheFollower`). Three bounds are stated rather than
  implied. **(1) A client with one unnamed input cannot be narrowed.** ALSA Raw-MIDI, JACK, OSS, sndio and the
  dummy client open ONE device and their bytes carry no port identity, so they deliver an unnamed clock, which
  matches an unset source and nothing else: on such a client the submenu offers **Any Input** alone, and naming
  a source follows nothing — honestly (the state shows `locked:false` with `ignored` climbing), never silently.
  ALSA **sequencer** is the one backend that names its senders, so it is the one where a chosen source selects a
  device. **(2) The sender's name is resolved once per sender and cached for the life of the client**
  (`MidiAlsaSeq::run`), so a device that is renamed or renumbered while Zene runs keeps being followed until the
  client is reopened; the follower's lock then drops on its own after `lock_timeout_ms` rather than following a
  name that no longer exists. **(3) Only two backends deliver the clock family at all** — the ALSA sequencer and
  the raw parser (ALSA Raw-MIDI, JACK, OSS, sndio): on Windows (WinMM) and macOS (CoreMIDI) clients the engine
  builds note/control events only, so an incoming clock is not seen there and a chosen source is not what keeps
  it unseen.
- *(Added 2026-10-20, Q050-308.)* **Project tools ▸ Sync ▸ Follow MIDI Clock...** opens the `clock.slave_set`
  **form** - the same registered command an agent calls over `--control-socket` or MCP, so one implementation
  serves the menu, the form and the socket. Its fields are **enabled** and **follow_tempo** as switches,
  **source_port** as one of the same readable-port names (blank keeps the current choice; **Any Input** is the
  explicit empty one), and **drift_bound_ms** as an integer in whole milliseconds from **0** to **100**
  (`MidiClockTracker::MaxDriftBoundMs`) - how far one pulse interval may stray from the window's mean and still
  count as locked. `clock.get_state` reads both back as `slave.source_port` and `slave.drift_bound_ms`, so the
  form, the Edit menu's picker and an agent see the same numbers. One call is **one recorded undo step**:
  `clock.slave_set`'s class is `snapshot`, and the step restores the mode, the tempo-follow flag, the source
  and the drift bound (the tempo a FOLLOWING slave writes is a trajectory the inverse does not claim).
  Proofs: the Sync form route is `LibraryMenusTest::theSyncMenuOpensTheFollowClockForm`, the Edit-menu picker
  is `MidiClockMenuTest::theSourceSubmenuDrivesTheSameCommand`, and the fields, their bound and the read-back
  are `ControlClockCommands` (`clock.slave_set` accepts `source_port` and the bounded integer
  `drift_bound_ms`; a bound outside 0-100 or a source name longer than the engine can hold is a typed
  refusal). The drift-bound field is NOT in the Edit menu; only this form and the command carry it.
- *(Corrected 2026-10-20, Q050-308.)* **The clock has an interface now: Edit ▸ MIDI Clock and Project
  tools ▸ Sync.** The bullet below is the 2026-09-13 record of the engine-only release, kept for its bounds
  (the backend coverage, the tempo-error bound, MTC's absence) and NOT as a current claim about the
  interface: `src/gui/MidiClockMenu.cpp` (attached by `MainWindow.cpp`) offers **Send Clock**, **Follow
  External Clock**, **Follow Its Tempo**, **Clock Source** and a lock line, and Project tools ▸ Sync ▸
  **Send MIDI Clock...** / **Follow MIDI Clock...** open the registry forms for the same two commands.
  Drivable from the interface AND through `--control-socket`/MCP, over the same registered commands.
- **MIDI clock is the engine AND the socket — added 2026-09-13, engine half.** The DAW runs
  as a MIDI clock **master** (24 pulses to the quarter note, START/STOP/CONTINUE and a Song Position Pointer on
  the transport's own edges, emitted from the audio thread through the engine's existing MIDI output) and as a
  clock **slave** (it follows an incoming clock, measures its tempo over one quarter note of pulses, and writes
  that tempo to the song when told to follow) — drivable through `--control-socket` (`clock.get_state`,
  `clock.master_set`, `clock.slave_set`), with a registered ctest driving the real binary over the socket and a
  registered QTest proving the rate arithmetic. The interface half landed later (see the two bullets above:
  Edit ▸ MIDI Clock, and Project tools ▸ Sync for the forms); what this bullet records is the engine, and the
  bounds below are still the current ones. The bound is stated rather than implied:
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
  and the tree's ALSA Raw reader polls after a 5 ms sleep (`src/core/midi/MidiAlsaRaw.cpp`). Both halves are
  drivable from the interface and through the socket, over the same registered commands.

- *(Corrected 2026-09-30.)* **Edit ▸ Arm MIDI Capture and Capture MIDI** (`src/gui/MainWindow.cpp`,
  `midi.retro_capture_arm` / `midi.retro_capture_to_clip`; Ctrl+Shift+C captures onto the monitored track, R5.4)
  are the interface the bullet below says is missing. The 8192-event bound is unchanged.
- **Retrospective MIDI capture keeps the last 8192 events, not a recording, and has no interface — added
  2026-09-13.** Arming the mode makes the engine keep a rolling window of the MIDI it receives, so what you
  just played can be written into a new clip AFTER the fact — drivable through `--control-socket`
  (`midi.retro_capture_arm`, `midi.retro_capture_status`, `midi.retro_capture_to_clip`), with a registered
  ctest playing REAL MIDI into the running engine and recovering it — but **nothing in `src/gui/` shows the
  window, how long it is or what it holds**: there is no view of it and no "you played something" prompt
  *(2026-09-30: the Edit menu's Capture MIDI item now reads "Capture MIDI (N events, S s)" whenever the
  menu opens with something captured - the count and span, not the notes)*.
  *(Updated 2026-09-30, R5.4: the interface does arm and write it - Edit ▸ Arm MIDI Capture and Edit ▸
  Capture MIDI, Ctrl+Shift+C - and the capture lands on the monitored track, the one the MIDI input
  follows, when no clip is selected; it was "no keyboard shortcut, not from the interface".)*
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

- *(Updated 2026-10-06, MX-157, BUGS 11.34.)* **Hosted instrument latency.** A CLAP, VST3 or LV2 instrument
  now reports its plug-in's latency to delay compensation: the track's bus handle adds it to its own chain's
  (`AudioBusHandle::latencyFrames`, so `pdc.report`'s `track_inputs[].latency_frames` includes it), and the
  other tracks are delayed by it. The host reads it off the audio thread and publishes a lock-free value:
  CLAP after every (re)prepare (a CLAP plug-in may change its latency only inside `activate()`, and asks
  for that with `request_restart`, which the host's 100 ms poll answers), VST3 after every prepare and,
  on `restartComponent(kLatencyChanged)`, at the host's next 100 ms poll, LV2 from the plug-in's latency
  port on a 500 ms poll (the port is written as the plug-in runs, so a latent LV2 instrument reports 0
  until it has run a block and the poll has fired). Measured on rendered audio for all three formats by
  `InstrumentLatencyPdcTest` against the in-tree fixtures. What it does not do:
  - Between a plug-in's change and the next poll the alignment is the old one (up to 100 ms for CLAP and
    VST3, 500 ms for LV2); a change is a step, not a cross-fade, as every PDC change is here.
  - An instrument latency above the delay lines' capacity (`LatencyCompensation::MaxFrames`, 16384
    frames) is clamped like a chain's, and the published total says so, but no warning is logged for an
    instrument (the once-only warning lives in `EffectChain::refreshLatency`, which an instrument is not
    part of). *(Measured 2026-10-19, Q050-169: `InstrumentLatencyPdcTest::instrumentLatencyAboveTheCapacityIsClampedAndDiagnosed`
    reports 17000 frames, sees the reference delayed by only 16384 and `pdc.report`'s `delay_line_clamped`
    set, with no capacity warning.)*
  - Only the native CLAP, VST3 and LV2 instrument hosts report, so those three are the supported
    instrument hosts for delay compensation. Built-in instruments report 0 (none is latent). The
    out-of-process, VST2 (Vestige) and Carla paths are unsupported for delay compensation, not for
    hosting: they host and play, but report no instrument latency to PDC, so nothing is
    compensated for them.
  - Automation compensation (`AutomationLatency`) does not count an instrument's latency: an automated
    parameter of a latent instrument, or of an effect on its track's own chain, changes `latency` frames
    early against that instrument's audio. *(Measured 2026-10-19, Q050-169:
    `InstrumentLatencyPdcTest::automationCompensationDoesNotCountTheInstrumentsLatency` stamps both such
    clips at 0 while the channel the instrument feeds stamps at the full latency.)*
  - A channel fed by a latent instrument has a non-zero alignment point, so its rack's key zones do not
    route notes (`RackZoneFeed` routes only through a channel aligned at 0) - the existing zone rule, now
    met by latent instruments too.
  - Live playing through a latent instrument is late by its latency plus the alignment; nothing bypasses
    an instrument the way the low-latency mode bypasses latent effects. *(Measured 2026-10-19, Q050-169:
    `InstrumentLatencyPdcTest::lowLatencyModeDoesNotBypassALatentInstrument` arms `transport.low_latency`,
    sees a latent EFFECT stopped and the latent INSTRUMENT's reference still delayed by its latency.)*
- *(Updated 2026-09-30.)* **A mixer strip's tooltip now shows its delay compensation** - the latency its effects
  add and the point its input is aligned to, in frames and ms, and on the master the compensated total
  (`mixerLatencyText`, refreshed with the mixer's 500 ms sync; `MixerLatencyTextTest`). It is still not
  settable. The bullet below is the pre-display record.
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
- **The routing graph is readable, and the patcher group edits it; the Chain Canvas (Q050-087) draws and edits it where
  the engine allows — added 2026-09-14, extended 2026-09-15, canvas 2026-10-21.** The graph a
  signal is actually processed through (the effect chain's `RoutingGraph`: its nodes, connections, cached
  topological order and output node, plus a mixer channel's rack graph) is drivable through
  `--control-socket` with `routing.get_state`, and the mixer's routing is settable through `mixer.route_to` /
  `mixer.send_to` / `mixer.sidechain_to` / `mixer.route_remove` — and since Q050-087 the **Chain Canvas**
  (Rack > Chain Canvas...; `include/PatcherCanvas.h`) draws a chain's nodes and cables and edits them through the same
  commands (the canvas has no patch bay of its own and no node an agent cannot also add): the `patcher.*` group (added
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
  `tests/src/core/PatcherCommandsTest.cpp` and `tests/src/gui/PatcherCanvasTest.cpp` build to prove the edit. **On a chain
  of shipped devices the Chain Canvas therefore shows the cables read-only, with the reason, while adding, reordering
  and removing devices still work** (`docs/050-features/Q050-087.md`).
- **The patcher's edit is guarded, session-scoped and blocking — added 2026-09-15.** `patcher.get_state` /
  `patcher.set_wiring` (feature row 69) let an agent re-wire a target's effect chain through
  `--control-socket`: the wiring is addressed by ROLE (`"input"`, `"effect:<index>"`), it is re-applied by
  every derived rebuild, and `control.undo` restores the previous wiring as ONE step (a chain that was on its
  derived wiring comes back AS the derivation). Three bounds, stated rather than hidden: (1) **the canvas
  edits cables only on a chain the engine routes through its graph** (see the bullet above); (2) **the wiring is session state**
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
- *(Updated 2026-09-30.)* **Edit ▸ Plugin Quarantine** is rebuilt from `plugin.scan_cache_get_state` each
  time it opens: a line with the cache's file and quarantine counts, one **Release** item per quarantined
  file (its tooltip is the path and the reason; a file that is gone says so), **Quarantine a Plugin
  File...** (a picker, then `plugin.scan_cache_quarantine_add`) and **Rescan Plugins** (`plugin.rescan`),
  which applies an edit (`PluginQuarantineMenuTest`). *Corrected 2026-10-15 (Q050-014):* the cache's
  per-file records are no longer socket-only — **Project tools ▸ Plug-in Modules...** shows each record's
  path, status, name and error and offers **Quarantine...** / **Release** / **Rescan**
  (`CommandLists::pluginModules`, over `plugin.scan_cache_list`), while `plugin.scan_cache_list` /
  `_lookup` remain the agent route. The bullet below is the pre-menu record.
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
- *(Updated 2026-09-30.)* **Help ▸ Crash Reports** arms and disarms the reporter (**Report Crashes**,
  `crash.enable` / `crash.disable` - disarming deletes nothing), names its state on a line re-read from
  `crash.list_reports` each time the submenu opens (armed or not; no report, one not yet offered, or one
  already offered), opens the report directory (**Show Report Folder**), acknowledges a pending report
  (**Stop Offering the Report**, the file stays) and deletes it after a confirmation (**Delete the
  Report...**, `crash.discard_report`) (`CrashReportMenuTest`, POSIX; the module is a no-op on Windows).
  There is still no way to SEND a report, by design: this build has no upload. The bullet below is the
  pre-menu record.
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
- *(Updated 2026-10-12, Q050-049.)* **The crash marker now names the plug-in that was active, and the next start
  offers to quarantine exactly that file.** Before this, the marker recorded only the pid, time and project, so
  nothing could say which third-party plug-in killed the run. While `PluginFactory` dlopens a module (phase
  `scan`) or `Plugin::instantiate()` runs a plug-in's own entry point (phase `instantiate`), the marker carries
  `active_phase` / `active_format` / `active_plugin` / `active_file` through bounded atomic writes on the loading
  thread, never the audio thread. The I/O cost has not been benchmarked; a scope for an engine-owned module does
  not write the marker, and nested scopes restore their enclosing entry. When those writes succeed, the next
  start reads the suspect back (`safestart::crashSuspect()`): the GUI asks **once**
  ("quarantine this plug-in?", `gui::offerCrashQuarantine`), and an unattended run prints one stderr line;
  `safestart.get_state` reports `crash_suspect` and `offer.quarantine` (the registered
  `plugin.scan_cache_quarantine_add` call with its arguments). Yes goes through that command, so **Edit ▸ Plugin
  Quarantine ▸ Release** frees the entry like any other (`PluginQuarantineMenuTest`; the crash and the marker are
  proven by a forked child in `SafeStartTest`). Limits: only the in-process `lmms-module` load paths are
  instrumented (LADSPA's constructor-time scan, the out-of-process hosts and VST3/CLAP editors are not), a crash
  outside those windows still yields "the previous run did not exit cleanly" with no suspect, and the suspect is
  read at launch, so a session that exits cleanly afterwards drops it.

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
  **74 → 72 commands**, the diff being exactly `telemetry.consent` and `telemetry.status`. *Corrected
  2026-09-19 (task 691): the diff is still exactly those two ids — re-measured off the built binary, the
  `telemetry` group holds `telemetry.consent` and `telemetry.status` and nothing else — but the base is this
  tip's registry, not 74: `ZENE_TELEMETRY=OFF` takes the reference configuration's **340 commands to 338** and
  the release configuration's **332 to 330**.* (An unstripped
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

- *(Updated 2026-09-30.)* A sample clip's context menu now runs `clip.trim`: **Trim start to playhead / Trim end to playhead** (`ClipEditsMenuTest`). A drag with a slip modifier is still missing. The line below is the earlier record.
- **UI absence — one line: `clip.trim` is drivable through the socket, not from the interface.**
  The song editor does have the gesture the command implements — the left-edge drag in
  `src/gui/clips/ClipView.cpp`, whose three-line rule (position, length and source offset move
  together) `clip.trim` reproduces exactly — but no menu item, action or keybinding reaches the
  *command*: the GUI path is the drag, an agent's path is the id. Nothing in `src/gui/` invokes
  `clip.trim`.
- *(Updated 2026-09-30.)* A sample clip's context menu now runs `clip.slip`: **Slip content -1 beat / +1 beat** (`ClipEditsMenuTest`). A drag with a slip modifier is still missing. The line below is the earlier record.
- **UI absence — one line: `clip.slip` is drivable through the socket, not from the interface.**
  There is no slip gesture anywhere in the product: a case-insensitive grep for `slip` over `src/`
  and `include/` returns seven hits and every one of them is a comment or a doc-string. The
  nearest existing concept is the comp take's `srcpos`, which is applied to audio takes since 0.5.0 E2
  (`docs/COMPING.md`). `clip.slip` is the first implementation of the
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
  `clip.slip`'s tick offset. Since 0.5.0 (F3, ED-021) `clip.split frames` and `clip.trim
  start_frames/end_frames/zero_crossing` are that frame-domain edit: they author the window through
  its owner and reset the sub-tick `lead` on absence (see the wave 2 section at the end).
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
−14 LUFS-I streaming **convention** with a tolerance this project chose and states. **`mastering.run` does not
carry the dead-surface bound the render family carries** (`render.stems` above): it is ASYNCHRONOUS — the verb
starts the render child and answers with an acknowledgement in milliseconds (`state: "running"`, naming
`mastering.get_state` as the verb to poll), so the control surface — `control.ping` included — keeps answering
while a mastering run is in flight, however long the render takes. That was DEFECT-D3: the 2026-09-24
certification sweep measured the blocking reply twice as a socket-protocol violation ("hang >30s waiting for
mastering.run"), on a session whose child had rendered for 19.27 s of its own accord. **The run's own document
(therefore the candidate measurements, the files and the counts) is read back from `mastering.get_state`'s
`last_run` once its `state` reads `completed`** — and a run that fails reports `state: "failed"` with the
child's own reason in `error` there, because the ACK has already left the socket and nothing else can report
it. A **second** `mastering.run` while one is in flight is refused `busy` rather than queued behind a render
it cannot see, and because the inverse is recorded when the run **completes**, `control.undo` must not be
asked to take a run back until `get_state` says it completed. The run's own scratch (the serialised session,
the child's report and its stderr) lives in a temporary directory the run deletes, so it is **not** left in
the shared temp directory — the pre-fix shape wrote `/tmp/zene-master-<pid>-<ms>.mmp` and left it behind when
a run was cut short. The run is a **child
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

- *(Updated 2026-09-30.)* **The piano roll's note-tools menu now has Randomize velocities..., Humanize
  timing... and Scale velocities...** (`note.randomize` / `note.velocity_scale` on the selected notes, or
  the clip when none is selected; `NoteTransformActionsTest`), and slide notes have had a draw tool (Note
  tools ▸ Slide notes, Shift+L) since the slide work. *(Updated 2026-10-16, Q050-016 round 2.)* The same
  menu now has the MPE streams' GUI path beside them: **MPE pitch ramp...** (asks the bend at the note's
  end in cents and writes a `{0:0, len:value}` pitch stream through the shared `note.expression_stream_set`
  command, keeping the other axes) and **Clear MPE expression streams** (empties every axis of the notes
  that carry a stream, skipping the rest so no empty undo step is pushed) — one undo step per note either
  way (`NoteTransformActionsTest`: GUI trigger, registry effect, `control.undo`). Still socket-only: the seed, velocity offset and
  the typed transpose. The bullet below is the earlier record.
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
- *(Updated 2026-09-30.)* **Edit ▸ MPE Input now throws the switch** (`device.mpe_set`, the check mark
  re-read from `device.mpe_get_state` each time the menu opens; `MpeInputActionTest`). The per-note
  expression editor is still absent. The bullet below is the pre-menu record.
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
## Offline stem separation (`stem.*`, feature row 26) — built in, local, socket-only, and it needs the model

The engine (HTDemucs over ONNX Runtime, one job at a time) was already in the tree with five registered
tests; what was missing was any way to drive it. Eight ids now do — `stem.get_state`, `stem.job_start`,
`stem.job_status`, `stem.job_result`, `stem.job_cancel`, `stem.model_get_state`, `stem.model_download` and
`stem.tracks_create` — and this is the honest half. **It is local and offline: the separation runs on this
machine and nothing is uploaded.** The only network access is the one-time model fetch described below; with
the model already in the model store — or with `LMMS_STEM_OFFLINE` / an explicit `LMMS_STEM_MODEL` path — the
feature works with no network at all.

- **The UI and the agent surface are the SAME registered commands.** The sample clip's **"Split to
  stems"** context action (`src/gui/clips/SampleClipView.cpp` → `StemSplitController::splitClipToStems`)
  now starts its job through `stem.job_start`, polls `stem.job_status`, cancels through `stem.job_cancel`
  and materialises the four tracks through `stem.tracks_create` — the identical `ControlRegistry`
  commands an agent calls (a click and a command take one path, the whole point of SPEC A11). The action
  still exists only when a display does, and what remains absent from the interface is narrower: nothing
  shows an *agent's* job, its progress or its error, nothing lists or fetches models, and no menu item,
  toolbar button or keybinding reaches a `stem.*` id — the only route is the clip context menu.
- **The feature is ON by default since owner decision 14.** `WANT_STEM_SPLIT` defaults to ON. A
  build configured with it OFF compiles none of this engine and registers none of these ids. The A16
  rows are guarded by the same macro, so the contract table and the registry cannot disagree. Without
  the ONNX Runtime SDK the external-process backend is used, which needs `python3` with `onnxruntime`
  and the installed `stem_split_cli.py`; without those, every job is refused and says why.
- **The model is fetched on first use, never bundled.** The default spec is pinned (HTDemucs fp16,
  MIT, one commit of the model card's repository, SHA-256 and size; doc/STEM-SPLIT.md). A job that
  starts with the model absent fetches the 166 MB file on the job's worker first, so the surface keeps
  answering. `LMMS_STEM_OFFLINE` turns this off, and so does an explicit `LMMS_STEM_MODEL` path.
  `stem.get_state` reports `fetch_on_first_use`. The GUI's "Split to stems" action asks first
  (name, size, licence, model card, destination) and then fetches the model through the same job
  worker. Its progress dialog shows only the separation's percentage, not the transfer's.
- **No live mode is claimed, and none exists.** HTDemucs is a hybrid transformer that needs the whole
  7.8 s segment (343980 frames at 44100 Hz) as context, so no chunk size fits an audio buffer: this is an
  **offline job only**. `stem.get_state` reports `realtime: false` and the lookahead; the group offers no
  monitoring, no streaming and no realtime variant.
- **44.1 kHz output; other input rates are resampled.** The stems always come back at the model's
  own 44.1 kHz, but a source at another rate (48 kHz, 96 kHz, ...) is **resampled to it offline**
  before separation (the engine's best sinc converter, 0.5.0 E27) instead of being refused — the
  rejection-by-rate behaviour SPEC-stem-split.md OQ-1 left open was closed in 0.5.0. The job reports
  the model's `sample_rate`, so a caller sees 44100 regardless of its source's rate. A file
  `render.render` wrote is already in that format, which is why "bounce the session, then split the
  bounce" is the direct flow (a resample is then a no-op).
- **The source the socket names is a file.** `stem.job_start` names a platform-native absolute path (a
  drive or UNC path on Windows) and the mix is decoded from it; the socket has no way to hand the engine a
  live clip or a bus. The GUI action spans the
  gap for in-memory clips (recorded, pasted, generated) by writing the clip's own audio to a temporary WAV
  and passing that path to the same command, so an unsaved clip still splits.
- **`stem.job_result` writes files; `stem.tracks_create` builds tracks.** `stem.job_result` produces
  `<stem>.wav` (drums, bass, other, vocals) as float32 RIFF/WAVE with a sha256 per file and touches no
  project state. `stem.tracks_create` takes a completed job's four stems and adds them to the song as four
  `SampleTrack`s (one clip each, at the caller's position) — the user-facing result, and the same command
  the GUI's "Split to stems" action runs. It is the group's one editing verb and is ONE undo step: a single
  `control.undo` (or Ctrl+Z) removes all four tracks together.
- **Jobs are not project state.** They live in the instance's memory: a reload or a restart loses the id,
  its progress and its stems. The job, model and write verbs are `not_mutating` and leave no transaction;
  the one editing verb, `stem.tracks_create`, records a `true_inverse` transaction whose inverse removes
  all four tracks — a job itself is still not a document, and a written stem is an output.
- **`stem.model_download` carries a DECLARED BOUND.** A performing transfer (an explicitly pinned spec)
  runs on the control surface's own thread, so the surface does not answer — `control.ping` included —
  until it finishes or fails. This is the **same** defect `docs/RENDER-CHILD-WAIT.md:120-126` records for
  `render.render` and the bounce/freeze commands, and the deferred-reply fix that document designs is
  **not built in this release**. A stem job does not need this verb, because it fetches on its own
  worker. The transfer's performing path is **not exercised by any registered proof**: the proof runs
  with `LMMS_STEM_OFFLINE` set, so CI never fetches 166 MB. The proof that IS registered is the ctest `ControlStemCommands`
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
## Linked / smart clips (`clip.link_*`, feature row 6)

`clip.link_create`, `clip.link_remove`, `clip.link_make_unique`, `clip.link_get_state` and
`clip.link_sync` are available through the control surface. The arranger also shows a small chain marker on
linked MIDI clips, and each marker has an accessible name. The MIDI clip context menu offers **Make Unique**.

**Shared state:** notes, name and colour. A note edit in any linked instance updates every member's notes. The
link relation remains a persisted group id plus a write-through mirror, so it survives save/reload without a
shared pointer or load-order relinking pass.

**Per-instance state:** start, length, loop start/length/offset, fades, gain and mute. `clip.trim`, `clip.resize`,
arranger edge drags and VCA trims change only the instance being edited. A split cuts only that instance; its two
pieces stay linked and show the shared notes through their independent windows. Other linked instances remain
untouched. `clip.link_make_unique` gives one instance its own copy of the current content and is reversible with
one undo.

**Bound:** an audio clip cannot be a member. A link group's content channel is a MIDI note list, so
`clip.link_create` refuses a `SampleClip` typed (`invalid_args`). The design, edit-kind table and proof details are
in `docs/LINKED-CLIPS.md`; registered proof is in `ClipLinkTest`, `LinkedClipGroupEditsTest` and
`MidiClipLinkedEditTest`.

## Clip crossfades — clamp by default (owner ruling 2026-10-09)

Arranger drops and clip.crossfade default to edge-anchored crossfades with zero offsets. The previous V-shaped
container fade is an explicit gesture: hold Alt while dropping a clip onto another clip on the same track, or call
clip.crossfade with mode "container". Container mode uses the inner clip's authored fade-in and fade-out lengths;
the ramps must fit inside the inner clip, and the container stays silent if those ramps leave a gap. A missing or
non-fitting ramp is skipped in the arranger and refused by the command. Existing offset fades in project files
still load and render under the previous adjacent-pair rule. Full behavior and the additive gap attribute are
documented in docs/CLIP-CROSSFADE.md; the registered render and allocation checks are in ClipOffsetFadeTest and
ClipOffsetFadePairTest.

**CODE-6/CODE-7/CODE-8 — the change-plan code rows: what each of them does NOT do.** Four absences,
each one line, because each is a bound rather than a bug:
- *(Updated 2026-09-30.)* **File ▸ Script Memory Budget...** names the current cap and sets a new one in MiB
  (`script.set_memory_budget`; out-of-range values refused, one `control.undo` restores the previous cap)
  (`ScriptBudgetActionTest`). Still missing: a live readout of a running script's Lua bytes. The bullet below is
  the pre-menu record.
- **The Lua memory budget has an interface for the CAP, not for live bytes.** `script.set_memory_budget`
  sets the cap and `script.run` reports what a run measured against it; File > Script Memory Budget...
  (the bullet above) is where a user sets it, and the live-code panel's help text states the default and
  the accepted range. What is still missing is a **live readout of a run's Lua bytes**. The default is 64 MiB
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
project file or `warp.stretch` are the only two ways to author it. **A third mode, `rubberband`, routes the
same rate through Rubber Band 3's R3 engine** (`warp.stretch` with `mode="rubberband"`, `<warp
stretch="rubberband">`; owner decision 12) for tonal and polyphonic material where WSOLA's search runs out.
Its caveats, all structural: the library is an **optional dependency** (`WANT_RUBBERBAND`, default ON) and a
build without it **refuses** the mode while a project that asks for it still loads and renders through WSOLA
(`warp.list` then reads `stretch_algorithm:"wsola"`); a clip in the mode owns **two** stretcher voices, and a
pass for which both are busy (a third concurrent pass, or a re-trigger inside the ~4 ms recycle window)
renders through **WSOLA** for that pass — counted in `RubberBandPool::misses()`, never silence; and a
transposed Rubber Band clip renders through the stretcher even without a rate change (the transpose is the
pitch it renders, ED-038). The WSOLA caveats above apply verbatim to every fallback render.

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

**The chord track, its detection and its generators are drivable; the chord track itself has no
editor.** `chord.*` is the only way to write the track: there is no chord ruler, no chord-edit
popover and no chord-track editor (the arranger draws the track as a ruler lane - docs/CHORD-TRACK.md
§6a - but nothing edits it there). Since Q050-019 the Edit > Progression window
(`window.progression`, `gui::ChordProgressionPanel`) browses the catalogue and writes a NAMED
progression into a MIDI clip from the interface - but it writes NOTES into a clip through
`chord.progression_generate`, exactly as the headless workflow does, and it leaves the chord track
untouched (the panel's own bounds are in `docs/CHORD-TRACK.md` §6b). What IS there is the entity (a chord
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

*(Updated 2026-09-30: a MIDI-driven control's context menu - its "Connected to ..." submenu - now has
Soft takeover and LED feedback toggles through `controller.soft_takeover` / `controller.feedback`
(`MidiControllerMenuTest`), and Edit ▸ Controller Templates applies, saves and deletes the mapping
templates through `controller.template_*` (`ControllerTemplateMenuTest`).)* The record below predates that.

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
purpose, one line each: channel **pan** (a `MixerChannel` pan exists since 2026-09-29 and
`mixer.set_pan` drives it, but no Lua binding reaches it yet), channel/effect **removal** (no inverse exists for a deleted channel;
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
* *(Updated 2026-09-30, R1.2.)* The mode is **on by default for a clip carrying a curve** (Linear or
  CubicHermite progression) and off for a Discrete one, whose step has no curve inside a block; an explicit
  `automation.ramp_set` wins either way. Only an explicit setting is saved, so every project file keeps its
  bytes — but an OLD project's curve clips now render at sample precision where they used to carry the
  whole-block lag. Clips written through `automation.add_point` are Discrete, so they are unaffected until
  their progression is changed.

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

- *(Updated 2026-09-30.)* **Edit ▸ MIDI Controller Reconnect** arms and disarms it (`midi.reconnect_arm`) and
  names what it holds - "(N bound, M live, K lost)" from `midi.reconnect_status` - each time the menu opens
  (`MidiReconnectActionTest`). Binding a port (`midi.reconnect_set`) is still socket-only. The bullet below is
  the pre-menu record.
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

## An interrupted save is recovered the next time you open the file — and says so (ARCH-4 S5)

A save writes `<project>.new` first and only then, atomically, replaces `<project>`. If the app dies between
those two steps (a crash, a kill, a power cut) the completed `.new` used to be orphaned forever — the save you
asked for sat beside your project and was never opened. The next time you open that file this build now
**adopts** a complete `<project>.new` (it is the newer save, so it replaces the file) or **deletes** a truncated
one, and prints exactly one report line to the log/stderr either way — `Recovered interrupted save: … adopted
as …` or `Discarded incomplete … left by an interrupted save of …`. This is a **log line, not a dialog**: a
graphical user only sees it if they look at the console/log, there is no prompt asking whether to adopt, and
adopting **overwrites** the file that was already there — so a `.new` you meant to throw away is recovered, not
ignored. (Adds the `fsync`-before-rename durability step on POSIX; on Windows the file-buffer flush is left to
`QSaveFile::commit()` and no explicit `FlushFileBuffers` is issued, so a power cut there is not covered by this
guarantee. The `complete="1"` index marker the same spec section sketches is *not* implemented — it belongs to
`<z:index>`/`DocumentIndex`, which this slice is scoped out of.)

## The destructive waveform editor's first slice (`sample.*`, board card #706) — its menus and its bounds

Five commands exist — `sample.generate` (tone / white / pink / silence into a new audio clip),
`sample.amplify`, `sample.normalize`, `sample.reverse` and `sample.fade` (linear, in or out) — and
**every one of them has an interface entry point as well as the control socket**: a sample track's
**More** menu (the track header's and the Inspector's) offers `sample.generate` as **Insert Test
Signal...**, which opens the command's own form with that track fixed and position `0`, length `192`
and kind `tone` pre-filled; a sample clip's **Process** menu offers `sample.normalize`, `sample.amplify`,
`sample.reverse` and `sample.fade` (in / out). The MCP bridge and the control socket are the same
command, not a second implementation. There is still **no waveform view** that shows the result in
place: a generated clip is drawn by the ordinary sample-clip view, and the bakers' feedback is the
ordinary undo journal and clip repaint. **Placement beyond those two menus is an open owner decision
for the UI phase** — mixer-slot vs clip editor vs window — and this lane did not take it (board card
#706, question 4; `design/specs/DECISION-706-DESTRUCTION-MODEL.md` records the same defer). The
bounds around them, stated rather than left to be discovered: every operator transforms
the clip's **whole source** — there is no range/selection model, a clip playing a trimmed
`srcin/srcout` window is REFUSED with the typed error naming that limit, and a fade therefore always
spans the whole clip and has only the linear curve (`clip.set_fade` remains the non-baking window
fade; `clip.set_gain` remains the non-baking gain); `sample.amplify` applies **no limiter** (the
float pipeline keeps values past 0 dBFS); each step's undo bound is the ENGINE's own — `min(100
steps, 16 MiB)` of retained checkpoints (`docs/UNDO-BOUNDS.md`), against which a baked buffer's
serialized bytes count — not a private stack; and the deferred verbs register **no id at all** in this
slice: noise reduction, pitch/tempo change and chirp are named here as not started, not stubbed.

## Scheduled Lua evaluation (livecode.*) - board card #708 - 2026-09-22

The schedule clock (`livecode.schedule` / `livecode.unschedule` / `livecode.get_state`,
`src/core/ScriptClock.cpp`) evaluates a script on bar, beat or transport boundaries. Its bounds, each
measured by `ScriptClockTest` or the `ControlLivecodeCommands` transcript:

- **Fires up to one 25 ms poll after its boundary** (`ScriptClock::PollMs`), not sample-accurately:
  the transport is OBSERVED from the control thread (the accessors `transport.get_state` already
  reads), because the spec puts every Lua evaluation off the audio thread (SPEC-lua-api-v0 section 4)
  and the audio thread may not post events. The bar-line check in the transcript bounds the overshoot
  at 12 ticks.
- **Bar and beat hooks fire only while the transport PLAYS; the transport hook fires play/stop edges.**
  There is no wall-clock "idle" evaluation while stopped - a stopped clock is frozen (the transcript
  asserts it), and there is no seek event: a seek or meter change silently re-anchors the grid
  (`regrids` in `livecode.get_state`), and a loop wrap is a regrid, not an edge.
- **A fresh `lua_State` per fire** - the v0 invariant `ScriptEngine` has always kept - so globals do
  NOT survive between bars: no Lua-side counter or pattern state persists across fires. "Loaded once"
  means the SOURCE is captured and compile-checked once (`setSchedule`'s parse) and re-evaluated per
  boundary; a syntax error is refused at schedule time, never silently per bar.
- **The source is snapshotted at schedule time**: editing the file behind a `path` changes nothing
  until `livecode.schedule` runs again for that id (the live edit is re-scheduling it - replace in
  place, one slot).
- **Every fire is budget-bounded** (per-fire instruction budget; `budget: 0` inherits the engine's
  5,000,000): a runaway hits `instruction budget exceeded`, recorded typed in `last_error` /
  `budget_exceeded` - and a schedule that ALWAYS overruns still costs its control thread that budget
  on every bar (bounded, not free). The audio thread is never on this path, which the transcript's
  negative control proves by reading the play head advancing across the overrun.
- **Two hard caps, both stated in the live-code panel.** A single source is capped at 65,536 UTF-8 bytes
  (`ScriptClock::SourceByteCap`) and the registry holds at most 64 schedules
  (`ScriptClock::MaxScheduleCount`); scheduling past either is a typed refusal and changes nothing. The
  caps exist because each schedule's definition is also carried by its transaction record (before-state
  plus inverse) inside `control::MaxTransactionBytes`.
- **Schedules are process-local runtime state.** `ScriptClock`'s registry is in memory and no project
  save/load path carries it: a schedule does not survive a restart and is not in the `.mmp`/`.mmpz`, so
  project-file compatibility is untouched (nothing was added to the format).
- **The interface is Edit > Live Code (`window.livecode`).** The panel edits the source, schedules and
  cancels through the same `livecode.*` commands an agent uses, shows a compile refusal without dropping
  the active schedule, loads a schedule's captured text through `livecode.get_source` (a read-only
  companion of `get_state`, which reports source BYTES only so a poll never carries every definition),
  and its help text states the fresh-Lua-state rule and every bound above. Runtime schedules are still
  not listed in the project's missing-devices or task surfaces - they are not project data.
- **More than four crossings in one observation coalesce** (`ScriptClockGrid::CrossCap`): a forward
  seek or a control thread blocked inside a long `script.run` re-anchors the grid and counts the
  dropped bars as `coalesced` rather than firing stale bars late.
- **Beat means the quarter note** (four to a bar, `ticksPerBar / 4`): odd-metre eighth-note beats are
  not separately detected. No scheduled device definitions exist - a persistent Lua interpreter that
  defines one stays OQ-1 (`docs/LUA-SCRIPT-DEVICES-DESIGN.md`).

## The project file remembers who changed it only as `human` or `agent` (ARCH-4 S6)

- **The record is a session kind, never a person.** Since ARCH-4 slice S6 a save writes an
  append-only `<z:provenance>` section when this session recorded changes through the control
  surface: one `<z:change seq at actor instance command target before after/>` per undo step,
  bounded like the undo journal. `actor` is `agent` while a `--control-socket` client is being
  dispatched to and `human` otherwise, and `instance` is the same 12-character per-process
  UUID fragment (eight hex digits, a dash, three more) `<head writer>` wears - deliberately
  no name, path or address, because a project file is shared.
  Attribution is best-effort around the socket dispatch: automation that does not arrive on the
  socket records as `human` until its own path claims `agent`.
- **Nothing reads the section back yet outside the app and its test.** `ProvenanceSectionTest`
  proves the round trip (save, fresh load, digests intact, second save appends), but no control
  command, view or tool surfaces the history - the section is durable state waiting for a reader,
  not a feature you can browse. The `after` digest is taken over the command's post-change report;
  for state-shaped commands (`clip.move`, `transport.punch_set`, ...) that report is the
  after-state, and where a command reports something else the digest commits to the report rather
  than to a state that was never recorded.

## The cycle-permitted submode does NOT make a loop a normal path (feedback.*, board card #709)

`feedback.enable` lets a send close a loop (`bus -> effect -> same bus`), and every point of use
says that **compensation is SUSPENDED for the loop** - carrying REAPER's warning verbatim:
"feedback routing can in some instances be useful, but can risk damaging audio equipment"
(REAPER User Guide, main changes 6.66-6.70). What the submode does NOT do:

- **No per-send compensation estimate while suspended.** A loop-closing send's compensation is
  fixed at 0 frames and the alignment solve does not traverse it (a cyclic path has no single
  latency to compensate - the one-period snapshot delay is inherent, the same reason a deferred
  sidechain send is excluded from dependency counting, `research/mixer/SPEC-dynamic-routing.md`
  line 120). `feedback.get_state` and the write's own result state `pdc_suspended`, but no
  command estimates the audible offset inside the loop. The rest of the graph keeps its normal
  PDC, unchanged byte for byte from before the submode existed (the proof's negative control,
  `tests/control-feedback-commands.py`).
- **No automatic exit when the loop stops.** The mode stays on - and keeps permitting loop
  sends - until `feedback.disable` runs; it does not watch the audio for silence or decay.
- *(Updated 2026-09-30.)* **A mixer channel's context menu now has Allow feedback sends** (`feedback.enable` /
  `feedback.disable`, leaving asks first and names the loop-closing sends it deletes), and while it is on the
  mixer offers the send button for a channel whose send would close a loop (`FeedbackModeActionTest`). Still
  missing: a visual mark on a feedback send. The bullet below is the pre-menu record.
- **No GUI surface.** The submode and its sends are socket-only: MixerView still hides the
  loop-closing arrows it always hid (`src/gui/mixer/MixerView.cpp`, the `isInfiniteLoop` guard),
  so the interface cannot create or show a feedback send.
- **The rack/chain `RoutingGraph` is not covered.** That topology is a derived cache re-wired on
  every list change and its live edit path is contractually closed (`routing.get_state`'s design
  decision, `src/core/ControlCommandsRouting.cpp`), so there is no user-authored edge there that
  could cycle; only the mixer's sends - the settable signal graph - are in the submode.
- **The mode flag itself is not saved.** A project reloads with the submode ON exactly when it
  carries a feedback-flagged send (the per-send flag is the persisted half); enabling the mode
  and saving without ever creating a loop reloads with the mode off. A pre-#709 build reading a
  feedback-flagged send ignores the flag, sees a normal cycle it has no rule to schedule, and
  stalls that loop's downstream channels - downgrade is not supported for such a project.
- **Removing the loop's other send by hand does not clear the flag.** A send created as feedback
  keeps its flag until `feedback.disable`; disable removes every flagged send even when its loop
  was already broken manually (conservative - that is how normal PDC is guaranteed back).

## UI precondition #1 - accessible names, focus order, palette roles (branch 040/ui-precond, 2026-09-22)

*(Updated 2026-09-30, R8.5.)* **Every keyboard-focusable widget on the main surfaces now has a
name**, and a test holds it: `tests/src/gui/AccessibilityWalkTest.cpp` builds the real application
(the main window's toolbar, the song, piano-roll, automation and pattern editors, the mixer, the
controller rack, project notes and the microtuner) plus the clip-launch grid, the start hub and the
shortcuts page, then an instrument track's and a sample track's own window (every page of every
nested tab) and the settings and export dialogs, and asks Qt's accessibility layer for the name of
every widget Tab can reach - 155 of them. The walks found 43 announced as nothing; all 43 are
named, and the ratchet list is empty. **Knobs are now keyboard-operable** (Tab reaches them;
arrows step as the wheel does, PageUp/PageDown coarsely, Home/End to the ends - `KnobKeyboardTest`)
and named from their label, model or hint. **Faders and model (LED) buttons take Tab focus too**
(0.5.0 theme 10) and answer the slider and button keys; a mixer strip's controls are walked with
Tab and Shift+Tab (wrapping inside the strip), and Ctrl+Tab / Ctrl+Shift+Tab move to the same
control on the adjacent channel, stopping at the first and last channel (`MixerModesTest`). Each
strip control is named with its channel's name through Qt's accessibility bridge. **What it does
not cover:** dialogs other than those walked are not checked, and no screen reader has been run
(the bridge is asserted, not a real screen reader - see the note below). The paragraph below is
the 2026-09-22 record.

The accessibility + palette pass of SPEC-zene-ui-v0 §5 item 1 swept exactly three
surfaces - the Focus Desk shell (`src/gui/FocusDesk.cpp`, `FocusDeskPane.cpp`,
`FocusDeskPlacement.cpp`; `FocusDeskModules.cpp` is the widget-free register and has
nothing to announce), the main window's menus and toolbar (`MainWindow.cpp`, plus every
`ToolButton` in the product through `widgets/ToolButton.cpp`'s constructor), and the two
palette files named by the spec (`LmmsStyle.cpp`, `widgets/TabWidget.cpp`) - with the
reusable mechanism in `include/Accessibility.h` (announce/describe) and
`include/WcagContrast.h` (the WCAG contrast helper, registered as WcagContrastTest).
Two stated absences ship with it:

1. **The rest of the interface is unswept.** Every other widget source is deferred to
   the wave-2 sweep; none of them has an accessible name added, so any screen-reader use
   outside the Focus Desk and the main window toolbar/menus still meets the measured
   starting point of zero. The complete deferred set (120 files):

```
src/gui/ActionGroup.cpp
src/gui/AudioAlsaSetupWidget.cpp
src/gui/AudioDeviceSetupWidget.cpp
src/gui/AutomatableModelView.cpp
src/gui/ControlLayout.cpp
src/gui/ControllerDialog.cpp
src/gui/ControllerRackView.cpp
src/gui/ControllerView.cpp
src/gui/Controls.cpp
src/gui/EffectControlDialog.cpp
src/gui/EffectRackView.cpp
src/gui/EffectView.cpp
src/gui/FileBrowser.cpp
src/gui/FileRevealer.cpp
src/gui/FileSearchJob.cpp
src/gui/LadspaControlView.cpp
src/gui/LfoControllerDialog.cpp
src/gui/LinkedModelGroupViews.cpp
src/gui/LmmsPalette.cpp
src/gui/Lv2ViewBase.cpp
src/gui/MainApplication.cpp
src/gui/MidiCCRackView.cpp
src/gui/MidiLearnGui.cpp
src/gui/MidiSetupWidget.cpp
src/gui/MixerChannelView.cpp
src/gui/MixerView.cpp
src/gui/ModelView.cpp
src/gui/PeakControllerDialog.cpp
src/gui/PinConnector.cpp
src/gui/PluginBrowser.cpp
src/gui/ProjectNotes.cpp
src/gui/RowTableView.cpp
src/gui/SampleThumbnail.cpp
src/gui/SampleTrackWindow.cpp
src/gui/SendButtonIndicator.cpp
src/gui/SideBar.cpp
src/gui/SideBarWidget.cpp
src/gui/StemSplitController.cpp
src/gui/StringPairDrag.cpp
src/gui/SubWindow.cpp
src/gui/TelemetryConsentDialog.cpp
src/gui/ToolPluginView.cpp
src/gui/clips/AutomationClipView.cpp
src/gui/clips/ClipView.cpp
src/gui/clips/MidiClipView.cpp
src/gui/clips/PatternClipView.cpp
src/gui/clips/SampleClipView.cpp
src/gui/editors/AutomationEditor.cpp
src/gui/editors/Editor.cpp
src/gui/editors/PatternEditor.cpp
src/gui/editors/PianoRoll.cpp
src/gui/editors/PositionLine.cpp
src/gui/editors/Rubberband.cpp
src/gui/editors/SongEditor.cpp
src/gui/editors/StepRecorderWidget.cpp
src/gui/editors/TimeLineWidget.cpp
src/gui/editors/TrackContainerView.cpp
src/gui/embed.cpp
src/gui/instrument/EnvelopeAndLfoView.cpp
src/gui/instrument/EnvelopeGraph.cpp
src/gui/instrument/InstrumentFunctionViews.cpp
src/gui/instrument/InstrumentMidiIOView.cpp
src/gui/instrument/InstrumentSoundShapingView.cpp
src/gui/instrument/InstrumentTrackWindow.cpp
src/gui/instrument/InstrumentTuningView.cpp
src/gui/instrument/InstrumentView.cpp
src/gui/instrument/LfoGraph.cpp
src/gui/instrument/PianoView.cpp
src/gui/menus/MidiPortMenu.cpp
src/gui/menus/RecentProjectsMenu.cpp
src/gui/menus/TemplatesMenu.cpp
src/gui/modals/AboutDialog.cpp
src/gui/modals/ColorChooser.cpp
src/gui/modals/ControllerConnectionDialog.cpp
src/gui/modals/EffectSelectDialog.cpp
src/gui/modals/ExportProjectDialog.cpp
src/gui/modals/FileDialog.cpp
src/gui/modals/RenameDialog.cpp
src/gui/modals/SetupDialog.cpp
src/gui/modals/VersionedSaveDialog.cpp
src/gui/tracks/AutomationTrackView.cpp
src/gui/tracks/FadeButton.cpp
src/gui/tracks/InstrumentTrackView.cpp
src/gui/tracks/PatternTrackView.cpp
src/gui/tracks/SampleTrackView.cpp
src/gui/tracks/TrackContentWidget.cpp
src/gui/tracks/TrackGrip.cpp
src/gui/tracks/TrackLabelButton.cpp
src/gui/tracks/TrackOperationsWidget.cpp
src/gui/tracks/TrackRenameLineEdit.cpp
src/gui/tracks/TrackView.cpp
src/gui/widgets/AutomatableButton.cpp
src/gui/widgets/AutomatableSlider.cpp
src/gui/widgets/BarModelEditor.cpp
src/gui/widgets/CPULoadWidget.cpp
src/gui/widgets/CaptionMenu.cpp
src/gui/widgets/ComboBox.cpp
src/gui/widgets/Draggable.cpp
src/gui/widgets/Fader.cpp
src/gui/widgets/FloatModelEditorBase.cpp
src/gui/widgets/Graph.cpp
src/gui/widgets/GroupBox.cpp
src/gui/widgets/Knob.cpp
src/gui/widgets/LcdFloatSpinBox.cpp
src/gui/widgets/LcdSpinBox.cpp
src/gui/widgets/LcdWidget.cpp
src/gui/widgets/LedCheckBox.cpp
src/gui/widgets/LeftRightNav.cpp
src/gui/widgets/MeterDialog.cpp
src/gui/widgets/MixerChannelLcdSpinBox.cpp
src/gui/widgets/NStateButton.cpp
src/gui/widgets/Oscilloscope.cpp
src/gui/widgets/PeakIndicator.cpp
src/gui/widgets/PixmapButton.cpp
src/gui/widgets/SimpleTextFloat.cpp
src/gui/widgets/TabBar.cpp
src/gui/widgets/TempoSyncBarModelEditor.cpp
src/gui/widgets/TempoSyncKnob.cpp
src/gui/widgets/TextFloat.cpp
src/gui/widgets/TimeDisplayWidget.cpp
```

   (`src/gui/MicrotunerConfig.cpp` is not deferred - it belongs to lane zene-712.)
2. **The system dark/light hook cannot run on any builder in this wave.**
   `QStyleHints::colorSchemeChanged` exists only from Qt 6.5; this box builds Qt 6.4.2
   and CI builds Qt 5.15, so the guarded block in `GuiApplication.cpp` compiles out of
   both and the connection is `unverified` by execution - reviewed against the Qt 6.5
   documentation only. One palette variant ships, so the handler re-applies it and the
   variant pick is the one branch still to add.

Two palette replacements are deliberate behaviour changes, recorded here rather than
hidden: `LmmsStyle`'s titlebar branch no longer forces #ffffff/#c0c0c0/#404040 over the
palette (the palette's Text roles decide), and its MDI-button override of #dfe4ec is
gone with the branch. Both were win32-era constants; their effect on a Windows titlebar
is `unverified` - no Windows builder exists in this wave. The frame bevel,
`hoverColors()` and TabWidget's tab background now read Shadow/Window/Dark/Mid roles;
under LmmsPalette the bevel is byte-identical (its Shadow role *is* black), the others
move to whatever the theme defines - which is the point of the change.

## Host-wide dynamic tuning (`mts.*`, board card #712, lane `zene-712`) — added 2026-09-22

**The one line this card owes, in its own heading: the session-wide tuning table (the
`mts.*` group) has no interface surface yet — it is socket-only (SPEC A16), and the
MTS-ESP publication path is inert (a typed refusal names the library) until upstream's
separately-installed `libMTS.so` is present.**

Fleshed out, because both halves of that sentence hide something real:

- *(Updated 2026-09-30.)* **Edit ▸ Session Tuning** reaches the table: a line with its state (12-TET, or the
  scale and keymap in force), Load Scala Scale... / Load Keymap... (`mts.load_scale` / `mts.load_keymap`, which
  activate it), Reset to 12-TET (`mts.reset`) and Publish as MTS-ESP Master (`mts.master_set`, offered only
  where the MTS-ESP library is present) (`SessionTuningMenuTest`). Per-note edits (`mts.set_note`,
  `mts.set_tuning`) stay socket-only. The bullet below is the pre-menu record.
- **No GUI reaches the table.** `mts.load_scale` / `mts.set_tuning` / `mts.set_note` /
  `mts.load_keymap` / `mts.reset` / `mts.master_set` are reachable only through the
  control socket; the existing Microtuner dialog merely FEEDS the table's content
  (`MicrotunerConfig::applyScale()` / `applyKeymap()` call `SessionTuning::feedScale()` /
  `feedKeymap()`), and feeding never activates it — there is no switch anywhere in the
  interface that turns the session table on. An INACTIVE table is the default, and then
  render takes exactly the pre-#712 branches (proved by
  `tests/src/core/SessionTuningTest.cpp::baselineInactiveKeepsTheOldRenderPath`).
  Those two dialog lines compile and call the tested setter; the dialog itself cannot be
  driven headless, so their run-time path is `unverified` by the ctest.
- **MTS-ESP's IPC library is not shipped here.** `thirdparty/mts-esp/` is upstream's
  client/master pair (0BSD, provenance verified in its README), but upstream ships the
  actual shared-memory core as a separately-installed `libMTS.so`
  (`/usr/local/lib/libMTS.so` on Linux) that the wrappers `dlopen` at startup — without
  it every master call is an inert no-op. So `mts.master_set {"enabled": true}` is a
  typed REFUSAL naming that path on every builder without the library (this box: proven
  by the `masterSetReportsItsOwnMtsEspState` slot), and the CLIENT half is compiled but
  never called — no "follow an external MTS-ESP master" mode exists yet. The
  library-present branch of that slot stays runnable for a builder that does have
  upstream's MTS-ESP installed; **no builder in this wave has one**, so the arm →
  publish → client-observe round trip is `unverified` here, as is the Windows
  presence probe (`GetModuleHandleW` — no Windows builder in this wave).
- **The two measurement constants this group moved are LANE-LOCAL, the merge tip must
  re-measure**: the A16-HISTOGRAM block in `docs/RELEASE-NOTES-v0.3.0-alpha.md`
  (re-taken with `bash tools/dawproject-proof.sh`, part 2) and `ControlRegistryTest`'s
  `commandCount()` (`85 + 7 + 5 + 5 + 5 + 3 + 3 + 7`). Two sibling lanes add groups
  too; the train re-takes both at the merged tip.

## 0.5.0 catch-up wave 2 — what each new feature does not do (2026-10-04)

**F1, hardware I/O (MX-040/041/036/061).**
- JACK and ALSA multichannel output are written, and they pass the syntax check, but neither has
  been run: there was no jackd or multichannel interface on the build machine. The Dummy device's
  `LMMS_DUMMY_OUTPUT_CHANNELS` is what the tests measure.
- PulseAudio, SDL, OSS, sndio and SoundIo stay stereo; routes beyond 1/2 answer
  `multichannel:false` / `stereo_backend`. PortAudio is not compiled in this build.
- Input channel names and buses are labels only; JACK port names do not use them.
- A hardware insert is bypassed during an export: the dry signal passes and it reports 0 latency.
- `pdc.report` does not list the insert's latency per channel, although PDC includes it.

**F2, instruments (MX-114/112/120).**
- Output buses have an interface: Instrument Setup > Output Buses on the track's header menu, its
  Inspector "More" menu and its instrument's device card lists the buses, routes one to a channel
  (or un-routes it) and makes a channel for each unrouted bus, all through the plugin.output_* commands.
  A bus count is the plug-in's own: there is no adding or removing a bus, only routing one.
- MIDI parts have an interface: Instrument Setup > MIDI Parts lists a track's parts, adds one, edits one
  in place (plugin.midi_part_set, which keeps the part's place in the first-match order) and removes one.
  The Edit form starts blank - a field left blank keeps its value - rather than showing the current one.
- Layers have an interface: Instrument Setup > Layers > Layer Rack... is a panel with a row per layer
  (layer-0 is the track's own instrument) - Mute, Solo, Level (a gain on top of the track's volume,
  0..200 %), Open (the layer instrument's own view, in a window that closes with the layer) and Remove -
  with Add Layer... and Zones... under it; Add a Layer... and Layer Zones... are in the menu too. Mute,
  solo and level are rack.layer_set, saved with the track. Layer-0 has no level of its own (it is the
  track's volume) and no Remove or Open there (its own window and device card do that). A layer's mute
  and solo are a gain of 0 on its bus: a muted layer still runs its notes (CPU), it is just not heard.
  A plug-in layer opens the host's view of it, not the plug-in's own editor (plugin.editor_open does
  not address a layer).
- Per-voice modulation has an interface: Instrument Setup > Per-Voice Modulation lists the modulators
  (make one per-voice on the track's notes or not, bind it to a parameter of the track's instrument,
  remove it, create one) and the modulation assignments (change a depth, remove one). The modulation
  layer is the song's, so Modulation Assignments lists every track's assignments with a 'track' column
  rather than only this track's; the parameter to bind is typed by its display name (plugin.param_get
  lists them), there is no parameter picker; and a modulator's shape and rate are edited in the
  Modulators panel, not here.
- Layers receive the track's played notes, not the notes its chord or arpeggio functions add.
- Each layer is a hidden track with its own bus handle and MIDI port, so under JACK each one has its
  own ports.
- *(Updated 2026-10-06, MX-157.)* Hosted CLAP, VST3 and LV2 instruments now report their latency to PDC,
  and a routed output bus carries it: the mixer counts the instrument's latency in the bus's target
  channel's alignment point, and delays the bus only by what that alignment adds on top
  (`InstrumentLatencyPdcTest::routedOutputBusIsAlignedWithTheMainOutput`). A layer is a hidden instrument
  track with its own bus handle, so a hosted instrument on a layer reports through that handle like any
  track; that case is not separately tested. What remains is listed under "Hosted instrument latency"
  below.
- Removing a layer while live MIDI is forwarded from the MIDI thread has the same unguarded window
  as deleting any track.
- CLAP note events now carry `note_id = channel × 128 + key` instead of -1.

**F3, arranging (ED-021, WF-107, WF-042, WF-048).**
- Frame edits are refused on warped and source-tempo clips.
- Waveform drawing, transients, pre-fades and comp slip ignore the sub-tick lead, so they can be up
  to one tick off on a frame-edited clip. Shift-knife does not snap to zero crossings.
- The reference project is read-only and never plays: Engine and Song are singletons, so this is
  not a multi-document engine.
- Automation and pattern clips cannot be copied from the reference project, and `copy_devices`
  rebuilds the target chain, so its fx ids renumber.
- EditLocks does not refuse `reference.copy_clips` onto a locked track.

**F4, AAF import (WF-038).**
- OMF is refused by name, because LibAAF reads AAF only.
- MXF essence is listed as missing, because the sample decoder does not open .mxf.
- A multichannel clip plays its first source only.
- Timing uses the global tempo; an active tempo map is reported, not followed.
- Solo is counted as lost, not imported.
- Builds without LibAAF (hosted CI today) compile the reader out, and its two fixture cases skip.

**F5, video (ED-054).**
- There is one video per project, stored by absolute path, and `project.missing_assets`/relink do
  not know it.
- `video.extract_audio` decodes on the interface thread, so a long video freezes the interface for
  the decode.
- Builds without FFmpeg compile the decoder out and every `video.*` id answers "unavailable".

**F6, peer-to-peer collaboration and remote control (WF-050/052).**
- It is off by default and experimental, and LAN-first.
- Over the internet it is best effort, with no STUN or hole punching, because both need a server.
- Behind carrier-grade or double NAT, port mapping cannot help. The build machine is in that case,
  measured read-only: NAT-PMP answered "no external address" and the IGD's external address was
  empty. The invite then falls back to the LAN address and says so.
- Edits that bypass the command registry (many interface gestures today) re-sync the whole project
  to peers. On a peer they cannot be sent and are reverted.
- Samples are not transferred.
- Undo is shared, not per peer. A peer's Edit > Undo is local and is reverted; `control.undo` is
  forwarded.
- Windows and macOS packages need Qt's OpenSSL TLS plug-in for PSK, otherwise `collab.host`
  answers "unavailable".
- There is no reconnect, and no HTML remote page, because browsers cannot do TLS-PSK.

**F7, surround (MX-060).**
- Multichannel export is WAV only (WAVE_FORMAT_EXTENSIBLE: 6 channels for 5.1, 8 for 7.1, 3 for LCR,
  4 for quad, at 16/24/32-bit). Stereo, with the BS.775 downmix, stays the default; FLAC, OGG and MP3
  export stereo. Choose it in the export dialog's Channels row, with `--channels surround`, or with
  `render.render`'s `channels: "surround"` (docs/SURROUND-EXPORT.md).
- A surround export carries no loudness report (the meter measures stereo): it is refused while the
  report is on, and the dialog turns the report off.
- Per-track renders, stem exports, mastering candidates, the render queue (`render.queue_add`) and
  `render.stems` stay stereo; export presets do not store the channel choice.
- A surround channel's extra channels get no PDC delay.
- Its devices, post-fader inserts, mono/polarity/width/EQ and meters act on L/R only.
- Cue mixes, listen and the recorder carry L/R.
- The pan acts on the extra channels at its block value.

## 0.5.0 catch-up wave 3 — the redesign themes 8-24, and what they leave out (2026-10-04)

**Workspace and layouts (themes 8, 22).**
- The controller rack and project notes stay floating tools rather than docked panels: docking them would change what every project file saves.
- ~~The main window's own geometry is not remembered; it opens maximised.~~ *(Resolved 2026-10-07: it opens where it was left - screen, normal size and place, maximised or not - fitted to a connected screen; maximised only when nothing is remembered. `MainWindowGeometryTest`, docs/UI-SHELL.md §5. An agent-driven or offscreen run neither reads nor writes the place.)*
- The header and footer overflow are tested offscreen only.

**Arranger and launcher (themes 9, 14).**
- Zoom is not animated (reduced motion).
- Wheel and pinch zoom act directly rather than through a command, so they don't flood the zoom history.
- `session.jump_slot` keeps its default of 0.

**Canvases without a mouse, and announcements (themes 10, 17).**
- The editable item list is a floating tool window, not a docked panel. It opens from the List button on its canvas (the note editor's and the arranger's toolbars; under the lane chooser while it shows a controller lane; beside the XY pad's axes; beside Add lane in the take lanes), with Ctrl+Shift+L, or with `window.item_list`.
  Since 2026-10-06 (ED-194) the automation editor has a list too (`surface: "automation"`, see the section below). It has
  no List button: the List buttons (050/feat-ui-gaps) and the automation list (050/feat-editor-lanes) landed in parallel,
  so that list opens with Ctrl+Shift+L or `window.item_list` only.
- The sample editor's keyboard route is its range fields. The waveform itself takes no keys.
- An MPE lane key edits only the cursor note.
- Screen readers (NVDA, VoiceOver, Orca) are reached through Qt's bridge, which the tests assert. No screen reader was run.

**Lanes and the sample editor (themes 11, 15).**
- ~~The lanes have no playhead.~~ *(Resolved 2026-10-07: the expression and controller lanes draw the note area's playhead; `LaneReadoutTest::theLanesShowThePlayhead`, docs/NOTE-EDITOR.md §5.)*
- *(Resolved 2026-10-06, ED-130.)* The lane chooser now lists every controller number, CC 0 to CC 127, beside
  pitch bend, aftertouch and program change (`NoteEditorTest::anyControllerNumberCanBeChosenFromTheLane`).

**Devices and the Inspector (themes 12, 13).**
- *(Resolved 2026-10-07, plugin-hosting lane.)* VST3 and CLAP vendors no longer read "Not reported". The
  device picker (the Device panel's Add, the device card's Replace and Insert) shows the vendor the module's
  own factory reports: a VST3 class's vendor, which the SDK fills from `PFactoryInfo` when the class names
  none, or a CLAP descriptor's vendor (`include/PluginVendors.h`). A plug-in that reports no vendor still
  reads "Not reported". *(Resolved 2026-10-16, Q050-090.)* `plugin.list` carries the same vendor on every
  entry (empty when the format reports none), and the Plug-in Manager list shows it as its Vendor column.
- Channel pan and width still read in the knob's own units.
- Mixed values cover the selected notes only: the selection holds one clip.
- *(Resolved 2026-10-07, plugin-hosting lane.)* The rack's own "Add effect" (a mixer channel's or an
  instrument's effect chain) now opens the device picker the Device panel uses. It searches every loadable
  effect of every format and shows each one's vendor and state. The chosen device is still appended to that
  rack's chain with its controls opened. A device that can no longer be loaded is now reported, where the old
  dialog added a placeholder. The older select dialog (`EffectSelectDialog`) is no longer opened from anywhere.

**Mixer (theme 16).**
- *(Superseded 2026-10-06, `050/feat-meters`: the filter has a "Selected" choice - MX-005, `docs/MIXER-METERING.md` §2.)*

**Lists, help and workflows (themes 18, 19, 21).**
- Results of commands run from menus or the palette still float briefly.
- List reads show a busy cursor rather than a progress bar, because the list commands are synchronous.
- Editing workflow steps, and portable workflows, are deferred.

**Themes, shared controls and background tasks (themes 20, 23, 24).**
- Theme changes apply at the next start, and every reply and the window say so.
- *(Partly resolved 2026-10-16, Q050-090.)* The plug-in scan runs in the background on request
  (`plugin.scan_start`, one file per event-loop turn) with progress and cancellation
  (`plugin.scan_status`, `plugin.scan_cancel`) and appears in the task area; the synchronous
  `plugin.rescan` stays for scripts. `render.render`, `render.stems` and `browser.peaks` stay
  synchronous and are not in the task area.
- Accessible mode's larger targets are not re-applied if a knob later resets its own fixed size.

## 0.5.0 L08 metering — ballistics, the Selected filter, live input meters, the phase view (050/feat-meters, 2026-10-06)

The record of what was built is `docs/MIXER-METERING.md`; this is what each piece does not do.

**MX-128, meter ballistics.**
- The fall-off is a factor per tick of the main window's 60 Hz timer, as the inherited one was: a busy
  interface that misses ticks lets the bars fall more slowly. "fast" is the inherited 1.25 per tick
  (about 116 dB/s); medium and slow are about 26 and 12 dB/s.
- After its hold, the peak line still falls by the inherited 0.05 (linear) per tick; there is no
  "hold until clicked" setting.
- The RMS meter mode takes the same fall-off; its reading is still one period's RMS, not a 300 ms
  VU integration.

**MX-005, the Selected channel filter.**
- The selection is the mixer's own: the arranger's track selection does not feed it.
- It is interface state, never saved with the project or in the config file; it is forgotten when the
  mixer view is cleared for another project. With no interface (no mixer view to clear) an agent's
  selection is kept until it sets another.
- The mode bar's Pick... dialog starts with nothing ticked (it replaces the selection); Ctrl+click on a
  strip adds or removes one channel.

**MX-134, live input meters.**
- They meter the engine's stereo input bus - the frames the retrospective capture and the stereo record
  routes read. A wide capture (more than two channels, `AudioInputPath`) is not metered channel by
  channel, and no record-armed track or input-monitoring strip carries its own input meter.
- They are shown in Large Meters (the mixer's Mixer View menu) only; the record menu's Input Level is
  still the one-shot `meter.input` reading, and no command reads the live meter (an agent reads
  `meter.input`).
- One live reader at a time: `inputmeter::live()` empties its own maxima, so a second Large Meters-style
  reader would split the peaks with the first. Only one Large Meters window is ever open.

**MX-137, the stereo phase view.**
- One channel at a time, in its own Phase Scope window (Mixer View menu, a channel's More menu, or
  `window.phase_scope`); it is not drawn on the strips or inside Large Meters.
- It shows the block the correlation is measured from: after the channel's inserts and pan, before its
  fader. A muted channel feeds nothing, and the view says "No signal" a quarter of a second after the
  frames stop.
- It draws the newest 1024 frames, auto-scaled to the loudest of them, every 40 ms; it keeps no
  persistence trail, and the scale is not shown in dB.
- In the one period in which the chosen channel changes, a push from the old channel still in flight
  makes the new channel's first block wait for the next period (the tap has one writer at a time).
- The agent reads the correlation and `frames_seen` (proof the tap is fed), not the drawn points.


## 0.5.0 editor lanes — controller lanes, keyboard automation editing, fades in the automation view, write to end (2026-10-06)

**ED-130, any controller lane.** The note editor's lane chooser lists every controller number (CC 0 to 127), pitch
bend, aftertouch and program change (`docs/NOTE-EDITOR.md` §5). What it leaves out:
- The chooser is one long list (24 rows visible at a time); there is no search or number field in it.
- Only the five common controllers and about twenty standard ones carry a name; the rest read "CC <n>".

**ED-194, keyboard editing of automation.** In the automation editor, Ctrl+Left/Right choose the previous/next point
(from none: the first, or the last), Left/Right move the chosen point one grid step (Shift: four), Up/Down change its
value by a hundredth of the control's range (Shift: a tenth, never less than the control's own step), Escape lets go
of it and the arrows scroll again. The chosen point is ringed and named to a screen reader. Each key is one edit and
one undo step: `automation.move_point` when the control has a command address. A move onto another point is refused
and spoken. Ctrl+Shift+L, or `window.item_list` with `surface: "automation"`, opens the points as an editable list
(position, value as a share of the range, stored value); its edits are the same moves, and its current row follows
the chosen point both ways (`AutomationEditorEditingTest`). What it leaves out:
- A control the command surface cannot name (a track's volume or pan, the tempo, a mixer fader) and a second clip
  of the same control are moved in place behind one journal checkpoint, not through `automation.move_point`: still
  one undo step, but no `control.transactions` entry.
- One point at a time: there is no keyboard multi-selection, and neither the keys nor the list add or remove points
  (the canvas, `automation.add_point` and `automation.remove_point` do).
- Undoing a key move drops the chosen point; Ctrl+Left/Right choose one again.
- A moved point keeps its curve and bend but not a separate out-value (a step): the move writes one value, as
  `automation.move_point` does.
- The list types a value in whole percent of the range; the "Stored value" column shows the exact value.

**ED-180, fades drawn in the automation view.** An audio clip set as the automation editor's ghost ("Set as ghost in
automation editor") now has its gain curve drawn over its waveform: the fades, the clip gain and the gain envelope -
the same curve `clip.fade_curve` reports (one definition, `clipGainCurveAt`) - with what the fades take away shaded
(`AutomationEditorEditingTest::aSampleClipsFadesAreDrawnBehindTheCurve`). What it leaves out:
- It is drawn, not edited: the fade handles stay on the clip in the arranger (and `clip.set_fade`).
- Pre-fades (the part of a fade outside the clip) and speed fades are not drawn.
- The curve runs from tick 0 over the clip's length; the ghost waveform is placed by its own (inherited) rule, so on
  a clip with a start offset or a trimmed window the two may not line up exactly. Not measured.
- The curve is redrawn when the editor repaints; a fade changed elsewhere shows on the next repaint.

**ED-201, write to end.** `automation.write_to_end` holds a control's value (its current one when left out) from a
tick to the song's end: every later point goes, the point at the tick steps from the curve's value there to the new
one, and a point at the song's end (the end of its last clip) holds it - one undo step. A control's context menu has
**Write value to end**, which writes the current value from the song position
(`AutomationLanesTest::writeToEndHoldsAValueToTheSongsEnd`, `AutomationModeMenuTest::theContextMenuWritesTheValueToTheEnd`).
What it leaves out:
- It is a helper run after (or instead of) a pass, not a mode of the pass itself: a Touch or Latch pass does not
  write to the end when it stops, and there is no "write to punch" or "write to start".
- It acts on the control's first automation clip; a clip that starts after the tick is refused rather than extended
  backwards.
- The menu item is offered only for a control the command surface can name (as the mode submenu is).

## Recording audio into a launcher cell (WF-055, 0.5.0 L02 `launcher-rec`) — added 2026-10-07

`session.record_slot` / `session.record_slot_status` / `session.record_slot_land` record a sample track's input
into an empty launcher cell: it starts on the launch quantisation, stops on the cell's quantised stop or after
`bars`, and lands as the cell's audio clip (a 24-bit WAV), one undo step. Design record and proof:
`docs/SESSION-AUDIO-RECORD.md`. What it does not do:

- **One take at a time**, on one cell. A finished take blocks the next `session.record_slot` until it is landed
  or cancelled (`cancel: true`).
- **Empty cells only.** A filled cell is refused; clear it first. There is no "record over" or overdub.
- **Sample tracks only**, and the take is the engine's **stereo input bus** (what an arrangement clip records),
  not a record route's chosen input channel. `track.set_arm` is not consulted: arming the cell is the arm.
- **The start is period-granular.** The session scheduler decides launches once per audio period, so a take
  starts with the period in which its grid line is reached (as the cell's own playback does); its length is
  exact - whole frames of the span, shifted by the recording round trip like an arrangement take.
- **Landing is explicit on the socket.** The take waits in `done` until `session.record_slot_land` writes it;
  an agent polls `session.record_slot_status`. The WAV is never deleted by undo.
- **From the interface** (View ▸ Clip Launcher): an empty cell's right-click menu has Record Audio Into Cell,
  Record Audio for Bars... and Cancel Recording; the cell reads "● Rec" while it records ("○ Rec" while it waits
  for its launch, and its accessible name says so in words); the column's stop button ends the take on the
  quantised stop, and the grid lands the take it started into the user's `samples/Recordings` folder. There is no
  record BUTTON on the cell itself, no keyboard shortcut for it, no count-in, and the grid does not land a take it
  did not start (an agent's take is left for the agent).
- **A launched audio cell renders only on a sample track's column**, through that track, from the clip's top (or
  its loop start), looping the whole file. The cell's gain, transpose, detune, RAM mode and loop LENGTH are not
  applied to audio, and there is no warping: a file plays at its own speed whatever the tempo. A file at another
  sample rate is converted once, when it is first launched. An audio cell on an instrument track's column still
  only silences the column. (Before this lane no launched audio cell rendered at all - the bullet "The Session
  View is in the 0.3.0-alpha builds..." above records that.)
- **Clips are cached by path** for the open project: a file changed on disk after its cell was first launched
  plays the old audio until the project is reopened.

## 0.5.0 audio clips — looped content, rotation and the crossfade editor (lane `audio-clips`, 2026-10-07)

**Looped audio content (ED-029, `clip.set_loop` on an audio clip).** An audio clip's audio from `start` ticks
after its in-point, for `length` ticks, repeats across the clip once playback passes the loop's end
(`include/ClipAudioLoop.h`; proved on rendered PCM by `tests/src/core/AudioClipLoopTest.cpp`). What it does not do:
- The loop is measured in ticks when it is set and then kept in **source frames** (`loopfrom` / `loopframes`), so a
  later tempo change keeps it on the same audio rather than on the same bars. There is no "loop follows the grid".
- It plays only on a plain clip: one with no warp markers and no source tempo, no transpose, no speed change, not
  armed to record, and on a track with no comp. `clip.set_loop` refuses such a clip, typed; a loop on a clip that
  becomes one of these later is kept in the file but not played until it is plain again. Pre-fades are not played
  on a looped clip.
- A loop is not split-aware: splitting or trimming a looped clip keeps the loop on the source frames it had, and a
  split past the loop's first pass does not carry the folded position into the right half.
- The loop is drawn nowhere: the clip still shows its source waveform once, and the only interface is the clip
  menu's "Loop the Content..." form.
- An older build loads a looped clip as an unlooped one (it ignores the new attributes), so the clip falls silent
  after its audio ends there.

**Rotated audio content (ED-028, `note.rotate` on an audio clip).** The clip's content turns `amount` ticks later
(negative: earlier) with wrap-around - inside its loop, or inside the clip, which then becomes a loop over exactly
the audio it plays - one undo step each (`AudioClipLoopTest`). What it does not do:
- It is a loop offset, not an edit of the audio: the waveform drawn on the clip does not move, and the rotation
  plays only where the loop plays (the plain-clip rule above).
- It refuses, typed, a clip longer than its audio with no loop (loop it first), a clip whose audio starts after
  the clip, and a looped clip whose start plays before its loop (an intro does not repeat, so it cannot turn).
- The command keeps its MIDI name (`note.rotate`) rather than adding a `clip.rotate`, so the canary did not move;
  `scope` is not read on an audio clip. The clip menu's entry is Process > Rotate Content...

**The crossfade editor (ED-176).** An audio clip's menu offers Crossfade Editor... while another audio clip on
its track overlaps it; the editor shows the overlap, each side's ramp length (ticks) and shape - the clips' own
ramps, or the overlap at equal power where a side has none - draws both ramps, and Apply runs `clip.crossfade` with
both sides (`CrossfadeEditorTest`). What it does not do:
- It edits numbers, not handles: there is still no crossfade drawn on the clips in the arranger and nothing to drag.
- It pairs the clip with ONE neighbour (the nearest overlapping clip after it, else before it); a clip overlapped
  on both sides is edited one pair at a time from each clip's menu.
- The crossfade is still two independent fades, not a linked object: moving a clip afterwards does not move it.
- `clip.crossfade`'s undo restores the TRACK checkpoint, which rebuilds the track's clips: an editor left open
  across an undo loses its clips (Apply then does nothing) and has to be opened again from the menu.

## The allocation probe: what a "zero allocations" test proves, and what it does not (2026-10-07)

`tests/src/core/AllocationProbe.h` is the runtime half of the realtime rule: a test arms
`lmms::test::tlCountAllocations` around an audio-thread call and asserts that
`tlAllocationCount` stayed at 0. Until 2026-10-07 that assertion was **vacuous for library
code**. The probe's replacement `operator new`/`new[]`/`delete`/`delete[]` were declared
`inline`, which the standard forbids for a replacement allocation function
([replacement.functions]); GCC then emitted no definition at all, so `nm -C` on every test
binary showed only `U operator new(unsigned long)@GLIBCXX_3.4`. Allocations in the test's own
file were counted (the inline body was visible there), but every allocation inside lmmsobjs,
the code each test exists to check, went to libstdc++ and was never seen.

**What it guarantees now.** The replacement functions live in `tests/src/core/AllocationProbe.cpp`,
an ordinary (non-inline) definition compiled once and linked into every test whose source
includes the header (`lmms_link_allocation_probe` in `tests/CMakeLists.txt`, also used by
`Vst3InstrumentTest`, which compiles several translation units). One definition in the
executable replaces `operator new` for the whole process: the test's file, the statically linked
lmmsobjs, Qt's own `new` calls and any plugin module the test loads. Every form is replaced
(plain, array, `nothrow`, `align_val_t`, sized delete). The counters are defined in that file
too, so a test that includes the header without the probe linked fails to link instead of
quietly counting nothing (that check caught `WarpMarkersTest` and the three `SampleClip*`
tests, which spell the include `"../core/AllocationProbe.h"`).

Evidence, all from the `050/feat-alloc-probe-fix` lane:
- `AllocationProbeTest::countsAnAllocationMadeInLibraryCode` (an allocation inside
  `RoutingGraph::createNode()`, compiled into lmmsobjs) **failed on the old probe**
  ("counted 0") and passes on the fixed one. `nm -C` on all 41 probe binaries now shows
  `T operator new(unsigned long)`; before the fix, none of the 40 did.
- A positive control on two real tests: one `::operator new(16)` injected temporarily into
  `MasterLoudnessTap::feed()` and `SessionScheduler::processAudio()`. On the old probe
  `MeterTapTest` and `SessionSchedulerTest` still **passed**. On the fixed probe they fail
  (`feedingAnArmedTapAllocatesNothing`, `audioThreadPathDoesNotAllocate`: "audio-thread path
  allocated 20000 times", `commandsCrossThreadsWithoutLoss`). The injection was reverted.
- All 40 tests that include the probe were rebuilt and re-run on the fixed probe: **40/40 pass**,
  the same as on the old one. No audio-thread path these tests cover allocates through
  `operator new`, so the lane has no allocation fixes to list. That is now a measured result
  rather than an unchecked assumption.

**What it still does not prove.**
- It counts `operator new` only. `malloc`, `realloc` and Qt's implicitly shared containers
  (`QString`, `QByteArray` and `QList` allocate through `QArrayData` with `malloc`) are
  **not counted**. A `QString` built on the audio thread passes a probe test. The static sweep
  (`docs/RT-SAFETY-SWEEP.md`) is what looks for those.
- It counts per thread, only on the thread that armed it and only inside the window. A
  path that hands work to another thread, or allocates on its first call before the window opens,
  is not measured.
- Locks, system calls and logging are not measured; it is an allocation counter, not a
  realtime sanitizer (that is the dispatch-only RTSan job, `lmms::RealtimeScope`).
- It proves the paths that the 40 tests drive, nothing more. Coverage of the audio thread is
  whatever those tests call.
- `tests/src/core/LinkRealtimeProbe.h` (`050/feat-link-interop`) was a second, correct
  probe written for the same reason. It was folded into this one on the 2026-10-07 feature merge:
  it defines no allocation function now, only an `allocationsDuring()` helper over these counters.

## 0.5.0 launcher in performance — perform mode and sub-scenes (lane launcher-perform, 2026-10-07)

**Perform mode (WF-071).** View > Perform Mode and Launcher > Perform Mode (`window.toggle` 'perform') open
`PerformView`: the setlist's song and the next one, transport and tempo, the playing scene and the next, one big
button per scene, each column's playing cell and a large Stop All - every press a registered command
(`PerformViewTest`). What it does not do:
- Its keys (1-9, Up/Down, Enter, Space, Escape, Page Up/Down) are the window's own and cannot be rebound: the
  shortcut map (`shortcuts.set`) binds main-window menu actions only. Launcher > Launch Scene 1-8 and Stop All
  Clips remain rebindable there.
- MIDI learn reaches what the launcher's controller bindings can do - launch a scene, stop everything
  (`session.bind_learn`). "Select the next scene", "launch the selected scene", play/stop, tempo and the song
  steps cannot be bound to a note.
- Keys 1-9 reach only the first nine scenes; past them, Up/Down and Enter (the scene list scrolls to the
  selected scene).
- It does not remember its place or size (`ToolWindowMemory` does not know it), and it is not in the footer's
  Tools menu.
- The bar and beat it shows assume four beats of 48 ticks to the bar; a scene's own time signature is not
  reflected there.
- Found while testing it: `transport.set_tempo` is undone through a Song checkpoint, which rebuilds the tracks,
  and each track's view is made by a queued call. Two such undos in one turn of the event loop, with the
  interface up, crashed in `TrackContainerView::createTrackView` (on a track the second restore had deleted).
  The application's event loop runs between two presses, so the test lets it run too; the hazard itself is not
  fixed here.

**Sub-scenes (WF-064).** A sub-scene is a named subset of one scene's cells (some of its columns), made from a grid
selection (Ctrl+click or Ctrl+Space, then Sub-scenes > Make a Sub-scene From the Selection) or
`session.subscene_create`, renamed and deleted (one undo step each), launched with `session.subscene_launch`, and kept
in a project file as its own `<z:subscenes>` section (`docs/SCENES-NATIVE.md` §4; `SessionSubSceneTest`,
`SessionSubSceneGridTest`). What it does not do:
- A sub-scene is one scene's cells. Cells from several scenes cannot be combined into one.
- Launching one does not mark the row as the launched scene, so the scene's own Follow Action chain does not fire,
  and the grid's scene button shows the row's state, not the sub-scene's. The columns outside it play on.
- It names columns by position. Moving or deleting a track, or shrinking the grid, does not move or drop a
  sub-scene: a column or scene that is no longer there is skipped at launch (`skipped`), or the launch is refused
  when its scene is gone.
- Making one from the grid names it "Sub-scene N"; renaming is a second step (Sub-scenes > Rename...). The selection
  stays after it is made.
- Perform mode has no sub-scene buttons, and the controller bindings (`session.bind`) cannot launch a sub-scene.
- A project whose `<z:subscenes>` section came from a newer build keeps that section as it came; sub-scenes made in
  this build on top of it are not saved while it is there.
- What an older build does with a new project was checked by reading its loader (`Song::restoreNamedSection` has no
  branch for `<z:subscenes>`, and its `<z:scenes>` claim, unchanged here, still claims the section this build writes -
  asserted in `SessionSubSceneTest`), not by running an older binary on the file.

## Undo checkpoints for project.open and script.run (Q050-044, 2026-10-09)

`project.open` over a session with unsaved changes, and a `script.run` that changes the project, leave one undo step
that restores the whole project (`docs/A16-REVERSIBILITY.md` §1.2; `SessionCheckpointTest`). What they do not do:
- The checkpoint is the document a save would build, not a copy of the machine's state. Audio files are referenced
  by path, so a file deleted or edited on disk between the run and the undo is not brought back. Samples that live
  only in memory (no file) are inside the document and count toward the size.
- It is bounded by the undo stack's byte budget (`control.undo_depth` `cap_bytes`, default 16 MiB, set with
  `control.set_undo_depth`), measured as the sum of the compressed pre-state and redo-state snapshots. A pre-state over
  the limit is REFUSED before the open or script runs. If the post-state makes the pair too large, the old project is
  restored and the action is REFUSED; the message names the sizes and budget. There is no "do it anyway": save the
  project, or raise the budget.
- Only a session with unsaved changes is checkpointed by `project.open`. A saved one is held by its file, so the open
  records no step (`reversible: false`, the displaced file and hash are named) and `control.undo` refuses it typed.
- When a checkpoint is held, partial loads retain their skipped-section list in its metadata. Undo and redo preserve
  the partial state and filename for a partial `project.open` result or a script run that leaves a partial result.
- Undoing an open brings back the displaced session and its file name, but not its earlier undo steps: the stack
  holds the steps of the project that was opened. A project that persists its undo history (WF-033) and is opened
  over a dirty session has a step with no stored snapshot on top of its loaded history, so a save from there writes
  no history until newer steps replace it (`project.save`'s `undo_history` says so).
- `script.run` replaces the finer undo steps the script pushed (Lua `addCheckPoint()`, edits through the bindings)
  with its one step; the order of its edits is not separately undoable. Effects outside the project (files the script
  wrote, the console) are not undone.
- `control.undo` after a `control.redo` of the same step refuses typed, as for every command (a redo gives the step a
  fresh serial, so the first run's record no longer names it); the interface's Ctrl+Z, which has no record, undoes it.
- `project.new`, `project.switch`, `project.new_document` and `session.setlist_go` keep their `irreversible` class and
  `project.new` and `project.new_document` take no checkpoint. `project.switch` and `session.setlist_go` run `project.open`, so a dirty session they displace is
  checkpointed and the interface's Ctrl+Z restores it (`project.switch` saves the current project first, so there is
  usually nothing to displace), but their own records still refuse `control.undo`.
- **The v2 project container is opt-in and older builds cannot open it (Q050-007).** Saving as a v2 container
  (`project.save {format: "v2"}`, the Save As checkbox, or `app/savecontainerv2`) writes a larger `.mmpz` that
  builds before 0.5.0 refuse; ordinary saves stay legacy. The asset verbs (`project.missing_assets`,
  `project.hash_assets`, `project.relink`) and the revision compare do not read containers yet. See
  `docs/PROJECT-CONTAINER-V2.md`.
