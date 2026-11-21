FIXED
Decision: REAL. Cluster new-p10-350, branch bugs/qwen-new-p10-350.
Typed times whose frame count overflows qint64 were converted with an unchecked llround and accepted.

## Evidence for REAL

- Before the fix, `frameOfSeconds` (src/gui/SampleRangeFields.cpp:59-62 at HEAD) returned `std::llround(seconds * rate)` with no finiteness or range check. llround of an out-of-range double is undefined.
- Before the fix, `timeFrom` (src/gui/SampleRangeFields.cpp:116-128 at HEAD) called `toDouble()` without ok-flags and passed the total straight to `frameOfSeconds`.
- Contract: include/SampleRangeFields.h:69-70 says `frameFromText` returns false for text that is none of the accepted forms. A 40-digit clock is not a representable frame, so it must be refused.
- Red run on the unfixed product: test-1-red.log:80 reads `FAIL! hugeTypedTimesAreRejected() '!samplerange::frameFromText(QString(40, '9'), ...)' returned FALSE`, which means the parser accepted it. The failing assertion is tests/src/gui/SampleEditorDialogTest.cpp:315. Totals: 27 passed, 1 failed (test-1-red.log:122).

## Fix

- `#include <limits>` added (src/gui/SampleRangeFields.cpp:29).
- New `checkedFrameOfSeconds` (src/gui/SampleRangeFields.cpp:66-77). It returns false unless the frame count is finite, >= 0 and < double(qint64 max). The bound is strict because double(LLONG_MAX) rounds up to 2^63. Otherwise it runs llround into `*frame`.
- `timeFrom` (src/gui/SampleRangeFields.cpp:131-149):
  - The hours, minutes and seconds `toDouble` calls now take ok-flags, and any failed parse returns false (:145).
  - An absent minutes group is 0, not a parse failure.
  - The total goes through `checkedFrameOfSeconds` (:148).
- `frameOfSeconds` (:60) is untouched.
- Product registration already exists: tests/fork-sources.txt:2389 lists src/gui/SampleRangeFields.cpp. No change was needed there.

## Completeness

- `framesFrom` (src/gui/SampleRangeFields.cpp:121-128) already checks `toLongLong` with ok.
- `barsFrom` (:152) and `stepFrames` (:235) only see tick_t values. The MusicalTime parser rejects values above MaxTick (src/core/MusicalTime.cpp:196 and :205), so they never reach a float-to-int conversion with unbounded input.
- Callers of `frameOfSeconds`: only the tick path at src/gui/SampleRangeFields.cpp:97. Its input is bounded as above, and the function itself is not changed.
- Callers of `frameFromText`: the dialog's value and validator (src/gui/SampleRangeFields.cpp:274, :281), and the unit dispatch at :219-232.
- No control command, undo, saved-data or audio-thread code is changed.

## Honest note on the UI path

The dialog's UI range check masks the garbage frame, so the dialog-level assertions could pass without the fix. The RED evidence is therefore the parser assertions at lines 315-317. The dialog-level assertions (lines 321-325) were not reached in the red run, so they are not evidence of the red.

## Test matrix (tests/src/gui/SampleEditorDialogTest.cpp:309-326, slot `hugeTypedTimesAreRejected`)

| Input (fixed strings) | Expected | Line |
|---|---|---|
| 40 x '9' | refused | 315 |
| `999999999999999999999:00` | refused | 316 |
| `999999999999999999999:00:00` | refused | 317 |
| `999999999:00:00` | accepted, frame > 0 | 319 |
| 40 x '9' typed into "Range start" | range start stays at 4410 | 322-323 |
| `0:00.250` typed into "Range start" | range start becomes Rate/4 | 324-325 |

## Commands and exit codes

Exit codes are cited only where a log shows them. The rerun below (run r1 logs, 2026-11-21) appends `EXIT=$?` to each log, so the code shown is the last line of that log. Log paths are under the run's `logs/` folder, outside the worktree.

| Step | Command | Log | Evidence | EXIT (last line of log) |
|---|---|---|---|---|
| apply-reverse | `git apply -R fix.patch` | apply-reverse.log (line 2) | `src/gui/SampleRangeFields.cpp` 1 file, 5 insertions, 26 deletions | EXIT=0 |
| build-5-reversed | `cmake --build build --target zene SampleEditorDialogTest` (reversed) | build-5-reversed.log | Built target SampleEditorDialogTest | EXIT=0 |
| test-5-reversed | `ctest -R SampleEditorDialogTest` (reversed) | test-5-reversed.log | FAIL at line 80 (`hugeTypedTimesAreRejected`); "The following tests FAILED" at line 130 | EXIT=8 |
| apply-restore | `git apply fix.patch` | apply-restore.log | patch reapplied | EXIT=0 |
| build-6-restored | build (restored) | build-6-restored.log | Built target SampleEditorDialogTest | EXIT=0 |
| test-6-restored | `ctest -R SampleEditorDialogTest` (restored) | test-6-restored.log | 100% tests passed, 1/1 | EXIT=0 |
| test-6-detail | `./SampleEditorDialogTest` (restored) | test-6-detail.log | Totals: 28 passed, 0 failed | EXIT=0 |
| test-6-family | `ctest -R 'SampleEditor\|LaneReadout'` | test-6-family.log | 100% tests passed out of 3 | EXIT=0 |
| gates-2 | `feature_check.py` | gates-2.log | RESULT: PASS | EXIT=0 |
| gui-2 | `control-shell.py` (offscreen) | gui-2.log | PASS: shell control-surface driver | EXIT=0 |

The pre-fix RED first seen in test-1-red.log:80 is the same failure that the recorded reversal reproduces (test-5-reversed.log:80, EXIT=8).

Screenshot: gui-shots/shell-arrange.png (kept in the log folder, outside the worktree). It is PNG 1600x1000 (gui-2-file.log, EXIT=0), sha256 a67d15648608d49a683c771215ca96e472cbb36838a2b20b39012bdd10b3657b (gui-2.sha256). The earlier gui-1 hash 0f76075e… is superseded.

## Self-check

- **Reversal:** RED on the reverted product: test-5-reversed.log:80 (FAIL line), EXIT=8. GREEN after restore: test-6-restored.log (100%, 1/1, EXIT=0) and test-6-detail.log (28 passed, 0 failed, EXIT=0).
- **Deterministic:** inputs are fixed digit strings. The test uses no clock, sleep, timers, or threads.
- **Cause and completeness:** the cause is the unchecked llround and unchecked toDouble in `timeFrom`. Callers and siblings are covered in "Completeness" above.
- **No collateral damage:** in-range clock text runs the same arithmetic (h*3600 + m*60 + s, times max(1, rate), then llround). The only change is the finiteness and bound check. `999999999:00:00` is about 1.6e17 frames at 44.1 kHz, below qint64 max, so it stays accepted (test line 319).
- **Not ALREADY-FIXED or FALSE-POSITIVE:** the unfixed product fails the new test at test-5-reversed.log:80, EXIT=8. The fix is in this commit.

## Other findings

None.
