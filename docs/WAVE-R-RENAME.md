# Wave R — the deferred rename (report)

**Branch:** `post-alpha/wave-r-rename` · **Base:** `0c23587d2` (tag `v0.1.0-alpha`) ·
**Commit:** `018d2041f` · **Checklist:** `DOCS-NAMING.md` §"Deferred rename checklist — wave R", items 2–7
(item 1, the repo rename, was already done 2026-09-09).

**Verdict:** items 2–7 are executed and verified on this tree. The product no longer installs
and introduces itself as the old name: the project is `zene`, the executable is `build/zene`
(installs as `bin/zene`), the packages are `zene-*`, and the window title, About dialog and
`--version` all say Zene Studio.

## 1. Environment / how it was run

```
cd <worktree>
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4     # see deviations below
```

* The documented invocation is `tools/local-ci.sh …`; this tree commits that script with mode
  `100644` (`git ls-files -s tools/local-ci.sh` → `100644`), so it is run as `bash tools/local-ci.sh`.
  That is the only invocation change; no flag was altered.
* **Printed deviation** (from the script, not silent): `Qt5 development files not found: added
  -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)`.

## 2. Unpiped exit codes and test totals

`cmd > log 2>&1; echo EXIT=$?`, never piped:

| step | command | exit |
|---|---|---|
| configure | `cmake -S . -B build -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_QT6=ON` | **0** |
| build | `cmake --build build -j4` | **0** |
| ctest | `(cd build/tests && ctest --output-on-failure -j2)` | **0** |
| overall | `tools/local-ci.sh` | **0** |

`ctest totals: 100% tests passed, 0 tests failed out of 25` — 25 tests, not 0. Logs:
`build/{configure,build,ctest}.log`.

The build was re-run to completion after the last source edit, so the committed tree is the built tree
(three runs during the work: `configure EXIT=0 / build EXIT=0 / ctest EXIT=0 / 25-of-25` each time).

## 3. Item 7 — verification sweep (run live, after the commit)

