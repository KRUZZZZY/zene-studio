# Cluster new-p17-49

**Verdict: FIXED**

Three findings in one file, `src/gui/shell/DeviceCard.cpp` (the shell's device card). One verdict
each, with its own fix, its own regression test and its own RED/GREEN proof.

new-p9-104: FIXED

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
  RED: `ctest -R DevicePanelTest` exit 8 with both new cases failing on `'host != card' returned
  FALSE`; GREEN: the same command exit 0. The proof is `logs/fix-p9-104.patch` (the product diff
  only) applied with `-R` and then re-applied.
- **The test is deterministic.** No wall-clock timing, no sleeps, no races: the test finds the menu
  the right-click opened by walking the application's widgets, closes it itself, and reads the
  card's own `QPointer` for its lifetime.
- **The fix is at the cause and complete.** The cause is the stack menu's ownership: it is no
  longer a child of the widget its own entry can retire. The sibling path - the Actions button's
  menu and the shared `EffectDeviceMenu` entries the card's menu reaches - was checked and keeps
  working; the entries' host is now guarded in both menu builders.
- **No collateral damage.** The menu offers the same entries in the same order, the same
  registered commands are dispatched, the dialogs still belong to the card, and no project data,
  save format or command changed.
- **Realtime / undo / commands.** Nothing on the audio thread changed; no new control command was
  added (the commands the menu dispatches are unchanged, so the control snapshot does not move).
- **Old projects load as before.** No saved data, no schema and no file format was touched.
- **Portability.** Only `QPointer` and `QMenu::setParent` (both Qt 5.15 and Qt 6) are used.
