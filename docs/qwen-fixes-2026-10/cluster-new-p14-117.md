FIXED

# Cluster new-p14-117: the input meter drops a sample when a reader clears it in the raise's window

## Verdict

**REAL.** The finding's mechanism is exactly what the code does.

`src/core/InputMeter.cpp:52-66` (the function the finding points at - the line numbers moved, the
`raise()` helper is at 52, the stale observation at 54) raised a held peak like this before the fix:

```cpp
float current = held.load(std::memory_order_relaxed);
while (value > current && !held.compare_exchange_weak(current, value, std::memory_order_relaxed)) { }
```

The decision to skip the sample is taken against `current`, a value loaded **before** the
compare/exchange. A reader (`take()` at `src/core/InputMeter.cpp:93-98`, `live()` at
`src/core/InputMeter.cpp:100-105`) clears the cell with an `exchange(0.f)` - so if the clear lands
between the load and the check, the check still compares against the value that no longer exists and
the sample is skipped. The sample is then in **no** reading at all: the reader that cleared took the
older, larger peak, and the cell is empty, so the next read reports silence. The finding's scenario
(the held peak is 0.8, the audio thread measures 0.5, a reader clears in between) loses the 0.5
sample completely.

## What this round changed

1. **The raise is now atomic with respect to a reader's clear** (`src/core/InputMeter.cpp:58-65`).
   The sample is only skipped when the cell *still* holds what was observed: the loop always attempts
   the compare/exchange, with the desired value `max(sample, observed)`. When nothing has changed,
   the exchange writes the same value back (a no-op compare/exchange) and succeeds. When a reader
   cleared the cell in between, the compare/exchange **fails** (the cell no longer holds the observed
   value), `current` is refreshed with the cell's real value, and the sample is raised into the
   cleared cell instead of being dropped with it. The raise is therefore ordered either wholly
   before the reader's clear (the reader gets the sample) or wholly after it (the cell keeps the
   sample for the next read) - the two are linearizable without a generation counter, a handoff slot
   or a lock.
2. **A deterministic test seam at the raise's observation point** (`include/InputMeter.h:55-68`,
   `src/core/InputMeter.cpp:46-50,55-57,70-75`). The seam is a hook that is called after the audio
   thread observed the held peak and before it commits the raise; a test installs it to clear the
   cell exactly there - the one instant a reader can land in. It is compiled out of the shipped
   build (`#ifdef LMMS_TESTING`, the same pattern as `include/CPULoadWidget.h:50`) and it is null in
   production, so the shipped audio path is unchanged.
3. **The regression test** (`tests/src/core/InputMeterTest.cpp:129-145`, helper at 57-60):
   `aClearInTheObservationWindowDoesNotSwallowTheSample` measures 0.8, installs the reader at the
   observation point, measures 0.5, and then reads the meter back: the 0.5 must still be there
   (`take()` and `live()` both report 0.5). Without the fix the read back is 0 - the sample is gone.

## Evidence

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-117.r1/logs`. CTest ran from
`build/tests`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED on the original product code (seam + test only, the stale check still in place) | `ctest -R '^InputMeterTest$' --output-on-failure` | **8** | 6 passed / 1 failed: `aClearInTheObservationWindowDoesNotSwallowTheSample`: actual `0`, expected `0.5` (`test-1-red.log`) |
| Build for that RED run | `cmake --build build --target zene InputMeterTest` | **0** | `build-1.log` |
| GREEN with the fix | `ctest -R '^InputMeterTest$' --output-on-failure` | **0** | 1/1 test target passed, 7 test functions (`test-2-green.log`) |
| Build for the GREEN run | `cmake --build build --target zene InputMeterTest` | **0** | `build-2.log` |
| Inverse proof: isolate and revert the product fix | `git diff -- src/core/InputMeter.cpp include/InputMeter.h > .../logs/fix.patch && git apply -R .../logs/fix.patch` | **0** | `fix.patch` holds **only** the fix hunk (the seam is already staged, so `git diff` compares the working tree against the index); `proof-1-invert.log` |
| Inverse build | `cmake --build build --target zene InputMeterTest` | **0** | `build-3-inverse.log` |
| Inverse RED | `ctest -R '^InputMeterTest$' --output-on-failure` | **8** | 6 passed / 1 failed, same assertion (actual `0`, expected `0.5`) - the test catches the bug (`test-3-inverse-red.log`) |
| Restore the product fix | `git apply .../logs/fix.patch` | **0** | `proof-2-restore.log` |
| Restored build | `cmake --build build --target zene InputMeterTest` | **0** | `build-4-restored.log` |
| Restored GREEN | `ctest -R '^InputMeterTest$' --output-on-failure` | **0** | 1/1 (`test-4-restored-green.log`) |
| Affected family (meter and input tests) | `ctest -R '^(InputMeterTest\|MeterTapTest\|LufsMeterTest\|InputMonitorTest\|RecordingInputPathTest\|InputChannelRingTest)$' --output-on-failure` | **0** | **6/6** test targets passed (`test-5-family.log`) |
| The `meter.input` command's own socket test | `ctest -R '^ControlMeterCommands$' --output-on-failure` | **0** | 1/1 passed (`test-7-controlmeter.log`); the first run failed **8** only because the tree had no loadable `audiofileprocessor` fixture module (`test-6-controlmeter.log`) - building that fixture (`cmake --build build --target audiofileprocessor`, exit **0**, `build-6-fixture.log`) made it pass |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-117` | **0** | PASS table, below (`gate-1.log`) |

Feature-check table (`gate-1.log`):

| Gate | Result |
|---|---|
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS - no new item in your files |
| file-length | PASS - no new item in your files |
| **RESULT** | **PASS (exit 0)** |

## Self-check

