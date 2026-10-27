IMPLEMENTED

# Q050-502 - Open plug-in windows on Wayland desktops

## Round 2: integration onto `050/m0-train-20`

Integration job: the accepted patch `feat/050-Q050-502-oc @ a35a24f72` applied on `050/m0-train-20 @ c6ee4fc7b`.
The driver's three-way apply left 8 files conflicted. The source was Q050-490 (VST3/CLAP editors embedded on
Windows and macOS, `include/PluginWindowSystem.h`), which reached the train after this branch was cut. Every conflict
was resolved by keeping both sides. No new design was needed, so the verdict stays IMPLEMENTED. Logs are in
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-502.r2/logs`.

### Conflicts and how they were resolved

| File | Resolution |
| --- | --- |
| `src/core/ControlCommandsPluginEditor.cpp` | Only the description string conflicted. It keeps the train's wording (X11 / HWND / NSView) and adds the Wayland sentence, scoped "On Linux". The `requires` refusal hunk merged cleanly. |
| `data/manual/index.html` | Both paragraphs are kept. The train's sentence "start Zene with `QT_QPA_PLATFORM=xcb`" became "On a Wayland desktop see below". The Wayland paragraph now begins "On Linux". |
| `tests/src/gui/PluginNativeEditorTest.cpp` | Both includes are kept (`PluginWindowSystem.h`, `PluginWindowPlatform.h`). |
| `tests/src/plugins/ClapEffectIntegrationTest.cpp` | Both slots are kept: the train's `testTheButtonNamesThePlatformAPluginCannotEmbedOn` and this patch's `testOnNativeWaylandTheEditorRefusesWithTheReason`. The added lines are identical to the accepted patch, except `<QLabel>`, which the train already includes. |
| `tests/fork-sources.txt`, `tests/upstream-modifications.txt` | Union of both sides. |
| `tests/all-sources.txt` | Re-derived with its header recipe -> REPRODUCES. |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | Took the train's version, then regenerated it live with `snapshot_live.py` (871 ids / 76 groups). Only `plugin.editor_open`'s description and the capture metadata differ from the train. |

### Semantic difference from the accepted patch

Q050-490 added its own native-Wayland check inside each format's host: `PluginWindowSystem::None` ->
`noPluginWindowSystemError()`, at `plugins/Vst3Effect/Vst3EditorSession.cpp:203` and
`plugins/ClapEffect/ClapHostGui.cpp:66`. This patch's checks run before that one, in `plugin.editor_open`
(`src/core/ControlCommandsPluginEditor.cpp:68`) and in `PluginNativeEditor::open` (`src/gui/PluginNativeEditor.cpp:228`).
So on native Wayland the reason the user sees is still this patch's, and it names the start-up decision. The
format-level check is now a second line of defence.

`pluginwindow::editorRefusal()` returns nothing on every platform except `wayland*`, so Q050-490's Windows and macOS
paths are untouched. Its tests pass on the integrated tip.

### Failure paths and platform hazards

- **Native Wayland, user override.** Typed `requires` with the reason.
- **Native Wayland, no XWayland (no `DISPLAY`).** Typed `requires`, saying to enable XWayland.
- **XWayland chosen but libqxcb missing.** The Qt 6 fallback `xcb;wayland` starts on Wayland, and the refusal names
  the missing plug-in.
- **Plug-in with no X11 editor.** The format host's existing refusal.
- **Windows and macOS.** `chooseStartupPlatform()` is a no-op under `LMMS_BUILD_WIN32`/`LMMS_BUILD_APPLE`, and
  `editorRefusal()` is empty on `windows`/`cocoa`.
- **Qt 5.15.** `xcb` only, with no list syntax (guarded by `QT_VERSION`).

### Round 2 evidence (all exit codes as seen)

| Step | Command | Result |
| --- | --- | --- |
| Build | `cmake --build build --target zene PluginWindowPlatformTest PluginNativeEditorTest ClapEffectIntegrationTest Vst3InstrumentIntegrationTest clapeffect clap-test-gain vst3effect vst3instrument vst3-test-instrument -- -j4` [build-2] | exit 0 |
| Focused tests | `ctest -R '^(Vst3EditorTest\|ClapEffectIntegrationTest\|Vst3InstrumentIntegrationTest\|PluginNativeEditorTest)$' -V` [test-2] | exit 0. The Q050-502 cases PASS, as do Q050-490's |
| PluginNativeEditorTest with amplifier | `ctest -R '^PluginNativeEditorTest$'` [build-4, test-3] | exit 0, 12 passed / 0 skipped |
| Revert proof | Wiring hunks in `main.cpp`, `PluginNativeEditor.cpp` and `ControlCommandsPluginEditor.cpp` reversed; `decide()` and `editorRefusal()` stubbed to `{}`; rebuilt [build-6-reverted, exit 0] | `PluginWindowPlatformTest` 8, `PluginNativeEditorTest` 8, `ClapEffectIntegrationTest` 8, `Vst3InstrumentIntegrationTest` 8. Only the Q050-502 cases failed [test-6-reverted-*] |
| Restored | `git checkout --` on the 4 files; rebuilt [build-7-restored, exit 0]; 8 suites run [test-7-restored] | Exit 0 for `PluginWindowPlatformTest`, `PluginNativeEditorTest`, `ClapEffectIntegrationTest`, `Vst3InstrumentIntegrationTest`, `Vst3EditorTest`, `ReversibilityContractTest` and `ControlCommandsSnapshot`. `MainCliStartupTest` was not built in that run; built and run in [build-8, test-8] -> exit 0 |
| Start-up neighbours | `MainCliStartupTest`, `ControlShutdown` [test-8]; `AppearanceStartupTest` [build-9, test-9] | exit 0 |
| Snapshot | `snapshot_live.py $PWD` [snapshot-1] | exit 0, 871 ids / 76 groups |
| Doc figures | `python3 tests/doc-figures-gate.py` [docfig-1] | exit 0, PASS |
| A16 | `ReversibilityContractTest` [test-5] | exit 0. No class changed, so the histogram line is unchanged |
| Gates | `feature_check.py`, copy with only its log dir and ROOT made absolute [feature-check-2] | exit 0. All 8 rows PASS |

The original `feature_check.py` exits 1 from this lane [feature-check-1] with `PermissionError`, because it creates
`<worktree>-feature-check` beside the worktree, outside the write-jail. This is the same environment issue as round 1.
Builds used `CCACHE_TEMPDIR=/tmp/q502-ccache-tmp`, because the configured ccache temp dir is not writable here
[build-1 failed on that, exit 2].

## Round 1 record (accepted patch)

Branch `feat/050-Q050-502-oc` from `050/m0-train-19` (`5c1c6715d`). The full record, the measurements and the
reasoning behind the design are in `docs/050-features/Q050-502.md`. Logs are in
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-502.r1/logs`. Every exit code below was read from those runs.

