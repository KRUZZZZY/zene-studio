# Zene Studio 0.2.0-alpha: known limitations

What this alpha does **not** do, in the order you are most likely to hit it. If something here surprises you,
that is this page's fault — report it and it gets added.

> **Verification convention.** `[VERIFY AT FREEZE]` marks a claim that must be re-checked against the built
> artefact before it ships; nothing carrying it goes out unverified, and no unverified claim goes out without
> one. Applied verbatim from `drafts/KNOWN-LIMITATIONS-v0.2.0-alpha-DRAFT.md` (reviewed) on
> `post-alpha/release-prep` (base `post-alpha/integration` @ `34c1f4f86`) except for the marker resolutions
> noted inline; the resolution table is `docs/RELEASE-PREP-0.2.0.md` §3.

## Before you download

- **This is an alpha.** Parts of it are unfinished and crashes are possible.
- **Keep backups.** Files saved by this build may not open in a later build, an older build, or in LMMS. Copy
  the project folder or use **File > Save As** before you open anything here, and keep the original.
- **The builds are unsigned.** Windows SmartScreen and macOS Gatekeeper will warn about an unknown developer.
  The one-time steps are in the release notes. Do not answer either warning by turning protection off.
  `[VERIFY AT FREEZE: this pointer is dangling — the 0.2.0 release notes carry no per-platform first-run
  steps, and this page does not either. The 0.1.0 page's "Getting it running" section (Linux: FUSE 2 /
  `--appimage-extract-and-run`, no menu entry; Windows: More info → Run anyway; macOS: Privacy & Security →
  Open Anyway, and the macOS 15 note that right-click-Open no longer works) was preserved as
  `docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md` when this page replaced it. Copy that section forward into the
  0.2.0 documents or restore it here — a pointer to steps that do not exist is worse than no pointer]`
- **Every save writes a `.bak` next to your project.** Verified in the tree: the backup is
  `<project file>.bak`, beside the project — `src/core/DataFile.cpp:347` composes `fullName + ".bak"` and
  `:435` moves the current file there before the new one is renamed into place at `:438`. It is not written
  when the `app/disablebackup` setting is on (`:427`).

## Files, formats and older builds — read this before you trust a project file

- **The version of the application that wrote a project is stamped into the file** (`creatorversion`), and that
  is what drives the behaviour below. Verified in the tree: `creatorversion` is an attribute of the project
  root, written as the writing build's version string (`src/core/DataFile.cpp:140`, `:2105`) and read back on
  load (`:2179`), where it drives both the "Version difference" notice and the choice of upgrade routine to
  run (`legacyFileVersion()`, `:2226-2238`).
  `[VERIFY AT FREEZE: the *behaviour* half — "an older build opens a new file and silently drops parts of it" —
  is a claim about a different binary (LMMS 1.3.0-alpha) and cannot be executed on this machine or from this
  tree]`
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
- **After a failed open, save and restart.** A failed load leaves modified-tracking, undo and autosave in a
  degraded state until the application is restarted.
  `[VERIFY AT FREEZE: whether this release fixed it — the lane that owns the fix
  (`post-alpha/saveload-integrity`) is not in the release-prep base 34c1f4f86, and no fix for it is present
  there]`
- **A failed save is now reported rather than silent.** If a project cannot be moved aside on save (an existing
  file the platform refuses to rename over), the save is refused **and you are told**, rather than reporting
  success.
  `[VERIFY AT FREEZE: the wording you actually see, and the behaviour itself — `post-alpha/saveload-integrity`
  is not in the release-prep base 34c1f4f86, and the defect is still observable there: the two renames that
  publish a saved project (`src/core/DataFile.cpp:435`, `:438`) discard their return values, so
  `DataFile::save` returns `true` even when they fail]`
- The project file is now `<zene-project creator="Zene Studio">`. The reader still accepts the old root, so
  older files open — but the writer only emits the new one.

## What this alpha cannot do at all

