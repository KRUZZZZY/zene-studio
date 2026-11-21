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

Exit codes are cited only where a log or a run in this session shows them.

| Step | Command | Log | Evidence | EXIT |
|---|---|---|---|---|
| build-1-red | build SampleEditorDialogTest (unfixed) | build-1-red.log | "Built target SampleEditorDialogTest" | not recorded in log |
| test-1-red | ctest SampleEditorDialogTest (unfixed) | test-1-red.log | 27 passed, 1 failed; FAIL at line 80 | not recorded in log |
| build-2-green | build (fixed) | build-2-green.log | build output | not recorded in log |
| test-2-green | ctest SampleEditorDialogTest (fixed) | test-2-green.log | 100% passed, 1/1 | not recorded in log |
| test-2-family | ctest family (3 targets) | test-2-family.log | 100% passed, 3/3 | not recorded in log |
| diff-1 | reversal of the product fix | diff-1.log | 2 files changed, 20 insertions (test and ledger only) | not recorded in log |
| build-3-reversed | build (reversed) | build-3-reversed.log | build output | not recorded in log |
| test-3-reversed | ctest (reversed) | test-3-reversed.log | 27 passed, 1 failed; FAIL at line 80 | not recorded in log |
| diff-2 | restore fix from fix.patch | diff-2.log | 3 files changed, 46 insertions, 5 deletions | not recorded in log |
| build-4-restored | build (restored) | build-4-restored.log | build output | not recorded in log |
| test-4-restored | ctest (restored) | test-4-restored.log | 100% passed, 1/1 | not recorded in log |
| test-4-restored-detail | SampleEditorDialogTest detail | test-4-restored-detail.log | 28 passed, 0 failed | not recorded in log |
| gates-1 | feature-check gates | gates-1.log | RESULT: PASS (8 gates; six show "exit 0", two show "no new item in your files") | not recorded in log |
| gui-1 | shell control-surface driver | gui-1.log | "PASS: shell control-surface driver" | not recorded in log |
| build-5-verify | `cmake --build build --target SampleEditorDialogTest` (this session) | duet/10-worker/tmp/001-build-5-verify.log | 16 lines | EXIT=0 |
| test-5-verify | `ctest -R SampleEditorDialogTest` run from build/tests (this session) | duet/10-worker/tmp/003-test-5-verify.log | 100% passed, 1/1 | EXIT=0 |

Screenshot: gui-shots/shell-arrange.png (kept in the log folder, outside the worktree). sha256 0f76075ee2dfe8ac8c0e0e1a7075634ea84eb778221baadf701f2d5a989bba97 (gui-1.sha256). The PNG header is 1600x1000 (checked this session).

## Self-check

- **Reversal:** RED on the reverted product: test-3-reversed.log:80, 27 passed and 1 failed. GREEN after restore: test-4-restored (100%, 1/1) and test-4-restored-detail (28 passed, 0 failed). The EXIT codes for the RED and reversal steps are not recorded in any log file.
- **Deterministic:** inputs are fixed digit strings. The test uses no clock, sleep, timers, or threads.
- **Cause and completeness:** the cause is the unchecked llround and unchecked toDouble in `timeFrom`. Callers and siblings are covered in "Completeness" above.
- **No collateral damage:** in-range clock text runs the same arithmetic (h*3600 + m*60 + s, times max(1, rate), then llround). The only change is the finiteness and bound check. `999999999:00:00` is about 1.6e17 frames at 44.1 kHz, below qint64 max, so it stays accepted (test line 319).
- **Not ALREADY-FIXED or FALSE-POSITIVE:** the unfixed product fails the new test at test-1-red.log:80. The fix is in this commit.

## Other findings

None.
