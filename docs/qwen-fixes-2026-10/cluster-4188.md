FIXED

4188: FIXED

Evidence: the shared cause was split document-type/view synchronization. `syncDocumentTypeView()` in `src/gui/LibraryCommandMenus.cpp:332-334` reads the current `ProjectInfo` type and uses the existing view application. `documentChanged()` now calls it for `Edited` as well as `Created` (`:349-351`), and `Song::projectLoaded` calls it unconditionally (`:364-365`), including for the implicit default `production` type. The menu also routes through that helper (`:115`).

Regression: `LibraryMenusTest::loadingDefaultProductionRestoresArrangeView` (`tests/src/gui/LibraryMenusTest.cpp:314`) saves a production project, sets the current shell to Mix, and loads the file. Before the fix and in the step 4b reverse-patch run, it failed with actual `mix`, expected `arrange` (logs `003-red-test.log` and `007-proof-red-test.log`).

Commands and proof:

- RED: `cmake --build build --target zene LibraryMenusTest` — EXIT 0 (`002-red-build.log`); `ctest -R '^LibraryMenusTest$' --output-on-failure` from `build/tests` — EXIT 8 (`003-red-test.log`, 1 CTest test executed).
- GREEN: same build command — EXIT 0 (`004-green-build.log`); same ctest command — EXIT 0 (`005-green-test.log`, 1/1 passed).
- Step 4b: `git diff -- src/gui/LibraryCommandMenus.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4188.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4188.r1/logs/fix.patch` — EXIT 0; build — EXIT 0 (`006-proof-red-build.log`); ctest — EXIT 8 (`007-proof-red-test.log`, this regression failed at the expected assertion). `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4188.r1/logs/fix.patch` — EXIT 0; build — EXIT 0 (`008-proof-green-build.log`); ctest — EXIT 0 (`009-proof-green-test.log`).

new-p8-144: FIXED

Evidence: `project.settings` changes the type through `ProjectInfo::Change::Edited`; that path now reaches `syncDocumentTypeView()` unconditionally at `src/gui/LibraryCommandMenus.cpp:349-351`, so menu, Lua, socket, and other command callers share the same view update. The regression `LibraryMenusTest::aSettingsCommandSwitchesToItsDocumentView` (`tests/src/gui/LibraryMenusTest.cpp:296`) invokes the command directly from Arrange and expects Mastering to select Mix. Before the fix and in the step 4b reverse-patch run, it failed with actual `arrange`, expected `mix` (logs `003-red-test.log` and `007-proof-red-test.log`). The test also checks that repeating the same type is stable and an unknown type is refused without changing type or view.

Commands and proof:

- RED: `cmake --build build --target zene LibraryMenusTest` — EXIT 0 (`002-red-build.log`); `ctest -R '^LibraryMenusTest$' --output-on-failure` from `build/tests` — EXIT 8 (`003-red-test.log`, 1 CTest test executed).
- GREEN: same build command — EXIT 0 (`004-green-build.log`); same ctest command — EXIT 0 (`005-green-test.log`, 1/1 passed).
- Step 4b: reverse-patch command — EXIT 0; build — EXIT 0 (`006-proof-red-build.log`); ctest — EXIT 8 (`007-proof-red-test.log`, this regression failed at the expected assertion). Reapply-patch command — EXIT 0; build — EXIT 0 (`008-proof-green-build.log`); ctest — EXIT 0 (`009-proof-green-test.log`). The final verbose run reports 28 QtTest cases passed and 1/1 registered CTest test passed (`017-verbose-test.log`).

## Test matrix

- Normal command route: production/Arrange to Mastering/Mix through `project.settings` outside the menu.
- Normal load route: Mastering/Mix to a saved default production project, ending in production/Arrange.
- Refusal and reconfiguration: unknown type is rejected with type and view preserved; same-type repeat is stable; both production-to-mastering and mastering-to-production transitions are covered. This path has no resource-capacity or exhaustion condition.
- Realtime: all changed calls run from UI-thread project settings, project load, or menu events. No audio render/process function or audio-thread synchronization is changed.
- Platform/build: no platform-specific code; the configured C++20/Qt `zene` and `LibraryMenusTest` targets built on the current supported build environment.

## Gates and scope repair

The initial `feature_check.py` run exited 1 because the inherited `tests/all-sources.txt` list again contained 1,148 vendored sfizz paths excluded by its published recipe. I re-derived only the manifest entry list from that recipe, without changing any gate or baseline. `bash tests/all-sources-reproduce.sh` then exited 0 (`014-all-sources-reproduce.log`), `bash tests/fork-sources-gate.sh` exited 0 (`015-fork-sources-gate.log`), and the final feature check exited 0 (`016-feature-check-final.log`). Its table reports PASS for fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, and file-length.

The completion check `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4188 4188` exited 0 (`020-submit-check-postcommit.log`): commit, clean-tree, report, test-registration, and feature-gate checks all PASS; its five ASK items are answered in Self-check above.

## Files in the shared-fix commit

- `src/gui/LibraryCommandMenus.cpp`
- `tests/src/gui/LibraryMenusTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/all-sources.txt` (recipe re-derivation required to restore the failing registration gate)
- `docs/qwen-fixes-2026-10/cluster-4188.md`

## Self-check

- Regression proof for 4188: the reversed product patch made `loadingDefaultProductionRestoresArrangeView` fail (actual `mix`, expected `arrange`; ctest EXIT 8); reapplying it made the same family pass (ctest EXIT 0). For new-p8-144: the reversed patch made `aSettingsCommandSwitchesToItsDocumentView` fail (actual `arrange`, expected `mix`; ctest EXIT 8); reapplying it made the same family pass (ctest EXIT 0). The ctest family executes 1 registered test and the verbose QtTest output records 28 cases passed.
- Determinism: the GUI is initialized over `QTemporaryDir` by `guitest::startGui`; views and commands are driven synchronously through `ControlRegistry`, and project loading is synchronous. There are no sleeps, timers, wall-clock values, or races in either regression.
- Root cause and callers: `rg` found the type view behavior in the menu, `ProjectInfo` change hook, and `Song::projectLoaded` wiring in `LibraryCommandMenus.cpp`. The menu and both hooks now use one `syncDocumentTypeView()` helper. The exported `applyDocumentType(const QString&)` remains available and unchanged as the underlying explicit type-to-view operation. `project.new_document` (Created), `project.settings` (Edited), and loads are covered by these paths.
- Collateral/saved data: the default production project fixture is saved and loaded with the existing implicit-default format; the type list and command schema are unchanged. The patch changes no project serialization, undo behavior, realtime path, or capacity limit. There is no resource exhaustion path in this view sync.
- Verdict alternative check: neither report is ALREADY-FIXED or FALSE-POSITIVE. The original `Created`-only guard and default-type load filter were present in the pre-fix source and both red assertions reproduce their reported symptoms.
- Platform/build: the change is platform-neutral C++20/Qt. Both affected targets build in the configured environment; no other build platform requirement was introduced.
- Gates: final `feature_check.py` EXIT 0 with every listed gate PASS. The first run exited 1 because the inherited `tests/all-sources.txt` had 1,148 excluded sfizz entries; the manifest was re-derived verbatim from its published recipe, then `all-sources-reproduce.sh` and `fork-sources-gate.sh` both exited 0. No gate limits or baselines were changed.
