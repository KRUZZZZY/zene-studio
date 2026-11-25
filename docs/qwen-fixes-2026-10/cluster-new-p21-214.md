FIXED

# Cluster new-p21-214: LfoControllerDialog::modelChanged() dereferences a possibly null model

`LfoControllerDialog::modelChanged()` stored `castModel<LfoController>()` and dereferenced it for
every knob and button group. `ModelView::setModel()` calls `modelChanged()` for *whatever* model it
is handed (`src/gui/ModelView.cpp:73`), including `nullptr` and the default-constructed `Model` that
`ModelView::unsetModel()` installs (`src/gui/ModelView.cpp:81-83`), and `castModel` is a
`dynamic_cast` that answers null for both (`include/ModelView.h:54-57`). The method now returns
before the first control when the cast fails (`src/gui/LfoControllerDialog.cpp:252-254`).

## Evidence in the product files

- `src/gui/LfoControllerDialog.cpp:248-261` — `modelChanged()`; the six dereferences are
  `:256-261` (`&m_lfo->m_baseModel` … `&m_lfo->m_multiplierModel`). Before this fix `:250` was
  followed directly by `:256`. The guard is at `:252-254`:
  `if (m_lfo == nullptr) { return; }`.
- `src/gui/ModelView.cpp:53-75` — `ModelView::setModel()` accepts any `Model*` (null included),
  runs `doConnections()` (`:89-96`, two `QObject::connect` calls on the model pointer) and then
  `modelChanged()` (`:73`), unconditionally.
- `src/gui/ModelView.cpp:81-83` — `unsetModel()` is the base class's documented detach: it installs
  a default-constructed `Model`, which is non-null but is not an `LfoController`.
- `include/ModelView.h:40-41` — `setModel`/`unsetModel` are public and virtual; `:54-57` `castModel()`
  is `dynamic_cast<T*>( model() )`.
- `src/core/LfoController.cpp:335-338` — the shipped construction path always passes a real
  `LfoController`, so the crash is not reachable from the Controller Rack today. It is reachable
  through the public `ModelView` API of a `LMMS_EXPORT` widget, which is the same reachability
  argument the sibling fixes in this tree accepted (`docs/qwen-fixes-2026-10/cluster-new-p14-329.md`,
  `cluster-new-p12-263.md`), and the constructor takes a plain `Controller*`
  (`include/LfoController.h:129`), so a caller that hands it another controller type crashes too.

## Siblings checked

- `PeakControllerDialog::modelChanged()` (`src/gui/PeakControllerDialog.cpp:81-84`) only assigns
  `m_peakController = castModel<PeakController>()` and dereferences nothing — nothing to fix.
- The dialog's own `askUserDefWave()` (`src/gui/LfoControllerDialog.cpp:209-219`) does
  `dynamic_cast<LfoController*>(model())` at `:214` and dereferences it at `:216`. Left unchanged: its
  dereference is only reachable after the user picks a file in the modal `FileDialog::openWaveformFile()`
  (`src/gui/modals/FileDialog.cpp`, `openFileDialog.exec()`), which a test cannot drive without a
  fake file selection, so a guard there could not be held by a deterministic regression. Recorded
  under "Other findings" for a separate job.
- The other path into `modelChanged()` is `ModelView::setModel()`/`unsetModel()` themselves, covered
  by the tests below, plus the constructor's `setModel(_model)` (`src/gui/LfoControllerDialog.cpp:191`),
  which the normal-path test drives through the shipped `LfoController::createDialog()`.

## Regression tests

`tests/src/gui/KnobKeyboardTest.cpp` — the file that already holds the two sibling detached-model
regressions for knob model views (`aTempoSyncKnobStopsAfterWarningAboutADetachedModel:228`,
`aTempoSyncBarModelEditorStopsAfterWarningAboutADetachedModel:249`). A new test *target* was not
possible: this lane's build tree refuses reconfiguration (see Limitations), so the five cases live in
the existing target. They use two subclasses (`:157-185`) to reach the protected models and controls;
no product header is changed for the tests.

| Test (line) | Path it covers | Exit without the fix | Exit with it |
| --- | --- | ---: | ---: |
| `anLfoControllerDialogBindsEveryControlOfItsController` (`:318`) | normal path: shipped `createDialog()`, all six controls bound to the controller's own models | 0 | 0 |
| `anLfoControllerDialogSurvivesADetachedModel` (`:334`) | failure: `setModel(nullptr)` | 139 (SIGSEGV) | 0 |
| `anLfoControllerDialogSurvivesUnsetModel` (`:348`) | failure: `unsetModel()`, the base class's dummy `Model` | 139 (SIGSEGV) | 0 |
| `anLfoControllerDialogRefusesAForeignModel` (`:361`) | exhaustion of the accepted type: a `FloatModel` handed to the dialog | 139 (SIGSEGV) | 0 |
| `anLfoControllerDialogBindsAgainWhenTheControllerComesBack` (`:375`) | reconfiguration: detach, then re-attach the controller | 139 (SIGSEGV) | 0 |

