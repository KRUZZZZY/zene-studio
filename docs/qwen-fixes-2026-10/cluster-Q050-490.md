BLOCKED

Q050-490 (round 2): the code and CI changes the review asked for are done and pass on Linux. Two things are left that a
lane cannot do:

1. **The hosted Windows and macOS runs have not been observed.**
   - The in-tree VST3 fixture is now laid out per platform:
     - **Windows:** a `Contents/<arch>-win/<name>.vst3` package entered through `dllmain.cpp`;
     - **macOS:** a CFBundle, `Contents/MacOS/<name>` plus `Info.plist`, entered through `macmain.cpp`;
     - **Linux:** unchanged.
   - The macos and msvc jobs pass `WANT_VST3_TEST_INSTRUMENT=ON`.
   - The new `Vst3EditorTest` is built on every platform. On those jobs it opens, resizes (host and plug-in), closes and
     reopens the editor as an NSView / HWND. Its explicit cases cover all three window systems.
   - Lanes are never pushed, so no hosted run of this branch exists.
   - This box has no Windows or macOS toolchain. The lane guard refuses any CMake configure, including a scratch
     cross-configure in `/tmp` that I tried in order to check the generated layout. So the Windows and macOS halves of
     the fixture have **not been compiled**. They follow, step for step, the recipe `tests/data/vst3-test-effect`
     already runs on those jobs: per-platform entry file, `Contents/<arch>-win/<stem>.vst3`, `Contents/MacOS` plus a
     `configure_file` Info.plist, ARM64 asked of the compiler. The first hosted run of the merged line is their proof.