One thing is left to the owner, as criterion 5 asks: a check on the real KDE Wayland desktop with a third-party
editor. Everything else was measured in a nested headless `kwin_wayland --virtual --xwayland` (KWin with XWayland)
on this box.

## What changed

- `include/PluginWindowPlatform.h`, `src/core/PluginWindowPlatform.cpp` (new):
  - `decide()` is the pure decision.
  - `chooseStartupPlatform()` handles start-up: in a Wayland session with XWayland it sets
    `QT_QPA_PLATFORM=xcb;wayland`, and it never touches a platform the user set.
  - `editorRefusal()` returns the reason an editor cannot open on a `wayland*` Qt platform. On xcb, offscreen,
    Windows or Cocoa it returns nothing.
- `src/core/main.cpp`: calls `chooseStartupPlatform()` before the GUI application exists, and prints one line to
  stderr.
- `src/gui/PluginNativeEditor.cpp`: refuses before any window or id exists.
- `src/core/ControlCommandsPluginEditor.cpp`: `plugin.editor_open` refuses with a typed `requires` before the plug-in
  is touched. Its description now states the Wayland behaviour.
- Snapshot regenerated live: 868 ids / 76 groups, and only that one description changed.
- Manual and `docs/KNOWN-LIMITATIONS.md` updated.

