BLOCKED

Q050-490: BLOCKED on one part of criterion 2. Criteria 1, 3, 4 and 5 are met and proven, and so is most of 2. What is
missing, and why:

- **VST3 on the Windows and macOS hosted CI jobs.** The acceptance asks for open, resize, close and reopen to be
  "covered by the existing in-tree VST3 and CLAP fixtures on each platform's hosted CI job". The in-tree VST3 fixture
  (`tests/data/vst3-test-instrument`) builds only on Linux:
  - its CMake lays out `<name>.vst3/Contents/<arch>-linux/<name>.so` and compiles `public.sdk/source/main/linuxmain.cpp`;
  - `WANT_VST3_TEST_INSTRUMENT=ON` is passed by the linux-x86_64 job alone (`.github/workflows/build.yml:44-79`).

  So no Windows or macOS job runs a VST3 editor, before or after this change. To close it, port the fixture: a `.vst3`
  folder with `dllmain.cpp` on Windows; a CFBundle with `macmain.cpp` and an `Info.plist` on macOS. Then turn the option
  on in the macos / msvc jobs. I did not do that here. Neither platform's toolchain is on this box and lanes are never
  pushed, so the port would be an unbuilt, unrun CI change. A CI compile error is a send-back, and I would rather report
  the gap than ship that. The HWND and NSView paths of the VST3 host *are* proven, by explicit cases run on Linux (below).
- **No hosted run of this change has been observed** on any platform (lanes are not pushed). On the hosted macOS (x86_64,
  arm64), msvc and msys2 jobs, `ClapHostTest` and `ClapEffectIntegrationTest` are built and run by ctest
  (`tests/CMakeLists.txt:4427` `if(TARGET lmms_clap)`, no platform guard; ctest at `build.yml:699`, `:1200`, `:1419`). With
  this change their default-path tests use cocoa / win32, and the CLAP fixture accepts both. That is the CLAP CI coverage.
  Its first result is still to be seen.