Determinism: no wall clock, sleeps, threads or timers. Every case is a synchronous call on a stack
`LfoController` and a stack dialog; the crash is a real `SIGSEGV`, and the RED stack in
`red-1-anLfoControllerDialogSurvivesADetachedModel.log` shows it inside
`QtSharedPointer::ExternalRefCountData::getAndRef` — the `QObject::connect` that
`ModelView::doConnections()` makes on the bogus `&m_lfo->m_baseModel` (`accessing address 0x…0b8`,
i.e. the offset of that member off a null `m_lfo`).

Audio thread: nothing here is on the audio path — the change is one early return in a GUI
`modelChanged()`; no allocation, lock, wait, log or signal is added. No command was added or
changed (`ControlCommandsSnapshot` canary untouched). Undo: none of this is journalled, one call
still moves no undo step.

## Commands and exit codes

Log folder (`LOG`): `/home/kruzzzzy/.config/kads/zene/state/runs/new-p21-214.r1/logs`.
All commands ran in the worktree; ctest ran from `build/tests`.

| Step | Command | EXIT |
| --- | --- | ---: |
| build, test only (RED) | `cmake --build build --target KnobKeyboardTest -j4` → `build-2-red.log` | 0 |
| RED, control case | `./KnobKeyboardTest anLfoControllerDialogBindsEveryControlOfItsController` → `red-1-…BindsEveryControl….log` | 0 |
| RED, detach case | `./KnobKeyboardTest anLfoControllerDialogSurvivesADetachedModel` → `red-1-…SurvivesADetachedModel.log` | 139 |
| build (GREEN) | `cmake --build build --target zene KnobKeyboardTest -j4` → `build-3-green.log` | 0 |
| GREEN, all five cases, one command each | `./KnobKeyboardTest <case>` → `green-1-<case>.log` (5 files) | 0, 0, 0, 0, 0 |
| GREEN, whole binary | `./KnobKeyboardTest` → `green-2-all.log` | 0 (39 passed, 0 failed) |
| GREEN, ctest family | `ctest -R '^(KnobKeyboardTest\|KnobAccessibleTargetSizingTest)$' --output-on-failure` → `test-1-green-family.log` | 0 (2 of 2 passed) |
| proof: patch out | `git diff -- src/gui/LfoControllerDialog.cpp > LOG/fix.patch` | 0 |
| proof: revert | `git apply -R LOG/fix.patch` | 0 |
| proof: rebuild | `cmake --build build --target zene KnobKeyboardTest -j4` → `build-4-reverted.log` | 0 |
| proof: RED, control | `./KnobKeyboardTest anLfoControllerDialogBindsEveryControlOfItsController` → `red-2-….log` | 0 |
| proof: RED, detach | `./KnobKeyboardTest anLfoControllerDialogSurvivesADetachedModel` → `red-2-….log` | 139 |
| proof: RED, unsetModel | `./KnobKeyboardTest anLfoControllerDialogSurvivesUnsetModel` → `red-2-….log` | 139 |
| proof: RED, foreign model | `./KnobKeyboardTest anLfoControllerDialogRefusesAForeignModel` → `red-2-….log` | 139 |
| proof: RED, re-attach | `./KnobKeyboardTest anLfoControllerDialogBindsAgainWhenTheControllerComesBack` → `red-2-….log` | 139 |
| proof: reapply | `git apply LOG/fix.patch` | 0 |
| proof: rebuild | `cmake --build build --target zene KnobKeyboardTest -j4` → `build-5-reapplied.log` | 0 |
| proof: GREEN again, all five | `./KnobKeyboardTest <case>` → `green-3-<case>.log` (5 files) | 0, 0, 0, 0, 0 |
| proof: family again | `ctest -R '^(KnobKeyboardTest\|KnobAccessibleTargetSizingTest)$'` → `test-2-reapply-family.log` | 0 |
| GUI harness | `QT_QPA_PLATFORM=offscreen python3 LOG/gui-screenshot.py . LOG/gui` → `gui-harness-1.log` | 0 |
| GUI harness image | `sha256sum LOG/gui/controller-rack.png` → `gui-sha256.txt` | 0 |

