# Zene Studio 0.1.0-alpha: first public alpha

**This is the first installable alpha of Zene Studio.** Expect crashes, missing features and rough
edges. It is for testing and feedback, not for music you cannot afford to lose. Read
[KNOWN-LIMITATIONS.md](KNOWN-LIMITATIONS.md) before you install it.

## Read this before you download

- **This is an alpha.** Some areas are unfinished, crashes are possible, and features can change or
  disappear without notice.
- **Back up your projects before you open them.** Files saved by this build may not open in a later
  build, in an older build, or in LMMS. Copy the project folder or use File > Save As before you
  open anything here, and keep the original file. There is no easy way back once a project has been
  saved by this alpha.
- **All builds are unsigned.** Windows SmartScreen and macOS Gatekeeper warn about an unknown
  developer. That is expected for this alpha. It is not a sign that a download is infected, and it
  is not something you fix by disabling your protection. Verify your download against the SHA-256
  digest shown next to the file on the release page; the commands are under [Install](#install).
- **Platforms with builds:** Linux x86_64 and arm64, Windows x64 (two installers) and Windows on
  Arm, macOS Apple Silicon and Intel. Only platforms whose build job is green have packages.

## What this is

Zene Studio is an open-source (GPL-2.0-or-later) digital audio workstation built on
[LMMS](https://github.com/LMMS/lmms) 1.3.0-alpha plus this project's work. It is not affiliated
with the LMMS project and not endorsed by it. Upstream notices are retained.

### Which build you have

The release is **Zene Studio 0.1.0-alpha**, tagged `v0.1.0-alpha`. The app still installs and
introduces itself as `lmms`: the rename wave (binary name, desktop entry, icons, window titles,
config and data paths) is deferred, so `--version` and Help > About print an `LMMS` version string.
That string is build provenance, not a second product name:

- a build from this release reports `LMMS 0.1.0-alpha`;
- CI snapshots built before this release's version bump report `LMMS 1.3.0-alpha`, the inherited
  LMMS version.

Use that string to tell which build you have. Until the rename lands:

- the executable, the desktop entry, the window title and the About box all say LMMS;
- the config file is `~/.lmmsrc.xml` and the working folder is `~/Documents/lmms/`;
- if you already run LMMS, this build shares that config file and folder with it. Settings you
  change here (audio device, plugin folders, samples) change there too.

The rename checklist is in `DOCS-NAMING.md` in the repository root.

## Download

Packages are attached to the GitHub release:

- Release page: https://github.com/KRUZZZZY/zene-studio/releases (tag `v0.1.0-alpha`)

If the page shows no build, the release is not published yet. Packages are uploaded for a tag build
or a manual CI run only, so until the release appears there is nothing to download. Only the
platforms whose build job is green have packages.

| Platform | File |
|---|---|
| Linux x86_64 | `lmms-0.1.0-alpha-linux-x86_64.AppImage` |
| Linux arm64 | `lmms-0.1.0-alpha-linux-aarch64.AppImage` |
| Windows x64 | `lmms-0.1.0-alpha-msvc2022-win64.exe` or `lmms-0.1.0-alpha-mingw-win64.exe` |
| Windows on Arm | `lmms-0.1.0-alpha-clangarm64-arm64.exe` |
| macOS | `lmms-0.1.0-alpha-mac<os>-<arch>.dmg` (`arm64` or `x86_64`) |

Windows x64 gets two installers because the project builds with two compilers. Both install the same
alpha.

## Install

**Linux (AppImage, no installation needed).** The build is one file. There is no installer, no
tarball and no `.deb`.

1. Download the AppImage for your architecture.
2. Make it executable and run it:

   ```bash
   chmod +x lmms-0.1.0-alpha-linux-x86_64.AppImage
   ./lmms-0.1.0-alpha-linux-x86_64.AppImage
   ```

3. If it fails with an error mentioning **FUSE**, install the FUSE 2 library and try again:
   `sudo apt install libfuse2`. On Ubuntu 24.04 the package is named `libfuse2t64`.
4. If it still does not start, run it unpacked:
   `./lmms-0.1.0-alpha-linux-x86_64.AppImage --appimage-extract-and-run`.
   Use this as a fallback only: it unpacks the app on every run, so it starts slower.

AppImages do not add a menu entry by themselves, so the app does not appear in your applications
menu unless you install a desktop-integration helper.

**Windows (unsigned installer).** Run the `.exe` installer. It installs the app and adds Start menu
entries. The installer is not code-signed, so SmartScreen warns about an unrecognised app: choose
*More info*, then *Run anyway*. That is the whole detour, and the installer runs normally after it.
Do not turn SmartScreen off for this; the warning is expected, and the checksum below is how you
check the file.

**macOS (unsigned disk image).** Open the `.dmg` and drag the app into **Applications**. The app has
an ad-hoc signature only (no Developer ID, no notarisation), so Gatekeeper refuses the first launch.
Click **Done** on the warning, then open **System Settings > Privacy & Security**, scroll to
**Security**, click **Open Anyway**, and confirm with **Open**; your login password may be requested.
After that the app opens normally by double-clicking. On macOS 15 (Sequoia) and later the old
"right-click, then Open" shortcut no longer works, so use the Privacy & Security button. Do not turn
Gatekeeper off. Terminal alternative: `xattr -dr com.apple.quarantine "<app>"`.

**Verify your download.** The release page shows a SHA-256 digest next to each file. Compute the
checksum of the file you downloaded and compare:

- Linux: `sha256sum lmms-0.1.0-alpha-linux-x86_64.AppImage`
- macOS: `shasum -a 256 <the .dmg you downloaded>`
- Windows (Command Prompt): `certUtil -hashfile <the .exe you downloaded> SHA256`

If the values differ, delete the file and download it again. Do not run it.

There is no package repository, no release channel and no auto-update. Check the release page for
the next one.

## What works (and is tested)

- **Multi-channel engine.** Dynamic routing, sidechain sends and parallel buses on an unbounded
  mixer, with plugin delay compensation across chains and summing points (null-tested).
- **VST3 and CLAP hosting, effects only.** Modern effects load, run and save their state.
  Instruments in either format do not load; see the limitations page.
- **Two-track recording (prototype).** Two input channels record to two tracks. In this build the
  audio input only reaches the recorder through the JACK and SDL backends; see the limitations
  page.
- **Lua 5.4 scripting.** Script the DAW, with an instruction budget. The WebAssembly DSP sandbox
  needs wasmtime at configure time and is off in these builds.
- **AI DSP.** RNNoise noise suppression and NAM neural amp modelling. Offline HTDemucs stem
  separation is not compiled into these builds.
- **Slide notes, HiDPI scaling and git-friendly `.mmpz` project files**, with a semantic diff and
  a merge driver in `tools/mmpz-git`.
- **Autosave recovery.** The project is autosaved to a recovery file, and the next start offers
  to recover it. This is not a crash reporter.
- **Everything inherited from LMMS.** Piano Roll, Beat/Bassline editor, Song Editor, mixer, 15+
  built-in synthesizers, SoundFont2, VST2 (Vestige), LADSPA, LV2, GUS patches, MIDI import and
  export.

## Missing features in this alpha

No instrument hosting (VST3 and CLAP are effects only). No clip editing (trim, slip, fades,
crossfades, clip gain). No take lanes or comping. No punch in/out. No input monitoring. No plugin
scanning or rescan; plugins are found in the standard folders. No multicore graph scheduling. No
automation modes. No LUFS metering. No freeze, no bounce-in-place and no stem export. No warp or
time-stretch. No Session View. No racks or macros. No MIDI learn. No Ableton Link. No crash
reporter. WASM scripting and offline stem separation are compiled out of these builds; the binary
reports `WANT_WASM=OFF` and `WANT_STEM_SPLIT=OFF`. The Patcher node-graph engine is in the tree but
nothing in the app uses it.

Full list, ordered by when you will hit it: [KNOWN-LIMITATIONS.md](KNOWN-LIMITATIONS.md).

## Known issues in this release

- **The first Linux launch prints a line about Carla.** Started from a terminal, the AppImage may
  print `[AppRun] Carla does not appear to be installed, we'll remove it from the plugin listing.`
  That is expected: it hides the Carla plugin and the patchbay and rack built on it, because Carla
  is not installed system-wide. The other AppRun lines (JACK, the Qt platform) depend on your
  system.
- **No log file and no crash reporter.** The app prints to the terminal you start it from and
  writes no log file; a crash sends no report anywhere. The terminal output is the record, and
  autosave recovery is only as fresh as the last autosave.
- **Plugin delay compensation covers in-process chains only.** It is based on the latency each
  effect reports. Out-of-process plugins (VST2 through Vestige, ZynAddSubFx) do not report their
  own latency, so a delaying one is not compensated.
- **The four plugin families migrated last (Vestige, ZynAddSubFx, VstBase, VstEffect) are proven at
  build and unit-test level only.** The out-of-process plugin path has no sample-exact render
  comparison yet.
- **Three effects added by this project still use the legacy per-buffer interface** (the RNNoise
  denoiser, the NAM amp and the WASM sandbox). They work, but they expose no audio ports, so the
  new multi-channel routing does not apply to them.

## Your projects

- There is **no project-format stability promise** yet; it arrives with v1.0. A project saved by
  this alpha may not open in an older build, in LMMS, or in a previous alpha. Do not try to open a
  v0.1 project in an older build.
- LMMS projects open, but no migration guarantee is offered for this alpha.
- After a crash, the autosave recovery file is offered on the next start. It is only as fresh as
  the last autosave, so save when you care about the work.

## Reporting a bug

Use the alpha feedback form:
https://github.com/KRUZZZZY/zene-studio/issues/new?template=alpha-feedback.yml

It asks for the build string, your OS and distribution, the audio backend, steps to reproduce,
expected vs actual behaviour, the terminal output with the exact command, the project file if you
can share it, and the plug-ins you used. **This build writes no log file.** Start the app from a
terminal and paste the text, or attach a capture of it. On Windows, run `lmms.exe` from a Command
Prompt to get that text.

## Build details

- Packages are built by GitHub Actions. They are produced for a tag build or a manual CI run, and
  the per-platform job logs are in the repository's Actions tab.
- Unsigned: Windows packages carry no signature and macOS packages are ad-hoc signed only, so the
  first launch warns (see [Install](#install)).
- The version string the app reports, and why it still says `LMMS`, is under
  [Which build you have](#which-build-you-have).

## What's next

The roadmap waves (Session View, warp, racks and macros, comping, MPE, modulation, Ableton Link, a
tag and similarity browser) are direction, not shipped features, and there are no dates. The plan
and the current status are in `docs/STATUS.md` in the repository.

## Thanks

LMMS and its contributors for the foundation. The upstream projects whose work this build carries:
RNNoise, RTNeural, NAM/NeuralAmpModelerCore, HTDemucs/ONNX Runtime, Lua, LuaBridge, CLAP and the
VST3 SDK. Licence details are in `LICENSE.txt` and the per-plugin notice files.