Branch `feat/050-Q050-490-oc` (from `050/m0-train-18`, `7641e9493d`). Feature record: `docs/050-features/Q050-490.md`.
All runs were on this worktree on Linux (CachyOS, GCC, Qt 6, offscreen). Logs are in
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-490.r1/logs/`.

## What changed

- `include/PluginWindowSystem.h` (new, header-only): `PluginWindowSystem {X11, Win32, Cocoa, None}`, chosen when an editor
  opens. Windows → Win32, macOS → Cocoa; elsewhere X11, except a native Wayland session (`QGuiApplication::platformName()`
  starts with `wayland`) → None, refused.
- `Vst3EditorSession::open`: `isPlatformTypeSupported` / `attached` with `kPlatformTypeX11EmbedWindowID`, `kPlatformTypeHWND`
  or `kPlatformTypeNSView`. On Cocoa, `IPlugViewContentScaleSupport` is skipped; the SDK keeps it to Windows and Linux.
- `HostedPlugin::openEditor` (CLAP): `is_api_supported` / `create` / `set_parent` with x11, win32 or cocoa, and the parent
  in that api's `clap_window_t` union member. `set_scale` is not sent on cocoa (`clap/ext/gui.h`).
- Refusal (typed `ControlErrorKind::Refused` through `plugin.editor_open`): *"the plug-in's editor cannot embed on <X11 |
  Windows | macOS> (VST3 platform type <type> | CLAP window API <api>)"*. Nothing is created, so no empty window is left.
- `plugin.editor_open`'s description now names the three window types. The snapshot was regenerated live (867 ids / 76
  groups).
- Fixtures:
  - Both embed in all three APIs. An env var restricts each to one API (for the refusal), and another lets the plug-in
    resize its own editor.
  - The CLAP fixture reads the parent from the right union member and refuses a cocoa `set_scale`.
- Docs:
  - `docs/KNOWN-LIMITATIONS.md` and `docs/PLUGIN-COMPATIBILITY.md` (the per-platform table, rendered by
    `tests/plugin_compat_page.py`) say what was proved on CI, what was not observed, and the real-machine check on the
    owner's Windows laptop and Mac mini.
  - The user manual (`data/manual/index.html`) describes the editor on the three platforms.

## Acceptance

| # | Criterion | Proof (test, what it shows) | Exit |
| --- | --- | --- | --- |
| 1 | Vst3EditorSession / ClapHostGui select the platform window type at run time | `Vst3InstrumentTest::testTheEditorEmbedsOnEveryWindowSystem` and `ClapHostTest::testTheEditorEmbedsOnEveryWindowSystem`: the fixture is restricted to one type/api at a time, and each window system's editor opens with exactly that one (X11EmbedWindowID/HWND/NSView; x11/win32/cocoa); `ClapHostTest::testTheWindowSystemFollowsThePlatform` (xcb/offscreen → X11, wayland → None on Linux) | 0 (`test-14-final.log`) |
| 2 | Open, resize (host- and plug-in-initiated), close, reopen | the two `...OnEveryWindowSystem` tests: 400x300 → host 640x480 → minimum 100x80 → close → reopen on all three; plug-in-initiated 520x390 (VST3 `IPlugFrame::resizeView`, CLAP `request_resize` → `ClapHostTest::testThePluginResizesItsOwnEditorOnEveryWindowSystem`) | 0 (`test-14-final.log`) |
| 2 | ... on each platform's hosted CI job | CLAP: `ClapHostTest` and `ClapEffectIntegrationTest` are run by ctest on the macOS, msvc and msys2 jobs, where the default path is cocoa / win32. **Not observed** (not pushed). **VST3: not covered**: the fixture is Linux-only (see above) | - |
| 2 | Linux X11 path and its tests unchanged | the pre-existing editor tests in `ClapHostTest`, `Vst3InstrumentTest`, `ClapEffectIntegrationTest`, plus `PluginNativeEditorTest`, `DevicePanelTest`, `DeviceToolsChainTest`, `Vst3InstrumentIntegrationTest`, `Vst3EffectIntegrationTest`, `ClapInstrumentViewTest`, all unchanged and passing | 0 (`test-14-final.log`) |
| 3 | A plug-in that cannot embed is refused with a typed error naming platform and window API, never a crash or an empty window | `Vst3InstrumentTest::testAnEditorThatCannotEmbedIsRefusedByName`, `ClapHostTest::testAnEditorThatCannotEmbedIsRefusedByName` (all three platforms, plus a Wayland session); GUI: `PluginNativeEditorTest::theShowEditorButtonOpensOnThisPlatformOrNamesTheWindow` (real CLAP fixture loaded by `plugin.load`: the button opens the editor, and when restricted to another API the click is refused with "... cannot embed on X11 (CLAP window API x11)", no editor and no window), `ClapEffectIntegrationTest::testTheButtonNamesThePlatformAPluginCannotEmbedOn` (same through the effect control dialog) | 0 (`test-14-final.log`) |
| 4 | Content scaling applied where the API offers it, documented where not | `Vst3InstrumentTest::testTheDeviceScaleOnEveryWindowSystem` (1.5: X11/Windows ByView 600x450, macOS ByWindow 600x450); `ClapHostTest::testCocoaIsNeverAskedToScale` (fixture refuses a parent after a cocoa set_scale; opens on all three); documented in KNOWN-LIMITATIONS, PLUGIN-COMPATIBILITY and the manual (no CLAP zoom on macOS) | 0 (`test-14-final.log`) |
| 5 | KNOWN-LIMITATIONS and PLUGIN-COMPATIBILITY state per platform what CI proved and what needs a real machine | `docs/KNOWN-LIMITATIONS.md` (Q050-490 entry), `docs/PLUGIN-COMPATIBILITY.md` "Plug-in editors on each platform"; `python3 tests/plugin-compat-matrix-gate.py` | 0 |

## Self-check

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> Q050-490`: see the last section for its final
result.

