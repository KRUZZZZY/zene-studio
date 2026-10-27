# Cluster new-p14-329: tempo-sync bar editor dereferences a missing model

**Verdict: FIXED**

`TempoSyncBarModelEditor::modelChanged()` logged "no TempoSyncKnobModel has been set!" and then fell through. `updateDescAndIcon()` (and the other methods that use the specialised model) then dereferenced the null result and segfaulted. The fix returns after the warning, and each method that needs the specialised model returns early when it is missing. Valid models behave exactly as before.

**Reachability.** The shipped LADSPA caller always passes a real model (`plugins/LadspaEffect/LadspaWidgetFactory.cpp:74`, `ladspaControl->tempoSyncKnobModel()`), so no shipped path triggers the crash today. It is reachable through the public API, which is why it is fixed: the constructor takes any `FloatModel *` (`include/TempoSyncBarModelEditor.h:45`, and `src/gui/widgets/TempoSyncBarModelEditor.cpp:44`), and `ModelView::setModel(nullptr)` / `unsetModel()` call `modelChanged()` (`src/gui/ModelView.cpp:73`, `:81-83`). Before the fix, the RED run shows a SIGSEGV on both paths.

## Evidence in the product file

File: `src/gui/widgets/TempoSyncBarModelEditor.cpp`

- `:63-70` `modelChanged()` returns right after the warning (`:67-70`), matching the sibling `TempoSyncKnob::modelChanged()` (`src/gui/widgets/TempoSyncKnob.cpp:85-91`, fixed earlier as `new-p12-263`).
- `:85-95` `contextMenuEvent()` returns after `mouseReleaseEvent(nullptr)` when the model is null (guard at `:91`).
- `:167-174` `updateDescAndIcon()` returns early when `model() == nullptr` (guard at `:170`). This also covers a late `syncModeChanged` from a model that was detached.
- `:215-220` `showCustom()` returns early when the model is null (guard at `:217`), before the meter dialog is created.
- `:239-247` `updateTextDescription()` (guard at `:243`) and `:288-293` `updateIcon()` (guard at `:290`) return early when the model is null.

Unchanged: `getCustomMeterModel`, the connections, the description and icon mapping for a valid model.

## Regression tests

The finding named `tests/reference/LadspaEffect/LadspaWidgetFactory.cpp` as covering test. That file is a reference copy of plugin source, not a test, and no `^LadspaWidgetFactory$` ctest exists. The regression therefore goes into the existing `KnobKeyboardTest`, which already holds the sibling `TempoSyncKnob` detached-model case (`tests/src/gui/KnobKeyboardTest.cpp:169`, `aTempoSyncKnobStopsAfterWarningAboutADetachedModel`) and the bar-editor cases for `BarModelEditor`.

File: `tests/src/gui/KnobKeyboardTest.cpp`

- `:192` `aTempoSyncBarModelEditorStopsAfterWarningAboutADetachedModel`: detaches with `setModel(nullptr)`, asserts the warning, then changes the old model's sync mode and sends a context-menu event.
- `:217` `aTempoSyncBarModelEditorRefusesAPlainFloatModel`: constructs the editor with a plain `FloatModel`, asserts the warning and an empty model.
- `:233` `aTempoSyncBarModelEditorStillFollowsItsModel`: normal-path control; a valid model still produces "Synced to Quarter Note".

