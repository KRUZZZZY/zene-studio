FIXED

# cluster gui-1105-menu-navigate-next-clip: navigate refusal notification is placed off-screen

## Verdict: REAL, fixed

- `src/gui/CommandRun.cpp:87-97` `announce()` shows a command's result or refusal with
  `TextFloat::displayMessage(..., parent)`. Menu commands pass their host window as `parent`
  (`CommandRun.cpp:128` and `:150` `runCommand(hostOf(menu), ...)`; other callers pass `window()`,
  `QApplication::activeWindow()` or the main window too, e.g. `InspectorPanel.cpp:74` and `LayoutMenu.cpp:100`).
- `src/gui/widgets/TextFloat.cpp` `displayMessage` (the finding's line 113) placed the float at
  `parent->width() + 2` to the right of the parent's top-left corner (`moveGlobal`, `include/TextFloat.h:56-59`).
  That spot suits a knob, but when the parent is a maximised main window it lies past the screen's right edge,
  so nobody sees the message.
- I measured this with the fix reverted: on the 800x800 offscreen screen the float's frame landed at x=835
  (log `10-test-reverted.log`: `message at 835,2 152x80 is outside the screen 0,0 800x800`).

## Fix

`TextFloat::displayMessage` still computes the same position for both branches (with a parent, or the main window's
lower left), then keeps it if its frame fits a virtual sibling screen's `availableGeometry()` or clamps it inside a
screen if it does not (new file-local helper `onScreen()` in `TextFloat.cpp`). This preserves a float that legitimately
spills onto a neighbouring monitor while avoiding gaps in the virtual desktop's bounding rectangle.

## Regression test

`tests/src/gui/GrooveMenuTest.cpp` `aMessageOverAWindowFillingTheScreenStaysOnScreen`: it sizes the real main window
to the virtual desktop's available geometry, shows it, calls `TextFloat::displayMessage(..., 0, mainWindow)` (the same
call `announce()` makes) and checks that `frameGeometry()` lies inside one screen's available geometry. It has no
timers: timeout 0 means no singleShot, and the message is deleted directly.

## Evidence (LOG FOLDER = ~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs)

The `CCACHE_DISABLE=1` prefix is there because ccache's temp dir (`.../dsh/home/ccache-tmp-r6`) is outside this
lane's write-jail. The first build (`01-build-red.log`) failed with EXIT=2 because of that, not because of the code.

| step | command | EXIT |
| --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target GrooveMenuTest -j4 > LOGS/03-build-red.log` | 0 |
| RED test (before fix) | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > LOGS/04-test-red.log` | 8 (1 test, new slot FAIL: `message at 837,4 148x76 is outside the screen 0,0 800x800`) |
| first fix attempt | `08`/`06-test-green.log`: clamped with the client size and was off by the 2px frame margin; changed to clamp the frame | 8 |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > LOGS/07-build-green.log` | 0 |
| GREEN family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > LOGS/08-test-green.log` | 0 (2/2 tests passed) |
| 4b revert | `git diff -- src/gui/widgets/TextFloat.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| 4b reverted build + test | `cmake --build ... zene GrooveMenuTest` / `ctest -R '^GrooveMenuTest$'` (`09`, `10`) | 0 / **8** (FAIL at 835,2) |
| 4b reapply | `git apply LOGS/fix.patch` | 0 |
| 4b reapplied build + test | (`11`, `12`) | 0 / **0** (1/1 passed) |
| gates | `python3 LOGS/feature_check_logdir.py <worktree> > LOGS/14-feature-check.log` | 0 (all 8 rows PASS) |

About the gates: the stock `feature_check.py` exited 1 with `PermissionError` (log `13-feature-check.log`), because it
creates its log dir `<worktree>/../<name>-feature-check`, which is outside this lane's write-jail. I ran a copy that differs
in only two lines (`diff` shown when it was created): the logs go to `LOGS/feature-check`, and `ROOT` points at the real `qwen/`
folder, so FIX-BASE and gate-baselines are the real ones. Result: fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS.

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt` (new reason line for the inherited `TextFloat.cpp`), this report.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it:** reverted test EXIT=8
  (`10-test-reverted.log`, `aMessageOverAWindowFillingTheScreenStaysOnScreen` FAIL, frame at 835,2 on an 800-wide
  screen); restored test EXIT=0 (`12-test-reapplied.log`, 100% of 1 passed).
- **Deterministic:** there are no timers, sleeps or event-loop waits. The test sets the geometry explicitly to the
  offscreen screen's available geometry, uses timeout 0 (so no `QTimer::singleShot`), reads the geometry synchronously
  after `displayMessage` returns, and deletes the float directly.