- **No instrument editor.** You can load a VST3 instrument and play it, but the plugin's own GUI **does not
  open**. What you get instead is the host's generated control grid, and we have **run it** rather than assumed
  it: with a VST3 instrument track loaded, the instrument window opens and lists the plugin's controls (verified
  against our own test instrument, whose `Level` knob appears as expected). A third-party instrument's grid may
  be larger or less tidy than that one — that is untested, not claimed.
  Verified against this tree, for the "no editor" half: `grep -rn IPlugView src/ include/ plugins/Vst3Effect/
  plugins/ClapEffect/` returns **0** hits — the only `IPlugView` occurrences in the repository are inside the
  vendored Carla copy of the VST3 SDK headers (`plugins/CarlaBase/carla/source/includes/vst3sdk/...`) — so the
  plug-in's own editor is not implemented in the host and the parameters can only surface as the generated grid
  (`docs/INSTRUMENT-HOSTING-SPEC.md` §0).
  `[VERIFY AT FREEZE: the "window opens and lists the controls" half — `post-alpha/instrument-view-safety`
  (Block D5) is not in the release-prep base 34c1f4f86]`
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
  `[VERIFY AT FREEZE: post-alpha/vca is not in the release-prep base 34c1f4f86, and `<vcagroup>` appears
  nowhere under `src/` or `include/` there — so on this base the premise of this bullet does not hold at all,
  and the bullet must be kept, sharpened or dropped once that lane is merged]`
- **No racks in the interface, and no scripting access.** Parallel chains and a chain selector exist and are
  saved with the project, but a user can only load a project that already contains a `<rack>`; there is no UI
  and no binding. Switching chains is not crossfaded, so it can click.
  `[VERIFY AT FREEZE: post-alpha/racks is not in the release-prep base 34c1f4f86]`
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
  `Build options:` line). Everything in the release notes is present in this build — the release-honesty check
  enforces that — but these two are deliberately absent.

## Where the quality bars are not met yet

- **Renders are reproducible — with two exceptions.** Exports now render on a single thread, so for **7 of the
  9 bundled projects two renders are byte-identical**. **Two are not**: `Root84` and `StrictProduction` differ
  even with the CPU pinned, ASLR disabled, `rand()` fixed and the clock frozen, and for `Root84` bypassing all
  twelve of its effect chains changes nothing — the cause is inside those instruments, not the renderer, and it
  is named in our notes. In practice: treat a render as reproducible for most projects, and verify rather than
  assume for those two. (Stock LMMS 1.3.0-alpha.2 is non-reproducible for the same demos, so this is a fix we
  carry that upstream does not.) The two, named exactly as the tree ships them:
  `demos/StrictProduction-DearJonDoe.mmp` and `shorties/Root84-TrancyLoop.mmpz` —
  `docs/RENDER-DETERMINISM.md` §9 and §10.
- **No measured crash-free rate.** The crash reporter is new in this release; until there is a body of reports
  the "how often does it crash" number does not exist. That number is the point of shipping an alpha.
- **Our own test coverage is 84.34 % of the lines we instrument, over 67 of the 139 scope entries.** The other
  72 are **29 sources that are not compiled in this configuration** (7 CLAP, 7 VST3, 6 stem separation, 5 WASM,
  3 Session View, 1 harness), **41 headers that no translation unit instantiates**, and 2 tooling entries. That
  means the figure is **not** a statement about the whole source tree — the gate now prints that split itself —
  and it is still **below our own 85 % aspiration**, which we are not claiming to meet.
  `[VERIFY AT FREEZE: the number from the frozen tree. No coverage run exists for the release-prep base
  34c1f4f86: 84.34 % is the unmerged `post-alpha/coverage-green` lane's figure, and the coverage run that IS
  merged measured 81.46 % fork-scope (3747/4600 over 61 files with a record, `docs/COVERAGE-RUN.md`). Re-run
  Gate 2 on the frozen tree and paste whatever it says — 84.34 % must not be shipped unverified]`
- **Automation is not sample-accurate.** Modes work (Read / Touch / Latch / Write) and riding a control in Read
  cannot destroy written automation, but automation is evaluated once per tick, so it lands on a tick boundary
  rather than a sample. Verified in the tree via the automation lane's own record: `docs/AUTOMATION-MODES.md`
  states that automation is evaluated once per tick and names the line that stands between that and a
  per-frame read.
- **Warping changes pitch.** The warp engine attaches markers and lets a clip follow or lead the project tempo,
  but the time-stretch is done by resampling: a 2× stretch is an octave up. Pitch-preserving stretch is not
  built.
  `[VERIFY AT FREEZE: post-alpha/warp is not in the release-prep base 34c1f4f86]`
