FIXED

# cluster new-p15-99: a stale destruction callback can erase a replacement model

Finding: `src/core/LinkedModelGroups.cpp:83` (historical line; now `:96`). The destroy callback checked only that
the model's current objectName was registered, then erased that name.

## Verdict and evidence

Verdict: **FIXED**, with a split on the finding's scenario:

- **The reported sequence is already safe on this base.** Remove a model, register a same-named replacement, then
  destroy the old model: the base (`5c1c6715d9`) already checks identity before erasing (the old
  `getModel(model->objectName().toStdString()) == model` guard). Measured on the unfixed base: the case
  `staleDestroyKeepsSameNamedReplacement` passes (`logs/test-1.log` lists only one FAIL).
- **A real defect remains on this base, and is fixed here.** The guard looks up the model's *current* objectName.
  If a model was renamed after registration, the callback and `removeControl` look up the wrong key, and the entry
  under the registered name is never erased. `m_models` then keeps a pointer to a deleted model, and
  `saveValues` / `loadValues` iterate `m_models` and call through it (`src/core/LinkedModelGroups.cpp:57-75`).
  Measured RED: `renamedModelIsErasedUnderRegisteredName` fails at `tests/src/core/LinkedModelGroupsTest.cpp:216`
  (`Actual (log.removed.size()): 0`, expected 1).

The earlier version of this report (from the r2 run, against base `cf3618e61a`) listed four failing cases. On this
base only the rename case fails. I did not re-measure against `cf3618e61a`.

## Changes (line numbers on this branch)

- `include/LinkedModelGroups.h:133-136`: private helper `eraseRegisteredModel(key, model)`. It emits `modelRemoved`
  and erases `key` only if the entry still registers `model`.
- `src/core/LinkedModelGroups.cpp:90-96`: `addModel` sets the objectName, captures the registered key
  (`const std::string key(name.toUtf8().data())`, the same string as `emplace`), and the destroy callback calls the
  helper with that key and the model.
- `src/core/LinkedModelGroups.cpp:108-110`: `removeControl` calls the helper with `objectName()` and the model.
- `src/core/LinkedModelGroups.cpp:116-123`: the helper. `find(key)`; return unless the entry exists and
  `m_model == model`; then emit and `erase(key)`.
- The duplicate-name refusal at `src/core/LinkedModelGroups.cpp:82-89` came from the base and is kept unchanged.
  It keeps a second model from overwriting the first's entry.

Callers and siblings checked:

- `addModel` callers: `src/core/lv2/Lv2Proc.cpp:852` (`m_proc->addModel(amo, ctrl.uri())`). Key strings are built
  the same way as `containsModel` (`QString::toStdString` is UTF-8), so lookups match.
- `removeControl` caller: `src/gui/LinkedModelGroupViews.cpp:102` (the remove button). It passes the model the view holds.
- `eraseModel(const QString&)` (by name, `src/core/LinkedModelGroups.cpp:128`, `include/LinkedModelGroups.h:127`)
  now has no callers. It is public API and was left alone.
- Not changed, residual: `removeControl` on a model renamed after registration still matches nothing (lookup by
  current objectName), so it emits nothing until the model is destroyed. The base behaves the same. The only
  `setObjectName` on a registered model in `src/` is `addModel`'s own (`:90`). I did not grep `plugins/` exhaustively.
- `saveValues`, `loadValues`, `linkControls`, `getModel`, `containsModel`: untouched.

## Regression test

`tests/src/core/LinkedModelGroupsTest.cpp` (new in the integration; registered at `tests/CMakeLists.txt:82`).
Deterministic: synchronous calls, `Qt::DirectConnection`, the model deleted by a plain `delete` on the test thread.
No threads, timers or sleeps. `Engine::init(true)` in `initTestCase`, as in `AutomatableModelTest`.