Anchored on the identifier forms the rename owns. Directory exclusions: `.git`, `build/`, and the
`doc/wiki` **submodule** (upstream content, not this repository's).

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-rename
PAT='PROJECT\(lmms\)|lmms\.desktop|lmms\.xml|lmms\.rc|lmms\.exe|lmms\.lib|lmms\.VisualElementsManifest|lmms\.plist|lmms\.spec|doc/lmms\.1|bash-completion/lmms|/usr/bin/lmms|bin/lmms\b|lib/lmms\b|share/lmms\b|apps/lmms\.(png|svg)|build[\\/]lmms-|"LMMS %s|Usage: lmms|Start LMMS|"LMMS %1"|ADD_EXECUTABLE\(lmms|INSTALL\(TARGETS lmms'
grep -rInE --exclude-dir=.git --exclude-dir=build --exclude-dir=wiki "$PAT" .
```

Output, 4 hits in 3 files (this report is excluded from the run, see the note below):

```
./plugins/NeuralAmp/CMakeLists.txt:53:# (src\zene.lib since the wave-R rename, src\lmms.lib before it) — the *executable's*
./plugins/NeuralAmp/CMakeLists.txt:58:#   lmms.lib(lmms.exe) : error LNK2005: std::vector<float>::~vector(...) already defined in
./DOCS-NAMING.md:22:2. `CMakeLists.txt`: change `PROJECT(lmms)` → `PROJECT(zene)` and update
./docs/RELEASE-NOTES-v0.1.0-alpha.md:188:terminal and paste the text, or attach a capture of it. On Windows, run `lmms.exe` from a Command
```

All four are one of these cases by construction:

1. `plugins/NeuralAmp/CMakeLists.txt:53` — my own annotation, which names the pre-rename import
   library on purpose; `:58` is a **verbatim quoted CI log line** from before the rename, kept
   verbatim so the quote is not falsified.
2. `DOCS-NAMING.md:22` — the checklist item that *orders* the change, which necessarily spells the
   old spelling out.
3. `docs/RELEASE-NOTES-v0.1.0-alpha.md:188` — the shipped release's own notes; that release really did
   ship the old executable name, so this document stays historical.

Excluding those three files and this report (a report that quotes the sweep command and its own
output cannot avoid matching its own pattern), the same grep returns **nothing**:

```
grep -rInE --exclude-dir=.git --exclude-dir=build --exclude-dir=wiki \
  --exclude=DOCS-NAMING.md --exclude=RELEASE-NOTES-v0.1.0-alpha.md --exclude=CMakeLists.txt \
  --exclude=WAVE-R-RENAME.md "$PAT" . | grep -v "plugins/NeuralAmp/CMakeLists.txt"
# → no output (exit 1)
```

**Zero references to the old product name remain in any surface wave R owns** — build metadata,
packaging, CI, install paths, desktop/metadata, window title, `--version`, README, docs. The
residual classes (namespace, format, plugin ABI, user state, upstream URLs) are enumerated in §6.

## 4. What changed, with file:line

### Item 2 — CMake project metadata

| role | where |
|---|---|
| CMake project name | `CMakeLists.txt:26` |
| project author | `CMakeLists.txt:60` |
| project URL | `CMakeLists.txt:61` |
| project description | `CMakeLists.txt:63` |
| copyright (upstream attribution kept) | `CMakeLists.txt:64` |
| nested `project()` in `doc/` | `doc/CMakeLists.txt:3` |

`PROJECT_COPYRIGHT` reads `2008-2026 LMMS Developers, 2026 Zene Studio contributors`, so the upstream
notice survives into `--version` and the About dialog. `PROJECT_EMAIL` was **not** changed (see §6).

### The coupling the checklist does not spell out: the project name *is* the install layout

`cmake/linux/LinuxDeploy.cmake`, `cmake/apple/MacDeployQt.cmake` and the CPack/NSIS templates derive
their paths from `CMAKE_PROJECT_NAME` / `PROJECT_NAME_UCASE` — they look in `usr/bin/<project>`,
`usr/lib/<project>`, `Contents/lib/<project>`, `${PROJECT_NAME_UCASE}.AppDir` and
`<project>-<arch>.AppImage`. So renaming `PROJECT()` without renaming `PLUGIN_DIR`/`LMMS_DATA_DIR`
would have left every packaging step looking in a directory that no longer exists. Changed together:

| role | where |
|---|---|
| installed plugin dir | `cmake/modules/DetectMachine.cmake:161` |
| installed data dir | `cmake/modules/DetectMachine.cmake:163` |
| runtime plugin search path | `src/core/PluginFactory.cpp:82` |
| runtime data search path | `src/core/ConfigManager.cpp:90` |
| stem-split CLI path | `src/core/ExternalProcessStemSeparator.cpp:179` |
| AppImage hardcoded binary path | `cmake/linux/LinuxDeploy.cmake:99` |
| AppRun JACK hook path | `cmake/linux/apprun-hooks/jack-hook.sh:4` |
| demo script binary path | `tools/mmpz-git/run-demo.sh:23` |

### The built binary and the executable target

`PROJECT()` alone does not rename the executable — `ADD_EXECUTABLE(lmms …)` was a literal. Renamed,
with every reference: the target (`src/CMakeLists.txt:132`), its install rule (`:254`), its properties
(`:236`), the generated Windows `.rc` (`:56`), the man-page install (`:249`), the plugin link
(`cmake/modules/BuildPlugin.cmake:54`), the two test Doubles (`tests/CMakeLists.txt:269`). The
`lmms_export.h` API header (`BASE_NAME lmms`), the `lmms_plugin_main` entry symbol and the `lmmsobjs`
object library are **unchanged**, so the plugin ABI, the export header and the whole code tree are
untouched.

### CI package-glob coupling (the failure mode that ships an empty release)

Package file names now come out as `zene-0.1.0-alpha-linux-x86_64` (verified in the generated
`build/CPackConfig.cmake`). Every upload glob and every packaging reference was updated together:

| role | where |
|---|---|
| CI AppImage glob (×2) | `.github/workflows/build.yml:108`, `:197` |
| CI dmg glob | `.github/workflows/build.yml:297` |
| CI exe glob, bash + cmd | `.github/workflows/build.yml:384`, `:513`, `:583` |
| CI Windows import-binding proof | `.github/workflows/build.yml:476` (name + 3 `findstr`/`dumpbin` lines) |
| CI shellcheck target | `.github/workflows/checks.yml:32` |

### Item 3 — desktop entries and application metadata

Renamed with every reference: `zene.desktop`, `zene.xml` (mime package), the XDG menu file,
`zene.spec.in`, `zene.rc.in`, `zene.exe.manifest`, `zene.VisualElementsManifest.xml`,
`zene.plist.in`, the app icon set (`apps/zene.png` ×13, `apps/zene.svg`).

| role | where |
|---|---|
| desktop file / mime install | `cmake/linux/CMakeLists.txt:2`, `:3` |
| desktop Name / Icon / Exec | `cmake/linux/zene.desktop:2`, `:12`, `:13` |
| mime type comment | `cmake/linux/zene.xml:5` |
| CPack install dir / menu entry | `cmake/CMakeLists.txt:14`, `:17` |
| NSIS display name / `.rc` / manifest | `cmake/nsis/CMakeLists.txt:14`, `:69`, `:72` |
| exe FileDescription + ProductName | `cmake/nsis/zene.rc.in:18`, `:22` |
| macOS bundle id | `cmake/apple/CMakeLists.txt:37` |
| macOS `Info.plist` | `cmake/apple/MacDeployQt.cmake:64`, `cmake/apple/zene.plist.in:16` |
| Doxygen project name | `doc/Doxyfile.in:35` |
| vcpkg manifest name | `vcpkg.json:3` |
| man page | `doc/zene.1:5`, `:19` |
| bash-completion | `doc/bash-completion/CMakeLists.txt:3`, `doc/bash-completion/zene` |
| makeself installer label | `cmake/linux/LinuxDeploy.cmake:228` |

The macOS bundle identifier **had to change how it is computed**: it was derived by reversing
`PROJECT_URL` into a domain (`https://lmms.io` → `io.lmms`), and `PROJECT_URL` is now a repository URL
— the old derivation produced `comKRUZZZZYzene-studio.github`. It is now stated explicitly
(`io.github.kruzzzzy.zene-studio`).

### Items 4 and 5 — window title and `--version`

| role | where |
|---|---|
| window title | `src/gui/MainWindow.cpp:601` |
| `--version` banner | `src/core/main.cpp:143` |
| `--help` usage / action lines | `src/core/main.cpp:162`, `:164` |
| About dialog title / body / link | `src/gui/modals/about_dialog.ui:20`, `:43`, `:100`, `:152`, `:221` |
| working-dir prompt | `src/gui/GuiApplication.cpp:98` |
| settings path label | `src/gui/modals/SetupDialog.cpp:843`, `:1341` |
| connection tooltip | `src/gui/PinConnector.cpp:495` |
| theme reload notice | `src/gui/LmmsStyle.cpp:153` |
| import error message | `src/core/ImportFilter.cpp:85` |
| plugin warning | `src/core/PluginFactory.cpp:188` |
| root-user message, recovery dialog, help dialog | `src/core/main.cpp:369`, `:845`, `:850`; `src/gui/MainWindow.cpp:493`, `:952` |

`--version` output, verbatim (`./build/zene --version`, exit **0**):

```
Zene Studio 0.1.0-alpha
(Linux x86_64, Qt 6.4.2, GCC 13.3.0)
...
Copyright (c) 2008-2026 LMMS Developers, 2026 Zene Studio contributors
```

### Item 6 — README

`README.md:125` (Naming section) records that wave R landed, names the new executable and package
prefix, and lists the deliberate keeps; `README.md:83` notes that the build produces `build/zene`.
`docs/KNOWN-LIMITATIONS.md:48` (the "the app is called …" bullet) and `:141` (the `--version`
provenance note) were updated because both directly contradicted the rename;
`INSTALL.txt:36` too.

## 5. Proofs

**A real render, with the renamed build** (`QT_QPA_PLATFORM=offscreen ./build/zene render <project>
-o <out>.wav -f wav`, exit **0** each; measured with `wave` + numpy):

| project | exit | frames | rate | ch | peak | RMS |
|---|---|---|---|---|---|---|
| `data/projects/shorties/Root84-TrancyLoop.mmpz` | 0 | 477 440 | 44 100 | 2 | **32767** (full scale) | **8502.1** |
| `data/projects/demos/Momo64-esp.mmpz` | 0 | 13 651 712 | 44 100 | 2 | **32767** | **8140.3** |
| `data/projects/tutorials/editing_note_volumes.mmp` | 0 | 680 192 | 44 100 | 2 | **32767** | **7211.2** |

Not files that merely exist: peak at full scale and RMS in the thousands.

**Installed layout** (`cmake --install build --prefix /tmp/wave-r-install`, exit **0**) — this is what
the AppImage/dmG/NSIS steps consume:

```
bin/zene                                  lib/zene/            (64 plugin modules)
share/applications/zene.desktop           share/zene/          (data: samples, presets, themes)
share/mime/packages/zene.xml              share/man/man1/zene.1
share/icons/hicolor/256x256/apps/zene.png
```

`/tmp/wave-r-install/bin/zene --version` → `Zene Studio 0.1.0-alpha`, and that installed binary renders
`Root84-TrancyLoop.mmpz` to peak 32767 / RMS 8498 — i.e. the renamed plugin dir and data dir are found
at runtime, not just at configure time. (One environmental detail from that run: a bare `cmake --install`
tree cannot load the Carla natives without `lib/zene/optional` on `LD_LIBRARY_PATH`; with it, exit 0.
It is a pre-existing packaging property — `LinuxDeploy.cmake` deploys that directory explicitly — not a
wave-R regression.)

**Behaviour preservation.** No serialization, format or audio-path code is in the diff. A project
round-tripped through the app's own writer (`build/zene upgrade <demo> /tmp/upgraded.mmpz`, exit 0)
still carries `<lmms-project … version="31" creator="LMMS" …>` — the format identifiers the published
alpha wrote. The 25-test suite includes the sample-exact Part-C reference renders, and all 25 pass.

## 6. Deliberate residuals (out of scope per `DOCS-NAMING.md` → "Explicitly out of scope")

Identifiers below are written with a `+` where a plain spelling would also match §3's verification
grep, so that grep stays a meaningful test of the tree rather than of this report.

* **`lmms::` namespace**, `LMMS_*` macros, `lmms_export.h` / `lmmsconfig.h` / `lmmsversion.h` /
  `lmms_math.h` / `lmms_constants.h`, `lmms.qrc`, `LmmsStyle`/`LmmsPalette` — code identifiers; renaming
  them is a code-wide change with no user-visible effect.
* **Project and preset file format**: `<lmms-project>`, `version="31"`, `creator="LMMS"`, `.mmp`/`.mmpz`,
  `.xpf` (`<lmms-project … creator="LMMS">` in 175 shipped presets), `vocoder-`+`lmms`. The checklist
  puts the format out of scope, and renaming the `creator` attribute would make the app misreport who
  created older files.
* **Licence headers** and "derived from LMMS" attribution (`src/**`, `plugins/**`, README §License).
* **Plugin IDs and ABI**: the `LMMS` plugin descriptor name used by the test doubles, the
  `lmms_plugin_main` entry symbol, the `lmms-plugin-logo` resource key (54 call sites).
* **User state**: `~/.lmmsrc.xml`, the config XML root `<lmms>`+`-config-file`, `~/Documents/lmms/`, and
  the `lmms-workspace` portable marker. Renaming any of these would silently orphan an existing
  install's settings and projects — the working
  folder is also the default location users' projects live in. The sharing is now documented in
  `docs/KNOWN-LIMITATIONS.md` and `README.md`.
* **JACK / PulseAudio client name `lmms`** (`src/core/audio/AudioJack.cpp:174`, `:477`,
  `src/core/audio/AudioPulseAudio.cpp:129`, `:177`, `src/core/midi/MidiJack.cpp:171`) — renaming it
  breaks users' saved patchbay connections and stream routing.
* **Registered MIME identifiers**: `application/x-lmms-project` (file association) and the clipboard
  types `application/x-lmms-stringpair` / `application/x-lmms-clipboard`. *(Superseded: rename layer 1
  renamed these to `application/x-zene-project` / `application/x-zene-*`; the raster mimetype icon
  files followed on 2026-09-12 — see `docs/QDEBUG-CLASS-AND-MIME-RENAME.md`. Kept as the wave-R
  record of the decision at the time.)*
* **Upstream URLs and identities**: `.gitmodules` submodule URLs, `lmms.io`, `github.com/LMMS/*`,
  `.mailmap` entries, `.tx/config` resource key, `.github/FUNDING.yml` and issue-template
  `config.yml` (they point at the upstream project's own Discord/donation page — the text is true of
  those URLs), `doc/wiki/**` (a submodule of the upstream wiki).
* **Historical documents**: `docs/phase-f/**` (labelled program artifacts), `docs/STATUS.md` and
  `docs/RELEASE-NOTES-v0.1.0-alpha.md` — the last is the record of the release that actually shipped
  with the old executable name, so it is kept as-is on purpose.
* **`cmake/linux/zene.spec.in`** is a legacy autotools-era RPM template (`%configure`) that already
  referenced an undefined `${LMMS_VERSION}` before this change; the name is updated, the rest is not
  repaired here.
* **`src/gui/MicrotunerConfig.cpp:578`, `:614`** write `! Exported from LMMS <version>` into exported
  Scala tuning files. Left alone: it is file output, and no checklist item covers it.

## 7. What I could NOT do (plainly)

1. **Regenerate the translation catalogs.** `data/locale/*.ts` (38 of them contain the changed source
   strings) need `lupdate`; there is no Qt5 Linguist toolchain on this box and no sudo, so
   `lupdate`/`lupdate-qt6` is not installable here. Until a machine with Qt Linguist regenerates them,
   the renamed strings fall back to English in translated UIs. This is the one checklist-adjacent
   surface left undone, and it is a generated-artifact refresh, not a code change.
2. **Windows, macOS and AppImage packaging are unbuilt.** No mingw/MSVC/Darwin toolchain here, and
   `cpack -G External` needs linuxdeploy downloads. The AppImage/dmg/exe *file-name* change is verified
   from the generated `build/CPackConfig.cmake` (`CPACK_PACKAGE_FILE_NAME =
   zene-0.1.0-alpha-linux-x86_64`) and the upload globs match it, but no package artifact was produced or
   inspected. The Windows import-descriptor diagnostic cannot be exercised on Linux.
3. **The macOS bundle identifier** (`io.github.kruzzzzy.zene-studio`) is chosen, not verified against a
   real registered identifier — the previous value came from the LMMS Foundation's registered Apple
   creator code, which this project does not have. `CFBundleSignature` now carries `ZENE` for the same
   reason.
4. **"A project saved by this build opens in the published alpha"** is argued, not executed: the
   released alpha binary is not on this box. The argument is that no serialization/format file appears
   in the diff and the writer still emits the identical root element. Running the real cross-check needs
   the alpha AppImage.
5. **Side effect of the install test:** running the installed binary created/updated
   `~/.lmmsrc.xml` (1649 bytes, default settings) in the user's home. The worktree runs use
   `build/.lmmsrc.xml` and touch nothing outside the tree.
6. **Install directories did change** (`lib/zene`, `share/zene`) as a direct consequence of item 2 —
   that is the coupling described in §4, and it is a real change for third-party packagers. The
   *user-facing* config and working directories deliberately did not.

## 8. Residual risk

* The CI glob change is the highest-value part of this commit, and it is verified only against the
  generated CPack config, not against a produced artifact (see §7.2). A cheap belt-and-braces
  follow-up: make `build.yml` fail loudly when the upload glob matches no file, instead of
  `if-no-files-found` defaulting to a warning.
* Windows plugin loading depends on the executable's import library, which is now `zene.lib`/`zene.exe`;
  the plugin link target and the CI diagnostic were updated together, but no Windows build has
  exercised it yet.
* Third-party packagers using the old installed paths (`lib`/`share` + old project name) must update.
