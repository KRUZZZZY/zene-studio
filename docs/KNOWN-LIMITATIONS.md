# Zene Studio 0.2.0-alpha: known limitations

What this alpha does **not** do, in the order you are most likely to hit it. If something here surprises you,
that is this page's fault — report it and it gets added.

> **Verification convention.** `[VERIFY AT FREEZE]` marks a claim that must be re-checked against the built
> artefact before it ships; nothing carrying it goes out unverified, and no unverified claim goes out without
> one. This page's path is `docs/KNOWN-LIMITATIONS.md` — there is no version-suffixed 0.2.0 limitations file.
> Applied verbatim from `drafts/KNOWN-LIMITATIONS-v0.2.0-alpha-DRAFT.md` (reviewed) on
> `post-alpha/release-prep` (base `post-alpha/integration` @ `34c1f4f86`) except for the marker resolutions
> noted inline; the resolution table is `docs/RELEASE-PREP-0.2.0.md` §3.

## Before you download

- **This is an alpha.** Parts of it are unfinished and crashes are possible.
- **Keep backups.** Files saved by this build may not open in a later build, an older build, or in LMMS. Copy
  the project folder or use **File > Save As** before you open anything here, and keep the original.
- **The builds are unsigned.** Windows SmartScreen and macOS Gatekeeper will warn about an unknown developer.
  Do not answer either warning by turning protection off.
  **The pointer that used to sit here — "the one-time steps are in the release notes" — is deleted, not
  repaired.** The 0.2.0 release notes carry no per-platform first-run steps, and neither does this page, so the
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
- **Session View data survives a round-trip through this build, but the feature is not in it.** The session
  data layer and the launch scheduler that builds on it are **in the source and not compiled into these
  builds** (`WANT_SESSION_VIEW` defaults off and our release jobs do not pass it; the same is true of the WASM
  sandbox and stem separation). A project containing `<session>` data is **preserved** by this build rather
  than dropped — that is fixed in this release — but a build with the feature compiled out cannot *use* it, and
  even a build with the flag on has **no clip launcher and no clip grid**, so there is no way to operate it from
  the interface.
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
  (`tests/CMakeLists.txt`, default `OFF`), so the default CI configuration does not build or run it; the guard was proven by running it out of
  band, green with it and `SIGSEGV` exit 139 at address `0x8` without it (§4).
- **Instrument hosting is new and narrow.** One instrument per track, MIDI in to audio out. **No third-party
  VST3 instrument has been tested by us** — the only instrument this release has been proven against is a
  purpose-built test instrument we ship in the source tree
  (`tests/data/vst3-test-instrument/`, an MIT VST3 fixture, proven by the SDK's own validator and by the
  `Vst3InstrumentFixtureProbe` probe — `docs/VST3-INSTRUMENT-FIXTURE.md` §0, §5). There is no multi-out, no
  preset management, no instrument latency compensation, and no out-of-process hosting. **No instrument hosting
  in CLAP.**
- **No VCA groups in the interface.** Mix-and-edit groups exist, are tested, and are saved with the project —
  but **a group can only be created by editing the project file** (`<vcagroup>`); there is no way to create one
  from the interface yet.
  Verified in the tree: the premise of the bullet holds and is now checkable — the group is a real entity
  (`src/core/VcaGroup.cpp`, `include/VcaGroup.h`, owned by the mixer via `Mixer::createVcaGroup`,
  `src/core/Mixer.cpp:720`), its gain is applied on the audio path (`:528-545`), and the save/load element it is
  written as is `vcagroup` with a `vca` child (`:1893`, `:1898`, `:2022`, `:2037`). `docs/VCA-GROUPS.md` is the
  implementing lane's report; the lane is an ancestor of this tip. What has *not* changed is the half that
  matters to a user: nothing in the interface creates a group, so the way to get one is still to edit the
  project file.
