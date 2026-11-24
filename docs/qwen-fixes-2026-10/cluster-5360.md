FIXED

## Decision

REAL. `SessionGridView::rebuild()` created a `QToolButton` for every track/scene pair
(`src/gui/SessionGridView.cpp:202`, inside the loops at `:191` and `:200`), while
`session.set_grid` accepts 256 tracks and 512 scenes (`src/core/ControlCommandsSession.cpp:290-291`).
A maximum grid therefore attempted 131,072 cell buttons on the UI thread.

The fix is the UI-specific limit the finding permits: the authored grid remains unchanged, but the
view draws at most 64 tracks by 64 scenes. `MaxVisibleTracks`/`MaxVisibleScenes` are at
`src/gui/SessionGridView.cpp:55-56`; `rebuild()` keeps the authored dimensions separately and builds
only the capped prefix at `:184-187`. The status line says the grid is truncated at `:278-282`, and
the Grid Size menu still defaults from the authored dimensions at `:510`. The drop-preview status
also uses the authored dimensions (`src/gui/SessionGridDrop.cpp:132`). The member split is in
`include/SessionGridView.h:178-181`.

## Regression test

`tests/src/gui/SessionGridViewTest.cpp:497-524` (`aLargeGridIsDrawnWithBoundedCellWidgets`) first
proves `session.get_state` still accepts 256 x 512, then sets 65 x 65 (one cell beyond the UI cap),
constructs the real view, and counts only buttons whose `controlCommand` is `session.launch_slot`.
It requires exactly 4,096 such buttons, rejects the first cell outside the cap in either direction,
checks the truncation message, and finally resizes back to 2 x 2 to exercise reconfiguration.

The RED run does not construct a 131,072-widget view (the freeze under test); it exercises the
same unbounded construction path at 65 x 65, where the old code deterministically creates 4,225
cell buttons instead of 4,096.

## Commands and results

All build/test output is in `/home/kruzzzzy/.config/kads/zene/state/runs/5360.r1/logs/`.

| Phase | Exact command | Exit | Evidence |
|---|---|---:|---|
| RED build | `cmake --build build --target zene SessionGridViewTest -j4` | 0 | `build-2.log` |
| RED test | `ctest -R '^SessionGridViewTest$' --output-on-failure` (from `build/tests`) | 8 | `test-2-red.log`: actual `4225`, expected `4096` |
| GREEN build | `cmake --build build --target zene SessionGridViewTest -j4` | 0 | `build-4-green.log` |
| GREEN test | `ctest -R '^SessionGridViewTest$' --output-on-failure` (from `build/tests`) | 0 | `test-4-green.log`: 1/1 passed |
| Family build | `cmake --build build --target zene SessionGridRecordTest SessionSubSceneGridTest SessionTransportBarTest SessionCellPlaybackMenuTest AccessibilityWalkTest -j4` | 0 | `build-5-family.log` |
| Family test | `ctest -R '^(SessionGridViewTest|SessionGridRecordTest|SessionSubSceneGridTest|SessionTransportBarTest|SessionCellPlaybackMenuTest|AccessibilityWalkTest)$' --output-on-failure` (from `build/tests`) | 0 | `test-5-family.log`: 6/6 passed; repeated after restore in `test-8-family-restored.log` |

Reverse-patch proof:

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Save product-only patch | `git diff -- include/SessionGridView.h src/gui/SessionGridView.cpp src/gui/SessionGridDrop.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/5360.r1/logs/fix.patch` | 0 | `reverse-1.log` |
| Reverse it | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/5360.r1/logs/fix.patch` | 0 | `reverse-1.log` |
| Rebuild without fix | `cmake --build build --target zene SessionGridViewTest -j4` | 0 | `build-6-reversed.log` |
| Test without fix | `ctest -R '^SessionGridViewTest$' --output-on-failure` (from `build/tests`) | 8 | `test-6-reversed.log`: actual `4225`, expected `4096` |
| Restore fix | `git apply /home/kruzzzzy/.config/kads/zene/state/runs/5360.r1/logs/fix.patch` | 0 | `reverse-2.log` |
| Rebuild with fix | `cmake --build build --target zene SessionGridViewTest -j4` | 0 | `build-7-restored.log` |
| Test with fix | `ctest -R '^SessionGridViewTest$' --output-on-failure` (from `build/tests`) | 0 | `test-7-restored.log`: 1/1 passed |

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5360`
exited 0. The table in `logs/gate-1.log` has PASS for fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, and file-length.

## GUI proof

`QT_QPA_PLATFORM=offscreen python3 /home/kruzzzzy/.config/kads/zene/state/runs/5360.r1/logs/capture_5360.py`
exited 0 through the real control socket. It set a 256 x 512 session grid, showed the launcher, and
wrote `logs/cluster-5360-launcher.png` at 1600 x 1000, sha256
`22c4c68fbdbeda64a40b188f6dd6ab7cb1ac6a3c8df4013e10574f28c41fd2f2` (`screenshot-2.log`). The
captured status reads `256 tracks, 512 scenes (showing the first 64 x 64: the launcher draws at most
4096 cells)`.

## Test matrix

- Normal path: the existing 2 x 2 launcher tests and the five family targets above still pass.
- Failure/exhaustion path: 65 x 65 exceeds the UI cap; the view is bounded and the model still accepts 256 x 512.
- Reconfiguration path: the regression resizes 65 x 65 -> 2 x 2 and verifies a visible cell exists.
- Audio-thread constraints: no render/process/handle code changed, so nothing new allocates, locks, waits or logs on the audio thread.
- Supported-platform build requirements: no new API was introduced; the Linux/Qt 6 build is verified here.

## Self-check

- The regression test fails with the product change reverted (`test-6-reversed.log`, exit 8) and passes with it restored (`test-7-restored.log`, exit 0).
- The test is deterministic: it uses fixed dimensions and counts buttons immediately after a synchronous `refresh()`; there is no sleep, wall-clock assertion, or race.
- The fix is at the cause and complete: `rebuild()` uses the capped counts for cells, stops, scene buttons and the Stop All position; `refresh()` paints only the drawn prefix; the authored dimensions still drive refresh detection, status text, drop preview, and Grid Size defaults.
- No collateral damage: `session.set_grid` still accepts 256 x 512, no save format or command schema changed, and the six launcher-family targets pass.
- Undo and realtime: no control command, undo action, or audio-path code was added or changed.
- Registration and gates: all eight `feature_check.py` rows PASS in `gate-1.log`.
- Portability: the change uses only C++20 and existing Qt APIs; no Qt-6-only call was added.

## Other findings

None.
