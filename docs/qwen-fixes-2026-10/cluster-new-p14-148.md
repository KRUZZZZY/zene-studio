FIXED

# cluster new-p14-148: AudioRing capacity rounding and storage size can wrap size_t

## Verdict

FIXED. `AudioRing` accepted a capacity whose power-of-two rounding or frames-times-channels product wraps `size_t`. The rounding loop shifted `2^63 << 1` to 0 and the constructor sized storage from a wrapped product, so writes landed out of bounds. Both cases now throw `std::length_error` from the constructor.

## Evidence

- **Old rounding loop.** `fa4a5f2e78:src/core/LinkAudioCodec.cpp:61-66` (`roundUpPowerOfTwo`). `while (p < v) { p <<= 1; }` has no bound. For `v > 2^63` it shifts `p` to 0 and then loops forever. The fixed version is at `src/core/LinkAudioCodec.cpp:63-72`.
- **Old storage size.** `fa4a5f2e78:src/core/LinkAudioCodec.cpp:157`, `m_data(m_capacity * AudioChannels, 0.0f)`. With `m_capacity = 2^63` the product wraps to 0, so the ring had zero-length storage. The fixed version is `ringStorageSize()` at `src/core/LinkAudioCodec.cpp:75-82`, called from the constructor at `:171-173`.
- **Sibling guards, same two checks.** `include/SampleFrameRingBuffer.h:155-158` (rounding guard) and `include/InputChannelRing.h:196-199` (rounding guard) and `:207-214` (`storageSize`, `throw std::length_error("ring storage size overflows size_t")`). The fix follows the InputChannelRing pattern. Note: the brief cited `SampleFrameRingBuffer.h:139-149`, but those lines are `reset()`, not a guard. The rounding guard is at 155-158.
- **Only product caller.** `src/core/LinkAudioStream.cpp:107-108` constructs `m_tx(AudioRingFrames)` and `m_rx(AudioRingFrames)`. `include/LinkAudioStream.h:77` sets `AudioRingFrames = 65536`. A grep for `\bAudioRing\b` in `src/` found no other construction site.

## Commands and results

The logs in `~/.config/kads/zene/state/runs/new-p14-148.r1/logs/` hold the tool output. They do not print an exit line, so the EXIT column is the code recorded for each step in the p1–p4 run record. The meaning of each code is confirmed by its log: ctest 8 means tests failed, QtTest 1 means assertion failures, 124 is the `timeout` kill.

| Step | Log | EXIT | What the log shows |
|---|---|---|---|
| build-1 | build-1.log | 0 | `Built target LinkAudioCodecTest` |
| test-1-red | test-1-red.log | 8 | `rejectsCapacityWhoseStorageWouldWrap` FAIL at test line 84, then ctest Timeout (60.07 s) |
| test-1b-red-fast | test-1b-red-fast.log | 1 | `Totals: 2 passed, 1 failed` |
| test-1c-hang-alone | test-1c-hang-alone.log | 124 | Only `initTestCase` printed, then killed |
| build-2b-green | build-2b-green.log | 0 | Built |
| test-2b-green | test-2b-green.log | 0 | `LinkAudioCodecTest` Passed |
| test-2b-family | test-2b-family.log | 0 | `LinkAudioStreamTest` Passed (6.34 s), `LinkAudioCodecTest` Passed; 2/2 |
| test-2b-direct | test-2b-direct.log | 0 | `Totals: 7 passed, 0 failed` |
| build-3-reverted | build-3-reverted.log | 0 | Built |
| test-3-reverted-red | test-3-reverted-red.log | 8 | ctest Timeout (60.06 s) |
| build-4-restored | build-4-restored.log | 0 | Built |
| test-4-restored-green | test-4-restored-green.log | 0 | Passed (0.10 s) |
| cmp fix.patch | (no log file) | 0 | Re-checked this session: `git diff -- src/core/LinkAudioCodec.cpp \| cmp - logs/fix-final.patch` gives EXIT 0 |
| build-5-mut-shift | build-5-mut-shift.log | 0 | Built |
| test-5-mut-shift-red | test-5-mut-shift-red.log | 8 | ctest Timeout (60.07 s); only the SIZE_MAX slot hangs |
| build-6-mut-storage | build-6-mut-storage.log | 0 | Built |
| test-6-mut-storage-red | test-6-mut-storage-red.log | 1 | `Totals: 2 passed, 1 failed` (storage-wrap assertion) |
| test-6b-mut-storage-sizemax | test-6b-mut-storage-sizemax.log | 0 | `Totals: 3 passed, 0 failed` |
| build-7-final | build-7-final.log | 0 | Built |
| test-7-mut-restored-green | test-7-mut-restored-green.log | 0 | Passed (0.22 s) |
| feature-check | feature-check.log | 0 | `RESULT: PASS` |
| test-8-final-committed (p5b) | test-8-final-committed.log | 0 | `ctest -R '^LinkAudioCodecTest$'` on the committed tree: `100% tests passed out of 1` |
| cmp-final-committed-2 | cmp-final-committed-2.log | 0 | `git diff fa4a5f2e78 HEAD -- src/core/LinkAudioCodec.cpp \| cmp - fix.patch`: the committed product diff matches the saved patch byte for byte |