Determinism: no timing, sleeps or threads. The warning is captured through the existing `qInstallMessageHandler(collectQtWarning)` seam already used in that file; no wall-clock values are involved.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-329.r1/logs/`.

| Step | Command | EXIT |
| --- | --- | --- |
| build (first try) | `cmake --build build --target KnobKeyboardTest -j2` (`build-1-red.log`) | 2 (ccache could not write its temp dir; `ccache: error: ... Permission denied`) |
| build (RED, test only) | `CCACHE_DISABLE=1 cmake --build build --target KnobKeyboardTest -j2` (`build-2-red.log`) | 0 |
| RED | `build/tests/KnobKeyboardTest aTempoSyncBarModelEditorStopsAfterWarningAboutADetachedModel` (`red-*.log`) | 139 (SIGSEGV) |
| RED | `build/tests/KnobKeyboardTest aTempoSyncBarModelEditorRefusesAPlainFloatModel` | 139 (SIGSEGV) |
| control on unfixed code | `build/tests/KnobKeyboardTest aTempoSyncBarModelEditorStillFollowsItsModel` | 0 (PASS) |
| build (GREEN) | `CCACHE_DISABLE=1 cmake --build build --target zene KnobKeyboardTest -j2` (`build-3-green.log`) | 0 |
| GREEN | the two regressions and the control, run by name | 0, 0, 0 |
| GREEN, full binary | `build/tests/KnobKeyboardTest` (`green-all.log`) | 0; 29 PASS, 0 FAIL |
| GREEN, ctest family | `ctest -R "KnobKeyboardTest|KnobAccessibleTargetSizingTest" --output-on-failure` (`test-1-green-family.log`) | 0; 2 of 2 passed |
| proof: revert | `git diff -- src/gui/widgets/TempoSyncBarModelEditor.cpp > fix.patch && git apply -R fix.patch`; build `zene KnobKeyboardTest` (`build-4-revert.log`) | 0, 0, 0 |
| proof: RED after revert | the two regressions by name | 139, 139 |
| proof: reapply | `git apply fix.patch`; build (`build-5-reapply.log`) | 0, 0 |
| proof: GREEN after reapply | the two regressions by name | 0, 0 |
| proof: family after reapply | `ctest -R "KnobKeyboardTest|KnobAccessibleTargetSizingTest"` (`test-2-reapply-family.log`) | 0; 2 of 2 passed |

Affected-family test count: 2 (`KnobKeyboardTest`, `KnobAccessibleTargetSizingTest`), both passing.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 before running any check. It creates `<worktree-parent>/<name>-feature-check` for its logs, which lies outside the allowed write paths; the log is `gate-feature-check-1.log`. I did not change the gate.

I ran the gate's own commands directly in the worktree, logging to the LOG FOLDER:

| Gate command | EXIT |
| --- | --- |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 |
| `bash tests/complexity-gate.sh --check` | 1 |
| `bash tests/file-length-gate.sh --check` | 1 |

The two ratchet gates exit 1 on the raw run. The output names no file this lane changed (`TempoSyncBarModelEditor.cpp`, `KnobKeyboardTest.cpp`, `upstream-modifications.txt`); the flagged items are in other files, and the ratchet's own "baseline not written" note says the new-item comparison did not run. The exact per-file verdict of `feature_check.py` is therefore **not verified** here.

## Self-check

`submit_check.py` (`submit-check-1.log`, EXIT 1) before commit reported FAIL for: commit not yet made, tree not clean (cleared by this commit), this report missing (now written), the Self-check section (this section), tests added (satisfied by this commit), and gates (see above). Answers to the ASK items:

- **Test fails with the change reverted and passes with it:** yes. Revert gives SIGSEGV (139) for both regressions; with the fix they exit 0. The control test passes in both states. See the proof rows above.
- **Deterministic:** yes. No timing, sleeps or races. The warning is captured through `qInstallMessageHandler(collectQtWarning)`, the injection already used in this file.
- **Fix at the cause and complete:** all six methods that dereference `model()` in `TempoSyncBarModelEditor.cpp` are guarded (`:67`, `:91`, `:170`, `:217`, `:243`, `:290`). Callers checked: the constructor, `setModel` / `unsetModel` via `ModelView::setModel` (`src/gui/ModelView.cpp:73`), and the `syncModeChanged` connection, which `ModelView::setModel` disconnects on detach (`src/gui/ModelView.cpp:63`, `m_model->disconnect( widget() )`).
- **No collateral damage:** only the null-model path changes, and that path crashed before. Valid models keep the same description and icon (`aTempoSyncBarModelEditorStillFollowsItsModel` passes). No saved data, limits or checks change.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE:** the crash reproduces on this branch before the fix (RED evidence above).

## Observed, not changed

`src/gui/widgets/TempoSyncKnob.cpp:105-109` (`contextMenuEvent`) still calls `model()->displayName()` without a guard. A detached `TempoSyncKnob` that is right-clicked will crash. This is the sibling class of the same defect. It is outside this finding's scope, so it is not changed here; it should be its own finding.
