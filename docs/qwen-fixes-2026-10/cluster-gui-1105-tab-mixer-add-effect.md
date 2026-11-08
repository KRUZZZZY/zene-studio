FALSE-POSITIVE

# cluster gui-1105-tab-mixer-add-effect — "Add effect click does not open the picker"

Verdict: FALSE-POSITIVE. The sweep's click never landed on the button: it was sent to a point below the bottom
edge of the 1600×1000 screen. When the button really is clicked, the picker opens with the expected kind and title.
No product code changed. A deterministic regression test was added; it fails when the button is disconnected.

## Evidence

### 1. The sweep clicked off-screen (the cause of the finding)
Run `~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-25`:
- `results.jsonl`, row `tab-mixer|button|Add effect|…Mixer > Effects enabled`: click at `"x": 1257, "y": 1159`.
- `inventory.py:58` records the **centre** of the AT-SPI box (`y = box[1] + box[3] // 2`); `sweep.py:216-217`
  clicks exactly that point. With `h: 30`, the button spans y ≈ 1144–1174.
- `shots/0116-before.png` is `1600 x 1000` (`file` output). So the button lay 144+ px **below the screen**. In
  the screenshot the mixer dock is cut off at the bottom, under the "EFFECTS CHAIN" header. The click hit nothing,
  so "the mixer remains visible without an Add effect picker" is what an off-screen click looks like.
- AT-SPI still reported `"showing": true`, because the widget is not hidden. It is only clipped by its parent
  dock: the channel strip area has a minimum height (`src/gui/MixerView.cpp:207`) taller than the default bottom
  panel, and the strips' faders are clipped the same way. That is general bottom-panel sizing that affects the
  whole mixer, not this button. It is outside this finding's scope and needs a design decision (dock default
  height or scrolling), so it is not changed here.

### 2. The dispatch path is intact
- `src/gui/EffectRackView.cpp:76` connects `addButton` `clicked()` → `addEffect()`.
- `src/gui/EffectRackView.cpp:252-257`: `addEffect()` calls
  `DevicePicker::pick(this, DevicePicker::Kind::Effect, tr("Add effect"))` with no guard before it.
- `src/gui/shell/DevicePicker.cpp:277-282`: `pick()` opens the modal dialog (or the test seam `chooser()`).
- Live proof from this lane: the main-thread stack of the hung run (log 04, below) shows
  `QTest::mouseEvent → QAbstractButton::mouseReleaseEvent → EffectRackView::addEffect() → QDialog::exec()`.
  A real mouse click reaches the slot and opens the picker. Instrumented run (log 09): the active modal is
  `lmms::gui::DevicePicker` on the first poll.

### 3. Regression test (new slot, existing file)
`tests/src/gui/EffectRackAddTest.cpp` → `addEffectClickReachesThePicker()`: builds a mixer channel's
`EffectRackView`, sends a `QTest::mouseClick` to its "Add effect" button, and answers the picker through its test
seam `DevicePicker::setChooser` (`include/DevicePicker.h:88-89`). It asserts: exactly one call, `Kind::Effect`,
title `tr("Add effect")`, and that the cancel answer adds nothing. It uses no modal dialog, timer, sleep or wait.

## Side finding: the existing slot hung in this lane (a build-tree issue, not a product bug)
`addEffectPicksFromTheDevicePicker` hung for 300 s (QtTest watchdog) twice (logs 04, 05). Cause, from the
instrumented log 11: the picker's "Amplifier" query selected `dev-0`, a LADSPA effect. This lane's targeted build
had **no plug-in modules built** (`build/plugins/*.so`: 0 files), so `addEffect()` showed
`QMessageBox::warning` "the LADSPA host plugin 'ladspaeffect' is not in this build", and the test never closes that
box. After building `amplifier` and `ladspaeffect` (log 12), the whole family passes (log 14). Note for the loop:
`EffectRackAddTest` needs plug-in modules built in the lane tree. A lane that builds only `zene` + the test will
see this hang.

## Commands and exit codes
LOGS = `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-tab-mixer-add-effect.r1/logs`

