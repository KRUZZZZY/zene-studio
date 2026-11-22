Verdict: FIXED

new-p14-108: FIXED

Finding: a `Fader` whose model was destroyed (project replacement) still takes mouse, wheel, keyboard-adjust, double-click and context-menu input, and each of those reads the freed model. Paint was already guarded (BUGS_FOUND 11.12).

## Evidence

Real on the base (`639e1d23e`). Model-dependent input handlers had no null check. The reads, by path:

- Left/middle/right press: `Fader::mousePressEvent` calls `calculateKnobPosYFromModel()` (reads `m->minValue()`, `m->maxValue()`, `m->value()`) and, for non-left buttons, `AutomatableModelView::mousePressEvent`, which reads `modelUntyped()` (`src/gui/AutomatableModelView.cpp:170-177`, middle-click `reset()`).
- Drag: `Fader::mouseMoveEvent` calls `setVolumeByLocalPixelValue()` (reads `model()->maxValue()`/`minValue()`).
- Wheel: `Fader::wheelEvent` → `adjustByDecibelDelta()` → `adjustModelByDBDelta()` (reads `model()->value()`).
- Keyboard from the mixer: `MixerView::keyPressEvent` calls `fader->adjust()` (`src/gui/MixerView.cpp:732`) → the same wheel path.
- Double-click: `mouseDoubleClickEvent` → `adjustByDialog()` (reads `model()->maxValue()` and friends before the dialog).
- Right-click: `Fader::contextMenuEvent` → `AutomatableModelView::addDefaultActions()` reads `model->initValue<float>()` for "Reset" (`src/gui/AutomatableModelView.cpp:59-67`).

Already guarded on the base and unchanged: `paintEvent` returns early when `model() == nullptr` (`src/gui/widgets/Fader.cpp:616`, comment cites BUGS_FOUND 11.12); `keyPressEvent` maps a null model to `KeyStep::None` and `a11y::applyKeyStep` returns false for a null model (`src/gui/AccessibleWidgets.cpp:336`); `mouseReleaseEvent` checks `thisModel`.

## Fix

Five early returns, all `model() == nullptr` checks; no other line changes:

| Location (fixed file) | Guard |
| --- | --- |
| `src/gui/widgets/Fader.cpp:128` `adjustByDecibelDelta` | returns; covers `wheelEvent` and `adjust()` |
| `src/gui/widgets/Fader.cpp:139` `adjustByDialog` | returns before the dialog; covers double-click |
| `src/gui/widgets/Fader.cpp:177` `contextMenuEvent` | accepts and returns before `addDefaultActions` |
| `src/gui/widgets/Fader.cpp:196` `mouseMoveEvent` | accepts after the existing touch-start line |
| `src/gui/widgets/Fader.cpp:217` `mousePressEvent` | accepts before any branch (the base class reads the model for middle-click and copy-drag) |

Live-model behaviour is unchanged: each guard only fires on a null model; the journalling, touch and knob code below the guards is untouched.

Realtime / undo: all five handlers run on the UI thread; no audio path, no allocation, no lock, no new undo step. The guards return before `addJournalCheckPoint()`, which only runs with a live model.

## Regression test

`tests/src/gui/VcaStripLifetimeTest.cpp` (already registered at `tests/CMakeLists.txt:1168` and `tests/fork-sources.txt`/`tests/all-sources.txt:3155`). The test matrix:

| Function | Path | Expected |
| --- | --- | --- |
| `aLiveFaderStillTakesWheelInput` | live model, wheel up | model value > 1.0 (control, passes before and after) |
| `aFaderWhoseModelDiesIgnoresMousePressAndDrag` | dead model, press, move, release | no read, no crash |
| `aFaderWhoseModelDiesIgnoresWheel` | dead model, wheel | no read, no crash |
| `aFaderWhoseModelDiesIgnoresKeyboardAdjust` | dead model, `adjust(Up)` and `adjust(Down)` | no read, no crash |
| `aFaderWhoseModelDiesOpensNoContextMenu` | dead model, context-menu event | no read, no `exec()` (which would block) |
| `aFaderWhoseModelDiesIgnoresDoubleClick` | dead model, double-click | no read, no dialog |
| `aFaderWhoseModelDiesPaintsNothing` (existing) | dead model, paint | unchanged |

