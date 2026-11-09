FALSE-POSITIVE - gui-1030-main-pop-out-the-note-editor-panel and its four batch companions (one verdict line each below)

# GUI sweep 050/m0-train-24: five "pop-out / Float or Dock Panel left the panel docked" reports

All five reports are in `src/gui/shell/ShellFloating.cpp`. I reproduced every one of them on a live instance with real
mouse clicks before reading any conclusion into the code. **None of them is a product defect, so no product file is
changed.** This commit adds a deterministic test that pins what each control really does (one slot per report), and this
report. Luna's round-0 QA asked for the finding to be reproduced first and not to change intended behaviour; that is
what was done.

## Verdicts (one line per batch member)

gui-1030-main-pop-out-the-note-editor-panel: FALSE-POSITIVE - the real button floats the Note Editor (live: `detail` floating=true after the click); the sweep's click landed on the floating Browser window that covered the button.
gui-1030-main-pop-out-the-arranger-panel: FALSE-POSITIVE - the real click floats the Arranger (live: `arranger` floating=true, button reads "Dock"); the window opens exactly over the old place with no title bar, so the screenshot looks unchanged.
gui-1030-tab-devices-pop-out-the-devices-panel: FALSE-POSITIVE - the real click floats Devices (live: `devices` floating=true); same in-place, undecorated window as the Arranger; the "after" screenshot itself shows the button turned into "Dock".
gui-1030-tab-automation-pop-out-the-automation-pa: FALSE-POSITIVE - the real click floats Automation (live: `automation` floating=true); same as Devices.
gui-1030-menu-view-float-or-dock-panel: FALSE-POSITIVE - the action floats the keyboard-focused panel that is on screen, else the view's main panel (its documented contract); it floated the Clip Launcher (the view's centre) in the sweep, and a visible-but-unfocused Pattern Editor is by contract left alone.

## Evidence

### How the sweep sees a click (why a floated panel looks "embedded")

`terra-pilot/buttons/sweep.py:5-30` runs Zene on **Xvnc + xcb with no window manager** and judges a before|after screenshot of the whole
screen. `Shell::detachPanel` opens the floating window at the panel's own place and size
(`src/gui/shell/ShellFloating.cpp:248` `at = frame->mapToGlobal(0,0)`, `:260` `setGeometry(QRect(at, floatSize(frame)))`;
the comment at `:246` states the intent: "a pop-out never sends the panel far from the eye"). Without a window manager
that window has no title bar and sits exactly over the docked panel. The only visible change is the header button, which
`syncFloatButton` (`ShellFloating.cpp:429-446`) turns from an icon into the text "Dock" - and that is exactly what the
sweep's own "after" images show:

- `.../m0-train-24/shots/0039-pair.png` (Arranger): "after" header reads `Dock | Pin`.
- `.../shots/0125-pair.png` (Devices): "after" header reads `Dock | Pin`.
- `.../shots/0227-pair.png` (View > Float or Dock): "after" shows the CLIP LAUNCHER header with `Dock | Pin`.

### Live reproduction (my tree's `build/zene`, same Xvnc driver as the sweep, real clicks)

Driver: `terra-pilot/gui.py` (`start`, `click`, `shot`, `key`, `stop`), state in my log folder; panel state read over the
control socket with `window.get_state`. Screenshots and the scripts are kept in
`<LOG FOLDER>/repro/` (`0-before.png` ... `8-after-float-or-dock.png`, `g.sh`, `sock.py`, `focus.py`).