- **At the cause and complete:** the off-screen position is computed in `displayMessage`, and every `announce()`/
  `runCommand()` caller (the grep for `runCommand(|announce(` over `src/` covers the menu, inspector, layout,
  workspace and palette callers, many of which pass a top-level window) goes through it, so they are all fixed. The
  no-parent branch gets the same clamp for consistency. It could only go off-screen if the main window itself were
  partly off-screen. Siblings left unchanged on purpose: `FloatModelEditorBase.cpp:137` and `Fader.cpp:544` call
  `moveGlobal(this, width()+2, ...)` on their own shared value float for a small knob or fader. They do not use
  `displayMessage`, they are not part of this finding, and changing `moveGlobal` in `TextFloat.h` would affect the
  live drag float of every knob in the program. They are outside this fix's scope.
- **No collateral damage:** this is GUI placement only. No saved data, project format or command is touched, and
  nothing runs on the audio thread. The clamp is a no-op whenever the float already fits on the screens, so every
  existing visible placement is the same as before. There is no new limit, and if the screen is smaller than the float,
  `std::max` keeps the float at the screen's top-left instead of handing `std::clamp` an invalid range.
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed. The bug was reproduced (RED above).
- Undo and commands: no command was added or changed, and nothing touches the undo stack. Realtime: the change is in a
  GUI-thread widget only.
- **submit_check.py:** `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> gui-1105-menu-navigate-next-clip`
  exited 1 (`15-submit-check.log`). Every mechanical row passes except `gates (feature_check.py)`. That FAIL is the same
  `PermissionError` creating `<worktree>/../zene-qwen-gui-1105-menu-navigate-next-clip-feature-check`, outside this lane's
  write-jail, and not a gate result. I re-ran the gates on the committed tree with the logdir-only copy: EXIT=0,
  `RESULT: PASS` (`16-feature-check-committed.log`). The loop's own run of the stock script, which has write access there,
  should show the same.

## QA review follow-up

The first clamp used `availableVirtualGeometry()`, whose rectangle is the union bounding rectangle of all virtual sibling
screens and can include gaps. `onScreen()` now preserves the proposed location only when the full frame fits inside one
sibling's `availableGeometry()`; otherwise it clamps into the anchor screen or the screen containing the proposed
top-left. The regression assertion checks one screen's available geometry directly. `QScreen::virtualSiblings()` and
`availableGeometry()` are available in Qt 5.15.

For the RED proof, I saved the complete product-file diff from `af010bc3cc055d477045a54e2d866f59a95017d7` with
`git diff af010bc3cc055d477045a54e2d866f59a95017d7 -- src/gui/widgets/TextFloat.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs/20-product-reversal.patch`
(EXIT=0), then reversed it with `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs/20-product-reversal.patch`
(EXIT=0), leaving test changes in place. After RED, `git checkout -- src/gui/widgets/TextFloat.cpp` (EXIT=0) restored the
branch file; `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs/21-qa-source-delta.patch`
(EXIT=0) reapplied the review correction before the final rebuild.

| step | command | EXIT |
| --- | --- | --- |
| QA build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs/17-build-qa-fix.log 2>&1` | 0 |
| QA focused tests | `cd build/tests && ctest -R '^(GrooveMenuTest|PluginShortcutsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs/18-test-qa-fix.log 2>&1` | 0 (2/2 passed) |
| QA gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1105-menu-navigate-next-clip > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs/19-feature-check-qa-fix.log 2>&1` | 0 (all rows PASS; no new complexity or file-length items) |
| reversed product build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs/22-build-reverted-product.log 2>&1` | 0 |
| reversed product tests | `cd build/tests && ctest -R '^(GrooveMenuTest|PluginShortcutsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs/23-test-reverted-product.log 2>&1` | 8 (`aMessageOverAWindowFillingTheScreenStaysOnScreen` fails; `PluginShortcutsTest` passes) |
| restored product build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs/24-build-restored-product.log 2>&1` | 0 |
| restored product tests | `cd build/tests && ctest -R '^(GrooveMenuTest|PluginShortcutsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r1/logs/25-test-restored-product.log 2>&1` | 0 (2/2 passed) |

## Integration onto 050/m0-train-26 (round 2) - verdict: FIXED, integrated without a semantic change

Integration job: accepted `bugs/qwen-gui-1105-menu-navigate-next-clip @ f9150e8918` onto pinned target
`050/m0-train-26 @ af010bc3cc`, reported as a merge conflict in `src/gui/widgets/TextFloat.cpp`.

What I found in the integration worktree (branch `integration/gui-1105-menu-navigate-next-clip-af010bc3cc`, HEAD `af010bc3cc`):
- `git merge-base af010bc3cc f9150e8918` = `af010bc3cc`. The accepted branch was already cut from this target, so the
  target has no change of its own to `TextFloat.cpp` or `GrooveMenuTest.cpp` (`git diff <mb> af010bc3cc --stat -- <those files>`
  is empty). The driver's staged result has no conflict markers (`grep '<<<<<<<\|>>>>>>>'` finds nothing) and is
  byte-identical to the accepted tip: `git diff --cached f9150e8918` is empty for every file, this report included.
- So no conflict needed resolving by hand and there is **no semantic difference from the accepted patch**. The accepted
  design stays: file-local `onScreen()` in `src/gui/widgets/TextFloat.cpp:52-71`, applied once in `displayMessage` at
  `TextFloat.cpp:155-156`; the caller in the finding is `src/gui/CommandRun.cpp:95` (`announce()`).

