FIXED

# gui-1106-tab-mixer-effects-chain: Effects Chain LED does not visibly toggle on click

## Verdict: REAL (fixed)

The model and click path are correct: `EffectRackView::modelChanged` binds the group box to the chain's
`m_enabledModel` (`src/gui/EffectRackView.cpp:292`), `GroupBox::modelChanged` hands the model to the LED
(`src/gui/widgets/GroupBox.cpp:63-66`), and `AutomatableButton::mousePressEvent` toggles it
(`src/gui/widgets/AutomatableButton.cpp:114-129`). A click delivered straight to the LED widget toggles the chain.
I confirmed this with a first version of the test that sent the click directly to the button, and it passed on unfixed code.

The fault is the LED's **geometry**:

- The LED is a `PixmapButton` (a `QPushButton`) placed by hand at (3,0) and sized to its pixmap
  (`GroupBox.cpp:45-46`, `PixmapButton.cpp` `setActiveGraphic` → `resize(logicalSize)`).
- The Zene theme gives every `QPushButton` `padding: 5px 14px; min-height: 18px` plus a 1px border
  (`data/themes/zene/zene.qss:122-130`). Qt's stylesheet polish raises the LED's minimum height to 30px.
  The test measured `QRect(3,0 16x30)` under an 11px title bar, which matches the sweep's accessible rect (w 16, h 30).
- The effects area starts at `titleBarHeight() + 2` = 13px (`EffectRackView.cpp:58`) and is created after the
  LED, so it is stacked above the LED's lower half. A click at the LED's accessible centre (y≈15) goes to the
  effects area's `QWidget`, so the chain does not toggle and the LED does not change. This matches the sweep screenshot
  `shots/0111-pair.png`: the cursor sits ~6px below the drawn LED.

## Fix

`data/themes/zene/zene.qss:159-166` adds a rule `lmms--gui--GroupBox > lmms--gui--PixmapButton { padding: 0px;
border: none; min-height: 0px; }`. With this rule a group box's pixmap buttons keep their pixmap size (the LED is now
`16x14`), so the LED's whole rect, centre included, sits in the title band and takes the click.
`PixmapButton::paintEvent` draws only the pixmap, so the removed padding and border were never painted. Only the geometry changes.

Sibling check: there are 12 `new GroupBox` sites. The only other `PixmapButton` that ends up as a direct GroupBox
child is the microtuner's "Edit scales and keymaps" button. It is reparented into the MICROTUNER box by its layout
(`src/gui/instrument/InstrumentTuningView.cpp:88-100`). Its code asks for `setFixedSize(16, 16)`, and the same QSS
rule had overridden that. With the fix it gets its coded 16×16 size. This is the same defect and the code states the
intended size. No other PixmapButtons are touched. Mixer strips, transport and other buttons outside a GroupBox
keep the theme's sizing. The classic and default themes have no push-button min-height and are unchanged.

There are no data, save, undo, realtime or command changes: the fix is a stylesheet-only geometry rule.

## Regression test

`tests/src/gui/EffectRackAddTest.cpp:156` `effectsChainLedTogglesOnClick` builds an `EffectRackView` over a fresh mixer
channel's chain and shows it. It then checks that the widget at the LED's centre is the LED, clicks there **through the
window** (`QTest::mouseClick(rack.windowHandle(), ...)`), so whatever is stacked on top gets the event as it would for a
user, and checks that the chain becomes enabled and the LED's rendered image changes. A second click must turn it back
off with the original image. Nothing in it depends on timing.

## Evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-tab-mixer-effects-chain.r1/logs/`)

