# Zene Studio v0.1.0-alpha known limitations

What this alpha does **not** do, in the order a new user is likely to hit it. If something here
surprises you, that is this page's fault: report it and it gets added.

## First five minutes

- **The app is called `lmms`.** The executable, the desktop entry and the window title use LMMS
  names; the Zene Studio rename is a later wave. This build also shares LMMS's config file
  (`~/.lmmsrc.xml`) and working folder (`~/Documents/lmms/`) with any LMMS you have installed, so
  settings you change here change there too.
- **The first launch prints a line about Carla.** Started from a terminal, the Linux AppImage may
  print `[AppRun] Carla does not appear to be installed, we'll remove it from the plugin listing.`
  That means the Carla plugin (and the patchbay and rack built on it) is hidden from the plugin
  list because Carla is not installed system-wide. Nothing else changes; the other AppRun lines
  (JACK, the Qt platform) depend on your system.
- **No plugin scanning.** Plugins are found in the standard folders. There is no scan, cache or
  blacklist UI, no rescan button, and a plugin that fails to load does not tell you why.
- **VST3 and CLAP are effects only.** No instrument in either format can load. VST2 instruments
  still work through Vestige.
- **No crash reporter, but autosave recovery exists.** A crash sends no report anywhere. Autosave
  writes the project to `recover.mmp` in your working folder (by default about every two minutes,
  while not playing), and the next start offers to recover it. Recovery is only as fresh as the
  last autosave, so keep saving.
- **No log file.** The app prints to the terminal you start it from and writes nothing to disk.
  For a bug report, start it from a terminal and copy the text.

## Recording and editing

- **Two-track capture is a prototype, and only JACK and SDL feed it.** Two input channels record
  to two tracks. The recorder was verified end to end with a synthetic source and with a real
  capture device in the project's tests, but in the app the audio input only reaches it through
  the JACK and SDL backends. The ALSA backend has no capture path, so under ALSA hardware input
  records silence. There is no arbitrary input count and no input monitoring.
- **No punch in/out, no take lanes, no comping.** Recording starts and stops with transport; there
  is no way to punch a section or stack takes.
- **No clip editing.** No trim, slip, fades, crossfades or clip gain. Clips are placed and moved,
  not shaped.
- **No warp engine.** Audio does not follow the project tempo; there is no time-stretch or clip
  pitch shift.

## Mixing and finishing

- **No automation modes**, and no sample-accurate automation beyond what LMMS already provided.
- **No LUFS metering.**
- **No freeze, no bounce-in-place, no stem export.**
- **No multicore graph scheduling.** Expect real-time performance to be limited on large sessions.
- **PDC scope.** Plugin delay compensation covers in-process chains and the mixer graph, based on
  the latency each effect reports. Remote (out-of-process) plugins do not report their own
  latency, so a VST2 or ZynAddSubFx plugin that delays its output is not compensated.

## Workflow

- **No Session View.** The clip launcher is in development and not in this build.
- **No racks, chain selector or macros.**
- **No MIDI learn, no controller surfaces, no Ableton Link.**
- **No groove pool, scale awareness or note probability.**
- **No browser audition or drag-and-drop** beyond LMMS's existing file browser.
- **The Patcher node-graph engine is not available.** The engine is in the tree and unit-tested,
  but no GUI or audio path instantiates it yet.

## Plugin engine: what is migrated, and how far each part is proven

- **Migrated last:** `Vestige` (VST2 hosting), `ZynAddSubFx`, `VstBase` (the remote plugin base)
  and `VstEffect` (VST2 effects). The built-in devices and the hosts for VST3, CLAP, LV2, SF2,
  GIG, STK/Mallets and LADSPA were already on the new engine.
- **Proven sample-exact:** 44 plugins are compared sample-exact against stored pre-migration
  reference renders (`tests/reference/`). `PeakControllerEffect` is migrated but sits outside
  that render harness.
- **Proven by build and unit tests only:** the four directories above. The remote plugin process
  path has no sample-exact render comparison yet, because no such harness exists for
  out-of-process plugins.
- **Not on the new engine:** three effects added by this project (the RNNoise denoiser, the NAM
  amp and the WASM sandbox) still use the old per-buffer interface. They work, but they expose no
  audio ports, so the new multi-channel routing does not apply to them.

## Your projects

- **No project-format stability promise yet.** A project saved by v0.1 may not open in an older
  build. That commitment arrives with v1.0.
- **LMMS projects open, but no migration guarantee is offered** for this alpha.
- **Keep copies of anything you care about.**

## Platforms

- **Packages come from the release page, and only from there.** Build jobs upload packages for a
  tag build or a manual CI run, never for an ordinary push. If the release is not published yet,
  the only builds in existence are CI packages from earlier runs; those report
  `LMMS 1.3.0-alpha`, while a build from the release tag reports `LMMS 0.1.0-alpha`.
- **Linux ships as an AppImage**, one file per architecture. There is no tarball and no `.deb`.
- **Windows and macOS builds are not signed for distribution.** Windows warns through SmartScreen;
  macOS carries an ad-hoc signature only, so Gatekeeper refuses the first launch until you
  right-click *Open*.
- **Only the platforms whose build job is green have packages.** Check the release page for the
  current set.

## Not planned for the final product

This section lists features the project has decided not to build. It is being assembled: three
research passes (in-DAW capability gaps, DAW ecosystem gaps, and auto-mastering) are each
producing an evidence-based "not planned" list, and each list is appended here as it lands. Until
then, treat anything absent from the release bars in `docs/STATUS.md` as unplanned.

## Where the plan lives

The full picture: capabilities by release bar (alpha / complete DAW / competitive), the feature
backlog with task references, and the known quality-gate gaps. See `docs/STATUS.md` in the
repository.
