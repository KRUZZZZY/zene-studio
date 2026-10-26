FIXED

new-p18-1: FIXED

Decision: REAL. `depth_set` edited and recorded an undo step for an equal depth at `src/core/ControlCommandsModulatorRoutes.cpp:302`; its no-op return now precedes the edit and `recordDepth`. The sibling `rate_set` and `voice_set` handlers did the same at `src/core/ControlCommandsModulator.cpp:367` and `src/core/ControlCommandsModulatorVoices.cpp:122`; they now return before `editModulationLayer`/`addUndoStep` when their writable fields match. The tests at `tests/src/core/ControlModulatorCommandsTest.cpp:291`, `:308`, and `:323` put a real edit immediately beneath the no-op and show that one undo reaches that real edit.

RED, before fixes (each command from `build/tests`, each EXIT=1):

```text
QT_QPA_PLATFORM=offscreen ./ControlModulatorCommandsTest identicalDepthSetDoesNotShadowUndo
QT_QPA_PLATFORM=offscreen ./ControlModulatorCommandsTest identicalRateSetDoesNotShadowUndo
QT_QPA_PLATFORM=offscreen ./ControlModulatorCommandsTest identicalVoiceSetDoesNotShadowUndo
```

GREEN (same commands from `build/tests`, each EXIT=0): the three methods above passed after the fix. For step 4b, this product-only patch reversal sequence EXIT=0:

```text
git diff -- src/core/ControlCommandsModulatorRoutes.cpp src/core/ControlCommandsModulator.cpp src/core/ControlCommandsModulatorVoices.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-1.r1/logs/fix.patch
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-1.r1/logs/fix.patch
```

With that patch reversed, `cmake --build build --target zene ControlModulatorCommandsTest` EXIT=0 and each of the three RED commands above EXIT=1. Restoring with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-1.r1/logs/fix.patch` EXIT=0, rebuilding with the same build command EXIT=0, and rerunning each command EXIT=0 completed the proof. Evidence logs: `proof-red-new-p18-1-{depth,rate,voice}-1.log` and `proof-green-new-p18-1-{depth,rate,voice}-1.log`.

201: FIXED

Decision: REAL. `modulator.target_set` documents depth as -1..1 but previously let `modulationRouteFromArgs` clamp an out-of-range input, including 5, to an endpoint. The handler now checks the original double at `src/core/ControlCommandsModulatorRoutes.cpp:225` before constructing a route; the matching `depth_set` already refused out-of-range values. The parallel `instrument_target_set` path now performs the same check at `src/core/ControlCommandsModulatorVoices.cpp:215`. The test at `tests/src/core/ControlModulatorCommandsTest.cpp:342` checks +5, -5, infinity, both inclusive endpoints, and confirms a refusal creates no route/runtime entry. The instrument-path assertion checks the depth error itself, before parameter resolution.

RED, before fixes (from `build/tests`, EXIT=1):

```text
QT_QPA_PLATFORM=offscreen ./ControlModulatorCommandsTest targetSetEnforcesDocumentedDepthRange
```

GREEN (same command from `build/tests`, EXIT=0). For step 4b, the product-only patch reversal sequence EXIT=0:

```text
git diff -- src/core/ControlCommandsModulatorRoutes.cpp src/core/ControlCommandsModulatorVoices.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-1.r1/logs/fix.patch
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-1.r1/logs/fix.patch
```

With the patch reversed, `cmake --build build --target zene ControlModulatorCommandsTest` EXIT=0 and the RED command EXIT=1 at the instrument depth assertion. Restoring with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-1.r1/logs/fix.patch` EXIT=0, rebuilding with the same build command EXIT=0, and rerunning the command EXIT=0 completed the proof. The method also fails against the original rack handler at the +5 assertion. Evidence logs: `proof-red-201-final.log` and `proof-green-201-final.log`.

4802: FIXED

Decision: REAL. The prior `modulator.target_remove` transaction inverse at `src/core/ControlCommandsModulatorRoutes.cpp:358` named `modulator.target_set` with only a modulator ID, while that command requires a route address. The inverse carries the captured address, depth, `per_sample`, and original route index; rack and instrument target setters accept an optional insertion index so replay restores route ordering as well. The regression at `tests/src/core/ControlModulatorCommandsTest.cpp:383` removes the first of two rack routes, replays the reported inverse through the registry, and compares both route order and captured fields. Independent review initially caught that replay appended the removed route (RED: `review-test-red-index.log`, EXIT=8); the indexed inverse and bind path then passed the same regression.

RED, before fixes (from `build/tests`, EXIT=1):

```text
QT_QPA_PLATFORM=offscreen ./ControlModulatorCommandsTest targetRemoveInverseRecreatesCapturedRoute
```

GREEN (same command from `build/tests`, EXIT=0). For step 4b, the product-only patch reversal sequence EXIT=0:

```text
git diff -- src/core/ControlCommandsModulatorRoutes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-1.r1/logs/fix.patch
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-1.r1/logs/fix.patch
```

