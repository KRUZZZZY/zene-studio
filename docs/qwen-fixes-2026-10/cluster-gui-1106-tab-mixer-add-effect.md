FALSE-POSITIVE

# cluster gui-1106-tab-mixer-add-effect - "Add effect click does not open the device picker"

## Verdict
FALSE-POSITIVE. The button is wired to the picker and the picker opens on a real click. The sweep's click never
reached the button: its coordinates are outside the sweep's screen.

## Evidence
- Wiring: `src/gui/EffectRackView.cpp:76` connects the "Add effect" button's `clicked()` to `addEffect()`;
  `EffectRackView.cpp:252-257` calls `DevicePicker::pick(this, DevicePicker::Kind::Effect, tr("Add effect"))`
  first, with no fresh-project or other guard. A fresh mixer channel's chain takes the same path.
- Behaviour: `tests/src/gui/EffectRackAddTest.cpp:102-150` (`addEffectPicksFromTheDevicePicker`) builds a rack on a fresh
  mixer channel, clicks the real "Add effect" button with `QTest::mouseClick`, and asserts the modal is a
  `lmms::gui::DevicePicker` (line 127), that the pick lands in the chain, and that a cancelled picker adds nothing.
  It passes (below).
- Why the sweep saw nothing: the sweep starts Zene with `--size 1600x1000` (`terra-pilot/buttons/sweep.py:59,163`) and
  the screenshots are 1600x1000 (`shots/0112-before.png`). The recorded click point for this control is x=1655,
  y=1133 (`runs/m0-train-26/results.jsonl` line 113), which is off that screen (19 of 270 inventoried controls are).
  A click there cannot hit the button, so the "after" shot shows the unchanged mixer. The cursor in the pair image
  (`shots/0112-pair.png`) is still at the previous control (EFFECTS CHAIN, 1549,788). The same run reports
  "Routing Matrix..." at x=1714, also off-screen. Zene's log was empty (`zene log: []`), consistent with no click
  arriving.
- Not verified: why AT-SPI reports controls beyond the 1600x1000 window size (the main window may be larger than
  its requested size, or the mixer dock overflows). That would be a separate layout finding, not this
  cluster; I did not reproduce it in a live GUI.

## Commands and results
| Step | Command | EXIT |
| --- | --- | --- |
| build (first try) | `cmake --build build --target zene EffectRackAddTest -j4 > logs/build-1.log` | 2 (ccache temp dir outside the write jail: `ccache: error ... Permission denied`) |
| build | `CCACHE_DISABLE=1 cmake --build build --target zene EffectRackAddTest -j4 > logs/build-2.log` | 0 |
| test, plugins not built | `ctest -R '^EffectRackAddTest$' --output-on-failure > logs/test-1.log` (from build/tests) | 8; aborted after 300 s |
| build plugin | `CCACHE_DISABLE=1 cmake --build build --target amplifier -j4 > logs/build-3.log` | 0 |
| test | `ctest -R '^EffectRackAddTest$' --output-on-failure > logs/test-2.log` (from build/tests) | 0 (1/1 passed) |
| test, detail | `./EffectRackAddTest > logs/test-3.log` (QT_QPA_PLATFORM=offscreen) | 0; 4 passed, 0 failed, 0 skipped |

Note on test-1: with no plugin modules built in this tree (`build/plugins` had no `.so`), the test picks "Amplifier",
the instantiate fails, and `addEffect()` shows its `QMessageBox::warning` (`EffectRackView.cpp:268`); the test only
dismisses the first modal, so it hangs until the 300 s QtTest timeout. That is a missing build target (`amplifier`),
not a product defect, and is unrelated to the finding. test-1's stack shows `addEffect` -> `QDialog::exec` from the
button's `mouseReleaseEvent`, i.e. the click does reach the slot.

## Gates
`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` EXIT=1: it did not run. It does
`mkdir <worktree>-feature-check` next to the worktree (`feature_check.py:70-71`), which the write jail refuses
(`PermissionError: [Errno 13]`, `logs/gates-1.log`). No gate table exists for this lane. The commit adds only this
markdown file, so no complexity, file-length or registration gate has anything new to check.

## No code change
No product or test file was changed, so there is no RED/GREEN pair and no fix patch (steps 3, 4 and 4b do not apply to
a false positive). The existing test is the regression proof for the behaviour the finding says is missing.

## Files committed
- `docs/qwen-fixes-2026-10/cluster-gui-1106-tab-mixer-add-effect.md` (this report) only.