All builds use `CCACHE_DISABLE=1`. The configured ccache temp dir
(`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not writable from this lane: log 01 failed with
`ccache: error: ... Permission denied`.

| Step | Command | EXIT |
|---|---|---|
| RED (unfixed product) | `CCACHE_DISABLE=1 cmake --build build --target EffectRackAddTest -j4` (08) then from build/tests `./EffectRackAddTest effectsChainLedTogglesOnClick` (09) | build 0, test **1**: `'rack.childAt(centre) == led' returned FALSE. (the LED's centre is covered by QWidget)`, LED geometry `QRect(3,0 16x30)` |
| GREEN | `CCACHE_DISABLE=1 cmake --build build --target zene EffectRackAddTest -j4` (10) then `./EffectRackAddTest effectsChainLedTogglesOnClick` (11) | build 0, test **0**: 3 passed, LED geometry `QRect(3,0 16x14)` |
| 4b revert | `git diff -- data/themes/zene/zene.qss > logs/fix.patch && git apply -R logs/fix.patch` → rebuild zene+test (12) → run (13) | revert 0, build 0, test **1** (same childAt failure) |
| 4b reapply | `git apply logs/fix.patch` → rebuild (14) → run (15) | apply 0, build 0, test **0** (3 passed) |
| Family | `cmake --build build --target amplifier RackDeferredLifetimeTest -j4` (16); from build/tests `ctest -R '^(EffectRackAddTest\|RackDeferredLifetimeTest)$' --output-on-failure --timeout 600` (17) | build 0, ctest **0**: 2/2 tests passed |
| Related | `ctest -R '^(DesignTokensTest\|TakeLanesPanelTest)$'` (20); `python3 tests/scripted/check-strings` (19) | ctest **0**: 2/2; check-strings **0**: 0 errors |
| Gates | `feature_check.py` with the log dir redirected (22) | **0**: all 8 rows PASS, `RESULT: PASS` |

Notes:
- The first family run (log 06, before the fix) timed out in `addEffectPicksFromTheDevicePicker`
  because the `amplifier` plug-in module was not built in this build tree (`plugin-scan: ... 0 plugin(s)`). After building
  the `amplifier` target (log 16), the whole `EffectRackAddTest` passes (log 17). This was an environment issue and is unrelated to the fix.
- Gates: `feature_check.py` creates its log directory as a sibling of the worktree
  (`<worktree>-feature-check`), outside this lane's write-jail, and failed with `PermissionError` (log 21, EXIT=1).
  I ran a copy, `logs/feature_check_logdir.py`. It differs from the original only in `ROOT` (pinned to the original
  script's directory, so the same FIX-BASE and baselines are used) and the log directory (moved into the LOG FOLDER).
  The checks, limits and baselines are unchanged.

## Files committed

- `data/themes/zene/zene.qss`: the fix. It is already registered in `tests/upstream-modifications.txt:1289`.
- `tests/src/gui/EffectRackAddTest.cpp`: the regression slot. It is already a registered fork test.
- `docs/qwen-fixes-2026-10/cluster-gui-1106-tab-mixer-effects-chain.md`: this report.

## Self-check

- **Fails reverted / passes restored:** yes. `git apply -R logs/fix.patch` (EXIT 0), then a rebuild of
  `zene EffectRackAddTest` (EXIT 0) and `./EffectRackAddTest effectsChainLedTogglesOnClick`: **EXIT 1**
  (`the LED's centre is covered by QWidget`, log 13). After `git apply logs/fix.patch` (EXIT 0), a rebuild (EXIT 0) and
  the same run: **EXIT 0** (3 passed, log 15).
- **Deterministic:** yes. There are no sleeps, timers or wall-clock waits. The test reads widget geometry with
  `QWidget::childAt`, sends synthetic `QTest::mouseClick` events on the window and compares two `QWidget::grab()` images.
  The only wait is `QTest::qWaitForWindowExposed` on the offscreen platform, which is a synchronisation hook and not a timeout race.
- **At the cause and complete:** the cause is the theme rule `QPushButton { padding; min-height }`
  (`zene.qss:122-130`) applied to a hand-placed pixmap LED. The model/click path was proven correct by a direct click on
  unfixed code. The new rule covers every GroupBox LED: all 12 `new GroupBox` sites share `GroupBox.cpp:43-46`.
  It also covers the one other PixmapButton that is a direct GroupBox child, the microtuner edit button
  (`InstrumentTuningView.cpp:88-100`), which goes back to its coded `setFixedSize(16, 16)`.
  PixmapButtons outside group boxes are not touched by the selector.
- **No collateral damage:** the change is stylesheet-only. No model, serialisation, command, undo or audio code changed,
  so old projects load byte-for-byte as before. No limit or check was added. The tests in the family and the related
  tests pass: `EffectRackAddTest` and `RackDeferredLifetimeTest` 2/2 (log 17), `DesignTokensTest` and
  `TakeLanesPanelTest` 2/2 (log 20), and `tests/scripted/check-strings` 0 errors (log 19).
- **Not-real verdict:** n/a. The verdict is FIXED (REAL).
- **Gates row in submit_check:** submit_check runs the original `feature_check.py`. That script exits 1 with
  `PermissionError: ... zene-qwen-gui-1106-tab-mixer-effects-chain-feature-check` (log 21) because it creates its log
  directory beside the worktree, outside this lane's write-jail. The same checks, run with only the log directory
  redirected, all PASS with EXIT 0 (log 22, table above). This is an environment limitation and not a gate failure in the change.