With that patch reversed, `cmake --build build --target zene ControlModulatorCommandsTest` EXIT=0 and the RED command EXIT=1 (`args: missing required property 'channel'`). Restoring with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-1.r1/logs/fix.patch` EXIT=0, rebuilding with the same build command EXIT=0, and rerunning the command EXIT=0 completed the proof. Evidence logs: `proof-red-4802-1.log` and `proof-green-4802-1.log`.

## Test matrix and final verification

The deterministic QtTest methods use the real registry, rack fixture and ProjectJournal synchronously; they use no sleeps, wall-clock values or races. Valid writes, no-op undo ordering, invalid depths, inclusive bounds, unresolved instrument parameters, route removal/replay, and existing command refusals are covered. Production edits remain on the control/UI command path; no audio-thread code or persisted project format changed. Built `zene` and both related test binaries on the configured C++20/Qt build.

The first `feature_check.py` run returned 1 because the unchanged derived `tests/all-sources.txt` contained 1,148 vendored sfizz entries excluded by its embedded recipe. I regenerated only the entry list from that recipe, preserving its header/exclusions; `bash tests/all-sources-reproduce.sh` then returned 0. No threshold or gate baseline was changed.

| Check | PASS/FAIL | Evidence |
|---|---|---|
| new-p18-1 RED/GREEN and 4b | PASS | Three targeted methods each RED=1 with reversed patch and GREEN=0 restored; `proof-red-*` / `proof-green-*` logs. |
| 201 RED/GREEN and 4b | PASS | `targetSetEnforcesDocumentedDepthRange`: RED=1 reversed, GREEN=0 restored; `proof-red-201-final.log` / `proof-green-201-final.log`. |
| 4802 RED/GREEN and 4b | PASS | `targetRemoveInverseRecreatesCapturedRoute`: RED=1 reversed, GREEN=0 restored; `proof-red-4802-1.log` / `proof-green-4802-1.log`. |
| Affected ctest family | PASS | `ctest -R '^(ControlModulatorCommandsTest|InstrumentLayersCommandsTest|ModulatorToolsTest)$' --output-on-failure`, from `build/tests`: EXIT=0, 3/3 tests passed (`test-5-final.log`). |
| fork-sources | PASS | `feature_check.py`: EXIT=0. |
| all-sources-reproduce | PASS | `bash tests/all-sources-reproduce.sh`: EXIT=0; feature gate EXIT=0. |
| no-upstream-regression | PASS | `feature_check.py`: EXIT=0. |
| check-namespace | PASS | `feature_check.py`: EXIT=0. |
| no-tautology | PASS | `feature_check.py`: EXIT=0. |
| doc-figures | PASS | `feature_check.py`: EXIT=0. |
| complexity | PASS | No new item in changed files; `feature_check.py`: EXIT=0. |
| file-length | PASS | No new item in changed files; `feature_check.py`: EXIT=0. |
| feature_check.py | PASS | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-1`: EXIT=0 (`gate-3-final.log`). |

## Self-check

- Reverted-patch proof: each regression above has a separate RED command (EXIT=1), restored GREEN command (EXIT=0), and a rebuild of `zene` plus `ControlModulatorCommandsTest` (EXIT=0); the instrument test binary was also built and passed in the final family run.
- Determinism: tests invoke commands and undo synchronously; no timestamps, sleeps, races or scheduling assumptions are used.
- Cause and siblings: `modulator.depth_set` / `rate_set` / `voice_set` skip unchanged writes; `target_set` / `instrument_target_set` enforce the same depth range; `target_remove` produces matching rack and instrument inverses. `modulator.formula_set` and `trigger_set` already share `editModulator`, whose equality return is at `src/core/ControlCommandsModulatorTools.cpp:120`; `sameSource` includes the formula program and trigger track at `:90`. `create` and `remove` change collection membership, and `get_state` is read-only, so they have no idempotent value-write path. Invalid `target_remove` indices remain `NotFound`; duplicate target binds remain refused.
- Collateral behavior: stored depth stays in -1..1, endpoints remain valid, route ordering and captured `per_sample` are preserved by undo and replayed inverses, and the project serialization format is unchanged. No audio-thread function was changed.
- Decisions: all three findings were REAL, with current source locations and the original failing assertions recorded above.
- Sibling command audit: `modulator.create` (append), `modulator.remove` (remove), and `modulator.get_state` (read-only) are unaffected by equal-value write semantics; `depth_set`, `rate_set`, and `voice_set` now skip equal values; `target_set` and `instrument_target_set` reject non-finite/out-of-range depth; `target_remove` captures the matching inverse; `formula_set` and `trigger_set` already return through `editModulator`'s equality guard. `ModulatorToolsTest` passed in the final family run.
- Files committed: `include/ControlModulationSupport.h`, `src/core/ControlCommandsModulator.cpp`, `src/core/ControlCommandsModulatorRoutes.cpp`, `src/core/ControlCommandsModulatorVoices.cpp`, `tests/src/core/ControlModulatorCommandsTest.cpp`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`, and this report.
