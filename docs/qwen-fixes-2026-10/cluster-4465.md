FIXED

4465: FIXED
Decision: REAL. Before the fix, `ShellSelection` stored `Track` and `Clip` in `QPointer`s but only emitted `changed()` from its setters. `watchSelectedObjects()` now observes both selected objects and disconnects observers when selection changes; `clearDestroyedSelection()` clears the dependent track/clip state and emits `changed()` once. See `include/ShellSelection.h:42-43,76-82` and `src/gui/shell/ShellSelection.cpp:98-119,121-151`.

Regression: `DevicePanelTest::deletingSelectedTrackNotifiesListeners` covers deleting the selected track and deleting an older, deselected track. Its RED run failed the selected-track notification assertion; the restored-fix run passed. The final family run passed 6/6 CTest targets.

RED command (the QtTest output includes the named slot failure):

```sh
ctest -R '^DevicePanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/test-05-proof-red.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status
```

EXIT=8. The log reports `DevicePanelTest::deletingSelectedTrackNotifiesListeners` failed.

GREEN command:

```sh
ctest -R '^(DevicePanelTest|ChordProgressionPanelTest|RackMenuTest|ArrangerKeysNavigatorTest|InstrumentSetupUiTest|ArrangerTimelineTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/test-06-proof-green.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status
```

EXIT=0; all 6/6 tests passed.

new-p17-174: FIXED
Decision: REAL. The same missing destruction observer affected a selected clip and its paired track. The shared lifetime handler clears both before notifying listeners, so Inspector, Device panel, and selection-following consumers receive the change. See `src/gui/shell/ShellSelection.cpp:98-119`.

Regression: `DevicePanelTest::deletingSelectedClipNotifiesListeners` covers deleting the selected clip and deleting an older, deselected clip. Its RED run failed the selected-clip notification assertion; the restored-fix run passed.

RED command (the QtTest output includes the named slot failure):

```sh
ctest -R '^DevicePanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/test-05-proof-red.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status
```

EXIT=8. The log reports `DevicePanelTest::deletingSelectedClipNotifiesListeners` failed.

GREEN command:

```sh
ctest -R '^(DevicePanelTest|ChordProgressionPanelTest|RackMenuTest|ArrangerKeysNavigatorTest|InstrumentSetupUiTest|ArrangerTimelineTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/test-06-proof-green.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status
```

EXIT=0; all 6/6 tests passed.

## Shared RED/GREEN proof (step 4b)

The production-only patch was reversed while the two regression slots remained in the test binary. Both slots failed again (23 passed, 2 failed), then both passed after restoring the patch.

```sh
git diff -- include/ShellSelection.h src/gui/shell/ShellSelection.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/fix.patch
```

EXIT=0.

```sh
cmake --build build --target zene DevicePanelTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/build-05-proof-red.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status
```

EXIT=0.

```sh
ctest -R '^DevicePanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/test-05-proof-red.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status
```

EXIT=8; both regression slots failed.

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/fix.patch
```

EXIT=0.

```sh
cmake --build build --target zene DevicePanelTest ChordProgressionPanelTest RackMenuTest ArrangerKeysNavigatorTest InstrumentSetupUiTest ArrangerTimelineTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/build-06-proof-green.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status
```

EXIT=0.

## Matrix and gates

- Normal selection: the existing `theSelectionKeepsATrackAClipOrAChannel` covers track, clip, and channel transitions.
- Destruction and reconfiguration: the two new tests cover destruction of the selected object, then destruction of an older object after selection has moved. No bounded resource or exhaustion path exists in this selection state; the two `QMetaObject::Connection`s are replaced on selection changes.
- Realtime: `ShellSelection` setters are called by GUI selection/event-filter and control-command paths. The change adds no audio-thread call, allocation, lock, wait, or logging.
- Persistence and undo: this is transient UI selection state; no project format, commands, or undo behavior changed.
- Platforms: the configured Linux C++20/Qt build passed. Other platform builds were not run.

The first feature-check run exposed a stale `tests/all-sources.txt` entry list containing Sfizz vendor sources excluded by its own documented recipe. I regenerated only its entry list from that recipe, preserving the manifest header. The corrected source-scope gate and full feature check passed.

Feature-check command:

```sh
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4465
```

EXIT=0. Table: PASS for `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity` (no new item), and `file-length` (no new item).

## Self-check

- Shared cause: both reports came from the same missing observer around the `QPointer` selection state; one helper handles both object types.
- Both regression slots failed with the production patch reversed and passed with it restored. The normal and reselection paths also pass.
- The fix changes only transient UI selection notification. It adds no control command and does not alter saved projects or undo steps.
- The changed selection code is on GUI/control paths; no realtime audio path calls these setters.
- Build evidence is for the configured Linux Qt tree; other supported platform builds were not available in this run.
- Files in the fix commit: `include/ShellSelection.h`, `src/gui/shell/ShellSelection.cpp`, `tests/src/gui/DevicePanelTest.cpp`, `tests/all-sources.txt`, and `docs/qwen-fixes-2026-10/cluster-4465.md`.
