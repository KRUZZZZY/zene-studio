# Cluster new-p10-139-port: zero-duration DrumSynth tone envelope

**Verdict: FIXED**

`DrumSynth::GetDSFileSamples()` used the tone envelope's last point time (`envData[1][MAX]`) as the divisor of
the tone phase interpolation and of the droop rate setup. A `.ds` tone whose envelope ends at time zero therefore
divided by zero: the phase became NaN (static tone) or infinity/NaN (droop tone) and reached the `short`
conversion of the output buffer.

The renderer now asks a guarded helper for the phase: a tone without a positive envelope duration keeps its start
frequency `F1` instead of dividing by zero. The droop setup uses a one-sample fallback for its own parameter
calculation and only enables droop for a positive envelope duration, so `F2`/`ddF` stay finite. Behaviour for a
positive envelope duration is byte-identical to before: the same expressions are evaluated with the same values.

This is the same change that was accepted on `bugs/qwen-new-p10-139-port` (commit `fa0fe55c1`); it is re-applied by
hand here because that branch could not be merged into the newest line (`tests/src/core/DrumSynthTest.cpp` moved on
the newer line, so the merge conflicted there).

## File and line evidence

| File | Evidence |
| --- | --- |
| `src/core/DrumSynth.cpp:215-226` | New helpers `durationOrOneSample()` and `tonePhaseAtSample()`: the phase helper returns the start frequency when the envelope duration is not positive. |
| `src/core/DrumSynth.cpp:453-456` | The droop setup divides by the one-sample fallback instead of the zero duration and enables droop only for a positive envelope duration, so `TDroopRate`, `F2` and `ddF` stay finite. |
| `src/core/DrumSynth.cpp:668` | The renderer takes its phase increment from `tonePhaseAtSample()` instead of `F1 + (t / envData[1][MAX]) * ddF`. |
| `src/core/DrumSynth.cpp:48` | `MAX` is the envelope index of the envelope's last point time, the value that is zero for this bug. |
| `tests/src/core/DrumSynthTest.cpp:77-94` | `writeZeroDurationToneFixture()` writes a real `.ds` file whose tone envelope is `0,100` (last point at time zero), with `Droop=0` or `Droop=40`. |
| `tests/src/core/DrumSynthTest.cpp:155-177` | `zeroDurationToneUsesItsStartingFrequency()` renders that fixture and requires the first sample to be the tone at its start frequency `F1` (`10000 * sin(2*pi*440/44100)` = 626). |
| `tests/upstream-modifications.txt:2629` | The inherited product file records the change and the regression that covers it. |

The pre-fix run failed with `Actual (samples.front()): 0` versus `Expected (expected): 626` for both data rows
(`static` and `with-droop`); the NaN phase reached the output conversion and produced a silent sample.

