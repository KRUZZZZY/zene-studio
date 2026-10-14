# Cluster new-p9-577

**Verdict: FIXED**

The patrol's historical path `src/gui/editors/AutomationPointEdit.cpp` is absent on this branch. The matching current move path is `movePoint` in `src/core/ControlCommandsAutomationTools.cpp`. It previously saved the in value, removed the node, and used `putValue`, which recreated a symmetric point and dropped locked tangents.

## File and line evidence

| File and line | Evidence |
|---|---|
| `src/core/ControlCommandsAutomationTools.cpp:130` | `putMovedNode` reinserts a point with its retained out value, then restores curve/bend and hand-locked tangents. |
| `src/core/ControlCommandsAutomationTools.cpp:144` | `movePoint` copies the complete source node before removal and passes it to the reinsertion helper. |
| `tests/src/core/ControlAutomationModesTest.cpp:320` | Regression moves a discontinuous point through `automation.move_point` and checks both values plus locked tangent state and values. |

For a symmetric point, the helper follows the new in value on both sides. For a discontinuous point, it preserves the old out value, matching the existing drag behavior in `AutomationClip::setDragValue()`.

## RED/GREEN and ratchets

The final proof used the existing `ControlAutomationModesTest` family. CTest ran 1 test (`1/1`). The same regression failed with the production patch removed and passed after it was restored.

| Purpose | Exact command | EXIT |
|---|---|---:|
| Reverse production patch | `git diff -- src/core/ControlCommandsAutomationTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Build test target without fix | `logfile=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/build-12.log; cmake --build build --target ControlAutomationModesTest > "$logfile" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild app without fix | `logfile=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/build-13.log; cmake --build build --target zene > "$logfile" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED | `logfile=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/test-6.log; ctest -R ControlAutomationModesTest --output-on-failure > "$logfile" 2>&1; rc=$?; echo EXIT=$rc` (fails at the out-value assertion) | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Build test target with fix | `logfile=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/build-14.log; cmake --build build --target ControlAutomationModesTest > "$logfile" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild app with fix | `logfile=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/build-15.log; cmake --build build --target zene > "$logfile" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN | `logfile=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/test-7.log; ctest -R ControlAutomationModesTest --output-on-failure > "$logfile" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate | `logfile=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/gate-3.log; bash tests/complexity-gate.sh --check > "$logfile" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate | `logfile=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/gate-4.log; bash tests/file-length-gate.sh --check > "$logfile" 2>&1; rc=$?; echo EXIT=$rc` | 0 |

The fixture plugin was built for the test setup: `logfile=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-577.r1/logs/build-2.log; cmake --build build --target tripleoscillator > "$logfile" 2>&1; rc=$?; echo EXIT=$rc` (EXIT=0).
