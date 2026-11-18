Verdict: FIXED

new-p10-345: Reading into a null ring-buffer destination crashes

Evidence (before the fix): `SampleFrameRingBuffer::read()` at [include/SampleFrameRingBuffer.h:103](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-345/include/SampleFrameRingBuffer.h:103) had no destination check. It wrote `dst[i]` whenever frames were queued, so `read(nullptr, n)` with data available dereferenced null. The sibling `writeBlock()` at [include/SampleFrameRingBuffer.h:83](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-345/include/SampleFrameRingBuffer.h:83) already guards its source pointer. The finding is real.

Fix: [include/SampleFrameRingBuffer.h:105](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-345/include/SampleFrameRingBuffer.h:105) returns 0 for a null `dst` (or zero frames) before touching the indices, mirroring `writeBlock()`. Nothing is consumed on that path, so queued frames stay available for a later real read. The doc comment on `read()` now says so.

Production caller: [src/core/AudioEngine.cpp:870](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-345/src/core/AudioEngine.cpp:870) passes `m_inputBuffer[...]`, which is allocated for every slot at [src/core/AudioEngine.cpp:510](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-345/src/core/AudioEngine.cpp:510), so the new branch is unreachable there. The other callers ([include/SampleRecordAccumulator.h:292](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-345/include/SampleRecordAccumulator.h:292), [src/core/audio/TrackRecorder.cpp:379](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-345/src/core/audio/TrackRecorder.cpp:379)) pass non-null scratch buffers; the `frames == 0` path returns 0 exactly as before. Only the previously crashing case changes behaviour. The fix adds no allocation, lock, wait or log, so the realtime rule holds.

Regression test: `RecordingRealtimeTest::ringBufferReadRejectsNullDestination` at [tests/src/core/RecordingRealtimeTest.cpp:304](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-345/tests/src/core/RecordingRealtimeTest.cpp:304). It is deterministic (no timing or threads). Matrix covered: zero-length read into null with data queued; non-zero read into null with data queued (refused, frames kept); a repeated refused read; the frames then read back in order into a real buffer; null read on an empty ring. Before the fix it dies with SIGSEGV at address 0x0 inside the new slot.

### Evidence table