Failure paths and platform hazards, all together (unchanged from the accepted patch, re-checked):
- parent = maximised/screen-filling main window (the finding): clamped onto the anchor's screen - proved RED/GREEN below.
- parent = small widget (knob-like) with room to its right: frame fits a screen, position unchanged (no-op path).
- no parent: main-window lower-left position, clamped only if it would leave every screen.
- multi-monitor: a frame fitting on any virtual sibling is kept, so spill onto a neighbouring monitor stays; gaps in the
  virtual bounding rect are not treated as on-screen (the round-1 QA point).
- screen smaller than the float: `std::max` keeps a valid `std::clamp` range (top-left of the screen).
- `anchor->screen()` null (no screen yet): position returned unchanged.
- Platforms: only `QScreen::virtualSiblings()`/`availableGeometry()`/`QWidget::screen()` (Qt >= 5.14, all present in the
  supported Qt6 builds); no platform-specific code. Realtime/undo/commands: GUI thread only, no command or undo change.

Evidence (LOGS = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-clip.r2/logs`):

| step | command | EXIT |
| --- | --- | --- |
| build (ccache) | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > LOGS/build-1.log 2>&1` | 2 (`ccache: error: failed to create temporary file ... ccache-tmp-r6 ...: Permission denied` - write-jail, not code) |
| build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > LOGS/build-2.log 2>&1` | 0 |
| family tests | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > LOGS/test-1.log 2>&1` | 0 (2/2 tests passed) |
| 4b save + revert | `git diff HEAD -- src/gui/widgets/TextFloat.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| 4b reverted build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > LOGS/build-3-reverted.log 2>&1` | 0 |
| 4b RED | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > LOGS/test-2-red.log 2>&1` | **8** (`FAIL! : GrooveMenuTest::aMessageOverAWindowFillingTheScreenStaysOnScreen() 'fitsOnAScreen' returned FALSE. (message does not fit within any screen's available geometry)`; 1 test, 0% passed) |
| 4b reapply | `git apply LOGS/fix.patch` | 0 |
| 4b restored build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > LOGS/build-4-restored.log 2>&1` | 0 |
| 4b GREEN | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > LOGS/test-3-green.log 2>&1` | **0** (1/1 passed) |
| gates (stock) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-1.log 2>&1` | 1 (`PermissionError` creating `<worktree>-feature-check` beside the worktree, outside the write-jail; no gate ran) |
| gates | `python3 -I LOGS/fc-wrapper/feature_check_logs_redirected.py <worktree> > LOGS/gates-2.log 2>&1` | 0 (`RESULT: PASS`; all 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length "no new item in your files") |

The wrapper is a `sed` copy of the stock script differing in two lines only (`diff` checked at creation): `ROOT` points
at the real `qwen/` folder (real FIX-BASE `050/m0-train-26` and its already-cached gate baseline), and the log dir is
`LOGS/feature-check`.

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`, `tests/upstream-modifications.txt`
(reason line for the inherited `TextFloat.cpp`; `GrooveMenuTest.cpp` line), this report.

## Self-check (round 2, integration)

- **Test FAILS with the product change reverted and PASSES with it:** RED `LOGS/test-2-red.log` EXIT=8
  (`aMessageOverAWindowFillingTheScreenStaysOnScreen` FAIL, `'fitsOnAScreen' returned FALSE`); GREEN `LOGS/test-3-green.log`
  EXIT=0 (1/1). Family `LOGS/test-1.log` EXIT=0 (2/2).
- **Deterministic:** unchanged test; no timers, sleeps or event-loop waits. Seam: geometry is set explicitly to the
  offscreen screen's available geometry, timeout 0 means no `QTimer::singleShot`, the float is read synchronously and deleted.
- **At the cause and complete:** the integrated code is identical to the accepted tip (`git diff --cached f9150e8918` empty
  before commit). The only `displayMessage` caller in the finding's path is `src/gui/CommandRun.cpp:95`; every
  `announce()`/`runCommand()` path goes through `TextFloat.cpp:155-156`. Sibling knob/fader floats (`moveGlobal`) are out
  of scope as argued in round 1.
- **No collateral damage:** GUI placement only; no saved data, project format, command, undo or audio-thread change; the
  clamp is a no-op whenever the float already fits on a screen.
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; verdict FIXED (RED reproduced on this target).
- **submit_check.py:** `LOGS/submit-1.log` EXIT=1; its only FAIL is `gates (feature_check.py)`, because the stock script
  dies with `PermissionError` creating `<worktree>-feature-check` beside the worktree, outside this lane's write-jail
  (`LOGS/gates-3-stock-committed.log` STOCK_EXIT=1, no gate ran). The same gates via the logdir-only copy on the
  committed tree: `LOGS/gates-4-committed.log` EXIT=0, `RESULT: PASS`. The loop's own run, which can write there, should match.
