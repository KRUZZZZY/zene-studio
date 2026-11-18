# Cluster new-p12-36: No-op setters create undo entries

**Verdict: FIXED**

## Round 2 repair

The shared-parameter case was real. `setPair` previously ORed the two per-axis deltas, so a repeated pair looked changed when X temporarily wrote a value that Y then restored. Pair change detection now compares both macro scalars and the first prior value against the final write for each distinct resolved parameter. Pair application suppresses per-model checkpoints; the pair's single action undo records the net edit only when that net state changed. Standalone `rack.macro_set` retains normal parameter journalling.

## Round 3 QA rework

Pair writes now suppress only the per-model journal checkpoint. `AutomatableModel::setValue` accepts an independent `journal` flag while retaining its ordinary non-automated behavior, including updating `lastTouched`. `RackMacros::apply` uses that flag for pair writes and retains the existing explicit checkpoint for standalone `rack.macro_set`. The non-overlapping gain/pan pair regression is retained, and the shared-parameter pair regression remains a separate test; both assert touch tracking as well as undo behavior.

## Source evidence

- `src/core/ControlCommandsRackMacroPair.cpp:90-130` keys writes by resolved chain/effect/parameter and compares each parameter's initial and final values along with both macro scalars.
- `src/core/ControlCommandsRackMacroPair.cpp:168-170` applies both pair axes without individual parameter checkpoints, then uses the net-state check.
- `include/ControlRackMacroWrites.h:54` and `include/RackMacros.h:159-164` declare the pair checkpoint mode.
- `include/AutomatableModel.h:246` and `src/core/AutomatableModel.cpp:315-328` keep automated-write classification separate from optional journal checkpoint creation; non-automated writes still set `lastTouched` when journalling is suppressed.
- `src/core/RackMacros.cpp:230-236` retains the standalone explicit checkpoint and suppresses both checkpoints for pair writes while calling `setValue` as a non-automated write.
- `tests/src/core/RackMacrosTest.cpp:475-545` retains the non-overlapping gain/pan pair no-op undo case and separately tests the shared-gain pair, one-step undo, and `lastTouched` for both.
- The historical layer-mix location is absent in this tree: `src/core/ControlCommandsRackLayers.cpp` has no layer-mix setter.

## Evidence

Build commands ran from the worktree root. CTest commands ran from `build/tests`. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs`.

| Purpose | Exact command | Exit | Evidence |
|---|---|---:|---|
| Build initial regression target | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Built current `zene` and `RackMacrosTest` before changing pair behavior. |
| Initial RED | `ctest -R '^RackMacrosTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | 1/1 CTest target ran; shared-parameter regression failed with X at 0.25 after one undo instead of 0.0. |
| Intermediate net-delta attempt | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt the intermediate pair change detector. |
| Intermediate regression | `ctest -R '^RackMacrosTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Exposed per-model checkpoints from the transient shared-parameter writes; this led to suppressing those pair checkpoints. |
| Rejected broad build target | `cmake --build build --target all > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 97 | The build guard rejected target `all`; the explicit target list below was used. |
| Build fixed product and dependent test targets | `cmake --build build --target zene RackMacrosTest RackZonesTest RackZoneRoutingTest InstrumentRoutingTest InstrumentLayersCommandsTest ControlModulatorCommandsTest ModulationLayerTest ControlSuccessPathsTest ControlNoteExpressionCommandsTest ModulationLayerValueTest ModulationLayerProjectRoundTripTest SampleAccurateAutomationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt `zene` and all tests including the changed rack header. |
| Product-only reversal | `git diff -- include/RackMacros.h include/ControlRackMacroWrites.h src/core/RackMacros.cpp src/core/ControlCommandsRackMacroPair.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Reversed only product hunks; the shared-parameter regression remained in the test file. |
| Build reversed product and dependent test targets | `cmake --build build --target zene RackMacrosTest RackZonesTest RackZoneRoutingTest InstrumentRoutingTest InstrumentLayersCommandsTest ControlModulatorCommandsTest ModulationLayerTest ControlSuccessPathsTest ControlNoteExpressionCommandsTest ModulationLayerValueTest ModulationLayerProjectRoundTripTest SampleAccurateAutomationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt the unfixed product before the proof run. |
| Required RED proof | `ctest -R '^RackMacrosTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | 1/1 target ran; 12 QtTest cases passed and the shared-parameter regression failed at X=0.25, expected 0.0. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Reapplied the saved product-only patch. |
| Final build | `cmake --build build --target zene RackMacrosTest RackZonesTest RackZoneRoutingTest InstrumentRoutingTest InstrumentLayersCommandsTest ControlModulatorCommandsTest ModulationLayerTest ControlSuccessPathsTest ControlNoteExpressionCommandsTest ModulationLayerValueTest ModulationLayerProjectRoundTripTest SampleAccurateAutomationTest UndoHonestyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt `zene`, all changed-header dependents, and `UndoHonestyTest`. |
| Final GREEN | `ctest -R '^RackMacrosTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Same command as RED; 1/1 CTest target passed. |
| QtTest case count | `ctest -R '^RackMacrosTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | 13 passed, 0 failed. |
| Related undo family | `ctest -R '^UndoHonestyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | 1/1 CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed without changing baselines. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r2/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed without changing baselines. |

## Round 3 evidence