2. **The `git merge 050/m0-train-19` the brief asked for was refused** by the lane guard ("bunny-guard: 'git merge
   050/m0-train-19' is not allowed in a Bunny lane"). I did not try other spellings to get past it.
   - **Instead:** both conflicts were adjacent edits, not competing content, and I removed them on my branch.
     - **Snapshot:** its capture metadata (`captured_at`, `lane`, `source_socket`, `version`) went back to the base
       values; the `plugin.editor_open` description stays.
     - **`tests/upstream-modifications.txt`:** the loop's five registry lines moved from the file's end, where the
       train also appends, to beside `Vst3HostTest.cpp`.
   - **Check:** `git merge-tree --write-tree HEAD 050/m0-train-19` (read-only) now exits 0 with no conflict.
   - **Merged tree:** it holds the train's 868-id snapshot with my description, all five registry lines, and both new
     sources in `all-sources.txt`.
   - **So:** the loop's own freshen step (`qwenfix.py`, `git merge --no-edit <train tip>`) should now merge it without
     a session.

Branch `feat/050-Q050-490-oc` (from `050/m0-train-18`, `7641e9493d`). Feature record: `docs/050-features/Q050-490.md`.
All runs were on this worktree on Linux (CachyOS, GCC, Qt 6, offscreen). Logs: round 1 in
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-490.r1/logs/`, round 2 in `.../Q050-490.r2/logs/`.

## Round 2 - what changed for the review

- `tests/data/vst3-test-instrument/CMakeLists.txt`:
  - per-platform entry file and bundle layout, through one function used for both fixture modules;
  - on Windows, the arch directory is asked of the compiler (`check_cxx_source_compiles`, as the effect fixture does);
  - a configure-time check that the entry file is in the SDK checkout;
  - on MSVC, `/Zc:__cplusplus` (as `lmms_vst3_sdk`) and `_CRT_SECURE_NO_WARNINGS` for the fixture's `getenv`.
- `tests/data/vst3-test-instrument/Info.plist.in` (new): the CFBundle plist, with the same keys as the effect fixture's.
- `vst3-test-instrument.cpp`: one implicit int-to-float constant made explicit (MSVC C4305; same value).
- `tests/src/plugins/Vst3EditorTest.cpp` (new): the five VST3 editor cases, moved unchanged from `Vst3InstrumentTest.cpp`
  (which keeps its other 13 cases), with their own `cleanup()`.
  - **One change:** the repaint-timer case now waits for its five `onTimer` calls (up to 3 s) and checks they were not
    faster than 10 ms, instead of counting in a fixed 150 ms. `ClapHostTest`'s timer case made the same change after the
    hosted macOS VM delivered 2-4 calls in 150 ms. The lower bound (5) is unchanged, and the upper bound is new.
- `tests/CMakeLists.txt`:
  - `WANT_VST3_TEST_INSTRUMENT` builds the fixture and `Vst3EditorTest` on every platform;
  - the rest of the fixture suite (probe, `Vst3InstrumentTest`, integration and socket cases) is now under
    `WANT_VST3_TEST_INSTRUMENT AND CMAKE_SYSTEM_NAME STREQUAL "Linux"`. That suite has only ever run on linux-x86_64,
    and its behaviour there is unchanged.
- `.github/workflows/build.yml`: `-DWANT_VST3_TEST_INSTRUMENT=ON` added to the macos jobs (`CMAKE_OPTS`) and the msvc job,
  and the linux-x86_64 comment updated (it said "THIS job and by no other").
- Docs:
  - `KNOWN-LIMITATIONS.md`, `PLUGIN-COMPATIBILITY.md` (per-platform table, re-rendered) and `subjects.json` now name
    `Vst3EditorTest` on the hosted jobs.
  - `VST3-INSTRUMENT-FIXTURE.md` describes the per-platform layout.
  - `VST3-INSTRUMENT-HOSTING.md` uses the moved test's name.
  - The feature record has the new rows.
- Registration: `tests/all-sources.txt` re-derived with its recipe (adds `Vst3EditorTest.cpp`); `tests/fork-sources-gate.sh` PASS.

## Acceptance

| # | Criterion | Proof (test, what it shows) | Exit |
| --- | --- | --- | --- |
| 1 | Vst3EditorSession / ClapHostGui select the platform window type at run time | `Vst3EditorTest::testTheEditorEmbedsOnEveryWindowSystem` and `ClapHostTest::testTheEditorEmbedsOnEveryWindowSystem`: the fixture is restricted to one type or API at a time, and each window system's editor opens with exactly that one (X11EmbedWindowID / HWND / NSView; x11 / win32 / cocoa). `ClapHostTest::testTheWindowSystemFollowsThePlatform` (on Linux, xcb/offscreen → X11 and wayland → None) | 0 (`r2/test-4-final.log`) |
| 2 | Open, resize (host- and plug-in-initiated), close, reopen | The two `...OnEveryWindowSystem` tests on all three window systems: 400x300 at open, 640x480 after a host resize, 100x80 (the minimum), close, reopen, then a plug-in-initiated 520x390 (VST3 `IPlugFrame::resizeView`; CLAP `request_resize` in `ClapHostTest::testThePluginResizesItsOwnEditorOnEveryWindowSystem`) | 0 (`r2/test-4-final.log`) |
| 2 | ... by the in-tree VST3 and CLAP fixtures on each platform's hosted CI job | CLAP: ctest runs `ClapHostTest` and `ClapEffectIntegrationTest` on the macOS, msvc and msys2 jobs, on the native cocoa / win32 path. VST3: the fixture is laid out per platform, the macos and msvc jobs pass `WANT_VST3_TEST_INSTRUMENT=ON`, and `Vst3EditorTest` is built everywhere (its default-path cases use NSView / HWND there). **Not observed** (not pushed); the Windows and macOS fixture builds were not compiled here | Linux 0; hosted: - |
| 2 | The Linux X11 path and its tests unchanged | The pre-existing editor cases (now in `Vst3EditorTest`, plus `ClapHostTest`, `ClapEffectIntegrationTest`, `PluginNativeEditorTest`, `DevicePanelTest`, `DeviceToolsChainTest`, `ClapInstrumentViewTest`); the Linux fixture suite (`Vst3InstrumentTest`, `Vst3InstrumentFixtureProbe`, `Vst3InstrumentIntegrationTest`, `InstrumentLatencyPdcTest`, `ControlDeviceCatalogueTest`); `Vst3HostTest`, `Vst3EffectIntegrationTest` | 0 (`r2/test-4-final.log`; `ControlDeviceCatalogueTest` 0 in `r2/test-5.log` once `patman`, which its unrelated Patman case needs, was built) |
| 3 | A plug-in that cannot embed is refused with a typed error naming platform and window API, never a crash or an empty window | `Vst3EditorTest::testAnEditorThatCannotEmbedIsRefusedByName` and `ClapHostTest::testAnEditorThatCannotEmbedIsRefusedByName` (all three platforms, plus a Wayland session). GUI: `PluginNativeEditorTest::theShowEditorButtonOpensOnThisPlatformOrNamesTheWindow` (the real CLAP fixture via `plugin.load`; the button opens the editor; restricted to another API, the click is refused with "... cannot embed on X11 (CLAP window API x11)" and no window is left) and `ClapEffectIntegrationTest::testTheButtonNamesThePlatformAPluginCannotEmbedOn` | 0 (`r2/test-4-final.log`) |
| 4 | Content scaling applied where the API offers it, documented where not | `Vst3EditorTest::testTheDeviceScaleOnEveryWindowSystem` (at 1.5: X11 / Windows ByView 600x450, macOS ByWindow 600x450); `ClapHostTest::testCocoaIsNeverAskedToScale`. Documented in KNOWN-LIMITATIONS, PLUGIN-COMPATIBILITY and the manual (no CLAP zoom on macOS) | 0 (`r2/test-4-final.log`) |
| 5 | KNOWN-LIMITATIONS and PLUGIN-COMPATIBILITY state per platform what CI proved and what needs a real machine | `docs/KNOWN-LIMITATIONS.md` (Q050-490 entry), `docs/PLUGIN-COMPATIBILITY.md` "Plug-in editors on each platform"; `python3 tests/plugin-compat-matrix-gate.py` | 0 |

## Self-check

- **Each new test fails with the product change reverted and passes restored.**
  - **Round 2, `Vst3EditorTest`:** revert B, the old X11-only `Vst3EditorSession.cpp/.h` with the setter as a no-op.
    Build EXIT=0 (`r2/build-3-revertB.log`). `ctest -R ^Vst3EditorTest$` EXIT=8 (`r2/test-2-revertB.log`):
    `testTheEditorEmbedsOnEveryWindowSystem`, `testAnEditorThatCannotEmbedIsRefusedByName` and
    `testTheDeviceScaleOnEveryWindowSystem` fail, and the two pre-existing X11 cases pass. Restored: EXIT=0
    (`r2/test-3.log`).
  - **Round 1:**
    - (A) all product files at `7641e9493d`: the tests do not build, EXIT=2;
    - (B) the old CLAP and VST3 bodies: ctest EXIT=8, every new case failing;
    - the GUI case: EXIT=8;
    - restored: EXIT=0 (`r1/` logs).
  - **What revert cannot show:** the fixture port and the CI switch are build and CI changes. Linux cannot fail them. Only
    the hosted jobs can, and they have not run (see BLOCKED item 1).
- **Every criterion proven by a test I ran:** see the table. The hosted part of criterion 2 cannot be run from a lane.
- **One undo step / reversibility:** no new command and no edit. `plugin.editor_open` / `editor_close` stay
  `not_mutating`; only the description text changed. `ReversibilityContractTest` and `ControlCommandsSnapshot` EXIT=0
  (`r2/test-6.log`, after the snapshot metadata went back to the base). The A16 histogram is unchanged.
- **Realtime:** nothing new on the audio thread. The host changes are in the main-thread editor path. Round 2 touched
  only test fixtures, tests, CMake and CI. TSan does not run here, so race-freedom is reasoned (no new threads or
  shared state), not measured.
- **Projects:** no format change and no saved data touched.
- **Portability:**
  - **Product code:** no Qt-6-only API, no `std::filesystem`, no `windows.h` macro names, `#if defined(_WIN32)` /
    `__APPLE__` branches.
  - **Fixture on MSVC (`/W2 /WX` on the msvc job):** `_CRT_SECURE_NO_WARNINGS` covers `getenv`/`sscanf`. Compiled with
    `clang++ -Wfloat-conversion -Wc++11-narrowing -Wliteral-conversion -Wimplicit-float-conversion`, the fixture shows no
    float→int or narrowing warning, which is what MSVC flags at level ≤ 2 (C4244 level 2, C4838). The one constant
    int→float (C4305 risk) is now explicit.
  - **macOS:** configures with `USE_WERROR=OFF`.
  - **Not compiled here:** Windows, macOS, Qt 5.15.
- **Gates:** I ran the `feature_check.py` source with only its log folder moved into the run folder; the original
  writes beside the worktree, outside my write-jail. The copy is `/tmp/q490-tools/feature_check_runlogs.py`. On the
  final commit, every row passes (`r2/feature-check-2.log`): fork-sources, all-sources-reproduce, no-upstream-regression,
  check-namespace, no-tautology, doc-figures, complexity, file-length. `tests/plugin-compat-matrix-gate.py` PASS.
- **Not run / not checked:**
  - any hosted CI job;
  - a Windows or macOS compile;
  - a configure of the new CMake branches (guard);
  - the whole ctest suite (per the brief);
  - a real display or third-party plug-in on Windows or macOS.
- **Known limits:**
  - The host window does not follow a plug-in-initiated resize (as on X11 before).
  - Only the editor cases of the VST3 fixture suite run on Windows and macOS.
  - `HostPlugFrame` still offers `Linux::IRunLoop` on every platform.

## Completion check (`submit_check.py`)

`r2/submit-check-1.log`, EXIT=1. Every row passes except the two below; the one `?` item is answered under Self-check
(the round-2 revert proof).

- **FAIL gates (feature_check.py).** The original script dies on `PermissionError:
  .../zene-feat-q050-490-feature-check/fork-sources.log`: it writes beside the worktree, outside this session's
  write-jail. The same script with only that folder moved passes every gate on the same commit: `r2/feature-check-2.log`,
  EXIT=0, `RESULT: PASS`.
- **WARN canary figures.** The "new command ids" it lists (`dllmain.cpp`, `linuxmain.cpp`, `macmain.cpp`,
  `plugin.editor_open`, `plugin.list`) are file names and existing ids quoted in the diff. No command was added, and
  `tests/doc-figures-gate.py` passes.

## Commits

- **Round 1:** `e249684e77` feat · `cdeb5d6e4d` test · `116b88718b` docs · `5e4268018b` test cleanup · `56ff253ac8` GUI
  test · `b1e5e71c36` feature record · `54cc5040c1` complexity refactor · `58e69e04c5`, `7753614f13` report.
- **Loop:** `0460a4f42b` registry lines.
- **Round 2:** `db01b80dc4` fixture per platform · `940c3d8bc0` `Vst3EditorTest` and the CI switch · docs · mergeability
  chore · this report.
