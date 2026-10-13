# Cluster new-p12-86

Verdict: **FIXED**

## Evidence

| File and line | Evidence |
| --- | --- |
| `plugins/FrequencyShifter/FrequencyShifterEffect.cpp:102,254-256` | Route Mode 2 is Mute. Its dry coefficient is now zero, while the wet shifter/delay path remains scaled by Mix. |
| `plugins/FrequencyShifter/FrequencyShifterControlDialog.cpp:227-228` | The Mute route description says the input is not sent to output and only the delay line is heard. |
| `tests/src/core/DeviceDspTest.cpp:223-234` | Regression loads the real frequency shifter, selects Mute, sets Mix to 0.5, and checks both channels remain silent when the delay path has no signal. |
| `tests/upstream-modifications.txt:631` | The inherited production file has the new-p12-86 behavior reason. `DeviceDspTest.cpp` was already registered in `tests/fork-sources.txt:1942`. |

| Evidence item | Exact command or committed paths | Result |
| --- | --- | --- |
| RED | `QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/test-1.log 2>&1; echo EXIT=$?` (run from `build/tests`) | EXIT=8; dry output was 0.4. |
| GREEN | `QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/test-4.log 2>&1; echo EXIT=$?` (run from `build/tests`) | EXIT=0; 1/1 ctest passed. |
| Inverse-patch RED | `QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/test-3.log 2>&1; echo EXIT=$?` (run from `build/tests`) | EXIT=8; the same assertion again measured 0.4. |
| Affected-family test count | `ctest -R '^DeviceDspTest$' --output-on-failure` (from `build/tests`) | One registered ctest executed; final result 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | EXIT=0. |
| Files committed | `plugins/FrequencyShifter/FrequencyShifterEffect.cpp`; `tests/src/core/DeviceDspTest.cpp`; `tests/upstream-modifications.txt`; `docs/qwen-fixes-2026-10/cluster-new-p12-86.md` | Commit `b1eab6c4a`; the report is updated in a follow-up commit. |

On the original code, the regression failed with left output `0.4` from input `0.8`. Removing the production patch after the first green run reproduced the same failure. Reapplying it made the test pass.

The affected ctest family is `DeviceDspTest`: one registered ctest was selected and executed. The final run passed 1/1. The red runs reported the regression assertion failure; eight unrelated optional-module cases were skipped by that test build.

`fff grep` was not available in the exposed tools, so the numbered logs were checked with targeted `rg` searches.

## Commands and exit codes

All build, ctest, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/`.

Initial red run, with the fix absent:

```text
cmake --build build --target zene frequencyshifter DeviceDspTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/build-1.log 2>&1; echo EXIT=$?  # EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/test-1.log 2>&1; echo EXIT=$?  # from build/tests; EXIT=8
```

First green run after the fix:

```text
cmake --build build --target zene frequencyshifter DeviceDspTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/build-2.log 2>&1; echo EXIT=$?  # EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/test-2.log 2>&1; echo EXIT=$?  # from build/tests; EXIT=0
```

Inverse-patch proof and final green run:

```text
git diff -- plugins/FrequencyShifter/FrequencyShifterEffect.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/fix.patch; echo EXIT=$?  # EXIT=0
cmake --build build --target zene frequencyshifter DeviceDspTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/build-3.log 2>&1; echo EXIT=$?  # EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/test-3.log 2>&1; echo EXIT=$?  # from build/tests; EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/fix.patch; echo EXIT=$?  # EXIT=0
cmake --build build --target zene frequencyshifter DeviceDspTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/build-4.log 2>&1; echo EXIT=$?  # EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/test-4.log 2>&1; echo EXIT=$?  # from build/tests; EXIT=0
```

Required ratchet gates:

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/gate-1.log 2>&1; echo EXIT=$?  # EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-86.r1/logs/gate-2.log 2>&1; echo EXIT=$?  # EXIT=0
```

## Files committed

- `plugins/FrequencyShifter/FrequencyShifterEffect.cpp`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-86.md`