Deterministic: events are built with `QMouseEvent`/`QWheelEvent`/`QContextMenuEvent` and delivered synchronously with `QCoreApplication::sendEvent`. No timers, sleeps or threads. The dead-model paths return before any modal dialog or menu can open.

## Commands and exit codes

`$L` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-108.r1/logs`. Test binary runs are from `build/tests`. Builds need `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp` (see Deviations).

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try (ccache temp dir denied) | `cmake --build build --target VcaStripLifetimeTest -j2 > $L/build-1-red.log 2>&1` | 2 | build-1-red.log |
| RED build (test added, product unchanged) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp cmake --build build --target VcaStripLifetimeTest -j2 > $L/build-2-red.log 2>&1` | 0 | build-2-red.log |
| RED run, family | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure > $L/test-1-red.log 2>&1` | 8 (SIGSEGV in `aFaderWhoseModelDiesIgnoresMousePressAndDrag`, `Fader::mousePressEvent` → `calculateKnobPosYFromModel`) | test-1-red.log |
| RED run, each new function alone (press/drag, wheel, keyboard adjust, double-click) | `./VcaStripLifetimeTest <fn> > $L/test-1-red-<fn>.log 2>&1` | 139 each (SIGSEGV) | test-1-red-*.log |
| RED build, context menu test | `cmake --build ... VcaStripLifetimeTest` | 0 | build-8-menu-red.log |
| RED run, context menu test | `./VcaStripLifetimeTest aFaderWhoseModelDiesOpensNoContextMenu` | 139 (SIGSEGV in `addDefaultActions` ← `Fader::contextMenuEvent`) | test-8-menu-red.log |
| GREEN build (first fix, 4 guards) | `CCACHE_TEMPDIR=... cmake --build build --target zene VcaStripLifetimeTest -j2 > $L/build-3-green.log 2>&1` | 0 | build-3-green.log |
| GREEN run, ctest | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure > $L/test-2-green.log 2>&1` | 0 | test-2-green.log |
| GREEN run, binary | `./VcaStripLifetimeTest > $L/test-3-green-binary.log 2>&1` | 0 (12 passed, 0 failed) | test-3-green-binary.log |
| Family build (2 targets; see below) | `cmake --build build --target MixerSendsOnFadersTest CrossoverEQControlDialog -j2 > $L/build-4-family.log 2>&1` | 2 (`No rule to make target 'CrossoverEQControlDialog'`) | build-4-family.log |
| Family build (registered targets) | `cmake --build build --target MixerSendsOnFadersTest MixerModesTest MixerMeteringTest MixerSelectionIdentityTest KnobKeyboardTest VcaStripViewTest XyPadPairTest -j2 > $L/build-5-family.log 2>&1` | 0 | build-5-family.log |
| Family run, ctest | `ctest -R '^(MixerSendsOnFadersTest\|MixerModesTest\|MixerMeteringTest\|MixerSelectionIdentityTest\|KnobKeyboardTest\|VcaStripViewTest\|XyPadPairTest\|VcaStripLifetimeTest)$' --output-on-failure > $L/test-5-family-green.log 2>&1` | 0 (8/8) | test-5-family-green.log |
| 4b: save final product diff | `git diff -- src/gui/widgets/Fader.cpp > $L/fix-final.patch` | 0 | fix-final.patch |
| 4b: revert | `git apply -R $L/fix-final.patch` | 0 | |
| 4b: RED build (reverted) | `cmake --build build --target zene VcaStripLifetimeTest -j2 > $L/build-10-reverted-final.log 2>&1` | 0 | build-10-reverted-final.log |
| 4b: RED runs (reverted), each new function | `./VcaStripLifetimeTest <fn> > $L/test-11-reverted-<fn>.log 2>&1` | 139 ×5 (press/drag, wheel, keyboard adjust, context menu, double-click); 0 for the live control | test-11-reverted-*.log |
| 4b: RED run, ctest (reverted) | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure > $L/test-11-reverted-ctest.log 2>&1` | 8 | test-11-reverted-ctest.log |
| 4b: reapply | `git apply $L/fix-final.patch` | 0 | |
| 4b: GREEN build (reapplied) | `cmake --build build --target zene VcaStripLifetimeTest -j2 > $L/build-11-reapplied-final.log 2>&1` | 0 | build-11-reapplied-final.log |
| 4b: GREEN run, binary | `./VcaStripLifetimeTest > $L/test-12-reapplied-final.log 2>&1` | 0 (13 passed, 0 failed) | test-12-reapplied-final.log |
| 4b: GREEN run, ctest | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure > $L/test-12-reapplied-ctest.log 2>&1` | 0 | test-12-reapplied-ctest.log |
| Final family build | `cmake --build build --target zene VcaStripLifetimeTest MixerSendsOnFadersTest MixerModesTest MixerMeteringTest MixerSelectionIdentityTest KnobKeyboardTest VcaStripViewTest XyPadPairTest -j2 > $L/build-9-green-full.log 2>&1` | 0 | build-9-green-full.log |
| Final family run, ctest | `ctest -R '^(…same eight names…)$' --output-on-failure > $L/test-10-family-green.log 2>&1` | 0 (8/8) | test-10-family-green.log |