### The SIZE_MAX RED is a timeout

With the rounding guard removed (test-5), `rejectsCapacityThatCannotBeRounded` never returns, because the loop cannot reach `SIZE_MAX`. That is a harness timeout of a non-terminating call, not a timing assertion. The slot was run on its own (test-1c-hang-alone, exit 124, log shows only `initTestCase` before the kill), which matches the hang in test-1 and test-5.

The storage-wrap slot, `rejectsCapacityWhoseStorageWouldWrap`, gives the clean assertion RED: `Expected exception of type std::length_error to be thrown but no exception caught` (test-1, test-6).

### The two guards are covered independently

- Removing the rounding guard (test-5): the SIZE_MAX slot hangs, the storage-wrap slot is not the failing one.
- Removing the storage guard (test-6): the storage-wrap slot fails on its assertion, and SIZE_MAX still passes (test-6b, 3 passed), because the rounding guard still throws.

Each guard is therefore caught by its own test.

## Test matrix

| Case | Test | Coverage |
|---|---|---|
| Normal path | `normalCapacityRoundsUp` | 2→2, 3→4, `AudioRingFrames`→same, `AudioRingFrames+1`→`2*AudioRingFrames`. Passes before and after the fix (test-2b-green, test-4-restored-green) |
| Failure, storage wrap | `rejectsCapacityWhoseStorageWouldWrap` (2^63) | Throws `std::length_error`. RED without the fix (test-1, test-6) |
| Failure, rounding | `rejectsCapacityThatCannotBeRounded` (SIZE_MAX) | Throws `std::length_error`. Hangs without the rounding guard (test-1c, test-5) |
| Exhaustion, real stream | `LinkAudioStreamTest::ringKeepsOrderAcrossWrapAndRefusesOverflow` | Passes with the fix on the real `AudioRingFrames` ring (test-2b-family) |
| No reconfigure API | `AudioRing` has `const std::size_t m_capacity` and no resize (`include/LinkAudioStream.h:126`) | Capacity is fixed at construction, so the guard runs once per ring |
| Constructor only | Throws are in the constructor initializer, and `write`/`read`/`skip` are unchanged | No audio-thread allocation, lock, or log added |
| Platforms | Built and tested on Linux only. The guards use `size_t` arithmetic (`numeric_limits<size_t>::max()`), so they hold on any platform where `size_t` is the address-width unsigned type | Other platforms not built |

## Self-check

Answers to the ASK items. Each one is backed by the diff (`git diff fa4a5f2e78..HEAD --stat` is the two code files plus this report; the commits after `4f738a8756` are docs-only) or a logged run.

ASK items printed by `submit_check.py` (commit `4f738a8756`, log `submit-check-1.log`):