**Q: The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
RED `ctest -R '^InputMeterTest$' --output-on-failure` from `build/tests` = **exit 8** (6 passed /
1 failed; `aClearInTheObservationWindowDoesNotSwallowTheSample`: actual `0`, expected `0.5`) with
the product fix reverted - `test-3-inverse-red.log`. GREEN with the fix = **exit 0** (1/1 target, 7
test functions) - `test-4-restored-green.log`. The revert was done with the mandated command:
`git diff -- src/core/InputMeter.cpp include/InputMeter.h > .../logs/fix.patch && git apply -R
.../logs/fix.patch` (exit **0**, `proof-1-invert.log`; the patch itself is in `fix.patch`), and
restored with `git apply .../logs/fix.patch` (exit **0**, `proof-2-restore.log`). The patch contains
only the fix hunk because the test seam was **staged** before the fix was written, so `git diff`
(working tree against the index) sees the fix alone. The same test was also run RED on the original
code before the fix was written (`test-1-red.log`, exit **8**).

**Q: The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
values it uses).**
Deterministic. The seam is `inputmeter::detail::setPeakObservationHook`
(`include/InputMeter.h:55-68`), the raise's observation point
(`src/core/InputMeter.cpp:54-57`): the hook is called with the cell the audio thread observed, and
the test's hook (`tests/src/core/InputMeterTest.cpp:57-60`) clears that cell - the reader lands in
exactly the window the finding describes, with no thread, no sleep, no timer and no polling
anywhere in the test. The injected values are the two periods' peaks, 0.8 and 0.5
(`tests/src/core/InputMeterTest.cpp:131-135`). The test is single-threaded: the reader is the seam's
callback, not a racing thread.

**Q: The fix is at the cause and complete: every caller and sibling of what you changed was checked
and stays consistent.**
The cause is the one helper that owns every held peak: `raise()` (`src/core/InputMeter.cpp:52-66`).
It is called for all four cells - `s_left`, `s_right`, `s_liveLeft`, `s_liveRight`
(`src/core/InputMeter.cpp:87-90`) - so the `meter.input` pair and the live meter's pair are fixed by
the same change; there is no second copy of the pattern to keep in step. The three public entry
points were checked and are unchanged: `measure()` is the audio thread's half, called once per
period from `AudioEngine::pushInputFrames` (`src/core/AudioEngine.cpp:992`); `take()` is read by the
`meter.input` command (`src/core/ControlCommandsMixerTools.cpp:392`); `live()` is read by the
meters' 40 ms poll (`src/gui/InputLevelMeter.cpp:89`). The readers still clear with
`exchange(0.f)` - the fix changes no reader. Siblings of the changed value: the cell is a
`std::atomic<float>` with the lock-free `static_assert` still in place
(`src/core/InputMeter.cpp:38-39`); the wider type the value flows into is `float` throughout
(`left`/`right` in `measure()`, `take()`, `live()`), unchanged. The header change
(`include/InputMeter.h`) is a test-only declaration inside `#ifdef LMMS_TESTING`; every target that
includes it was rebuilt (`zene`, `InputMeterTest`, and the family targets) and the `zene` app links
cleanly.

**Q: No collateral damage: old projects and saved data still load as before, and any limit or check
you added does not hit other callers that legitimately exceed it.**
No project, save, schema, undo or command surface is touched: the change is inside the meter's own
peak arithmetic, plus a test-only seam. No limit, threshold or check was added, so no caller can
trip one. Behaviour outside the race is bit-for-bit the same: with no reader racing, the old code
stored the sample when it was larger and left the cell alone when it was not; the new code does the
same through a compare/exchange that writes back the identical value (the held peak, the
`theLiveReadingIsTheMostSinceTheLastRead`, `theLiveReadAndTheTakeAreIndependent`,
`aPeriodWithNoInputReadsSilence` and `measuringAllocatesNothing` cases all still pass unchanged).
The only observable difference is the race itself: a sample that a reader's clear used to swallow is
now raised into the cleared cell, so it is reported by the next read instead of being lost. The
readers' own values are unchanged - a reader still receives exactly the peak that was held when it
read.

**Q: If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
contract that shows it.**
Not applicable - the verdict is FIXED (the finding was real; the evidence lines above).

**Q: Realtime, undo and commands.**
The audio thread's path is unchanged in kind: `raise()` still does relaxed atomic loads and
compare/exchanges on lock-free `std::atomic<float>` (`static_assert` at
`src/core/InputMeter.cpp:38-39`), with no allocation, no free, no mutex, no wait and no logging; the
existing allocation probe in `measuringAllocatesNothing`
(`tests/src/core/InputMeterTest.cpp:150-170`) still reports 0 allocations over 64 measured periods.
The seam is a null-pointer check on the same path and is compiled out of the shipped build. Undo is
untouched - the meter has no undo step and none was added. No new control command was added (the
`ControlCommandsSnapshot` canary is unmoved); `meter.input` keeps its exact contract.

**Q: Registration and file ratchets.**
No new file: `include/InputMeter.h`, `src/core/InputMeter.cpp` and `tests/src/core/InputMeterTest.cpp`
are already listed in `tests/fork-sources.txt` (lines 872, 1713, 2350), and all three are fork files,
so `tests/upstream-modifications.txt` needs no entry. The feature gate reports "no new item in your
files" for both the complexity and the file-length ratchets (`gate-1.log`).

## Files committed

- `src/core/InputMeter.cpp` - the fix (the raise is atomic with respect to a reader's clear) and
  the test seam.
- `include/InputMeter.h` - the test seam's declaration (test builds only).
- `tests/src/core/InputMeterTest.cpp` - the deterministic regression.
- `docs/qwen-fixes-2026-10/cluster-new-p14-117.md` - this report.