- **MPE applies pitch only.** Per-note expression is captured from MPE input, stored on the note and editable;
  **pitch is applied on playback, while pressure and timbre are captured, stored and readable but not applied.**
  There is no per-note expression editor.
  `[VERIFY AT FREEZE: post-alpha/mpe is not in the release-prep base 34c1f4f86]`
- **Recording is a two-track prototype.** Two input channels captured into two tracks, with the capture path
  hardware-verified. Arbitrary input counts and input monitoring are not implemented, and the default Linux
  ALSA backend has **no capture path at all** — recording needs JACK or SDL.
- **Shutdown waits rather than aborts, and that is deliberate.** This release fixes a crash where the
  application could die on exit (`QThread: Destroyed while thread is still running`) because the engine gave up
  waiting for an audio worker. The fix makes that wait unbounded: if a job ever failed to return, shutdown
  would **block until it did** instead of aborting. We chose a hang over a crash.
  `[VERIFY AT FREEZE: post-alpha/test-hygiene is not in the release-prep base 34c1f4f86 — the fix, the
  stranded-worker assertion and the teardown test all live on that lane]`

## Telemetry and privacy

- **Telemetry is off unless you turn it on**, and the consent screen shows you the exact payload before you
  decide. The payload is built from a **closed allowlist of 24 fields** (platform and hardware summary, plugin
  counts, crash counters) and **cannot** carry a project name, a file path, a plugin name, an email address, an
  IP address or an installation ID — that is enforced in code and tested, and the bytes you preview are the
  bytes produced. It cannot be turned on by a default, and a distribution can build it out entirely.
  `[VERIFY AT FREEZE: post-alpha/telemetry is not in the release-prep base 34c1f4f86 — the allowlist, the
  preview and the "off by default" claim are all on that lane]`
- **Telemetry v1 is inert: there is no server to send to yet.** The client is complete and refuses to open a
  connection; even switched on, **nothing leaves your machine**. That is stated plainly because a privacy
  control that appears to do nothing is worth less than one you can see working.

## The name, honestly

This release renames the product to Zene Studio: the application name, the packaging, the desktop entry and man
page, the configuration and project paths (migrated from the old ones, so your settings are adopted rather than
orphaned), the MIME types, the names other audio software sees us by, and the plugin logo.

`[VERIFY AT FREEZE: the sentence above is contradicted by the tree and must be corrected before it ships. The
configuration and project paths are NOT migrated and were NOT renamed: `docs/WAVE-R-RENAME.md` §6 ("User
state") records that `~/.lmmsrc.xml`, the `<lmms>-config-file` root, `~/Documents/lmms/` and the
`lmms-workspace` marker were deliberately left alone because renaming them would orphan an existing install's
settings and projects, and `post-alpha/integration` @ 34c1f4f86 contains no migration code
(`grep -rniI "migrat" src/ include/` matches comments about the *plugin* migration and nothing else). What
does absorb an older install's state is that the paths never moved — the honest phrasing is that 0.2.0-alpha
reads the same files 0.1.0-alpha did, not that it migrated them]`

Two things deliberately keep the old name, and neither is an oversight:

- **The licence notices and the "derived from LMMS" attribution.** This is a derivative work under
  GPL-2.0-or-later; retaining upstream notices is a condition of the licence, and removing them would be
  misattribution, not a rename.
- **Internal code identifiers** — the `lmms::` namespace, `LMMS_*` macros and file names such as
  `lmmsconfig.h`. No user can see these, and renaming them is a large code-wide change rather than an identity
  change. Related and more important: the **plugin entry symbol is unchanged, so existing native plugins still
  load**. If that symbol is ever renamed it becomes a deliberate ABI break, and it will be announced as one.

The plugin logo's artwork is currently the upstream artwork, which is CC0-licensed and credited.
`[VERIFY AT FREEZE: whether the owner's replacement mark landed in time — `post-alpha/brand-placeholders` is
not in the release-prep base 34c1f4f86, so on this base the artwork is still the upstream mark. Per Block D0
the placeholder branch must additionally prove the mark resolves at runtime (PixmapLoader returns a 1×1
transparent pixmap on a miss, so a green build proves nothing)]`