## Acceptance

| # | Criterion | Evidence (command -> exit) |
| --- | --- | --- |
| 1 | Measure on Wayland what `plugin.editor_open` does today, for VST3 and CLAP | Base build, nested KWin, `QT_QPA_PLATFORM=wayland`. **VST3:** `PluginCompatEditorProbe` -> `open: true` (a false success) [measure-2-wayland, kwin exit 0, probe exit 0]. **Both formats:** `ClapEffectIntegrationTest` / `Vst3InstrumentIntegrationTest` open the editor -> PASS (exit 0, also a false success) [measure-3-wayland]. **The id:** on wayland, `winId()` is `0x55956fc2d690`, and `xprop -id` on it gives BadWindow (exit 1). On xcb it is `0x40000a`, and xprop exits 0 [measure-4-*] |
| 2 | Make it work (XWayland unless the user overrides it), choice documented | `ctest -R ^PluginWindowPlatformTest$` -> 0 [test-7-final]. Real `zene` in nested KWin, no override -> stderr `Wayland session: running on XWayland (QT_QPA_PLATFORM=xcb;wayland)...`, and its windows are XWayland clients [measure-5-auto, exit 0]. Editors open on xcb in nested KWin: probe `pass`, both integration tests PASS [measure-6-after-xcb, measure-7-after-xcb, exit 0]. The design choice and its reasons are in `docs/050-features/Q050-502.md` |
| 3 | Typed refusal naming the reason (no crash, hang or empty window) | `ctest -R "^(PluginNativeEditorTest\|ClapEffectIntegrationTest\|Vst3InstrumentIntegrationTest)$"` -> 0 [test-7-final]. The new cases check that no id is handed over and no window opens, that `plugin.editor_open` returns `requires` with the target and the reason, and that clicking "Show plugin editor" shows the reason. Real compositor after the change: the VST3 probe gives `unsupported` with the reason [measure-6-after-wayland], and both formats' open is refused with the reason [measure-7-after-wayland] |
| 4 | Decision test without a compositor; X11 path and tests unchanged | `PluginWindowPlatformTest` (7 cases: with XWayland, without XWayland, `QT_QPA_PLATFORM` and `-platform` overrides, an X11 session, the env step, the refusal per platform) -> 0. The existing editor cases in the three suites pass unchanged -> 0 [test-7-final] |
| 5 | Manual + KNOWN-LIMITATIONS state the behaviour; owner checks it on real KDE Wayland | `data/manual/index.html` (plug-in section) and `docs/KNOWN-LIMITATIONS.md` (plug-in editors) are written. **The owner's KDE check is pending.** To do it: start `zene` from Konsole without `QT_QPA_PLATFORM`, look for the XWayland line, then open a third-party VST3/CLAP editor |

Other suites run on the final tree: `MainCliStartupTest`, `ReversibilityContractTest` and `ControlCommandsSnapshot` -> 0
[test-7-final]. `AppearanceStartupTest` and `ControlShutdown` -> 0 [test-6].

## Self-check

- **Fails reverted, passes restored.** I reversed the wiring hunks in `main.cpp`, `PluginNativeEditor.cpp` and
  `ControlCommandsPluginEditor.cpp`. I also stubbed `decide()` and `editorRefusal()` to return nothing, which is the
  base behaviour. Then I rebuilt (`build-8-reverted`, exit 0).
  - Results: `PluginWindowPlatformTest` 8, `PluginNativeEditorTest` 8, `ClapEffectIntegrationTest` 8,
    `Vst3InstrumentIntegrationTest` 8. Only the new cases failed [test-4-reverted-*].
  - Restored and rebuilt (`build-9-restored`, exit 0): all four exit 0 [test-5-restored-*].
