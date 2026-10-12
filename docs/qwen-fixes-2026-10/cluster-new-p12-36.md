# Cluster new-p12-36: No-op setters create undo entries

**Verdict: FIXED**

## Round 2 repair

The shared-parameter case was real. `setPair` previously ORed the two per-axis deltas, so a repeated pair looked changed when X temporarily wrote a value that Y then restored. Pair change detection now compares both macro scalars and the first prior value against the final write for each distinct resolved parameter. Pair application suppresses per-model checkpoints; the pair's single action undo records the net edit only when that net state changed. Standalone `rack.macro_set` retains normal parameter journalling.

## Source evidence

- `src/core/ControlCommandsRackMacroPair.cpp:90-130` keys writes by resolved chain/effect/parameter and compares each parameter's initial and final values along with both macro scalars.
- `src/core/ControlCommandsRackMacroPair.cpp:168-170` applies both pair axes without individual parameter checkpoints, then uses the net-state check.
- `include/ControlRackMacroWrites.h:54` and `include/RackMacros.h:159-164` declare the pair checkpoint mode.
- `src/core/RackMacros.cpp:204-238` keeps journalling enabled by default and uses non-journalling model writes only when the pair requests it.
- `tests/src/core/RackMacrosTest.cpp:474-502` binds both macros to the same gain parameter, sets the same pair twice, and verifies one undo restores the macros and parameter to their pre-edit state.
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

The affected family ran one CTest target and 13 QtTest cases; the related undo family ran one CTest target. No test ran zero times. No callable fff grep tool was exposed, so numbered logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files in this commit

- `include/ControlRackMacroWrites.h`
- `include/RackMacros.h`
- `src/core/ControlCommandsRackMacroPair.cpp`
- `src/core/RackMacros.cpp`
- `tests/src/core/RackMacrosTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-36.md`