| step | command | exit |
|---|---|---|
| build (ccache temp dir outside the jail) | `cmake --build build --target zene EffectRackAddTest -j4 > LOGS/01-build.log` | 2 (ccache `Permission denied`) |
| build | `CCACHE_TEMPDIR=LOGS/ccache-tmp cmake --build build --target zene EffectRackAddTest -j4 > LOGS/03-build.log` | 0 |
| existing test, original tree | `ctest -R '^EffectRackAddTest$' --output-on-failure > LOGS/04-test.log` | 8 (hang, see side finding) |
| rerun | `… > LOGS/05-test-rerun.log` | 8 (same) |
| new slot alone | `./EffectRackAddTest addEffectClickReachesThePicker > LOGS/07-newslot.log` | 0 (3 passed) |
| plug-in modules | `cmake --build build --target amplifier ladspaeffect -j4 > LOGS/12-build-plugins.log` | 0 |
| family GREEN | `ctest -R '^(EffectRackAddTest\|RackDeferredLifetimeTest)$' --output-on-failure > LOGS/14-test-family.log` | 0 (2/2 tests) |
| RED proof (mutation: comment out `EffectRackView.cpp:76` connect, `LOGS/mutation.patch`) | rebuild `zene EffectRackAddTest` (LOGS/15) then `ctest -R '^EffectRackAddTest$' > LOGS/16-test-mutant.log` | build 0, test **8**: `addEffectClickReachesThePicker` `Actual (calls): 0, Expected (1): 1`, EffectRackAddTest.cpp:128 |
| restore | `git apply -R LOGS/mutation.patch`, rebuild (LOGS/17), `ctest -R '^(EffectRackAddTest\|RackDeferredLifetimeTest)$' > LOGS/18-test-restored.log` | build 0, test **0** (2/2) |
| gates | `python3 LOGS/run_feature_check.py <worktree> LOGS/feature-check > LOGS/20-gates.log` | **0**, RESULT: PASS (all 8 rows) |

The gates were run through `LOGS/run_feature_check.py`. It executes `qwen/feature_check.py` unchanged except for
its log directory: `feature_check.py:70` writes to `<worktree>/../<name>-feature-check`, which is outside this
lane's write-jail (the direct run, `LOGS/19-gates.log`, failed with `PermissionError`). Limits and baselines are
untouched; the base listing `gate-baselines/af010bc3c….json` was already cached.

Files committed: `tests/src/gui/EffectRackAddTest.cpp` (new slot), this report. No product file changed, so no
`fork-sources`/`upstream-modifications` entry is needed (the test file is already registered:
`tests/CMakeLists.txt:1274`).

## Self-check
- **Regression test fails without the fix, passes with it:** there is no product fix (FALSE-POSITIVE). In its
  place, a mutation that reproduces the reported symptom (the click not reaching `addEffect`; the `connect` at
  `EffectRackView.cpp:76` commented out) gives test exit 8 (`calls: 0`). Restored, exit 0. See the table above.
- **Deterministic:** yes. It uses the `DevicePicker::setChooser` seam (`include/DevicePicker.h:88-89`, honoured at
  `DevicePicker.cpp:279`) and a synchronous `QTest::mouseClick`, with no timers, sleeps or modal polling.
- **Fix at the cause / callers and siblings:** no product change. The other route into the same slot (an
  instrument track's rack, the same `EffectRackView` class) shares the line-76 connection, and the existing
  `addEffectPicksFromTheDevicePicker` covers the accept path.
- **No collateral damage:** test-only change. No project data, save format, command or audio path touched.
- **FALSE-POSITIVE evidence:** the click coordinate (y=1159) is outside the 1000 px screen (`results.jsonl`,
  `inventory.py:58`, `sweep.py:216-217`, `shots/0116-before.png`). The code path is
  `EffectRackView.cpp:76` → `:252-257` → `DevicePicker.cpp:277-282`. Live stack proof is in LOGS/04-test.log.
- **Realtime / undo / commands:** nothing changed in product code; no command added.
- **submit_check.py** (LOGS/21-submit.log, exit 1): every row PASSes except "gates (feature_check.py)". That row
  fails only because `submit_check.py` calls `feature_check.py` directly, and `feature_check.py:70-71` does `mkdir`
  outside this lane's write-jail (`LOGS/22-gates-direct.log`: `PermissionError`, exit 1). The same gates on the
  committed tree with the log directory redirected: `LOGS/23-gates.log`, exit 0, all 8 rows PASS. A run from
  outside the jail should show the gate row green.
