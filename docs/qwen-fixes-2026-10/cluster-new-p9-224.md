# Cluster new-p9-224: a duplicate model name corrupts the linked model group

Verdict: **FIXED**

The finding is real in the core group, but its "wrong view row" part is not reachable in this tree. `addModel()` ignored the result of `m_models.emplace` (original line 82, the finding's line; now `src/core/LinkedModelGroups.cpp:91`) and still connected the second model's `destroyed` handler and emitted `modelAdded`/`dataChanged`. When the second model was destroyed, its handler saw that the name was present (original `LinkedModelGroups.cpp:85`, `containsModel(objectName)`), emitted `modelRemoved` (`:87`) and erased the name (`:88`, `eraseModel`), which removed the **original** model from the group. `saveValues`, `loadValues` and `linkControls` all iterate `m_models`, so the original silently dropped out of save/load and linking.

`modelAdded`/`modelRemoved` have no subscriber in `src/` (`rg` finds only the emitters at `src/core/LinkedModelGroups.cpp` and the declarations at `include/LinkedModelGroups.h:114-115`), so no view row is removed by them. The "remove the wrong view row" claim is not what breaks here. `dataChanged` has no subscriber in the linked-group, Lv2 or view sources either (`rg dataChanged src/gui/LinkedModelGroupViews.cpp src/gui/Lv2ViewBase.cpp src/core/lv2 src/core/LinkedModelGroups.cpp` shows only the emit at `LinkedModelGroups.cpp:105`).

## Reachability

- The only `addModel` caller is `src/core/lv2/Lv2Proc.cpp:852`, keyed by `ctrl.uri()`. `Lv2Ports.cpp:299-301` (`PortBase::uri()`) returns `lilv_port_get_symbol(...)`, the LV2 port symbol. The LV2 specification requires port symbols to be unique within a plugin (spec requirement, not checked against the spec text here). So a duplicate needs a malformed plugin.
- The second model was never in `m_models`, so save/load output is the same before and after the fix. What changes is that the original is no longer removed, and no spurious `modelAdded`/`dataChanged` is emitted for the refused model.

## The fix (`src/core/LinkedModelGroups.cpp`)

1. `addModel` (`:80-106`) refuses a name that `containsModel` already reports (`:86-89`). It warns with `qWarning` and returns before `setObjectName`, `emplace`, `connect` and the two emits. This is load-time code (`Lv2Proc::createPorts`), not the audio thread.
2. The key is `name.toStdString()` (`:91`), the same bytes `containsModel` and `eraseModel` use. The old `name.toUtf8().data()` stopped at an embedded NUL, so the stored key could differ from the one looked up.
3. The `destroyed` handler (`:95`) and `removeControl` (`:114`) erase only when `getModel(name) == model`, so a name owned by another model is never erased. For every normal caller, the stored model is the one being destroyed or removed, so behaviour is unchanged.

No header change. The new `qWarning` needs `<QDebug>` (`:25`).

Callers and siblings checked:
- `addModel`: only `src/core/lv2/Lv2Proc.cpp:852`. Its sibling `m_connectedModels.emplace` (`:848`) also keeps the first entry, so both maps agree.
- `removeControl`: `src/gui/LinkedModelGroupViews.cpp:102` (removable controls only). Lv2 passes `removable=false` (`src/gui/Lv2ViewBase.cpp:119`), so the view's remove button is not reached from Lv2.
- `eraseModel`, `clearModels`, `containsModel`: core only; behaviour unchanged.

Allocation: the destroy and remove paths perform the same `toStdString()` allocation as before (`containsModel` was called with the same conversion). `addModel` adds one lookup, on the load path. Nothing on the audio thread changes. No undo or control command is touched.

## Not changed (residual, reported)

- `src/gui/LinkedModelGroupViews.cpp:115`: `LinkedModelGroupView::addControl` also drops a duplicate id silently (`m_widgets.emplace`, and the temporary `unique_ptr` deletes the new control). Reachable only with the same malformed plugin (Lv2ViewBase.cpp:119 passes the port symbol as the id). It is a view-side defect separate from the core corruption, so it is left for its own finding.
- `LinkedModelGroup` also allows one model object under two names (`addModel(a,"x")` then `addModel(a,"y")`). Not reached by any caller in `src/`, not covered by this fix.

## Tests

The finding asked for a new test file next to the closest area's tests. The lane build tree is configured and its `cmake` shim refuses to configure ("configuring is the parent's job"), so a new test target could not be generated here. The cases therefore live in the existing, already-registered core test `tests/src/core/AutomatableModelTest.cpp` (it already calls `Engine::init(true)` and tests the same model registration). No CMake change, no new file.

New cases (`tests/src/core/AutomatableModelTest.cpp:221-293`):
- `linkedGroupRefusesDuplicateNameAndAnnouncesNothingForIt` (`:223`) - a refused duplicate is not stored and is not announced.
- `linkedGroupDestroyingRefusedDuplicateKeepsTheOriginal` (`:241`) - destroying it keeps the original.
- `linkedGroupRemoveControlOfRefusedDuplicateKeepsTheOriginal` (`:260`) - `removeControl` on it keeps the original.
- `linkedGroupDestroyingStoredModelStillRemovesIt` (`:280`) - control: the stored model is still removed and announced.

All four use `QTest::ignoreMessage` for the new warning where it applies, and no timing, sleeps or threads.

## Verification evidence

Environment notes:
- The shell is fish, so the gate commands were run one per call.
- `ccache` failed the first build (`EXIT=2`) with `Permission denied` for its temp directory under `~/.local/share/zene-bunny-fix/dsh/...`. Every build after that used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-new-p9-224` (a build cache location inside the jail). No compile result was affected.
- Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-224.r1/logs/`. Names below are the file names there.

| Step | Command (run from the worktree, or `build/tests` for ctest) | EXIT | Log |
| --- | --- | --- | --- |
| RED build (original product code, final test) | `cmake --build build --target AutomatableModelTest -j2` | 0 | `build-red-3.log` |
| RED run | `ctest -R '^AutomatableModelTest$' --output-on-failure` | **8** (11 passed, 3 failed: `linkedGroupRefusesDuplicateNameAndAnnouncesNothingForIt` added 2 vs 1; `linkedGroupDestroyingRefusedDuplicateKeepsTheOriginal` removed 1 vs 0; `linkedGroupRemoveControlOfRefusedDuplicateKeepsTheOriginal` removed 1 vs 0) | `test-red-2.log` |
| GREEN build | `cmake --build build --target AutomatableModelTest -j2` | 0 | `build-green-2.log` |
| GREEN run | `ctest -R '^AutomatableModelTest$' --output-on-failure` | **0** (100%) | `test-green-2.log` |
| Count | `ctest -R '^AutomatableModelTest$' -V` | 0 - `Totals: 14 passed, 0 failed` | `test-count-green.log` |
| 4b: save patch | `git diff -- src/core/LinkedModelGroups.cpp > .../fix.patch` | 0 | `fix.patch` |
| 4b: unfix | `git apply -R .../fix.patch` | 0 | |
| 4b: rebuild unfixed | `cmake --build build --target AutomatableModelTest -j2` | 0 | `build-4b-unfixed.log` |
| 4b: test unfixed | `ctest -R '^AutomatableModelTest$' --output-on-failure` | **8** (same 3 failures) | `test-4b-unfixed.log` |
| 4b: re-apply | `git apply .../fix.patch` | 0 | |
| 4b: rebuild fixed | `cmake --build build --target AutomatableModelTest -j2` | 0 | `build-4b-fixed.log` |
| 4b: test fixed | `ctest -R '^AutomatableModelTest$' --output-on-failure` | **0** | `test-4b-fixed.log` |
| Family build | `cmake --build build --target zene Lv2StateTest Lv2ViewBaseLifetimeTest LinkedModelGroupViewLifetimeTest ControlDeviceCatalogueTest lv2instrument -j2` | 0 | `build-family-2.log` |
| Family run | `ctest -R 'Lv2\|LinkedModel\|AutomatableModelTest' --output-on-failure` | **0** - 5/5 (`Lv2StateTest`, `Lv2ViewBaseLifetimeTest`, `LinkedModelGroupViewLifetimeTest`, `Lv2InstrumentKeyRegressionTest`, `AutomatableModelTest`) | `test-family-2.log` |

An earlier `ctest` attempt on the stale binary (`test-red-1.log`, after the failed build) is not counted.

Test-count note: the `ctest` family for `AutomatableModelTest` is one ctest entry that executes **14** test functions (`Totals: 14 passed`). The whole family is 5 ctest entries (`AutomatableModelTest` plus the four `Lv2`/`LinkedModel` entries), all executed and all passing.

### Gates

`feature_check.py` could not write its own logs: it creates `zene-qwen-new-p9-224-feature-check` beside the worktree (`PermissionError`, `gate-feature-check-1.log`, EXIT=1). That directory is outside the write-jail, and the script was not edited. Instead I ran the same commands it runs, each to a log in the LOG folder:

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-1.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce-1.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression-1.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace-1.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-1.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 (run before this report was written; see the self-check) | `gate-doc-figures-1.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 - red on the train on purpose | `gate-complexity-1.log` |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 - red on the train on purpose | `gate-file-length-1.log` |

The ratchet rule is: fail only on a `REGRESSION:` item whose file this branch changed. Every `REGRESSION:` line in both logs names a file this branch does not touch (`WasmSandbox.cpp`, `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsSessionRecordLand.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsShortcuts.cpp`, `Vst3Host.cpp`, `TakeLane.cpp`, and several new test files). None names `LinkedModelGroups.cpp`, `AutomatableModelTest.cpp` or `upstream-modifications.txt`. So both ratchet rows are PASS under the gate's own rule, even though the script's exit status is 1.

Registration: the change adds no new file, so `tests/fork-sources.txt`, `tests/all-sources.txt` and `tests/CMakeLists.txt` are unchanged. The two inherited files changed (`src/core/LinkedModelGroups.cpp`, `tests/src/core/AutomatableModelTest.cpp`) are registered in `tests/upstream-modifications.txt` (the last two lines of that file).

## Files committed

- `src/core/LinkedModelGroups.cpp`
- `tests/src/core/AutomatableModelTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-224.md`

## Self-check

`python3 .../submit_check.py <worktree> new-p9-224` (log `submit-check-1.log`, EXIT=1):

- PASS commits on the branch; PASS working tree clean; PASS this report; PASS tests added or changed (`AutomatableModelTest.cpp`).
- FAIL `gates (feature_check.py)`: the script cannot write its own log directory. It creates `<worktree>-feature-check` beside the worktree and got `PermissionError` (`gate-feature-check-1.log`, EXIT=1). That location is outside the write-jail and the script was not edited. Its commands were run separately above: six simple gates EXIT 0, two ratchets EXIT 1 with no regression item in a changed file. Under the gate's rule both ratchet rows pass. The `feature_check.py` row itself has no clean run, and this is the one open item.

Answers to the four open questions:

1. **RED/GREEN with the product change reverted and restored** - yes. Reverted: `test-4b-unfixed.log`, EXIT=8, three duplicate-name cases fail. Restored: `test-4b-fixed.log`, EXIT=0, 14/14 (the table above has the commands).
2. **Deterministic** - yes. No clock, sleep, thread or race. The inputs are `FloatModel` objects with fixed values. Destruction is synchronous: `destroyed` is connected `Qt::DirectConnection`, so the `unique_ptr::reset()` call that destroys a model runs the handler before the next line. The only log check is `QTest::ignoreMessage` with an exact regex.
3. **Fix at the cause, complete** - the cause is that `addModel` accepted a name it could not store and that the erase paths ignored which model owned the name. Callers and siblings checked: `Lv2Proc.cpp:852` is the only `addModel` caller, and `createPorts` runs once per `Lv2Proc` (`Lv2Proc.cpp:213`), so the same name is never added twice on a legitimate path. `removeControl` callers are `LinkedModelGroupViews.cpp:102` (removable controls only). The sibling `LinkedModelGroupView::addControl` (`LinkedModelGroupViews.cpp:115`) keeps the same silent duplicate drop; it is listed as residual above, not fixed.
4. **No collateral damage** - a model under a unique name follows the same path as before, so project save and load are unchanged (`saveValues` and `loadValues` iterate the same map with the same keys). A refused duplicate was never in the map before the fix, so save output is unchanged for it too. There is no limit or size check, only the name-collision refusal, and its only caller never produces a collision in a legitimate project.

Verdict stays **FIXED**.
