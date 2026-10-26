FIXED

## Evidence and decision

**REAL.** `m_pauseModel` is initialized to `false` in [OscilloscopeControls.cpp](../../plugins/Oscilloscope/OscilloscopeControls.cpp:39), and [Oscilloscope.cpp](../../plugins/Oscilloscope/Oscilloscope.cpp:65) consults it to decide whether to capture samples. Before this fix, `OscilloscopeControls::loadSettings` and `saveSettings` at lines 47 and 57 handled amp, length, phase, and stereo, but skipped pause. The methods now load and save the `pause` setting at lines 52 and 62.

The project path reaches these methods through [Effect::saveSettings](../../src/core/Effect.cpp:172), which calls the controls' `saveState`, and `Effect::loadSettings` at line 181, which restores the matching controls state. `SerializingObject::saveState` and `restoreState` delegate to the controls' settings methods.

The test-first run failed because the saved controls element lacked `pause`. After the fix, a paused value survives load/save/load, legacy XML without `pause` stays unpaused, and amp and stereo still round-trip. The pause button is bound to this same model in [OscilloscopeControlDialog.cpp](../../plugins/Oscilloscope/OscilloscopeControlDialog.cpp:61).

## Regression matrix

| Path | Coverage |
| --- | --- |
| Normal paused state | `pauseSettingRoundTrips` loads true, saves it, reloads the saved element, and saves it again. |
| Legacy project | `legacySettingsDefaultPauseToFalse` loads a controls element without `pause`; saving emits the existing false default. |
| Sibling settings | Both cases verify amp and stereo are preserved. Length and phase continue through the unchanged existing model calls. |
| Missing or malformed settings | Missing pause is covered as a legacy case. This scalar model persistence path has no buffer exhaustion or capacity reconfiguration state. |
| Audio thread | The fix only adds project settings serialization calls. `Oscilloscope::processImpl` is unchanged; it still reads the model and writes captured samples only when unpaused. |
| Platform/build | The test uses the project’s C++20, Qt DOM, and QtTest setup, with no platform-specific APIs. The configured Linux build was used here; no other platform build was run. |

The regression is in `tests/src/plugins/OscilloscopeControlsPersistenceTest.cpp` and is registered as `OscilloscopeControlsPersistenceTest` in `tests/CMakeLists.txt`. The checked-out configuration had no tests registered under the three historical names `OscilloscopeControls`, `OscilloscopeControlDialog`, or `OscilloscopeGraph`, so the family filter ran the new persistence target: one CTest test, containing the two persistence cases above.

## Build and test evidence

Commands below are copied as run. Build and test output is in the named log under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs`.

| Run | Exact command | Exit and result |
| --- | --- | --- |
| RED build | `cmake --build build --target zene OscilloscopeControlsPersistenceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/build-3-red.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| RED test | `ctest -R '^OscilloscopeControlsPersistenceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/test-3-red.log 2>&1; rc=$?; echo EXIT=$rc` (cwd `build/tests`) | `EXIT=8`; 1 CTest test ran, with the two pause assertions failing because `pause` was absent. |
| GREEN build | `cmake --build build --target zene OscilloscopeControlsPersistenceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/build-4-green.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| GREEN family test | `ctest -R '^(OscilloscopeControlsPersistenceTest|OscilloscopeControls|OscilloscopeControlDialog|OscilloscopeGraph)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/test-4-green.log 2>&1; rc=$?; echo EXIT=$rc` (cwd `build/tests`) | `EXIT=0`; 1 CTest test ran and passed. |

The required fix-reversal proof also reproduced RED and then GREEN:

| Step | Exact command | Exit and result |
| --- | --- | --- |
| Save patch and reverse production fix | `git diff -- plugins/Oscilloscope/OscilloscopeControls.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Rebuild with fix reversed | `cmake --build build --target zene OscilloscopeControlsPersistenceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/build-5-proof-red.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Test with fix reversed | `ctest -R '^OscilloscopeControlsPersistenceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/test-5-proof-red.log 2>&1; rc=$?; echo EXIT=$rc` (cwd `build/tests`) | `EXIT=8`; 1 CTest test ran, and the same two pause assertions failed. |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Rebuild with fix restored | `cmake --build build --target zene OscilloscopeControlsPersistenceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/build-6-proof-green.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Test with fix restored | `ctest -R '^(OscilloscopeControlsPersistenceTest|OscilloscopeControls|OscilloscopeControlDialog|OscilloscopeGraph)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/test-6-proof-green.log 2>&1; rc=$?; echo EXIT=$rc` (cwd `build/tests`) | `EXIT=0`; 1 CTest test ran and passed. |

## Gates

The source manifest includes the new test in both fork and whole-tree scopes. The whole-tree manifest also had stale `plugins/SfzPlayer/sfizz/**` entries despite its own recipe explicitly excluding that vendored tree; those stale entries were removed without changing the recipe or gate thresholds.

| Gate | Exact command | Exit and result |
| --- | --- | --- |
| Feature check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-187 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/gate-6-feature.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; fork-sources, all-sources-reproduce, no-upstream-regression, namespace, no-tautology, doc-figures, complexity, and file-length all PASS. |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-187 new-p23-187 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-187.r1/logs/submit-1.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; commit, clean tree, report, test registration, and feature gates passed. The evidence prompts are answered below. |

## Files in the commit

- `plugins/Oscilloscope/OscilloscopeControls.cpp`
- `tests/src/plugins/OscilloscopeControlsPersistenceTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p23-187.md`

## Self-check

- **Verdict:** REAL; the persisted pause value is read by `Oscilloscope::processImpl`, while the settings methods had omitted it.
- **The regression fails with the product change reverted and passes with it:** the RED test in `test-3-red.log` exited 8; the required reversal run in `test-5-proof-red.log` also exited 8 after a successful build; the restored run in `test-6-proof-green.log` exited 0 after a successful build. Each CTest run executed one test target.
- **The test is deterministic:** `pauseSettingRoundTrips` and `legacySettingsDefaultPauseToFalse` use fixed XML attributes (`pause=1`, `amp=321.5`, and `stereo=1`) and direct DOM comparisons. They use no clock, sleep, thread scheduling, or race.
- **The fix is at the cause and complete:** `OscilloscopeControls::loadSettings` and `saveSettings` own this plugin's persisted controls. `Effect::saveSettings` and `Effect::loadSettings` reach them through controls' state serialization. The pause button and `Oscilloscope::processImpl` use the same `m_pauseModel`. The amp, length, phase, and stereo sibling calls remain in both settings methods; amp and stereo are checked by the regression, and length and phase code is unchanged.
- **No collateral damage:** A legacy element without `pause` restores the constructor's `false` default, as tested. Existing amp and stereo values remain unchanged. No limits, checks, control commands, or changes to audio processing or undo were added.
- **Conditional verdict check:** Not applicable; the verdict is REAL, supported by the missing serialization calls and their failing regression.
- **Registration and gates:** The new test is in both source manifests, and the feature-check table above passes with exit 0.
- **Files committed:** The seven explicit paths listed above are included in the cluster commit.