- **Every criterion proven by a test I ran:** see the table above. Criterion 5's owner check is the one item that
  cannot be run here.
- **Undo and reversibility:**
  - No command was added.
  - `plugin.editor_open` keeps its class, `not_mutating`. Opening an editor changes no project state, so no undo
    step. `ReversibilityContractTest` -> 0.
  - The start-up choice is environment, not an edit.
- **Realtime:**
  - Nothing is on the audio thread.
  - `chooseStartupPlatform()` runs once in `main()` before the application exists.
  - `editorRefusal()` runs on the main thread when an editor is opened.
  - The test seam is a main-thread static.
  - No TSan on this box: by design, nothing here is shared with another thread.
- **Projects:** no project, preset or state format is touched. Old projects load and render as before, and nothing
  new is saved.
- **Portability:**
  - The Qt 6 fallback list `xcb;wayland` sits behind `QT_VERSION >= 6`. Qt 5 gets `xcb`.
  - `qputenv`, `qunsetenv`, `qEnvironmentVariableIsSet` and `QGuiApplication::platformName` all exist in Qt 5.15.
  - The start-up step is a no-op under `LMMS_BUILD_WIN32` / `LMMS_BUILD_APPLE`, and the test skips the environment
    case there.
  - No std::filesystem, no reserved identifiers, no POSIX-only call.
- **GUI and agent, same command:** Open Editor, the "Show plugin editor" button and the agent all run
  `plugin.editor_open`. The tests click the real button (`QTest::mouseClick`) and check the reason it shows. The
  platform override is the `QT_QPA_PLATFORM` environment variable, which Qt reads before any window or setting
  exists. So there is no in-app control for it: this is documented as a limit, not hidden.
- **Gates:** `feature_check.py`, run from a copy whose only change is that its logs go to the run folder (the
  original writes beside the worktree, which is outside this lane's write-jail) -> exit 0, every row PASS
  [feature-check-2]. `tests/doc-figures-gate.py` -> 0 [docfig-1]. `tests/all-sources-reproduce.sh` -> REPRODUCES.
- **Environment workarounds, all inside the jail:**
  - Builds used `CCACHE_TEMPDIR=/tmp/ccache-q502`, because the ccache config's `temporary_dir` is not writable from
    this lane.
  - The nested KWin used `XDG_RUNTIME_DIR=/tmp/zq502-rt`, because `/run/user` is blocked.

### What `submit_check.py` reported

`submit_check.py <worktree> Q050-502` -> exit 1. Every mechanical row passed except two.

- **FAIL gates (feature_check.py).** This failure comes from the environment, not from a gate. Run from this lane, the
  original script stops before any gate runs:
  `PermissionError: [Errno 13] Permission denied: '.../zene-feat-q050-502-feature-check'`. It creates its log
  directory beside the worktree, and that is outside this lane's write-jail. The same script with only that log path
  moved to the run folder ran every gate on this branch -> exit 0, all eight rows PASS [feature-check-2]. The loop's
  own run at session end is not under this jail and should see the same result.
- **WARN canary figures.** The regex reads `lmms_export.h`, `lmmsconfig.h` and `song.mmpz` (include names and a test
  argv) and the existing `plugin.editor_open` as new ids. No command id was added. The snapshot still measures
  868 ids / 76 groups, and `doc-figures-gate.py` -> 0.
- **The audio-path hint.** It matched two things. One is an unchanged context line, `#include "NotePlayHandle.h"`
  in `Vst3InstrumentIntegrationTest.cpp`. The other is the `display(` inside `EnvGuard display("DISPLAY")` in the new
  test. No audio-path function is added or changed. The new code runs at start-up (`chooseStartupPlatform`) and on
  the main thread when an editor opens (`editorRefusal`, `PluginNativeEditor::open`, `editorOpen`).