Affected-family test count: 8 registered tests executed (`KnobKeyboardTest`, `VcaStripLifetimeTest`, `MixerSendsOnFadersTest`, `MixerModesTest`, `MixerMeteringTest`, `MixerSelectionIdentityTest`, `XyPadPairTest`, `VcaStripViewTest`), all passed. `VcaStripLifetimeTest` itself runs 13 QTest functions (init, cleanup, 11 tests).

Gates, run directly from the worktree after the final code change (see Deviations for why the wrapper could not run):

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh > $L/gate-final-fork-sources.log 2>&1` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > $L/gate-final-all-sources.log 2>&1` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > $L/gate-final-no-upstream.log 2>&1` | 0 |
| check-namespace | `tests/scripted/check-namespace > $L/gate-final-check-namespace.log 2>&1` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh > $L/gate-final-no-tautology.log 2>&1` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py > $L/gate-final-doc-figures.log 2>&1` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > $L/gate-2-complexity.log 2>&1` | 1 (7 train-wide `REGRESSION` lines, none naming `Fader.cpp` or `VcaStripLifetimeTest.cpp`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > $L/gate-2-file-length.log 2>&1` | 1 (22 train-wide `REGRESSION` lines, none naming the two changed files) |

## Files committed

- `src/gui/widgets/Fader.cpp`: five early returns.
- `tests/src/gui/VcaStripLifetimeTest.cpp`: six new test functions and one `#include <QWheelEvent>`.
- `tests/upstream-modifications.txt`: one reason line for the `Fader.cpp` change (`Fader.cpp` was already listed twice for earlier changes).
- `docs/qwen-fixes-2026-10/cluster-new-p14-108.md`: this report.

## Deviations (environment)

1. **ccache temp dir.** The first build failed with `ccache: error: failed to create temporary file for /home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6/...: Permission denied` (build-1-red.log, EXIT 2). This sandbox cannot write there. The build succeeded with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp` (a directory on disk under `~/.cache/zene-tmp`). Compiler flags are unchanged.
2. **`feature_check.py` could not run.** It exits 1 with `PermissionError` creating `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-108-feature-check` (`gate-1-feature-check.log`). That folder is outside the write-jail. The script also writes its ratchet baseline cache outside the jail. I did not change the script. I ran each of its gate commands from the worktree instead (table above). The ratchet rows are not a full PASS/FAIL comparison: they are my reading of the rule. No regression line names a file this branch changed, so no item is new in my files.
3. **`CrossoverEQControlDialog` is not a registered ctest target** in this build (`ctest -N` lists none; the build target does not exist, EXIT 2 in build-4-family.log). Not run.

## Not checked

- Other widgets with the same `addDefaultActions` pattern (`src/gui/widgets/LcdSpinBox.cpp:77`, `src/gui/widgets/MixerChannelLcdSpinBox.cpp:80`, `src/gui/widgets/TempoSyncBarModelEditor.cpp:91`) are outside this finding and were not examined.
- Right-click and copy-drag on a live fader: the live path is unchanged by the guards, but no new test drives those with a live model.

## Self-check

Completion check run: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-108 new-p14-108 > $L/submit-check-1.log 2>&1` → EXIT=1 (`submit-check-1.log`). PASS: commits on the branch, working tree clean, report with verdict, tests changed. FAIL before this section was written: "Self-check" section missing. FAIL that remains: "gates (feature_check.py)" (see item 0).

0. **Gates row (`feature_check.py`): not verified by the wrapper.** It exits 1 with `PermissionError` creating `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-108-feature-check` (`gate-1-feature-check.log`), a folder outside this write-jail. Its ratchet baseline cache also writes outside the jail. I did not change the script. Its gate commands, run directly from the worktree on the final tree: six simple gates EXIT 0; complexity and file-length EXIT 1 with train-wide regression lines, none naming `src/gui/widgets/Fader.cpp` or `tests/src/gui/VcaStripLifetimeTest.cpp` (`gate-2-complexity.log`, `gate-2-file-length.log`). Under the wrapper's rule (fail only on items in changed files) those rows would pass, but the wrapper itself has not produced that result. Needs a re-run from a location the sandbox allows.

1. **Regression FAILS with the product change reverted and PASSES with it.** Yes, with exit codes (full table above): RED on the unfixed base, exit 139 (SIGSEGV) for `aFaderWhoseModelDiesIgnoresMousePressAndDrag` (`test-1-red-*.log`), and 139 for each of the other four destroyed-model functions when run alone (`test-1-red-*.log`, `test-8-menu-red.log`); with the final diff reverted (4b), 139 for the five destroyed-model functions and 0 for the live control (`test-11-reverted-*.log`), ctest exit 8. Reapplied: 13 passed, 0 failed, exit 0 (`test-12-reapplied-final.log`), ctest exit 0.

2. **Deterministic.** No timers, sleeps, threads or wall-clock values. Events are built with `QMouseEvent`/`QWheelEvent`/`QContextMenuEvent` at fixed positions (20, 100) and delivered synchronously with `QCoreApplication::sendEvent`. The only numbers are the model's fixed range (0..2, value 1.0) and the wheel's 120-unit notch. The dead-model paths return before any `QInputDialog` or `QMenu::exec()` can open, so nothing blocks on an event loop. Test file: `tests/src/gui/VcaStripLifetimeTest.cpp:147-220`.

3. **Fix at the cause, every caller and sibling checked.** Every read of `model()` in `src/gui/widgets/Fader.cpp` was listed (rg for `updateTextFloat|getModelValueAsDbString|dbValueText|calculateKnobPosYFromModel|setVolumeByLocalPixelValue|adjustModelByDBDelta|adjustByDialog|adjustByDecibelDelta`). Results:
   - `adjustByDecibelDelta` (`:125`) is called by `adjust()` (`:122`, reached from `src/gui/MixerView.cpp:732`) and `wheelEvent` (`:310`). Guarded at `:128`.
   - `adjustByDialog` (`:136`) is called only from `mouseDoubleClickEvent` (`:275`). Guarded at `:139`.
   - `calculateKnobPosYFromModel`, `setVolumeByLocalPixelValue`, `updateTextFloat` and `adjustModelByDBDelta` are called only from the mouse handlers and `adjustByDecibelDelta`, all behind a guard, or from `paintEvent` (`:635`, behind the existing guard at `:616`).
   - `getModelValueAsDbString`/`dbValueText` (`:552`/`:562`) are null-safe already (`dbValueText` returns "-inf" for a null model); `accessibleValueText` (`:557`) and `modelValueChanged` (`:547`, connected to `dataChanged` at `:105`) go through `dbValueText`.
   - `keyPressEvent` (`:591`) and `mouseReleaseEvent` were already null-safe on the base.
   - The base-class paths that the fixed handlers no longer reach for a dead fader (`AutomatableModelView::mousePressEvent`, `addDefaultActions`) were left unchanged; they are still reachable by other widgets.
   - Other widgets with the same `addDefaultActions` pattern (`LcdSpinBox`, `MixerChannelLcdSpinBox`, `TempoSyncBarModelEditor`) were not changed and not examined.

4. **No collateral damage.** `git show --numstat HEAD` for `src/gui/widgets/Fader.cpp`: `16 0` (no deletions); the guards only fire when `model()` is null, which previously crashed. The live-model control (`aLiveFaderStillTakesWheelInput`) passes before and after, and the family (8 registered tests) passes. No save/load, no command, no limit, no check that other callers could hit. The journal and undo calls are below the guards and unchanged with a live model.

5. **Verdict FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** This ask is not applicable; the code is cited above (`src/gui/widgets/Fader.cpp:128, 139, 177, 196, 217`).