| case | path | base (HEAD) | fixed |
|---|---|---|---|
| removedModelDestroyedLaterEmitsNothing | normal: remove, then destroy | pass | pass |
| destroyingRegisteredModelErasesIt | normal: destroy a registered model | pass | pass |
| staleDestroyKeepsSameNamedReplacement | the finding's sequence | pass (already safe on base) | pass |
| replacementCleansUpItsOwnEntry | replacement cleans up its own entry | pass | pass |
| removeControlIgnoresAnotherModelWithSameName | sibling: `removeControl` on a different model with the same name | pass | pass |
| destroyingUnregisteredDuplicateKeepsRegisteredModel | duplicate name refused; destroy it | pass | pass |
| renamedModelIsErasedUnderRegisteredName | **bug**: renamed model destroyed | **FAIL** (`:216`, `log.removed.size()` 0, expected 1) | pass |

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-99.r4/logs/`. All commands ran from the
worktree; ctest ran from `build/tests`. The build's ccache temp directory is not writable from this sandbox, so every
build command sets `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp`. The shared ccache config was not changed.

| step | command | EXIT | log |
|---|---|---|---|
| 1 | `git diff HEAD -- src/core/LinkedModelGroups.cpp include/LinkedModelGroups.h > logs/fix.patch` | 0 (2404 bytes) | fix.patch |
| 2 | `git apply -R logs/fix.patch` (product files back to HEAD; `git diff HEAD --stat` on them empty) | 0 | - |
| 3 | `cmake --build build --target zene LinkedModelGroupsTest -j2` (no ccache override) | 2 (ccache temp dir permission denied; no compile error) | build-1.log |
| 4 | `CCACHE_TEMPDIR=... cmake --build build --target zene LinkedModelGroupsTest -j2` | 0 | build-2.log |
| **RED** | `ctest -R '^LinkedModelGroupsTest$' --output-on-failure` (product = HEAD) | **8**: 1 test run, 1 failed (the rename case) | test-1.log |
| 5 | `git apply logs/fix.patch` (fix restored) | 0 | - |
| 6 | `CCACHE_TEMPDIR=... cmake --build build --target zene LinkedModelGroupsTest -j2` | 0 (recompiles `LinkedModelGroups.cpp`, `ControlDeviceSupport.cpp`, `LinkedModelGroupViews.cpp`, `Lv2Proc.cpp`, `Lv2ControlBase.cpp`, `Lv2Manager.cpp`, `Lv2ViewBase.cpp`, the test) | build-3.log |
| **GREEN** | `ctest -R '^LinkedModelGroupsTest$' --output-on-failure` (fix restored) | **0**: 100% of 1 passed | test-2.log |
| 7 | `CCACHE_TEMPDIR=... cmake --build build --target zene AutomatableModelTest LinkedModelGroupsTest -j2` | 0 | build-4.log |
| **FAMILY-A** | `ctest -R '^(LinkedModelGroupsTest\|AutomatableModelTest)$' --output-on-failure` | **0**: 2/2 passed | test-3.log |
| 8 | `CCACHE_TEMPDIR=... cmake --build build --target AutomatableModelTest Lv2StateTest Lv2ViewBaseLifetimeTest LinkedModelGroupViewLifetimeTest ControlDeviceCatalogueTest -j2` | 0 | build-5.log |
| 9 | `ctest -N -R "LinkedModel\|AutomatableModel\|Lv2"` | 0: 6 tests listed | list-1.log |
| **FAMILY-B** | `ctest -R "LinkedModel\|AutomatableModel\|Lv2" --output-on-failure` | **0**: 6/6 passed (incl. `Lv2InstrumentKeyRegressionTest`) | test-4.log |
| G1 | `python3 .../feature_check.py <worktree>` (unmodified) | 1: `PermissionError` creating `<worktree>/../<name>-feature-check/`, outside the write area; no gate ran | gates-1.log |
| G2 | `python3 -I logs/gate_driver.py <worktree> <logs>` (same gate commands and the script's own comparison rule; logs go to the run folder) | 0 (see table) | gates-2.log, gate-*.log |

Affected family: 2 tests (`LinkedModelGroupsTest`, `AutomatableModelTest`), 2 passed. Wider family with the Lv2 and
view-lifetime tests: 6 tests, 6 passed. Header dependents were rebuilt by step 6 and step 7; `zene` compiled every
dependent in `lmmsobjs`.

Gates (`gate_driver.py` / G2; each row is the script's own rule):

| gate | command | EXIT | result |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | PASS |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | PASS: no new item in a changed file |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | PASS: no new item in a changed file |

Base for the ratchet comparison is `5c1c6715d9` (`FIX-BASE` is `050/m0-train-19`); the cached base listing was used.
The ratchet exit codes are the raw script codes; the wrapper's rule passes them.

## Files committed

- `include/LinkedModelGroups.h` (helper declaration)
- `src/core/LinkedModelGroups.cpp` (destroy callback, `removeControl`, helper; the duplicate-name refusal from the base is kept)
- `tests/src/core/LinkedModelGroupsTest.cpp` (new regression test)
- `tests/CMakeLists.txt` (test registration)
- `tests/all-sources.txt`, `tests/fork-sources.txt` (new test entry)
- `tests/upstream-modifications.txt` (reasons for the two inherited product files changed; the integration's conflict was resolved by keeping both sides)
- `docs/qwen-fixes-2026-10/cluster-new-p15-99.md` (this report)

## Environment notes

- The brief names branch `bugs/qwen-new-p15-99`, but this worktree is checked out on `integration/new-p15-99-5c1c6715d9`.
  The fix is committed on the checked-out branch. I did not switch branches. Earlier fix commits (`4d5f42fa1a`,
  `3287a140cc`) live on `bugs/qwen-new-p15-99` on a different base, and are not an ancestor of this branch.
- The integration step left the tree unmerged. `src/core/LinkedModelGroups.cpp` had two conflicts (the base's
  identity guard vs the accepted patch). I took the accepted side in both, which keeps the base's duplicate-name
  refusal above them. `tests/upstream-modifications.txt` had one conflict; I kept both sides' entries.
- `feature_check.py` cannot run from this sandbox because it writes its logs outside the write area. The gates
  were run through `logs/gate_driver.py` instead, which imports the same functions and uses the same rule.
  This needs either the wrapper's log location to move, or the parent to run it.

## Self-check

Answers to the completion-check items (`submit_check.py`):

1. **RED / GREEN.** Yes. RED = `test-1.log`, exit 8, product at HEAD: `renamedModelIsErasedUnderRegisteredName` fails
   at `LinkedModelGroupsTest.cpp:216`. GREEN = `test-2.log`, exit 0 after `git apply logs/fix.patch`: 1/1 passed.
   The product revert and restore were `git apply -R` (exit 0) and `git apply` (exit 0).
2. **Deterministic.** Yes. No wall-clock time, sleep, thread or timer. The model is destroyed by a plain `delete`
   on the test thread; the callback is `Qt::DirectConnection`, so it runs synchronously inside `delete`. The key is
   fixed by the test's `addModel(model, "x")` followed by `setObjectName("y")`.
3. **At the cause, complete.** Yes. Both places that erased by name (destroy callback, `removeControl`) now go
   through one helper that checks the entry still points at the model. Callers checked: `Lv2Proc.cpp:852`,
   `LinkedModelGroupViews.cpp:102`. The residual rename-plus-`removeControl` case is unchanged from base and is
   explained above.
4. **No collateral damage.** Yes. The map keys are the same strings as before (`toUtf8` and `toStdString` are the
   same encoding), so saved data and lookups are unchanged. Save and load are untouched. No limit or check was added.
   The refusal guard from base is kept. The one behaviour change is the rename case: a renamed model is now erased
   under its registered name instead of leaving a stale pointer.
5. **Realtime, undo, commands.** The changed paths run on the main thread: `addModel` from `Lv2Proc::createPorts`
   (plugin creation), `removeControl` from a GUI button, and the destroy callback when a model is deleted. None is on
   the audio thread. The one new allocation is the `std::string` key copied into the destroy lambda's capture, on the
   same plugin-creation path. No command was added or changed; no undo logic changed.
6. **Registration and gates.** Test registered at `tests/CMakeLists.txt:82`, in `fork-sources.txt` and
   `all-sources.txt`; the two inherited product files are in `upstream-modifications.txt`. Gates: six PASS and two
   ratchets PASS by the wrapper's rule (table above). `feature_check.py` itself exits 1 with `PermissionError` from
   this sandbox (G1), so `submit_check`'s gates item will show FAIL for that reason only.
7. **Verdict evidence.** FIXED. The same-name scenario was already safe on base (identity guard, measured passing
   on unfixed base). The rename variant was real (RED at `:216`, stale entry, `saveValues`/`loadValues` at `:57-75`)
   and is fixed by the captured key at `src/core/LinkedModelGroups.cpp:90-96` and `:116-123`.