- **No racks in the interface, and no scripting access.** Parallel chains and a chain selector exist and are
  saved with the project, but a user can only load a project that already contains a `<rack>`; there is no UI
  and no binding. Switching chains is not crossfaded, so it can click.
  Verified in the tree: a rack is saved as the `rack` element inside a `<mixerchannel>`
  (`src/core/Rack.cpp:48`, `RACK_ELEMENT`; the chains and the selector are built in `Rack.cpp` /
  `RackNodes.cpp`), `docs/RACKS.md` §0 is the implementing lane's report, and the lane is an ancestor of this
  tip. The user-facing half is unchanged and is the point of the bullet: no UI and no scripting binding, so a
  rack can only be reached through a project file.
- **No clip editing gestures.** The clip model is in (an authored window that survives playback and is saved
  with the project) but there are **no trim, slip, fade, crossfade or clip-gain tools** in the UI yet.
- **No take lanes and no comping.** Verified as an absence in this tree:
  `grep -rniI "takelane\|take lane\|comping" src/ include/` returns 0 hits.
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
- **Our own test coverage, measured on this release tree: 81.60 % of the lines we instrument (7,113/8,717),
  over the 119 of the 175 fork-scope entries the scope held then that produced a record.** (Merge train 3F
  has since grown the fork scope from 175 to 242 entries, so that pair belongs to the capture and is not a
  ratio over the whole scope today.) The **ratchet scope** — the gate's own per-file baseline — is at
  **85.77 %**, which is above our 85 % aspiration; the headline is lower because a fuller configuration
  instruments more files. The other 56 entries are sources this configuration does not compile, headers no
  translation unit instantiates, and tooling, and **the gate prints that split itself**, so the number is a
  claim about the 119 files it names and not about the whole scope.
  How it was measured, so you can repeat it: `tests/run-coverage.sh build-coverage` on this tree with the pinned
  VST3 SDK and CLAP headers provisioned and **`-DWANT_VST3_TEST_INSTRUMENT=ON`**, so the plugin modules are
  instrumented *and* their integration suites actually run. An earlier capture of the same tree with the fixture
  off measured **75.06 % over the same 119 files** — the 6.5-point difference is eight plugin files that go from
  0 % to covered once those suites run, which is why the fuller configuration is the honest one.
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
- **Warping changes pitch.** The warp engine attaches markers and lets a clip follow or lead the project tempo,
  but the time-stretch is done by resampling: a 2× stretch is an octave up. Pitch-preserving stretch is not
  built.
  Verified in the tree: `docs/WARP.md` §0 is the implementing lane's report — `WarpMarkers` is a child element of
  `<sampleclip>`, markers are pinned to source frames so a trim moves `sourceIn`/`sourceOut` and the markers stay
  on the audio, the map is monotonic and exact at every marker, with no markers it is the pre-warp arithmetic bit
  for bit, and a headless render puts a source whose transients are at 0/1/2/3 s at 0/0.5/1.0/1.5 s under a
  marker pair declaring 2× — while the base binary renders that project as if the `<warp>` element were absent.
  The lane `post-alpha/warp` is an ancestor of this tip.
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

## Telemetry and privacy

- **Telemetry is off unless you turn it on**, and the consent screen shows you the exact payload before you
  decide. The payload is built from a **closed allowlist of 24 fields** (platform and hardware summary, plugin
  counts, crash counters) and **cannot** carry a project name, a file path, a plugin name, an email address, an
  IP address or an installation ID — that is enforced in code and tested, and the bytes you preview are the
  bytes produced. It cannot be turned on by a default, and a distribution can build it out entirely.
  Verified in the tree: `docs/TELEMETRY-V1.md` is the implementing lane's report — the allowlist is
  **24 keys and closed** (§3), the mutator refuses anything outside it, the consent state defaults to all-false,
  and the preview renders the exact bytes produced; the packager kill switch is `option(ZENE_TELEMETRY … ON)`
  (`CMakeLists.txt:140`), whose `OFF` compiles the client, its consent screen and its networking code out —
  **the whole client, so the binary carries no `telemetry` symbol and no `telemetry` string, and the
  `telemetry.*` commands are absent from the registry (72 commands instead of 74)**. Verified in the tree:
  `docs/TELEMETRY-KILL-SWITCH.md` is the repair's report — both configurations measured, the ON object
  byte-identical, ctest 86/86, the render sha256 unchanged — and `tests/telemetry-off-build.sh` re-runs the
  OFF build and the two counts, so the switch cannot rot again in silence. The lane
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