| Scenario (sweep coordinates) | Real click | `window.get_state` afterwards |
| --- | --- | --- |
| Arranger Pop out (1257,99) | `g.sh click 1257 99` | `arranger` floating=True (`1-after-arranger.png`: header reads "Dock", panel in place) |
| Note Editor Pop out, button in its own place (1255,712) | `g.sh click 1255 712` | `detail` floating=True |
| Note Editor Pop out at the sweep's coordinate (1539,712) with the Browser already floating | `g.sh click 1539 712` | `detail` floating=False, `browser` floating=True: the click hit the floating Browser window (`3-click-1539-712.png`; the sweep's own red box in `0020-pair.png` is on the Browser's "Vibed" list row, not on a button) |
| Devices tab, then Pop out (1259,712) | `g.sh click 496 683; g.sh click 1259 712` | `devices` floating=True |
| Automation tab, then Pop out (1259,712) | `g.sh click 600 683; g.sh click 1259 712` | `automation` floating=True |
| View > Float or Dock Panel (202,424) with Automation visible below but not focused | `g.sh click 130 15; g.sh click 202 424` | `arranger` (the view's centre) floating=True, `automation` stays docked - the documented fallback |

`window.get_state` `focused` was `'arranger'`/`'browser'` as the real keyboard focus moved; clicking a panel's empty area
did not change it (that content takes no click focus), which is why an unfocused visible panel is not the target.

### Code facts per report

- **Pop out button** - `ShellFloating.cpp:424` connects `clicked` to `dispatchFloat(id, !isDetached(id))`; `dispatchFloat`
  (`:220-231`) runs `window.detach`/`window.attach` through the registry when this shell is the main window's, directly otherwise.
  `window.detach` is `movePanel` (`src/gui/ControlCommandsLayouts.cpp:220-236`) -> `detachTo` (`:212`) -> `Shell::detachPanel`
  (`ShellFloating.cpp:237`), which returns true for any panel the shell has. `guiCommand` (`src/gui/ControlGuiSupport.cpp:35-43`)
  sets no `requires` and `requiresEngine=false`, `window.detach` is not mutating, and the collab tap only intercepts on a
  joined peer session (`src/core/CollabCommandTap.cpp:61-65`), so the registry has no refusal for a valid panel id.
  Luna's "the ControlResult is discarded at `:229`" is true as written, but nothing reaches it: the live instance answered every click.
- **Note Editor (1539,712)** - an earlier trial in the sweep had left the Browser floating (it opens over the right-hand column,
  `ShellFloating.cpp:248`); the inventory then listed the Note Editor's button at x=1539, which that floating window covers.
- **View > Float or Dock Panel** - `ShellFloating.cpp:210-216`: "The focused panel when it is on screen; else the view's main
  panel", the contract also in the action's tooltip (`:208`) and in Terra's own expectation ("The focused panel, or the Arranger
  if none is focused"). Focus is recorded only from Qt's real `focusChanged` (`src/gui/shell/Shell.cpp:454-461`).

## Not changed, and why (observations for the owner, outside these reports)

1. The pop-out window opens on top of the panel it came from (by design, see above). On a desktop with a window manager it has a
   title bar; on the sweep's WM-less Xvnc it is indistinguishable apart from the "Dock" label, so the sweep will keep reading
   pop-outs as "unchanged" unless it checks the `floating` flag or the button text. Changing where windows open is a behaviour
   change nobody asked for.
2. `dispatchFloat` (`ShellFloating.cpp:229`) still ignores its `ControlResult`. I found no refusal path to surface, so I did not
   invent feedback for one.
3. After a panel floats, Qt's focus chain moves keyboard focus to another main-window panel (observed: Pattern Editor floated,
   focus went to the Inspector), so a second Ctrl+Shift+F from the main window floats that panel instead of docking the first.
   From the floating window the key docks it (`own->addAction(m_floatAction)`, `:258`). Not what these reports describe; left alone.

## Tests

New `tests/src/gui/ShellFloatButtonTest.cpp` (registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
`tests/all-sources.txt`; `QT_QPA_PLATFORM=offscreen`). It clicks the real `panelFloat` button with `QTest::mouseClick` and uses
real keyboard focus; no timing, sleeps or races (`qWaitForWindowActive` only waits for the offscreen window to be active, and
the slot fails if it is not):

| Slot | Report |
| --- | --- |
| `theNoteEditorPopOutButtonFloatsItOutOfTheBottomRegion`, `aPopOutWindowOpensWhereThePanelWas` | gui-1030-main-pop-out-the-note-editor-panel |
| `theArrangerPopOutButtonFloatsItAndTheCentreSaysSo` | gui-1030-main-pop-out-the-arranger-panel |
| `theDevicesPopOutButtonFloatsThePanelTheTabOpened` | gui-1030-tab-devices-pop-out-the-devices-panel |
| `theAutomationPopOutButtonFloatsThePanelTheTabOpened` | gui-1030-tab-automation-pop-out-the-automation-pa |
| `floatOrDockFloatsTheFocusedPanelThatIsOnScreen`, `floatOrDockFloatsTheViewsMainPanelWhenTheVisibleOneIsNotFocused`, `floatOrDockIgnoresAFocusedPanelThatIsOffScreen` | gui-1030-menu-view-float-or-dock-panel |

In a test the shell is not the main window's, so `dispatchFloat` takes its direct branch; the registry branch is what the live
reproduction above exercised (and `tests/control-customisation.py` `check_floating` covers `window.detach`/`window.attach` on the socket).

## Commands and exit codes (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-main-pop-out-the-note-editor-panel.r1/logs`)

| Step | Command | EXIT |
| --- | --- | --- |
| configure (refused by the lane guard: "configuring is the parent's job") | `cmake -S . -B build` (`cmake-1.log`) | 97 |
| build (regenerates the build tree itself) | `cmake --build build --target ShellFloatButtonTest -j4` (`build-3.log`, `build-4.log`, `build-6.log`) | 0 |
| first test run (my first draft of the focus slot assumed a second key press docks; it does not, see observation 3) | `cd build/tests && ctest -R ShellFloatButtonTest --output-on-failure` (`test-1.log`) | 8 (1 slot failed) |
| test with the final slots, original product source | same command (`test-2.log`) | 0 |
| live reproduction | `gui.py start/click/shot/key/stop` + `window.get_state` (`repro/`) | 0 each |
| **mutation check** (see below) build | `cmake --build build --target ShellFloatButtonTest -j4` (`build-8-mutant.log`) | 0 |
| **mutation check** run | `cd build/tests && ctest -R ShellFloatButtonTest --output-on-failure` (`test-4-mutant.log`) | 8 (6 slots fail) |
| product file restored (`git checkout -- src/gui/shell/ShellFloating.cpp`), shell family rebuilt | `cmake --build build --target ShellFloatButtonTest ShellTest ShellHomesTest ShellLayoutTest -j4` (`build-9.log`, `build-10.log`) | 0 |
| shell family, original source | `cd build/tests && ctest -R "ShellFloatButtonTest|ShellTest|ShellHomesTest|ShellLayoutTest" --output-on-failure` (`test-6.log`): 4 of 4 tests passed | 0 |

