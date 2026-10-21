# Cluster new-p17-49

**Verdict: FIXED**

Three findings in one file, `src/gui/shell/DeviceCard.cpp` (the shell's device card). One verdict
each, with its own fix, its own regression test and its own RED/GREEN proof.

new-p17-49: FIXED
new-p15-48: FIXED
new-p9-104: FIXED

## new-p15-48: FIXED

### The finding

The fold button's handler used the card **after** the dispatch:
`if (dispatch(this, "plugin.fold", args).ok) { m_body->setVisible(!folded); }`. The dispatch runs a
registered command, and a command can retire the card - the card's body would then be a dangling
pointer. The fix the finding asks for is a `QPointer<DeviceCard>` captured before the dispatch.

### Evidence in the tree (before the fix)

- `src/gui/shell/DeviceCard.cpp:277-283` - the handler read `m_body` (a raw `QWidget*` member) after
  the dispatch, with no check that the card was still there; the same branch also wrote to the
  header's fold button (`fold->setChecked(!folded)`), a child of the card.
- `src/gui/shell/DeviceCard.cpp:319` - `m_body` is a child of the card, so it lives exactly as long
  as the card: if the card is gone, the pointer dangles.
- The sibling entry of the same feature already guards the card it uses
  (`src/gui/shell/DeviceActionMenu.cpp:62`, `[card = QPointer<DeviceCard>(card)] { if (!card.isNull()) ... }`).

**Reachability, measured:** the panel retires a card with `deleteLater()`
(`src/gui/shell/DevicePanel.cpp:212`) and the dispatch path runs no nested event loop
(`ControlRegistry::invoke` → the `plugin.fold` handler → `applyOptions` → `EffectChain::dataChanged`),
so with the panel's own retirement the card is still alive when the handler continues - the finding
is latent, not everyday. The guard removes the dependence on that: any listener that retires the
card while the fold command runs (the test below does exactly that) no longer leaves the handler
writing to a card that is gone.

### The fix

`src/gui/shell/DeviceCard.cpp:311-325` (the new `DeviceCard::foldCard`, called by the header's fold
button) - the card is captured as a `QPointer<DeviceCard>` before the dispatch, and both writes
after it (the body's visibility, the button's check mark) happen only while the card is still
there. The handler moved out of `buildHeader` so that the header's own complexity did not grow
(the complexity gate rejects a function over 10). Nothing else changed: the same command is
dispatched, and a card that lives folds exactly as before.

### The regression test

`tests/src/gui/DevicePanelTest.cpp` - `foldingACardThatTheDispatchRetiresTouchesNothing`: a real
effect on a real mixer channel, a card of its own, and a listener on the chain's own signal that
retires the card while the fold command runs. The card's body is let out of the card first, so it
outlives it and the test can watch what the handler does to it. The test asserts that the card was
retired during the dispatch **and** that its body was left alone.

The test is deterministic: the retirement happens in a signal handler of the chain (a seam, not a
timing), and the body is read with `isVisible()`. The RED run was repeated five times and failed
the same way each time.

### Commands and exit codes

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED (fix reverted) | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-49.r1/logs/fix-p15-48.patch && cmake --build build --target DevicePanelTest zene -j 20` then `ctest -R DevicePanelTest --output-on-failure` (from `build/tests`) | 8 | `FAIL! foldingACardThatTheDispatchRetiresTouchesNothing() 'body->isVisible()' returned FALSE. (the fold touched a card that was already gone)`; 0% tests passed. Repeated 5 times: the same failure each time. |
| GREEN (fix restored) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-49.r1/logs/fix-p15-48.patch && cmake --build build --target DevicePanelTest zene -j 20` then `ctest -R DevicePanelTest --output-on-failure` (from `build/tests`) | 0 | 100% tests passed (1 ctest target, 23 test functions). |
| Dependents of the changed header | `cmake --build build --target InstrumentSetupUiTest RackMenuTest -j 20` then `ctest -R "DevicePanelTest\|InstrumentSetupUiTest\|RackMenuTest" --output-on-failure` (from `build/tests`) | 0 | 3/3 ctest targets passed. |

Logs: `build-14-red.log`, `test-12-red.log` (RED), `build-15-green.log`, `test-13-green.log`
(GREEN), `build-13.log`, `test-11-family.log` (the family) in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-49.r1/logs/`.

## new-p17-49: FIXED

### The finding

`DeviceCard::openEditor` threw away the result of `plugin.editor_open`:
`if (!invoke(...).ok) { openEffectDialog(); }`. When the fallback - the built-in control dialog -
had no view to show either (`if (dialog == nullptr) { return; }`), the function returned without a
word: no window opened and no error shown, so the user could not tell what went wrong.

### Evidence in the tree (before the fix)

- `src/gui/shell/DeviceCard.cpp:381` - the failed `ControlResult` was discarded, so the command's
  own reason (why the device has no editor of its own, or why it could not be opened) was lost.
- `src/gui/shell/DeviceCard.cpp:389` - the fallback's `if (dialog == nullptr) { return; }` returned
  silently as well: nothing was opened and nothing was said.
- `src/gui/shell/DeviceCard.cpp:72-81` - the card dispatches through `invoke`/`dispatch`; the
  shell's own way of telling a user that a command failed is `announce()`
  (`src/gui/CommandRun.cpp:87`), which this path never reached.

### The fix

- `src/gui/shell/DeviceCard.cpp:368-395` - the command's result is kept. When the device's own
  editor opens, or the built-in dialog opens, nothing is said (that is the ordinary path for a
  built-in effect). When **neither** opens, the card reports the command's own reason: the state
  line under the header says `Open Editor failed: <the command's words>` and the reason is
  floated as a message over the card (`announce`).
- `src/gui/shell/DeviceCardState.cpp:191-210` - the state line carries that reason until the editor
  opens (the state line is the card's own line of words, and it is what a screen reader reads).
- `include/DevicePanel.h:120-129` - `openEffectDialog()` says whether it opened something, and
  `setEditorError()` keeps the reason.

Nothing else changed: the same command is dispatched, the same dialog opens, and a device whose
editor opens is unaffected (no message, no state change).

### The regression test

`tests/src/gui/DevicePanelTest.cpp` - `theCardSaysWhyTheEditorDidNotOpen`: a test device whose
controls have no view of their own (`NoViewEffect`, built in the test file) sits on a real mixer
channel, so `plugin.editor_open` refuses it with the command's own reason. The test reads that
reason from the command, asks the card to open the editor, and asserts that the card's state line
carries **that** reason (and that the line's accessible name does too).

The test is deterministic: the reason comes from the command itself (no hard-coded string, no
timing), and the card's state line is read directly.

### Commands and exit codes

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED (fix reverted) | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-49.r1/logs/fix-p17-49.patch && cmake --build build --target DevicePanelTest zene -j 20` then `ctest -R DevicePanelTest --output-on-failure` (from `build/tests`) | 8 | `FAIL! theCardSaysWhyTheEditorDidNotOpen() 'state->text().contains(failed.errorMessage)' returned FALSE. (On)` - the card said only "On"; 0% tests passed. |
| GREEN (fix restored) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-49.r1/logs/fix-p17-49.patch && cmake --build build --target DevicePanelTest zene -j 20` then `ctest -R DevicePanelTest --output-on-failure` (from `build/tests`) | 0 | 100% tests passed (1 ctest target, 22 test functions). |
| Dependents of the changed header | `cmake --build build --target InstrumentSetupUiTest RackMenuTest -j 20` then `ctest -R "DevicePanelTest\|InstrumentSetupUiTest\|RackMenuTest" --output-on-failure` (from `build/tests`) | 0 | 3/3 ctest targets passed (`include/DevicePanel.h` is included by all three). |

Logs: `build-7-red.log`, `test-6-red.log` (RED), `build-8-green.log`, `test-7-green.log` (GREEN),
`build-9.log`, `test-8-family.log` (the family), in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-49.r1/logs/`.

## new-p9-104: FIXED

### The finding

`DeviceCard::contextMenuEvent` builds the card's right-click menu as a **stack** object parented to
the card: `QMenu menu(this);`. One of that menu's own entries (Remove) retires the card, so the card
can be destroyed while the menu's event loop is still running - and a card that dies takes its
children with it, so the stack menu would be destroyed once by the card and once by its own scope.

### Evidence in the tree (before the fix)

- `src/gui/shell/DeviceCard.cpp:362` - `QMenu menu(this);` (a stack QMenu whose QObject parent is
  the card), `menu.exec(event->globalPos())` at `:365` runs the menu's own nested event loop.
- `src/gui/shell/DeviceActionMenu.cpp:114` - the menu's "Remove" entry dispatches `plugin.unload`,
  the command that takes the device out of its chain; the panel then retires the card
  (`src/gui/shell/DevicePanel.cpp:206-214`, `retire()`).
- The card's menu entries capture the widget they belong to (`menu->parentWidget()`, i.e. the card)
  as a raw pointer: `src/gui/shell/DeviceActionMenu.cpp:69,85,100,126` and
  `src/gui/EffectDeviceMenu.cpp:212,218,246,272` - so a callback that ran after the card was gone
  would hand a dead widget to `runCommand`/`DevicePicker::pick`.
- The sibling entry in the same feature already guards the card it uses
  (`src/gui/shell/DeviceActionMenu.cpp:62`, `[card = QPointer<DeviceCard>(card)]`).

**Reachability, measured:** the panel retires a card with `deleteLater()`
(`src/gui/shell/DevicePanel.cpp:212`), and Qt processes a deferred delete only after the event-loop
level that posted it has been left. The everyday "Remove" path therefore destroys the card *after*
the stack menu's scope ended - the test below measured exactly that (the card was still alive when
`menu.exec()` returned, and gone only when the deferred delete was processed afterwards). The
double destruction is therefore latent rather than everyday, but the ownership is wrong: a stack
object is parented to a widget that its own entry retires, and the card's parent (the panel row, the
panel, the window) destroys the card **synchronously** when it goes - the one path where the stack
menu really is destroyed twice.

### The fix

- `src/gui/shell/DeviceCard.cpp:359-370` - the menu is still built as the card's child, so its
  entries keep naming the card as the widget their dialogs belong to, and it is let go of
  (`menu.setParent(nullptr)`) before it runs: the card can no longer take the stack menu with it.
- `src/gui/shell/DeviceActionMenu.cpp` and `src/gui/EffectDeviceMenu.cpp` - the widget a menu entry
  belongs to is captured as a `QPointer<QWidget>`, so an entry triggered after that widget is gone
  runs its command with no anchor instead of a dead widget. The commands themselves are unchanged.

Nothing else changed: the menu offers the same entries in the same order, the entries still
dispatch the same registered commands, and the Actions button's menu (a heap menu, a child of the
button) is untouched.

### The regression tests

`tests/src/gui/DevicePanelTest.cpp`:

- `theContextMenuIsNotTheCardsChild` - a right-click on a real card opens its action menu; the test
  finds the open menu (the visible `QMenu` among the application's widgets) and asserts that the
  menu is **not** the card's child.
- `removingADeviceFromItsOwnMenuDoesNotDestroyTheMenu` - the same menu, with "Remove" chosen: the
  menu is still not the card's child, and the card goes with its device.

Both are deterministic: no sleeps, no timing - the menu is closed by the test itself, and the
card's own `QPointer` says whether it is gone.

### Commands and exit codes

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED (fix reverted) | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-49.r1/logs/fix-p9-104.patch && cmake --build build --target DevicePanelTest zene -j 20` then `ctest -R DevicePanelTest --output-on-failure` (from `build/tests`) | 8 | `FAIL! theContextMenuIsNotTheCardsChild() 'host != card' returned FALSE` and `FAIL! removingADeviceFromItsOwnMenuDoesNotDestroyTheMenu() 'host != card' returned FALSE`; 0% tests passed. |
| GREEN (fix restored) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-49.r1/logs/fix-p9-104.patch && cmake --build build --target DevicePanelTest zene -j 20` then `ctest -R DevicePanelTest --output-on-failure` (from `build/tests`) | 0 | 100% tests passed (1 ctest target, 21 test functions). |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-49` | 0 | All eight gates PASS (`complexity` and `file-length`: "no new item in your files"). |

Logs: `build-4-red.log`, `test-4-red.log` (RED), `build-5-green.log`, `test-5-green.log` (GREEN),
`gate-1.log` in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-49.r1/logs/`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  RED: `ctest -R DevicePanelTest` exit 8 with the new case failing on the exact assertion quoted in
  each section; GREEN: the same command exit 0. The proof is one product patch per bug
  (`logs/fix-p17-49.patch`, `logs/fix-p15-48.patch`, `logs/fix-p9-104.patch`), each applied with
  `-R` and then re-applied.
- **The test is deterministic.** No wall-clock timing, no sleeps, no races: the menu is found and
  closed by the test itself, the card's lifetime is read from a `QPointer`, and the fold's
  retirement happens in a signal handler of the chain (a seam), not after a delay. The RED run of
  the fold test was repeated five times with the same result.
- **The fix is at the cause and complete.** Each fix is at the place the finding names: the
  discarded command result (`new-p17-49`), the unguarded post-dispatch access (`new-p15-48`), the
  stack menu's ownership (`new-p9-104`). The siblings were checked: the card's other header
  buttons, the card's other menu (the Actions button's), the shared `EffectDeviceMenu` entries the
  card's menu reaches, and the panel's own card retirement all keep working; the three tests in
  the family (`DevicePanelTest`, `InstrumentSetupUiTest`, `RackMenuTest`, the last two of which
  include the changed header) pass.
- **No collateral damage.** No project data, no save format, no command and no undo step changed;
  the menu offers the same entries, the editor opens the same way, and a card that lives folds
  exactly as before. The gates report "no new item in your files" for complexity and file length.
- **Realtime / undo / commands.** Nothing on the audio thread was touched (no `process`/`render`
  path). No new control command was added, so the control snapshot does not move. The fold command
  still makes one undo step - the test undoes the fold it made.
- **Old projects load as before.** No saved data, no schema and no file format was touched.
- **Portability.** Only `QPointer`, `QMenu::setParent` and `QWidget::setParent` (all Qt 5.15 and
  Qt 6) are used; no windows.h name, no `std::filesystem`.
- **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE"** - not applicable: all three findings are
  FIXED, and each section above cites the code the fix changes (`file:line`).

### The wider family

`ctest -R "DevicePanelTest|RackMenuTest|InstrumentSetupUiTest|EffectDeviceMenuTest|EffectRackAddTest|PluginControlDialogsTest" --output-on-failure`
(from `build/tests`) - exit 0, **6/6 ctest targets passed** (`test-14-wider.log`). These are the
tests that drive the Device panel, the device card's menus and the device's own dialogs.

### Files committed

- `src/gui/shell/DeviceCard.cpp` - the card's context menu is let go of before it runs
  (`new-p9-104`); the failed editor result is kept and reported (`new-p17-49`); the fold handler
  moved into `DeviceCard::foldCard` and guards the card (`new-p15-48`).
- `src/gui/shell/DeviceActionMenu.cpp`, `src/gui/EffectDeviceMenu.cpp` - the widget a menu entry
  belongs to is captured as a `QPointer` (`new-p9-104`).
- `src/gui/shell/DeviceCardState.cpp` - the card's state line carries the reason an editor did not
  open (`new-p17-49`).
- `include/DevicePanel.h` - `openEffectDialog()` says whether it opened something, `setEditorError()`
  keeps the reason, `foldCard()` is the fold handler (`new-p17-49`, `new-p15-48`).
- `tests/src/gui/DevicePanelTest.cpp` - the three regression tests and the test device they use.
- `docs/qwen-fixes-2026-10/cluster-new-p17-49.md` - this report.

### Commits (one per bug)

- `4f8cc7635` - `fix(new-p9-104): the card's context menu is not the card's child`
- `b339c4e45` - `fix(new-p17-49): an "Open Editor" that opens nothing says why`
- `a99bb4df6` - `fix(new-p15-48): the fold handler no longer touches a card the dispatch retired`