The GUI harness screenshot (`window.toggle {editor: controller_rack}` then
`window.screenshot {path}` over the control socket, from this tree's `build/zene`) is
`logs/gui/controller-rack.png`, 1600 × 1000, sha256
`c50f76575085fd1baee54ef83bd3735c64f6b91b3f658962c29fb6a5f2735f26`.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` could not run in this lane:

| Command | EXIT | Output |
| --- | ---: | --- |
| `FEATURE_CHECK_LOG_DIR=LOG/gates python3 …/feature_check.py <worktree>` → `gate-feature-check.log` | 1 | `bunny-guard: 'git -C …/qwen/gate-baselines/tree checkout -q --detach 9913d36d…' is not allowed in a Bunny lane` — its baseline for this branch's base commit is not cached, and the guard refuses the checkout it makes to compute one |

So every gate was run on its own, exactly as `feature_check.py` invokes it:

| Gate | Command | EXIT | Result |
| --- | --- | ---: | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` → `gate-fork-sources.log` | 0 | PASS: every tracked source in scope is registered |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` → `gate-all-sources-reproduce.log` | 0 | reproduces |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` → `gate-no-upstream-regression.log` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace` → `gate-check-namespace.log` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` → `gate-no-tautology.log` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` → `gate-doc-figures.log` | 0 | PASS |
| complexity | `PATH=<run tmp>/venv/bin:$PATH bash tests/complexity-gate.sh --check` → `gate-complexity.log` | 0 | PASS, no item in my files (first attempt exited 2: `lizard is not installed`; installed into a venv under the run folder, log `pip-lizard.log`) |
| file-length | `bash tests/file-length-gate.sh --check` → `gate-file-length.log` | 0 | PASS, no item in my files |

Registration: `tests/upstream-modifications.txt` gets one line for `src/gui/LfoControllerDialog.cpp`
and one for `tests/src/gui/KnobKeyboardTest.cpp`. No new source file was added, so
`tests/fork-sources.txt` and `tests/all-sources.txt` are unchanged (both gates above pass on that).

## Limitations met in this lane (environment, not the fix)

- `cmake -S . -B build` is refused: `bunny-guard: configuring is the parent's job; the lane build
  tree is already configured` (`configure-1.log`, EXIT=97). A new test target therefore cannot be
  registered, which is why the regression went into the existing `KnobKeyboardTest` target —
  `cmake --build build --target LfoControllerDialogTest` answered `No rule to make target`
  (`build-1.log`, EXIT=2) before that.
- `feature_check.py` and the `gates` row of `submit_check.py` cannot run here for the reason in the
  table above. I did not change either tool or any gate limit.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted:
  `git apply -R LOG/fix.patch` (EXIT 0), rebuild (EXIT 0), four cases exit **139** (SIGSEGV) and the
  normal-path case exits 0 (`red-2-*.log`). Restored: `git apply LOG/fix.patch` (EXIT 0), rebuild
  (EXIT 0), all five exit **0** (`green-3-*.log`), and the ctest family exits 0
  (`test-2-reapply-family.log`).
- **The test is deterministic.** No timing, sleep, thread or timer: a stack `LfoController` and a
  stack dialog, five synchronous calls (`setModel(nullptr)`, `unsetModel()`, `setModel(&foreign)`,
  detach-then-reattach, and the shipped `createDialog()`), each asserted on `model()`,
  `castModel<LfoController>()` and the six `Knob`/`AutomatableButtonGroup` model pointers. The seam
  is the public `ModelView::setModel()/unsetModel()` API, not a fake.
- **The fix is at the cause and complete.** The cause is the unconditional dereference at
  `src/gui/LfoControllerDialog.cpp:250` in a method `ModelView::setModel()` always calls. Every
  caller of `modelChanged()` was checked: the constructor's `setModel(_model)` (valid model, and the
  normal-path test drives it), `setModel(nullptr)`, `unsetModel()`, and a foreign model (all four
  tested). Siblings: `PeakControllerDialog::modelChanged()` assigns only (no dereference);
  `askUserDefWave()` is listed under "Other findings" with the reason it has no deterministic test.
- **No collateral damage.** A valid model binds exactly as before — same six models, same order,
  asserted by `anLfoControllerDialogBindsEveryControlOfItsController` and the ctest family (39
  passed, 0 failed). Nothing is saved or loaded differently: the change is in a GUI-only method and
  touches no model value, no save/load path and no project file. No limit was added that another
  caller could hit: the guard returns only when the cast genuinely fails.
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; the verdict is FIXED, with the crash reproduced
  in this session (`red-1-…SurvivesADetachedModel.log`, exit 139).

## Other findings

- `src/gui/LfoControllerDialog.cpp:214-216` (`askUserDefWave()`): the same class of defect - it
  dereferences `dynamic_cast<LfoController*>(model())` (`:214`) at `:216` without a null check. Only reachable after a
  file is chosen in the modal `FileDialog::openWaveformFile()`, so no deterministic regression can
  hold a guard there; worth its own job with a file-dialog seam.
- `src/gui/LfoControllerDialog.cpp:218` (`m_userWaveBtn->setToolTip(buffer->audioFile())`, with
  `buffer` from `SampleBuffer::fromFile()` at `:215`): `SampleBuffer::fromFile()` can answer null (an
  undecodable file), which would be a second null dereference. Same modal path; not fixed here.
