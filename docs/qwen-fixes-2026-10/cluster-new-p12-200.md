# Cluster new-p12-200

**Verdict: FIXED** (round 2, on top of round 1; addresses Luna's REWORK review
`qa/reviews/new-p12-200.r1.md`)

One retrospective take could label frames recorded at several engine sample
rates with a single rate. `RetroAudioCapture::push()` stored incoming frames
without their sample rate, and the take writer (`writeRetroAudioTake`, driven by
`record.retro_capture_to_take`) labelled the whole window with one rate taken
from `engineSampleRate()`. If the engine's sample rate changed while the capture
stayed armed, the retained window mixed 48 kHz frames and 96 kHz frames but the
written WAV header claimed one rate - playing the older frames back at the wrong
speed and pitch (Luna cluster new-p12-200, BUG 5).

## Round 1 (kept)

`push()` carries the engine input sample rate (`AudioEngine::inputSampleRate()`)
and drops the stale frames when the rate changes, so the window is
rate-homogeneous; `takeWindow()` reports the window's single rate in
`Window::sampleRate`, and the take command labels the file with that rate
instead of the live engine rate.

## Round 2 (this rework)

Luna's review found two holes: the producer called `RetroAudioRing::reset()` -
a consumer-only operation documented for when neither side runs - and, on the
quiesce-timeout path, `takeWindow()` read `m_captureRate` *after* `copyOut()`,
so a concurrent transition could still pair a copied old-rate window with the
newly stored rate. The rework makes the frames and their rate one validated
snapshot, and the producer-side discard a protocol-coordinated ring operation:

- `include/RetroAudioRing.h`: new `discardRetained()` moves the head to 0 under
  the same odd/even publication sequence `push()` writes slots under, so a
  consumer's `copyOut()` sees the sequence change and re-reads instead of
  pairing a pre-discard head with post-discard slots. Unlike `reset()` it is
  safe to run on the producer thread while the consumer reads, and it leaves
  the cumulative counters alone (`overwrittenCount()` and friends are the
  capture's bookkeeping; rewriting them from the producer raced the consumer).
  New `snapshotInFlight()` lets the producer see the consumer's request through
  the ring's own protocol and defer ring-visible state changes while it holds.
- `src/core/RetroAudioCapture.cpp`, `push()` (audio thread): the retained
  window's rate and its discard generation are published as ONE atomic tag
  (`generation << 32 | rate`). A rate transition applies
  `discardRetained()` first, then stores the new tag - so a consumer whose
  acquire-load sees the new tag also sees the discard the ring made before it.
  A transition arriving while a snapshot is in flight is DEFERRED to the next
  period after the consumer releases the ring; the period is dropped and
  acknowledged like every snapshot-paused period, and the window and rate the
  consumer is reading stay one consistent snapshot. Realtime-safe: one relaxed
  load, up to two release stores and the ring's existing O(1) work; no
  allocation, lock, syscall or logging.
- `takeWindow()`: `copyTagValidated()` reads the tag, copies under the ring's
  publication protocol, and reads the tag again; a tag that moved means a
  transition landed across the copy (reachable only once the quiesce wait
  timed out with the producer still live), so the read is retried a bounded
  number of times and, if it never settles, the window is REFUSED (frames
  cleared, retained frames reported in `refused`) rather than relabelled.

## Evidence

| Check | Result |
|---|---|
| RED (before fix, new tests on r1 product code) | `ctest -R '^RetroAudioCaptureTest$'` EXIT=8: `aRateTransitionDuringAnInFlightSnapshotKeepsTheWindow()` FAILED ("window.sampleRate" actual 96000, expected 48000 - the transition push during the in-flight snapshot discarded the 300-frame window and relabelled the read with the new rate); `aRateTransitionKeepsTheCumulativeCounters()` FAILED ("window.overwritten" actual 0, expected 1024 - the producer-side `reset()` zeroed the cumulative counter). All 10 pre-existing tests still passed. |
| GREEN (fixed) | `ctest -R '^RetroAudioCaptureTest$'` EXIT=0; direct binary: **13 passed, 0 failed** (initTestCase + 11 slots + cleanupTestCase). |
| Reverse-patch proof | `git diff -- include/RetroAudioRing.h include/RetroAudioCapture.h src/core/RetroAudioCapture.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` → rebuild EXIT=0 → `ctest -R '^RetroAudioCaptureTest$'` **EXIT=8** (both new tests FAIL). `git apply LOGS/fix.patch` → rebuild EXIT=0 → ctest **EXIT=0**. |
| Live control path | `ctest -R '^ControlRecordInputs$'` (drives retro audio arming/take through a running zene instance) EXIT=0. |
| Affected-family test count | 1 CTest target (`RetroAudioCaptureTest`), 11 test slots + init/cleanup = 13 results, 13 passed. |
| Complexity gate | EXIT=1 **both before and after** this change; the set of flagged functions is byte-identical before vs after (diff of the REGRESSION lines: empty) and contains none of my files. Pre-existing branch failures, not re-anchored. |
| File-length gate | EXIT=1 **both before and after**; flagged set identical before vs after; my files stay under the limit (largest: RetroAudioCaptureTest.cpp at 499 lines, RetroAudioCapture.cpp at 327). |
| Files committed (this commit) | `include/RetroAudioRing.h`, `include/RetroAudioCapture.h`, `src/core/RetroAudioCapture.cpp`, `tests/src/core/RetroAudioCaptureTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p12-200.md`. |

## Commands and exit codes (r2 logs: `~/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r2/logs`)

```text
bash tests/complexity-gate.sh --check > $LOGS/gate-complexity-baseline.log 2>&1; echo EXIT=$?   # EXIT=1 (before any edit)
bash tests/file-length-gate.sh --check > $LOGS/gate-filelength-baseline.log 2>&1; echo EXIT=$?   # EXIT=1 (before any edit)
cmake --build build --target RetroAudioCaptureTest -j12 > $LOGS/build-1-baseline-test.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > $LOGS/test-1-baseline.log 2>&1; echo EXIT=$?   # EXIT=0
cmake --build build --target RetroAudioCaptureTest -j12 > $LOGS/build-2-red-test.log 2>&1; echo EXIT=$?   # EXIT=0 (new tests, r1 product code)
cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > $LOGS/test-2-red.log 2>&1; echo EXIT=$?   # EXIT=8  RED
cmake --build build --target zene RetroAudioCaptureTest -j12 > $LOGS/build-3-fix.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > $LOGS/test-3-green.log 2>&1; echo EXIT=$?   # EXIT=0  GREEN
build/tests/RetroAudioCaptureTest > $LOGS/test-4-green-direct.log 2>&1; echo EXIT=$?   # EXIT=0; Totals: 13 passed, 0 failed
git diff -- include/RetroAudioRing.h include/RetroAudioCapture.h src/core/RetroAudioCapture.cpp > $LOGS/fix.patch && git apply -R $LOGS/fix.patch; cmake --build build --target zene RetroAudioCaptureTest -j12 > $LOGS/build-4-reverse.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > $LOGS/test-5-red-reverse.log 2>&1; echo EXIT=$?   # EXIT=8  RED without the fix
git apply $LOGS/fix.patch; cmake --build build --target zene RetroAudioCaptureTest -j12 > $LOGS/build-5-restore.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > $LOGS/test-6-green-restore.log 2>&1; echo EXIT=$?   # EXIT=0  GREEN with the fix
cd build/tests && ctest -R '^ControlRecordInputs$' --output-on-failure > $LOGS/test-7-control-green.log 2>&1; echo EXIT=$?   # EXIT=0
bash tests/complexity-gate.sh --check > $LOGS/gate-complexity-final.log 2>&1; echo EXIT=$?   # EXIT=1, flagged set identical to baseline (diff empty)
bash tests/file-length-gate.sh --check > $LOGS/gate-filelength-final.log 2>&1; echo EXIT=$?   # EXIT=1, flagged set identical to baseline (diff empty)
```

## Notes

- `include/RetroAudioRing.h`, `include/RetroAudioCapture.h` and
  `src/core/RetroAudioCapture.cpp` are product-new files already registered in
  `tests/fork-sources.txt`; no new product files were added, and no
  upstream-modifications entry changes (round 1 registered
  `src/core/AudioEngine.cpp`).
- The two new tests are deterministic: they drive both sides of the ring
  through the public API (`ring().beginSnapshot()` is the documented consumer
  request) with no wall-clock timing. The first leaves the snapshot
  unacknowledged so `takeWindow()`'s quiesce wait times out while the
  transition overlaps it; the second fills the ring so the overwrite counter is
  non-zero, then changes the rate.
- The refused path (`window.refused` on tag instability, `src/core/
  RetroAudioCapture.cpp`) reuses the existing "retained but not readable"
  reporting; `retroToTake` already refuses a frames-empty window with a typed
  error, so no command or reply contract changed.