## Verification evidence

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs`.

| Check | Exact command | Exit |
| --- | --- | ---: |
| Build the regression test on the original code | `cmake --build build --target DrumSynthTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED on the original code | `ctest -R '^DrumSynthTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/test-1-red.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Build the fixed code | `cmake --build build --target zene DrumSynthTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN on the fixed code | `ctest -R '^DrumSynthTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/test-2-green.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Build the other tests that cover this file | `cmake --build build --target DrumSynthLevelTest SampleBufferResampleTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Affected family (`DrumSynthTest`, `DrumSynthLevelTest`, `SampleBufferResampleTest`) | `ctest -R '^(DrumSynthTest\|DrumSynthLevelTest\|SampleBufferResampleTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/test-4-family.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Save and reverse only the product patch | `git diff -- src/core/DrumSynth.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Build with the product fix reversed | `cmake --build build --target zene DrumSynthTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/build-4-reversed.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED with the fix reversed | `ctest -R '^DrumSynthTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/test-5-reversed.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Reapply the product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild after reapplying | `cmake --build build --target zene DrumSynthTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/build-5-restored.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN with the fix restored | `ctest -R '^DrumSynthTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/test-6-restored.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Build the .ds import test | `cmake --build build --target ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Wider DrumSynth family including the `.ds` importer | `ctest -R '^(ImportDetectionTest\|DrumSynthTest\|DrumSynthLevelTest\|SampleBufferResampleTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/test-7-family.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Ratchet gate (all gates, baseline compared) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-139-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/gate-feature-check.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate (standalone, red on the train before this change) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/gate-complexity.log 2>&1; rc=$?; echo EXIT=$rc` | 1 |
| File-length gate (standalone, red on the train before this change) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-139-port-port.r1/logs/gate-file-length.log 2>&1; rc=$?; echo EXIT=$rc` | 1 |

The affected CTest family runs **4/4** tests (`DrumSynthLevelTest`, `ImportDetectionTest`, `SampleBufferResampleTest`,
`DrumSynthTest`); the Qt test case itself runs 6 rows (2 pre-existing cases, 2 new data rows, plus the Qt
init/cleanup rows). The regression failed on the original code and with the product patch reversed (exit 8) and
passed with the fix applied (exit 0).

The two standalone ratchet gates exit 1 because of pre-existing train regressions; none of the listed items is in
`src/core/DrumSynth.cpp` or `tests/src/core/DrumSynthTest.cpp` (see `gate-complexity.log` / `gate-file-length.log`:
the items are `src/core/TakeLane.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`,
`src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`,
`src/gui/XyPadPairTest.cpp` and long test files). `feature_check.py` compares those listings against the branch base
and reports `no new item in your files` for both ratchets, so the change adds no new gate item.

## Self-check

- **Does the regression test FAIL with the product change reverted and PASS with it (exit codes)?**
  Yes. Original code: `ctest -R '^DrumSynthTest$'` → exit 8 (`Actual 0`, `Expected 626`, both data rows).
  With `git apply -R fix.patch` and a rebuild: exit 8 again (`test-5-reversed.log`).
  With the patch reapplied and rebuilt: exit 0 (`test-6-restored.log`).
- **Is the test deterministic (no wall-clock timing, sleeps or races)?**
  Yes. The test writes a fixed `.ds` fixture into a `QTemporaryDir` and renders it once through
  `DrumSynth::GetDSFileSamples()`; it reads only the first rendered sample. There is no clock, no sleep, no
  thread and no shared state. The expected value is computed from the fixture's own parameters
  (`Level=100` → amplitude 10000, `F1=440`, `Fs=44100`, `Phase=0`), so it is a pure function of the file contents.
- **Is the fix at the cause and complete (every caller and sibling checked and consistent)?**
  Yes. `envData[1][MAX]` is the only envelope duration used as a divisor in the whole tree
  (`rg -n "envData" -l` finds only `src/core/DrumSynth.cpp`): the droop rate at `src/core/DrumSynth.cpp:455` and the
  tone phase at `src/core/DrumSynth.cpp:668`. Both are now guarded. The other envelope maxima
  (`envData[2][MAX]`, `[3]`, `[4]`, `[5]`, `[6]`) are only compared, never divided by. The sibling path into the
  same value is the droop branch of the same function, which is fixed together with the renderer branch. The
  callers of `GetDSFileSamples()` are `src/core/SampleDecoder.cpp:125` (the `.ds` importer) and the tests; all of
  them go through the single fixed function. For a positive envelope duration the fix evaluates the same
  expressions with the same values, so existing presets render exactly as before.
- **No collateral damage (old projects and saved data still load as before, no limit hit on other callers)?**
  The change only alters the rendering of a `.ds` tone whose envelope's last point is at time zero - a file that
  previously produced NaN samples. No project format, setting, limit or saved data is touched, no new command is
  added, and no parameter range is narrowed. `DrumSynthLevelTest`, `ImportDetectionTest` and
  `SampleBufferResampleTest` (the other tests covering this file) all still pass (4/4).
- **If the verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or contract that shows it.**
  Not applicable - the verdict is FIXED. Before this change the newest line still had the unguarded divisions
  (`git show HEAD:src/core/DrumSynth.cpp` line 440 `TDroopRate * -4.f / envData[1][MAX]` and line 654
  `phi[t - tpos] = F1 + (t / envData[1][MAX]) * ddF`), and the RED run on the original code proves it.

## Files committed

- `src/core/DrumSynth.cpp`
- `tests/src/core/DrumSynthTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-139-port-port.md`