| Step | Command (run from the worktree / `build/tests` as stated) | Result |
| --- | --- | --- |
| Build attempt 1 (sandbox) | `cmake --build build --target RecordingRealtimeTest -j4 > .../build-red-1.log 2>&1; echo EXIT=$?` | EXIT=2. ccache could not create its temp file in a directory outside the write-jail (`.../build-red-1.log`). No code error. |
| Build RED | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-p10-345 cmake --build build --target RecordingRealtimeTest -j4 > .../build-red-2.log 2>&1; echo EXIT=$?` | EXIT=0 (test built before the fix) |
| RED test | `ctest -R RecordingRealtimeTest --output-on-failure > .../test-red-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=8, `Received signal 11 (SIGSEGV) ... accessing address 0x0` in `ringBufferReadRejectsNullDestination`; `0% tests passed, 1 tests failed out of 1` |
| Build GREEN | `CCACHE_TEMPDIR=... cmake --build build --target zene RecordingRealtimeTest -j4 > .../build-green-1.log 2>&1; echo EXIT=$?` | EXIT=0; 0 `error:` lines; `Built target zene`, `Built target RecordingRealtimeTest` |
| GREEN test (ctest family) | `ctest -R RecordingRealtimeTest --output-on-failure > .../test-green-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0, `100% tests passed, 0 tests failed out of 1` (1 ctest test; the QTest binary executes all its slots) |
| GREEN new slot alone | `./RecordingRealtimeTest ringBufferReadRejectsNullDestination > .../test-green-2.log 2>&1; echo EXIT=$?` | EXIT=0; `PASS : ...ringBufferReadRejectsNullDestination()`; `Totals: 3 passed, 0 failed` |
| GREEN full binary | `./RecordingRealtimeTest > .../test-green-full.log 2>&1; echo FULL_EXIT=$?` | FULL_EXIT=0; `Totals: 55 passed, 0 failed, 0 skipped` |
| Step 4b: reverse fix | `git diff -- include/SampleFrameRingBuffer.h > .../fix.patch && git apply -R .../fix.patch` | APPLY_R_EXIT=0 (`git diff --stat` then showed only the test file, and no `dst == nullptr` guard remained) |
| Step 4b: rebuild without fix | `cmake --build build --target zene RecordingRealtimeTest -j4 > .../build-4b-red.log 2>&1` | BUILD_EXIT=0 |
| Step 4b: RED test | `ctest -R RecordingRealtimeTest --output-on-failure > .../test-4b-red.log 2>&1; echo EXIT=$?` | EXIT=8, SIGSEGV in `ringBufferReadRejectsNullDestination` (second reproduction) |
| Step 4b: re-apply fix | `git apply .../fix.patch` | APPLY_EXIT=0 |
| Step 4b: rebuild with fix | `cmake --build build --target zene RecordingRealtimeTest -j4 > .../build-4b-green.log 2>&1` | BUILD_EXIT=0 |
| Step 4b: GREEN test | `ctest -R RecordingRealtimeTest --output-on-failure > .../test-4b-green.log 2>&1; echo EXIT=$?` and `./RecordingRealtimeTest ringBufferReadRejectsNullDestination > .../test-4b-green-slot.log 2>&1; echo SLOT_EXIT=$?` | EXIT=0 and SLOT_EXIT=0; slot `PASS` |
| Gates (first try) | `python3 .../qwen/feature_check.py <worktree> > .../feature-check-1.log` | Failed before running any gate: `PermissionError` creating the sibling folder `<worktree>-feature-check` outside the write-jail (`feature-check-1.log`). |
| Gates (workaround) | An unmodified copy of `feature_check.py` in `$TMPDIR`, changed only in two lines: `ROOT` points at the shared qwen directory (so `FIX-BASE` and the cached baselines are read as-is) and the log folder points at this run's LOG FOLDER. Run with `python3 -I`. | Base `639e1d23e` has a cached baseline (`gate-baselines/639e1d23e...json`), so no write happened outside the jail. Final run on the final tree: `RESULT: PASS`, EXIT=0 (`feature-check-3.log`). |

Gate rows on the final tree (`feature-check-3.log`): fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in the changed files), file-length PASS (no new item in the changed files).

Registration: `include/SampleFrameRingBuffer.h` is already in `tests/fork-sources.txt` (line 1086) and `tests/all-sources.txt`, so no entry is needed. `tests/src/core/RecordingRealtimeTest.cpp` is an inherited file already listed in `tests/upstream-modifications.txt`; a reason line for this cluster was added there.

Rebuild scope, stated plainly: I rebuilt `zene` and `RecordingRealtimeTest`. `SampleFrameRingBuffer.h` is also included, through `AudioEngine.h` and `SampleRecordAccumulator.h`, by a large set of other test targets (for example `WarpQuantizeTest` and the `tests/src/gui` and `tests/src/plugins` suites). I did not rebuild or run those. The change is a body-only change to one inline function, so there is no layout change, but the broader family was not rerun here.

Files committed for this verdict: `include/SampleFrameRingBuffer.h`, `tests/src/core/RecordingRealtimeTest.cpp`, `tests/upstream-modifications.txt`, and this report.

## Self-check

Completion check (`submit_check.py`, saved as `logs/submit-check-1.log`, EXIT=1): PASS commits on the branch, PASS working tree clean, PASS report present, PASS tests changed. Two FAILs, answered below.

- **Gates row (`feature_check.py`) — FAIL, environmental, not a gate result.** `submit_check.py` runs the unmodified `feature_check.py`, which creates the sibling folder `<worktree>-feature-check` outside the write-jail and exits 1 with `PermissionError` before any gate runs (`logs/feature-check-1.log`; the same error is the root of the submit-check row). I did not edit the shared script. The gates were run through an unmodified copy whose only changes are the `ROOT` path and the log folder (see the evidence table). That run on the final commit tree gives all eight rows PASS and EXIT=0 (`logs/feature-check-3.log`). The submit-check row stays FAIL until someone runs the gate with a writable sibling folder.
- **Regression fails with the change reverted, passes with it:** RED `ctest -R RecordingRealtimeTest` EXIT=8, SIGSEGV in the new slot (`logs/test-red-1.log`, `logs/test-4b-red.log`); GREEN EXIT=0 with the slot `PASS` (`logs/test-green-2.log`, `logs/test-4b-green-slot.log`). Full binary `Totals: 55 passed, 0 failed` (`logs/test-green-full.log`).
- **Deterministic:** the test is single-threaded. It uses fixed `frameAt(i)` values and a fixed-capacity ring. It has no sleep, timing, thread, or randomness, and needs no injection seam: the fault is a null pointer dereference, which either happens or does not.
- **Fix at the cause, callers and siblings checked:** the fault is the missing guard in `read()` (`include/SampleFrameRingBuffer.h:105`). The sibling `writeBlock()` (`:83`) already has the matching guard. Callers: `src/core/AudioEngine.cpp:870` (destination always allocated at `:510`, so the new branch is unreachable there), `include/SampleRecordAccumulator.h:292` and `src/core/audio/TrackRecorder.cpp:379` (non-null scratch buffers). The other ring classes (`RetroAudioRing`, `RecordRingBuffer`, `InputChannelRing`) are separate types and were not changed. Scope note: I did not rebuild or run the other test targets that include this header through `AudioEngine.h` (see the rebuild-scope note above).
- **No collateral damage:** no persisted format, no new limit or check, and no change to any path that did not previously crash. The `frames == 0` path returns 0 as before.
- **Verdict is FIXED,** so no ALREADY-FIXED or FALSE-POSITIVE proof is needed.