- **Each new test fails with the product change reverted and passes restored.**
  - **(A)** The six product files were restored to `7641e9493d` and `include/PluginWindowSystem.h` was moved out. The
    tests do not build: `build-6-revertA.log`, EXIT=2, `fatal error: PluginWindowSystem.h`.
  - **(B)** Behavioural revert: the old X11-only `Vst3EditorSession.cpp/.h` and `ClapHostGui.cpp`, with the
    `setEditorWindowSystem` setters as no-ops. Build EXIT=0 (`build-7-revertB.log`). `ctest -R
    "^(ClapHostTest|Vst3InstrumentTest|ClapEffectIntegrationTest)$"` EXIT=8 (`test-4-revertB.log`). Every new editor case
    fails: CLAP 4, the CLAP GUI case 1, VST3 3. The pre-existing X11 cases in those binaries pass.
  - **(B), GUI test:** the old `ClapHostGui.cpp` alone, `clapeffect` rebuilt. `PluginNativeEditorTest` EXIT=8
    (`test-10-revertB-gui.log`); the new case fails and the other 10 pass.
  - **Restored:** EXIT=0 (`test-11.log`, and `test-14-final.log` on the final tree).
  - **Exception:** `ClapHostTest::testTheWindowSystemFollowsThePlatform` tests the new header itself. It fails under (A),
    because it does not build, and passes under (B), which keeps the header.
- **Every criterion proven by a test I ran:** see the table. The rows marked "-" are the CI part of criterion 2, which no
  local run can prove; that is why this report says BLOCKED.
- **One undo step / reversibility:** no new command and no edit. `plugin.editor_open` / `editor_close` are interface
  state, `not_mutating`, and nothing is journalled (unchanged). Only the description text changed.
  `ReversibilityContractTest` and `ControlCommandsSnapshot` pass (`test-14-final.log`). The A16 histogram is unchanged.
- **Realtime:** nothing new runs on the audio thread. Every change is in the main-thread editor path (`PluginNativeEditor`
  → `openNativeEditor` hooks → `HostedPlugin::openEditor` / `Vst3EditorSession::open`). The fixtures' new code runs in
  their GUI and main-thread callbacks. No locking or threading was added, so I see no race; TSan does not run on this
  box, so that is reasoning, not a measurement.
- **Projects:** no format change, no saved data touched. Old projects load and render as before; nothing new to round-trip.
- **Portability:**
  - No Qt-6-only API: `QGuiApplication::platformName`, `QString::arg` with QString/QLatin1String arguments, and
    `qputenv`/`qunsetenv` are all Qt 5.15.
  - `const char*` names are wrapped in `QLatin1String` before `arg`.
  - No `std::filesystem`, and no `small`/`near`/`far`/`interface`/`min`/`max` identifiers. The `windows.h` macro names
    are not used: `Win32` and `Cocoa` are scoped enumerators, not macros.
  - Platform branches are `#if defined(_WIN32)` / `#elif defined(__APPLE__)`. `clap_window_t`'s `win32`/`cocoa`
    members are `void*` in the CLAP headers.
  - The CLAP fixture (C, built on every job) parses with `strtoul`, not `sscanf`. The VST3 fixture's `sscanf` is in a
    Linux-only target.
  - Not compiled here: macOS, MSVC, mingw, clang-arm64, Qt 5.15.
- **Gates:** I ran the `feature_check.py` source with only its log folder moved into the run folder; the script's own
  folder, `<worktree>/../<name>-feature-check`, is outside my write-jail (PermissionError). The copy is
  `/tmp/q490-tools/feature_check_runlogs.py`. Every row passed (`feature-check-4.log`): fork-sources,
  all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length.
  `tests/doc-figures-gate.py` PASS; `tests/plugin-compat-matrix-gate.py` PASS.
- **Not run / not checked:**
  - `PluginEditorHostTest` was not built (I did not change `PluginEditorHost`).
  - The whole ctest suite, per the brief.
  - Any hosted CI job.
  - Any real display or third-party plug-in on Windows or macOS.
- **Known limits** (also in the feature record):
  - The host window does not follow a plug-in-initiated resize. The view gets the size; the window keeps its opening
    size, on every platform, as on X11 before.
  - `HostPlugFrame` still offers `Linux::IRunLoop` everywhere. Windows and macOS views do not ask for it.
- **Environment notes:**
  - ccache needed `CCACHE_TEMPDIR=/tmp/ccache-q490`: the configured temporary folder is outside the jail.
  - Editing `tests/CMakeLists.txt` made the build re-run CMake's own regeneration, as any lane's CMake edit does. I ran
    no configure.

## Commits

`e249684e77` feat · `cdeb5d6e4d` test · `116b88718b` docs · `5e4268018b` test cleanup · `56ff253ac8` GUI test ·
`b1e5e71c36` feature record · `54cc5040c1` complexity refactor · this report.
