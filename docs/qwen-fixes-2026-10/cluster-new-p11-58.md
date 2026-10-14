# cluster new-p11-58 — Online Help can fail without notifying the user

**Verdict: FIXED**

## The finding

`src/gui/ControlCommandsShell.cpp:187` (pre-fix line) — *the handler returns `opened: false` if
the desktop cannot open the URL, but the menu dispatch path discards the command result.*

## What was actually wrong (two defects, one user)

Following the reported path (Help ▸ Online Help) showed the reported symptom and one defect
underneath it that makes it permanent:

1. **The command never ran for anyone.** `app.online_help` declares
   `requires: {display, human}` (`ControlCommandsShell.cpp:184`), and
   `ControlRegistry::checkRequires` (`src/core/ControlRegistry.cpp:199-202`) returns
   `"command '%1' requires a human"` **unconditionally** — there is no flag that clears it.
   `MainWindow::browseHelp()` (`src/gui/MainWindow.cpp:1723-1727`) reached the handler through
   `dispatchShellCommand` → `ControlRegistry::instance()->invoke()`
   (`ControlCommandsShell.cpp:431-434`), which drops the refusal. So the menu entry opened
   nothing *even on a system where a browser is registered*, and the handler's `opened: false`
   never reached anyone either.
2. **The reported silent failure.** Once the handler runs and the desktop refuses the URL
   (no URL handler registered), the only report is `opened: false` inside a result the menu
   throws away. The user sees nothing happen and is given no reason.

The fork already documents the arrangement that (1) was missing: the Help menu's **Telemetry
action** next to it declares `telemetry.consent` (also `requires: display, human`) and its slot
calls `openTelemetryConsentScreen()` directly, because "the registry refuses it for any
unattended caller before the handler runs" (`src/gui/MainWindow.cpp:500-519`). `browseHelp()`
is the one `human` menu action that did not follow it.

## The fix (smallest change that makes the reported case true)

* `include/ControlWindowCommands.h` — declares `openOnlineHelp(gui::MainWindow*)`: the shared
  implementation, next to `dispatchShellCommand`.
* `src/gui/ControlCommandsShell.cpp:438-452` — `openOnlineHelp()` hands the documentation URL to
  the desktop and, when the desktop will not take it, puts the reason and the URL in the window's
  status bar. The command's handler now calls it; its result (`url`, `opened`), its `noInterface`
  refusal and its description/schema are unchanged, so the command surface does not move.
  A status line, never a modal box: this code also runs in an unattended instance, where an
  `exec()` is a wait on a click that never comes (`include/UnattendedRun.h`, task #625) — the
  same rule `MainWindow::captureMidiToClip` follows (`MainWindow.cpp:1498-1505`).
* `src/gui/MainWindow.cpp:1723-1731` — `browseHelp()` calls `openOnlineHelp(this)`, the Telemetry
  action's arrangement, so the menu item and the command still share one implementation
  (SPEC A11) and the action keeps declaring `app.online_help`.

## Evidence

| Step | Command | Exit |
|---|---|---|
| RED — before any product change | `cmake --build build --target OnlineHelpNoticeTest` → `ctest -R OnlineHelpNoticeTest --output-on-failure` (from `build/tests`) | build 0, **ctest 8 (2 of 4 test functions FAIL)** |
| GREEN — with the fix | same two commands | build 0, **ctest 0 (1/1 test passed, 4/4 functions)** |
| RED again — fix reverse-applied (`git apply -R fix.patch`) | `cmake --build build --target zene OnlineHelpNoticeTest` → `ctest -R OnlineHelpNoticeTest` | **ctest 8 (2 functions FAIL)** |
| GREEN again — fix re-applied (`git apply fix.patch`) | same two commands | **ctest 0** |

RED log (`logs/test-1.log`, unmodified tree), both defects visible at once:

```
QWARN : OnlineHelpNoticeTest::nothingOpeningIsSaidOutLoud() This plugin does not support QPlatformServices::openUrl() for 'https://example.invalid/'.
FAIL! : OnlineHelpNoticeTest::nothingOpeningIsSaidOutLoud() 'message.contains(QStringLiteral("https://"))' returned FALSE. ()
FAIL! : OnlineHelpNoticeTest::anOpenedUrlIsNotReportedAsAFailure() 'm_seen.contains(QStringLiteral("zene-studio"))' returned FALSE. ()
Totals: 2 passed, 2 failed
```

`m_seen` empty is defect (1): the URL never reached the desktop at all. The empty status message
is defect (2): even when the desktop refuses, the user was told nothing.

Regression: `tests/src/gui/OnlineHelpNoticeTest.cpp` (new, registered in `tests/CMakeLists.txt`
and in `tests/all-sources.txt`). It drives the **real** Help ▸ Online Help action over the real
`GuiApplication` main window, found through the command it declares:

* `nothingOpeningIsSaidOutLoud` — under `offscreen` the platform takes no URL
  (`This plugin does not support QPlatformServices::openUrl()`), the exact finding's case: the
  click must leave a message the user can read.
* `anOpenedUrlIsNotReportedAsAFailure` — a `QDesktopServices` URL handler takes the URL and
  answers true, so the working path must stay quiet (no nagging) and must reach the desktop.

### Affected-family runs (all from `build/tests`)

| Run | Command | Result |
|---|---|---|
| GUI + control families affected by the header change and by the menu action (18 tests) | `ctest -R "OnlineHelpNoticeTest\|HelpAccessTest\|AccessibilityWalkTest\|ArrangerTimelineTest\|CaptureMidiMenuTest\|CollabPanelTest\|MixerModesTest\|MixerSendsOnFadersTest\|ShellTest\|TrackFolderMenuTest\|VcaStripLifetimeTest\|VisibilitySetMenuTest\|ControlRegistryTest\|ControlSuccessPathsTest\|ControlResultSchemaTest\|ControlSurfaceReferenceTest\|CommandPaletteTest\|AutomationModeMenuTest" --output-on-failure` | **17 of 18 passed**; the 18th (`AutomationModeMenuTest`) fails with `no instrument to drive (LMMS_TEST_PLUGIN_DIR)` — **verified pre-existing**: reverse-applying the fix and rebuilding gives the identical failure (`logs/test-8.log`) |
| Command-surface canary (the menu action changed) | `ctest -R ControlCommandsSnapshot --output-on-failure` | **passed** (1/1) — no command surface moved |

### Gates (after the final edit)

| Gate | Command | Exit |
|---|---|---|
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | **0** |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | **0** |
| Unregistered tests | `bash tests/unregistered-tests-gate.sh` | **0** |
| Fork sources (incl. `all-sources.txt` reproduction) | `bash tests/fork-sources-gate.sh` | **0** |

## Files committed

Product:

* `src/gui/ControlCommandsShell.cpp`
* `src/gui/MainWindow.cpp` (inherited — registered in `tests/upstream-modifications.txt`)
* `include/ControlWindowCommands.h`

Test / registration:

* `tests/src/gui/OnlineHelpNoticeTest.cpp` (new, `git add`ed)
* `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`
* `docs/qwen-fixes-2026-10/cluster-new-p11-58.md` (this report)

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-58.r1/logs/`
(`build-1..13.log`, `test-1..10.log`, `gate-*.log`, `fix.patch`).