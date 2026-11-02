Verdict: FALSE-POSITIVE

gui-1030-main-mixer: FALSE-POSITIVE - the Mixer tab opens the Mixer when it is actually clicked; the sweep clicked a floating Inspector window that covered it (rows 12-13 of `results.jsonl`, geometry below).
gui-1030-main-devices: FALSE-POSITIVE - same cause: Devices tab centre (245, 684) lay under the floated Inspector window; the tab opens Devices when clicked (`clickingTheDevicesTabOpensTheDevices`).
gui-1030-main-automation: FALSE-POSITIVE - same cause: Automation tab centre (348, 684) lay under the floated Inspector window; the tab opens Automation when clicked (`clickingTheAutomationTabOpensTheAutomation`).
gui-1030-main-pattern-editor: FALSE-POSITIVE - same cause: Pattern Editor tab centre (468, 684) lay under the floated Inspector window; the tab opens the Pattern Editor when clicked (`clickingThePatternEditorTabOpensThePatternEditor`).

No product code was changed. Each batch member keeps its own test scenario (four click tests plus one geometry test that documents the cause); they pass on the unmodified product code, so there is no RED/GREEN pair and no step-4b reversal - nothing is fixed, and none is claimed.

## Evidence

1. The sweep's own state explains the clicks. `terra-pilot/buttons/runs/m0-train-24/results.jsonl`, row 12 is `button "Pop out the Inspector panel"` (clicked). Rows 13, 15, 16, 17 are the four reported tab clicks at x = 44, 245, 348, 468, all at y = 684. The "before" screenshot (`shots/0013-pair.png`) shows the Inspector with its floating-window "Dock | Pin" header over the left of the screen and no tab strip visible there. `Dock` is only shown for a floating panel (`src/gui/shell/ShellFloating.cpp:436-438`).
2. Floating the Inspector shifts the tab strip left by exactly the Inspector column and puts a top-level window over it. In the live `MainWindow` at 1600x1000 (temporary dump while diagnosing; not kept in the test): with the Inspector docked, tab centres were 285/382/486/590/709, y = 683; with it floated they were 43/140/244/348/467, y = 683 - the sweep's 44/141/245/348/468. The floated window was `QWidget "Inspector - Zene Studio" QRect(2,87 480x800)`, so it covers y = 684 for x = 2..482: the Mixer, Devices, Automation and Pattern Editor clicks all landed on it. Note Editor (row 14) was already the open tab. Hide (row 18, x = 1582) is outside the window and worked. In view `tab-mixer` the strip starts at 247 and Devices/Automation/Pattern (487/589/711) sit outside it, and those clicks were not flagged.
3. A real click on each of those tabs does what the tab promises. `MainWindowTest` drives the real `MainWindow` (`guitest::startGui`), starts from the Arrange view with the Note Editor open below, and sends `QTest::mouseClick` to the tab's rect centre; the shell opens the panel, the tab becomes current, and the Note Editor closes. All four pass on the unmodified code. The click goes through `ShellRegionTabs::onCurrentChanged` -> `choose` -> `toggle` -> `dispatchWindowToggle` (`src/gui/shell/ShellRegionTabs.cpp:194-217`), i.e. the path Luna named, with `controlMainWindow()->shell() == m_shell`.
4. On Luna's "root cause" (discarded `ControlResult`): `window.toggle` for `mixer`/`devices`/`automation`/`pattern` is a plain `togglePanel` (`src/gui/ControlCommandsWindow.cpp:132-143`, `kEditors` lines 64-91), and test 3 shows the shell does change. `choose()` re-syncing the strip to what is really open after a refused command is documented intended behavior (`ShellRegionTabs.cpp:209`), so I did not change it.

## Tests added (tests/src/gui/MainWindowTest.cpp, already in fork-sources.txt)

- `aFloatedInspectorCoversTheFirstBottomTabsInTheLiveWindow` - floats the Inspector and asserts all four tab centres (global) lie inside the floating window's frame (`QTRY_COMPARE`, no sleeps), then re-docks it.
- `clickingTheMixerTabOpensTheMixer`, `clickingTheDevicesTabOpensTheDevices`, `clickingTheAutomationTabOpensTheAutomation`, `clickingThePatternEditorTabOpensThePatternEditor` - one scenario per batch member.

## Commands (from `build/tests` for ctest; logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-main-mixer.r1/logs`)

| Evidence | Command | Result |
| --- | --- | --- |
| Build, first try | `cmake --build build --target MainWindowTest -j4 > logs/build-1.log 2>&1` | EXIT=2: ccache could not write its temp dir under the write-jail. Retried with `CCACHE_DISABLE=1` (all later builds) |
| Build | `CCACHE_DISABLE=1 cmake --build build --target MainWindowTest -j4 > logs/build-7.log 2>&1` | EXIT=0 |
| Reproduction | `ctest -R '^MainWindowTest$' --output-on-failure > logs/test-2.log 2>&1` (first helper version toggled `detail` off when it was already open - a test bug, fixed before test-2) | EXIT=0: the four click scenarios PASS on unmodified product code, so the bug does not reproduce |
| Geometry diagnosis | `./MainWindowTest zzDump > logs/test-4.log 2>&1` (temporary slot, removed) | EXIT=0: floated Inspector `QRect(2,87 480x800)`, tab centres 43/140/244/348/467 |
| Final family run | `ctest -R '^MainWindowTest$' --output-on-failure > logs/test-6.log 2>&1` | EXIT=0, 1/1 ctest test; binary run `logs/test-9.log`: 9 passed, 0 failed |
| Sibling family | `ctest -R '^ShellHomesTest$' --output-on-failure > logs/test-7.log 2>&1` | EXIT=0, 1/1; binary run `logs/test-8.log`: 17 passed, 0 failed |
| Gates | `python3 logs/feature_check_local.py $PWD > logs/gates-2.log 2>&1` | EXIT=0, all eight rows PASS (`logs/gates-2.log`) |

`feature_check.py` itself (`logs/gates-1.log`, EXIT=1) could not run as shipped: it creates `<worktree>-feature-check` beside the worktree, which the write-jail refuses (`PermissionError`). `logs/feature_check_local.py` is a copy that differs only in `ROOT` (fixed to the qwen directory so `FIX-BASE` and `gate-baselines` resolve) and the log directory (`logs/feature-check`); `diff` against the original shows just those lines, and the gate commands and limits are unchanged.

## What is not covered

Occlusion by a floating window cannot be reproduced pixel-wise on the offscreen platform; the claim rests on the sweep's own row order, its screenshot, and the measured geometry above. If the owner still sees a tab click do nothing with no floating window over it, that would be a different bug and needs a fresh repro. A product-side hardening (for example dock-or-shift behaviour when a floating panel covers the strip) would be a behavior change, so it was not made.
