# Cluster new-p12-206 — a later recording inherits the previous arming's pass count

Verdict: **FIXED**

## Finding

`SampleClip` counts, per arming, how many recording passes already became takes on
take lanes (`m_recordPasses`). `armRecordTake()` started a new arming (bumped
`m_armGeneration`, cleared the playing latch) but never reset that count, so a clip
armed again after a loop pass carried the old arming's passes into the new arming.

`acceptRecordedTake()` sends a take to a lane when `continues || m_recordPasses > 0`.
With a stale nonzero count the first take of the new arming was added as another lane
take instead of becoming the clip's own content (`setSampleBuffer`), so the recording
the user just made was invisible as clip content.

The stale count survives whenever the old arming's last take is reaped after the
re-arm (the reaper runs on a 10 ms timer, `SampleRecordHandle::reapRetiredTakes`),
because such a take is dropped early at the generation check and never resets the count
— the same race window the existing regression `reArmedClipIsNotDisarmedByAnEarlierTake`
covers from the other side.

## Evidence (file:line)

- `src/core/SampleClip.cpp:210` `SampleClip::armRecordTake()` — the arm branch
  (`++m_armGeneration`, playing latch, journal step) with no `m_recordPasses` reset (bug).
- `src/core/SampleClip.cpp:220` — after the fix, the new reset.
- `src/core/SampleClip.cpp:296` `SampleClip::acceptRecordedTake()` — `if (continues || m_recordPasses > 0)`
  is the lane/content decision the stale count steers.
- `src/core/SampleClip.cpp:288` — the generation check that drops the earlier arming's
  take before it can reset the count.
- `include/SampleClip.h:249` — `int m_recordPasses = 0;` ("Recording passes of the
  current arming that became takes on the lanes").
- Regression: `tests/src/core/RecordingRealtimeTest.cpp:714`
  `reArmingAfterALoopPassTakesItsFirstPassOverTheClip()`.

## Fix

One statement, main thread only (the record model's `dataChanged` reaches
`armRecordTake()` queued, never on the audio thread), no new allocation, lock or call:

```cpp
if (isRecord())
{
    ++m_armGeneration;
    // The pass count belongs to the arming it counted ...
    m_recordPasses = 0;
    ...
```

Nothing else changed: the loop-record path of one arming is untouched (`m_recordPasses`
still grows per pass inside that arming and still gates lane-vs-content), and the
header, the signals and the serialisation are unchanged.

## Commands and exit codes

RED (regression on the unfixed tree):

```
$ cmake --build build --target RecordingRealtimeTest > logs/build-red-1.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^RecordingRealtimeTest$' --output-on-failure > logs/test-red-1.log 2>&1
EXIT=8
```

```
FAIL!  : RecordingRealtimeTest::reArmingAfterALoopPassTakesItsFirstPassOverTheClip()
   Actual   (clip->sample().sampleSize()) : 0
   Expected (static_cast<std::size_t>(96)): 96
   Loc: [../../tests/src/core/RecordingRealtimeTest.cpp(749)]
```

GREEN after the fix:

```
$ cmake --build build --target RecordingRealtimeTest > logs/build-green-1.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^RecordingRealtimeTest$' --output-on-failure > logs/test-green-1.log 2>&1
EXIT=0
```

Affected family (record / take-lane / clip), all after `cmake --build build --target zene
...RecordingRealtimeTest RecordingLatencyTest TakeLaneTest TakeLaneCompTest
TakeLaneCompPlaybackTest SampleClipRubberBandTest SampleClipStretchTest SampleClipWarpTest
SampleClipWindowTest ControlSessionRecordLandTest ClipEditsMenuTest` (`BUILD_EXIT=0`):

```
$ cd build/tests && ctest -R '^(RecordingRealtimeTest|RecordingLatencyTest|TakeLaneTest|TakeLaneCompTest|TakeLaneCompPlaybackTest|SampleClipRubberBandTest|SampleClipStretchTest|SampleClipWarpTest|SampleClipWindowTest|ControlSessionRecordLandTest|ClipEditsMenuTest)$' --output-on-failure > logs/test-green-2.log 2>&1
EXIT=0
100% tests passed out of 11
```

Test-proof of the bug (fix reverted, then restored):

```
$ git diff -- src/core/SampleClip.cpp > logs/fix.patch && git apply -R logs/fix.patch
REVERT_OK
$ cmake --build build --target RecordingRealtimeTest > logs/build-rev-3.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^RecordingRealtimeTest$' --output-on-failure > logs/test-rev-3.log 2>&1
TEST_EXIT=8      # same assertion as RED: Actual 0, Expected 96
$ cd .. && cd .. && git apply logs/fix.patch
REAPPLY_OK
$ cmake --build build --target RecordingRealtimeTest > logs/build-green-4.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^RecordingRealtimeTest$' --output-on-failure > logs/test-green-4.log 2>&1
TEST_EXIT=0
```

## Ratchet gates — both FAIL, pre-existing, not from this change

```
$ bash tests/complexity-gate.sh --check > logs/gate-complexity-1.log 2>&1
COMPLEXITY_EXIT=1
$ bash tests/file-length-gate.sh --check > logs/gate-filelength-1.log 2>&1
FILELENGTH_EXIT=1
```

Both were already failing at this branch's HEAD before the fix. Proof: the same two
commands on a pristine `git archive HEAD` extraction (`/tmp/opencode/p12-206-head`,
nothing of this worktree touched) give the same exit codes and byte-identical
regression lists:

```
$ bash tests/complexity-gate.sh --check > logs/gate-complexity-head.log 2>&1
HEAD_COMPLEXITY_EXIT=1
$ bash tests/file-length-gate.sh --check > logs/gate-filelength-head.log 2>&1
HEAD_FILELENGTH_EXIT=1
COMPLEXITY REGRESSIONS IDENTICAL TO HEAD   (6 lines, e.g. XyPadPairTest.cpp, DawProjectReadTracks.cpp)
FILELENGTH REGRESSIONS IDENTICAL TO HEAD   (14 lines, e.g. DeviceDspTest.cpp, VideoTrackTest.cpp)
```

Neither gate log mentions `src/core/SampleClip.cpp` or
`tests/src/core/RecordingRealtimeTest.cpp`; the fix adds no branch, no function and
three lines to each file, all below the 500-line ratchet for a *new* file (both are
grandfathered inherited entries). The regressions are unrelated files merged onto the
train (take-lane, session-record, device-catalog tests); fixing them is out of scope for
this one bug and would change untouched behaviour. No gate limit or baseline was
re-anchored.

## Files

- `src/core/SampleClip.cpp` — the reset (product, inherited, declared in
  `tests/upstream-modifications.txt`).
- `tests/src/core/RecordingRealtimeTest.cpp` — the regression (inherited, declared in
  `tests/upstream-modifications.txt`).
- `tests/upstream-modifications.txt` — one declaration row per inherited file changed.
- `docs/qwen-fixes-2026-10/cluster-new-p12-206.md` — this report.