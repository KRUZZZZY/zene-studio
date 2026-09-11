# Zene Studio v0.1.0-alpha release notes

**This is the first installable alpha.** Expect crashes, missing features and rough edges. Read
[KNOWN-LIMITATIONS.md](KNOWN-LIMITATIONS.md) before you install it.

## What this is

Zene Studio is an open-source (GPL-2.0-or-later) digital audio workstation built on
[LMMS](https://github.com/LMMS/lmms) 1.3.0-alpha plus this project's work. It is not affiliated
with the LMMS project and not endorsed by it. Upstream notices are retained.

The build still installs as `lmms`. The rename wave (binary name, desktop entry, icons, window
titles, config and data paths) is deferred, so:

- the executable, the desktop entry, the window title and the About box all say LMMS;
- the config file is `~/.lmmsrc.xml` and the working folder is `~/Documents/lmms/`;
- if you already run LMMS, this build shares that config file and folder with it. Settings you
  change here (audio device, plugin folders, samples) change there too.

The rename checklist is in `DOCS-NAMING.md` in the repository root.

### Which version is which

The release is tagged `v0.1.0-alpha`, and the tagged build reports `LMMS 0.1.0-alpha`
(`--version`, or Help > About). Builds made from `main` before this release's version bump report
`LMMS 1.3.0-alpha`, the inherited LMMS version. That number tells you which build you have; it is
not a different product.

## Download

Packages are attached to the GitHub release:

- Release page: https://github.com/KRUZZZZY/zene-studio/releases (tag `v0.1.0-alpha`)

If the page shows no build, the release is not published yet. Packages are uploaded for a tag
build or a manual CI run only, so until the release appears there is nothing to download. Only
the platforms whose build job is green have packages.

| Platform | File |
|---|---|
| Linux x86_64 | `lmms-0.1.0-alpha-linux-x86_64.AppImage` |
| Linux arm64 | `lmms-0.1.0-alpha-linux-aarch64.AppImage` |
| Windows x64 | `lmms-0.1.0-alpha-msvc2022-win64.exe` or `lmms-0.1.0-alpha-mingw-win64.exe` |
| Windows on Arm | `lmms-0.1.0-alpha-clangarm64-arm64.exe` |
| macOS | `lmms-0.1.0-alpha-mac<os>-<arch>.dmg` (`arm64` or `x86_64`) |

Windows x64 gets two installers because the project builds with two compilers. Both install the
same alpha.

## Install

**Linux.** The build is one AppImage file. There is no installer, no tarball and no `.deb`.

```bash
chmod +x lmms-0.1.0-alpha-linux-x86_64.AppImage
./lmms-0.1.0-alpha-linux-x86_64.AppImage
```

If your system has no FUSE, add `--appimage-extract-and-run`.

**Windows.** Run the `.exe` installer. It installs the app and adds Start menu entries. The
installer is not code-signed, so SmartScreen warns about an unrecognised app: choose
*More info*, then *Run anyway*.

**macOS.** Open the `.dmg` and copy the app out. The app has an ad-hoc signature only (no
Developer ID, no notarisation), so Gatekeeper refuses the first launch: right-click the app and
choose *Open*, or run `xattr -dr com.apple.quarantine "<app>"`.

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

## What is not in this alpha

No instrument hosting (VST3 and CLAP are effects only). No clip editing (trim, slip, fades,
crossfades, clip gain). No take lanes or comping. No punch in/out. No input monitoring. No plugin
scanning or rescan; plugins are found in the standard folders. No multicore graph scheduling. No
automation modes. No LUFS metering. No freeze, no bounce-in-place and no stem export. No warp or
time-stretch. No Session View. No racks or macros. No MIDI learn. No Ableton Link. No crash
reporter. The Patcher node-graph engine is in the tree but nothing in the app uses it.

Full list, ordered by when you will hit it: [KNOWN-LIMITATIONS.md](KNOWN-LIMITATIONS.md).

## Your projects

- **Keep backups.**
- There is **no project-format stability promise** yet; it arrives with v1.0. A project saved by
  this alpha may not open in an older build, LMMS or a previous alpha.
- LMMS projects open, but no migration guarantee is offered for this alpha.
- After a crash, the autosave recovery file is offered on the next start. It is only as fresh as
  the last autosave, so save when you care about the work.

## Reporting a bug

Use the alpha feedback form:
https://github.com/KRUZZZZY/zene-studio/issues/new?template=alpha-feedback.yml

It asks for your OS, the build string, your audio backend and the terminal output. **This build
writes no log file.** Start the app from a terminal and paste the text, or attach a capture of it.
On Windows, run `lmms.exe` from a Command Prompt to get that text.

## Thanks

LMMS and its contributors for the foundation. The upstream projects whose work this build carries:
RNNoise, RTNeural, NAM/NeuralAmpModelerCore, HTDemucs/ONNX Runtime, Lua, LuaBridge, CLAP and the
VST3 SDK. Licence details are in `LICENSE.txt` and the per-plugin notice files.