Build commands ran from the worktree root. CTest commands ran from `build/tests`. Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r3/logs`.

| Purpose | Exact command | Exit | Evidence |
|---|---|---:|---|
| Initial deterministic regression build | `cmake --build build --target zene RackMacrosTest` | 0 | `build-3.log`; rebuilt `zene` and `RackMacrosTest` before the production edit. |
| RED on round 2 product | `ctest -R '^RackMacrosTest$' --output-on-failure` | 8 | `test-3.log`; 1/1 CTest target failed at both `lastTouched` assertions. |
| Build fixed product and test | `cmake --build build --target zene RackMacrosTest --parallel 4` | 0 | `build-5.log`. |
| GREEN before reversal | `ctest -R '^RackMacrosTest$' --output-on-failure` | 0 | `test-4.log`; 1/1 CTest target passed. |
| Save product-only patch | `git diff -- include/AutomatableModel.h include/RackMacros.h src/core/AutomatableModel.cpp src/core/RackMacros.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r3/logs/fix.patch` | 0 | Saved only production files; tests were not in the patch. |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r3/logs/fix.patch` | 0 | `test-5.log` ran against the reversed production code. |
| Build reversed product | `cmake --build build --target zene RackMacrosTest --parallel 4` | 0 | `build-6.log`; rebuilt before the proof run. |
| RED with product-only reversal | `ctest -R '^RackMacrosTest$' --output-on-failure` | 8 | `test-5.log`; both pair tests fail because `lastTouched` remains on the setup sentinel. This is the same test binary source as GREEN. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r3/logs/fix.patch` | 0 | Restored the saved production patch. |
| Rebuild product and first dependent target | `cmake --build build --target zene RackMacrosTest --parallel 4` | 0 | `build-7.log`. |
| Rebuild all named consumers of `AutomatableModel.h` | `cmake --build build --target zene ControlModulatorCommandsTest ControlNoteExpressionCommandsTest ControlSuccessPathsTest InstrumentLayersCommandsTest InstrumentRoutingTest ModulationLayerProjectRoundTripTest ModulationLayerTest ModulationLayerValueTest RackMacrosTest RackZoneRoutingTest RackZonesTest SampleAccurateAutomationTest UndoHonestyTest --parallel 4` | 0 | `build-8.log`; rebuilt `zene` and all 13 test targets whose dependency files include the changed header. |
| Restored GREEN | `ctest -R '^RackMacrosTest$' --output-on-failure` | 0 | `test-6.log`; 1/1 CTest target passed. |
| QtTest case count | `ctest -R '^RackMacrosTest$' --output-on-failure -V` | 0 | `test-7.log`; 14 passed, 0 failed, 0 skipped. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log`; passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log`; passed. |

### Final preservation edit and verification

The final production edit retained `addJournalCheckPoint()` for standalone macro writes. The product-only red/green proof and final checks were repeated after that edit.

| Purpose | Exact command | Exit | Evidence |
|---|---|---:|---|
| Build fixed target before final reversal | `cmake --build build --target zene RackMacrosTest --parallel 4` | 0 | `build-9.log`. |
| GREEN before final reversal | `ctest -R '^RackMacrosTest$' --output-on-failure` | 0 | `test-8.log`; 1/1 CTest target passed. |
| Save final product-only patch | `git diff -- include/AutomatableModel.h include/RackMacros.h src/core/AutomatableModel.cpp src/core/RackMacros.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r3/logs/fix.patch` | 0 | The test file is excluded. |
| Reverse final product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r3/logs/fix.patch` | 0 | Reverted only the product files. |
| Build reversed product | `cmake --build build --target zene RackMacrosTest --parallel 4` | 0 | `build-10.log`; rebuilt before the proof run. |
| RED with final product-only reversal | `ctest -R '^RackMacrosTest$' --output-on-failure` | 8 | `test-9.log`; both `lastTouched` assertions fail. |
| Restore final product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r3/logs/fix.patch` | 0 | Restored production files. |
| Rebuild `zene` and all named header consumers | `cmake --build build --target zene ControlModulatorCommandsTest ControlNoteExpressionCommandsTest ControlSuccessPathsTest InstrumentLayersCommandsTest InstrumentRoutingTest ModulationLayerProjectRoundTripTest ModulationLayerTest ModulationLayerValueTest RackMacrosTest RackZoneRoutingTest RackZonesTest SampleAccurateAutomationTest UndoHonestyTest --parallel 4` | 0 | `build-11.log`; includes all 13 test targets whose dependency files include `AutomatableModel.h`. |
| Final GREEN | `ctest -R '^RackMacrosTest$' --output-on-failure` | 0 | `test-10.log`; 1/1 CTest target passed. |
| Final QtTest count | `ctest -R '^RackMacrosTest$' --output-on-failure -V` | 0 | `test-11.log`; 14 passed, 0 failed, 0 skipped. |
| Final complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-5.log`; passed. |
| Final file-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-6.log`; passed. |

An earlier test setup did not isolate `lastTouched`: `cmake --build build --target zene RackMacrosTest` exited 0 (`build-2.log`) and `ctest -R '^RackMacrosTest$' --output-on-failure` exited 0 (`test-2.log`). The setup was corrected to force a different model as the touch sentinel before the pair write; the corrected test then produced the RED above. `cmake --build build --parallel 4` exited 97 (`build-4.log`) because the local `bunny-guard` requires named targets; the named-target rebuild above succeeded. The available tools did not expose an fff grep callable, so numbered logs were searched with `rg` for the required terms.

The final affected family ran one CTest target and 14 QtTest cases; no test ran zero times.

## Files in this commit

- `include/AutomatableModel.h`
- `include/RackMacros.h`
- `src/core/AutomatableModel.cpp`
- `src/core/RackMacros.cpp`
- `tests/src/core/RackMacrosTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-36.md`
