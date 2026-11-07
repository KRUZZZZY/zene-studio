FALSE-POSITIVE

# gui-1105-main-pop-out-the-arranger-panel: FALSE-POSITIVE

Finding: "Arranger pop-out does not bring its window forward". The sweep's judge saw "The Arranger remains in the main
window, with no separate window visible" after clicking the Arranger header's Pop out button. Luna's root cause was that
the fresh-detach path in `Shell::detachPanel` calls `show()` but not `raise()`/`activateWindow()`, so the new window
could stay behind the main window.

## Why it is not a defect

The sweep's own screenshots show that the Arranger *did* pop out and that its new window is **in front**. It looks
unchanged only because the pop-out deliberately opens exactly where the panel was, at the same size.

1. **The pop-out opens over the panel's old place, by design.**
   `src/gui/shell/ShellFloating.cpp:246-248`: "It opens where it was ... a pop-out never sends the panel far from the
   eye". The origin is `frame->mapToGlobal(QPoint(0, 0))`. The size is `floatSize(frame)`, which is the frame's own size
   clamped to 480x320..1200x800 (`ShellFloating.cpp:193-196`). Geometry is set at `ShellFloating.cpp:260`. The Arranger
   at 1600x1000 is about 1076x572, inside that clamp, so the floating window covers exactly the rectangle the panel
   left.
2. **The sweep runs with no window manager.** `~/.local/share/zene-bunny-fix/terra-pilot/gui.py:4` runs Zene on xcb
   inside Xvnc. `gui.py:89` says "no window manager". `gui.py:10` says `shot` captures "the whole screen". With no
   window manager, the new window gets no title bar or border. A newly mapped top-level X window also goes to the top of
   the stacking order.
3. **The "after" screenshot can only be the floating window drawn on top.** In
   `terra-pilot/buttons/runs/m0-train-25/shots/0039-after.png`, the header button reads **"Dock"**. That text is set by
   `Shell::syncFloatButton` only while the panel is detached (`ShellFloating.cpp:429-438`). The button belongs to the
   frame, and the frame was reparented into the floating window (`ShellFloating.cpp:255`). If that window were *behind*
   the main window, the screen would show the main window's centre placeholder: "Arranger is in its own window. Drag its
   header back here, or dock it." That placeholder becomes visible whenever the centre panel is detached
   (`ShellFloating.cpp:352-362`; covered by `ShellTest::theCentreSaysWhereItsPanelWentAndTheButtonSaysDock`,
   `tests/src/gui/ShellTest.cpp:288-300`). It does not appear in the screenshot.
4. **A pixel diff confirms it.** I compared `0039-before.png` and `0039-after.png`, both 1600x1000. The only differing
   pixels are in the bounding box (1226,86)-(1314,122): the pop-out button changing from an icon to "Dock". Everything
   else is pixel-identical. In other words, a borderless window holding the same Arranger content sits exactly over its
   old place and is frontmost.

The judge read "nothing moved" as "no separate window". What actually happened is the intended behaviour: the panel
opens in its own window where the eye already is. There is no window behind the main window to bring forward.

## Regression-test attempt (why no test is committed)

I wrote a candidate test in `tests/src/gui/ShellHomesTest.cpp`. It activated the shell, called
`detachPanel("arranger")`, then asserted `QApplication::activeWindow() == floatingWindow("arranger")`. I ran it against
the **unchanged** product code and it **passed**. So it cannot show any defect: the offscreen platform activates a window
when it is shown, and the code shows the new window (`ShellFloating.cpp:261`). The test was not a regression proof, so I
reverted it (`git checkout -- tests/src/gui/ShellHomesTest.cpp`). No product file was changed.

## Evidence table

| Step | Command | Result |
| --- | --- | --- |
| Build the candidate test (attempt 1) | `cmake --build build --target ShellHomesTest -j4 > logs/01-build-red.log 2>&1` | EXIT=2: ccache could not write its temp dir outside this lane's write-jail (`01-build-red.log:18`), not a code error |
| Build the candidate test | `CCACHE_DISABLE=1 cmake --build build --target ShellHomesTest -j4 > logs/03-build-red.log 2>&1` | EXIT=0 |
| Run the candidate test on unchanged product code (from `build/tests`) | `ctest -R '^ShellHomesTest$' --output-on-failure > logs/04-test-red.log 2>&1` | EXIT=0, 1/1 tests run and passed. No RED, so no defect is reproducible |
| Screenshot diff | PIL `ImageChops.difference(after, before).getbbox()` on `shots/0039-{before,after}.png` | `(1226, 86, 1314, 122)`: only the button changed |
| Gates (attempt 1) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/05-feature-check.log 2>&1` | EXIT=1: `PermissionError` creating `<worktree>-feature-check` beside the worktree, which is outside the write-jail |
| Gates | `python3 logs/fc_redirect.py <worktree> logs/feature-check > logs/06-feature-check.log 2>&1` | EXIT=0, all 8 rows PASS, RESULT: PASS. This is the same script; only its hard-coded log folder is redirected |

`logs/` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-pop-out-the-arranger-panel.r1/logs`.

Files committed: this report only. No product or test file was changed.

## Self-check

- **Regression test fails without the fix and passes with it:** not applicable. There is no fix. The candidate test
  passed on unchanged code (`04-test-red.log`, EXIT=0), which shows no reproducible defect, so it was reverted rather
  than committed as a fake regression.
- **Determinism:** no test was committed. The candidate used no timers or sleeps, only `QCoreApplication::processEvents()`.
- **Fix at the cause and complete:** no product change. I checked the callers of `detachPanel` for the record:
  - `dispatchFloat` (`ShellFloating.cpp:225`)
  - `floatOne`, used when a layout is restored (`ShellFloating.cpp:414`)
  - `detachTo`, used by `window.detach` (`src/gui/ControlCommandsLayouts.cpp:222`)

  Its sibling `raiseFloating` (`ShellFloating.cpp:291-300`) handles a window that already exists and may be hidden or
  minimized. A freshly created window has neither problem.
- **No collateral damage:** nothing changed, so saved projects and layouts load exactly as before.
- **Realtime / undo / commands:** untouched. No command was added.
- **Gates:** `06-feature-check.log` EXIT=0, all PASS. `submit_check.py` runs `feature_check.py` with its default log
  folder beside the worktree. This lane cannot write there, so its gate row may show FAIL from `PermissionError`, not
  from a gate. The redirected run above is the same check.