- **Regression fails reverted, passes with change.** Reverted or mutated: test-1 (exit 8), test-1b (exit 1), test-1c (exit 124), test-3-reverted-red (exit 8), test-5 (exit 8), test-6 (exit 1). With the change: test-2b-green (0), test-2b-direct (0), test-4-restored-green (0), test-7-mut-restored-green (0).
- **Deterministic.** The new tests use fixed constants only: `2^63` and `SIZE_MAX`, and `AudioRing(2/3/AudioRingFrames/AudioRingFrames+1)`. No wall-clock timing, sleeps, threads, or races. The one timing-looking result, the SIZE_MAX hang, is a non-terminating call under a 60 s ctest limit (see above). It is deterministic, not a race.
- **At the cause and complete.** The cause is the unbounded rounding loop (`fa4a5f2e78:src/core/LinkAudioCodec.cpp:64`) and the unchecked product (`fa4a5f2e78:src/core/LinkAudioCodec.cpp:157`). Both are guarded. The sibling `InputChannelRing.h` has the same two guards (`:196-199`, `:207-214`). `SampleFrameRingBuffer.h` has the rounding guard (`:155-158`) and no product, since its storage is one element per frame. The only product `AudioRing` construction is `LinkAudioStream.cpp:107-108`.
- **No collateral.** `AudioRing` is a runtime buffer. The diff touches no serialization or project-load code, so saved projects are unchanged. The new throws fire only above 2^63 frames or a storage product above `SIZE_MAX`. The only real caller uses 65536, so no legitimate caller reaches a limit.
- **Verdict is FIXED, not ALREADY-FIXED.** The bug is present at base `fa4a5f2e78` (`src/core/LinkAudioCodec.cpp:61-66` and `:157`, cited above), fixed at HEAD (`:63-72` guard at `:65-68`, `ringStorageSize` at `:75-82`, constructor at `:171-173`), and the fix is in commit `4f738a8756`.

- **Regression RED/GREEN.** RED: test-1 (exit 8, storage-wrap FAIL then SIZE_MAX timeout), test-1b (exit 1), test-1c (exit 124), test-3-reverted-red (exit 8). GREEN: test-2b-green (0), test-2b-family (0), test-2b-direct (0, 7 passed), test-4-restored-green (0), test-7-mut-restored-green (0). Mutation runs in test-5, test-6 and test-6b show each guard is individually covered.
- **Realtime.** The new `throw` sites are in `roundUpPowerOfTwo` and `ringStorageSize`. Both run only from the `AudioRing` constructor initializer (`src/core/LinkAudioCodec.cpp:171-173`). `write`, `read` and `skip` are not in the diff. The change adds no allocation, lock, or log on the audio path.
- **Undo.** No undo code and no control command changed. `ControlCommandsSnapshot` is not in the diff.
- **Collateral.** Only `src/core/LinkAudioCodec.cpp` and `tests/src/core/LinkAudioCodecTest.cpp` change, plus this report. The only product construction of `AudioRing` in `src/` is `src/core/LinkAudioStream.cpp:107-108`, which uses `AudioRingFrames = 65536`, well under the limit. Saved projects are not touched.
- **Registration.** No new source or test files. Both changed files were already in the build. This report is the only new file, and it is under `docs/qwen-fixes-2026-10/`.
- **Platform.** Linux was built and tested (build-1 through build-7). `include/LinkAudioStream.h` is unchanged (`AudioRing` header unchanged), so there is no ABI change.

## Other findings

- The earlier `cmp-final.log` (exit 1, `cmp: EOF on '-' which is empty`) ran `git diff` on the working tree. After commit `4f738a8756` that tree is clean, so the diff is empty. That command form cannot check committed work and is superseded by `cmp-final-committed-2.log` (exit 0, `EXIT=0` inside the log) in the table above.
- The run folder `new-p14-148.r1/` has `duet/` and `logs/` only. There is no `schemas/` directory.
- `logs/` has a `build-2-green.log` that the step table does not list. `cmp-final.log` is superseded by the committed-range compare, which is now logged as `cmp-final-committed-2.log` with its exit line (`EXIT=0`) and is the row in the table above.