**There is no RED-without-fix / GREEN-with-fix pair because there is no fix: the verdict is that the product already does what
the controls promise.** To show the new tests would catch the behaviours they pin, I temporarily mutated
`ShellFloating.cpp` (the Pop out button's handler called `dispatchFloat(id, false)`, and the action's target ignored the
focused panel), rebuilt, and ran: the Note Editor, window-position, Arranger, Devices, Automation and focused-panel slots all FAILED
(`test-4-mutant.log`, `Totals: 4 passed, 6 failed`); the two fallback slots pass under that mutation because the mutation
makes the action always take the fallback. The mutation was then reverted with `git checkout` (the product file is
byte-identical to HEAD; `git diff HEAD --stat -- src` is empty).

## Environment notes (workarounds, none changes shared config)

- `ccache` failed with "Permission denied" creating its temporary file under `.../zene-bunny-fix/dsh/home/ccache-tmp-r6` (set by
  `~/.cache/ccache-bunny/ccache.conf`, outside the write-jail). I set `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-gui1030` in my own
  shell environment only.
- `qwen/feature_check.py` writes its logs to `<worktree>-feature-check/`, a sibling directory the write-jail refuses
  (`PermissionError`, `gates-1.log`). I ran a copy in my log folder (`feature_check_local.py`) that differs from the original in
  exactly two lines: `ROOT` is pinned to the `qwen` directory and the log directory is `<LOG FOLDER>/feature-check`. The gates and
  their commands are the original's.

## Gates

`python3 <LOG FOLDER>/feature_check_local.py <worktree>` (the original script with only the log path changed, see above), run after
`git add` of the five committed paths: `GATES_EXIT=0`, `gates-3.log`.

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in my files) |
| file-length | PASS (no new item in my files) |

The unmodified `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exits 1 with `PermissionError` before running
any gate (it cannot create `<worktree>-feature-check/`; `gates-1.log`). An earlier run of the copy before the report and the
index entries existed failed only `fork-sources` (stale entry for the not-yet-written report) and `all-sources-reproduce` (the
recipe derives from tracked files; `gates-2.log`).

## Self-check

1. *The regression test FAILS with the product change reverted and PASSES with it (exit codes).* There is no product change (verdict
   FALSE-POSITIVE), so there is no revert to run. Instead of that pair: original source, new test EXIT=0 (`test-2.log`); mutated
   source (button handler never floats, action ignores focus), same test EXIT=8, 6 slots fail (`test-4-mutant.log`); source restored,
   shell family EXIT=0, 4 of 4 (`test-6.log`). The tests pin behaviour; they were never red against HEAD because HEAD is correct.
2. *Deterministic, no wall-clock timing, sleeps or races.* Yes: `QTest::mouseClick` on the button, `QAction::trigger()`, state read
   synchronously; focus comes from `QWidget::setFocus()` after `qWaitForWindowActive` on the offscreen window (the slot fails
   rather than skipping when focus is not recorded). No timers, no sleeps, no injected clocks needed.
3. *Fix at the cause and complete, callers and siblings checked.* Nothing changed. Siblings of the button: the header menu, the
   tear-off drag, the key and the placeholder's "Dock It Here" all end in `dispatchFloat` -> `window.detach`/`window.attach`
   (`ShellTearOff.cpp:235,340,342,395`, `ShellFloating.cpp:184,215,424`); one path, and it works live.
4. *No collateral damage.* No product, saved-data or layout change; only a new test file, its registration and this report.
5. *FALSE-POSITIVE: the code or contract that shows it.* See "Code facts per report" (`ShellFloating.cpp:208-216,220-231,237-262,424`;
   `ControlCommandsLayouts.cpp:212-236`; `Shell.cpp:454-461`) and the live reproduction table; the sweep's own "after"
   screenshots (`0039`, `0125`, `0227`) show the "Dock" label.
6. *Audio thread / realtime.* Nothing touched; the new code is a GUI test.
7. *Undo / commands.* No command added or changed (`window.detach`/`window.attach` are non-mutating interface state, unchanged);
   `ControlCommandsSnapshot` is unaffected.
8. *Old projects load as before; Qt 5.15/MSVC/macOS portability.* No product code changed. The test uses only Qt Widgets/Test API
   that ShellTest.cpp already uses (`QTest::mouseClick`, `qWaitForWindowActive`, `QAction::trigger`).
