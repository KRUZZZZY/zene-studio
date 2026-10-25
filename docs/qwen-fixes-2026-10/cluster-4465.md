FIXED

4465: FIXED
Decision: REAL. Before the fix, `ShellSelection` stored `Track` and `Clip` in `QPointer`s but only emitted `changed()` from its setters. The selection now watches the selected track's `destroyedTrack` signal and the selected clip's QObject destruction; deleting a track clears both pointers, while deleting a clip clears only the clip and preserves its live track. Both paths notify listeners. See `include/ShellSelection.h:42-43,76-82` and `src/gui/shell/ShellSelection.cpp:98-126,128-159`.

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
Decision: REAL. The same missing destruction observer affected a selected clip. Its destruction now notifies Inspector, Device panel, remote controls and other selection listeners, while keeping the still-live parent track selected. See `src/gui/shell/ShellSelection.cpp:104-126`.

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

## Independent QA correction: preserve the live track when a clip is deleted

`ShellSelection` represents a selected track with an optional clip; `setClip(nullptr)` already keeps the track. The original destruction handler cleared the track when only its selected clip was destroyed. The clip callback now clears only `m_clip`; the track callback uses `Track::destroyedTrack`, which runs before `Track::~Track` deletes its clips, so destroying a track and its selected clip emits one change.

The updated deterministic regression checks that deleting the selected clip leaves its track selected, then deleting that track emits one more change. It also selects a clip on the selected track before deleting the track, proving that parent teardown emits only one change.

Reverse only the QA product correction:

```sh
git diff -- include/ShellSelection.h src/gui/shell/ShellSelection.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/qa-product.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/qa-product.patch
```

EXIT=0.

```sh
cmake --build build --target zene DevicePanelTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/review-build-red-final.log 2>&1; status=$?; echo EXIT=$status; exit $status
```

EXIT=0.

```sh
ctest -R '^DevicePanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/review-ctest-red-final.log 2>&1; status=$?; echo EXIT=$status; exit $status
```

EXIT=8. `deletingSelectedClipNotifiesListeners` failed because the submitted handler left `selection->track()` null instead of retaining the live track.

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/qa-product.patch
```

EXIT=0.

```sh
cmake --build build --target zene DevicePanelTest ChordProgressionPanelTest RackMenuTest ArrangerKeysNavigatorTest InstrumentSetupUiTest ArrangerTimelineTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/review-build-04-restored.log 2>&1; status=$?; echo EXIT=$status; exit $status
```

EXIT=0.

```sh
ctest -R '^(DevicePanelTest|ChordProgressionPanelTest|RackMenuTest|ArrangerKeysNavigatorTest|InstrumentSetupUiTest|ArrangerTimelineTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/review-ctest-03-restored.log 2>&1; status=$?; echo EXIT=$status; exit $status
```

EXIT=0; all six header-dependent test targets passed.

## Matrix and gates

- Normal selection: the existing `theSelectionKeepsATrackAClipOrAChannel` covers track, clip, and channel transitions.
- Destruction and reconfiguration: the tests cover deleting an older deselected object, deleting a selected clip while its track remains selected, and deleting a selected track with a selected child clip. No bounded resource or exhaustion path exists in this selection state; both `QMetaObject::Connection`s are replaced on selection changes.
- Realtime: `ShellSelection` setters are called by GUI selection/event-filter and control-command paths. The change adds no audio-thread call, allocation, lock, wait, or logging.
- Persistence and undo: this is transient UI selection state; no project format, commands, or undo behavior changed.
- Platforms: the configured Linux C++20/Qt build passed. Other platform builds were not run.

The first feature-check run exposed a stale `tests/all-sources.txt` entry list containing Sfizz vendor sources excluded by its own documented recipe. I regenerated only its entry list from that recipe, preserving the manifest header. The corrected source-scope gate and full feature check passed.

Feature-check command:

```sh
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4465
```

EXIT=0. Table: PASS for `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity` (no new item), and `file-length` (no new item).

Submit-check command (the first run requested the explicit answers below):

```sh
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4465 4465 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4465.r1/logs/gate-08-submit-check.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit $gate_status
```

EXIT=0; the check passed and requested the five self-check answers supplied here.

## Self-check

- Shared cause: both reports came from missing destruction observers around the `QPointer` selection state. The shared watcher replaces both connections on selection changes; the track and clip callbacks retain their distinct selection semantics.
- RED/GREEN evidence: `test-05-proof-red.log` has separate failures for `deletingSelectedTrackNotifiesListeners` and `deletingSelectedClipNotifiesListeners` (CTest exit 8; QtTest totals 23 passed, 2 failed). `test-06-proof-green.log` passes all 6/6 CTest targets (exit 0), including `DevicePanelTest` with both slots. These runs use the exact commands above.
- Deterministic test behavior: there are no sleeps, clock values, timers, or races. Each test deletes an object synchronously and uses `QSignalSpy` to count the synchronous `changed()` signal; no test seam or injected timestamp is needed.
- Caller/sibling review: selection enters through the shell event filter, note editor, and `ControlCommandsSelection`; `setChannel()` also replaces observers when it clears track/clip state. All three setters route through `watchSelectedObjects()`. Track destruction clears both pointers; clip destruction clears just the clip. Tests prove old object connections are disconnected and parent teardown emits once.
- Collateral review: selection is transient UI state; no project serialization, project loading, undo implementation, or command schema changed. No limits or checks were added. Existing project data therefore follows the same load path.
- Realtime review: the call sites are GUI/control paths; no realtime audio path calls these setters, and the fix adds no audio-thread allocation, lock, wait, or logging.
- Verdict question: N/A for ALREADY-FIXED/FALSE-POSITIVE evidence; both reports are decided REAL and FIXED by the reproduced failures.
- Build evidence is for the configured Linux Qt tree; other supported platform builds were not available in this run.
- Files in the fix commit: `include/ShellSelection.h`, `src/gui/shell/ShellSelection.cpp`, `tests/src/gui/DevicePanelTest.cpp`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`, and `docs/qwen-fixes-2026-10/cluster-4465.md`.
