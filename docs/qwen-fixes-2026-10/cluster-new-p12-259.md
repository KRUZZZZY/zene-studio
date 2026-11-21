# cluster-new-p12-259 — Median calculation corrupts TapTempo's rolling interval window

Verdict: FIXED

## Evidence
- `plugins/TapTempo/TapTempo.cpp:98-101`: `std::nth_element` was called on `m_intervals` in place, reordering the rolling window so later taps indexed a circular buffer using positions whose contents no longer reflected chronological order.
- Fix: compute the median via `std::nth_element` on a local copy `sortedIntervals`, leaving `m_intervals` chronological.

## Changes
- `plugins/TapTempo/TapTempo.cpp`: median computed on copy.
- `tests/src/core/TapTempoTest.cpp`: regression `rollingIntervalWindowIsPreservedAcrossMedian` (RED on original, GREEN after fix).

## Commands and exit codes
- RED build: `cmake --build build --target TapTempoTest` EXIT=0; test `ctest -R '^TapTempoTest$'` EXIT=8 (1 failure: rollingIntervalWindowIsPreservedAcrossMedian).
- GREEN build: `cmake --build build --target TapTempoTest` EXIT=0; test `ctest -R '^TapTempoTest$'` EXIT=0 (3 tests).
- Proof test catches bug: `git apply -R .../fix.patch` EXIT=0; build EXIT=0; test EXIT=8 (fail); `git apply .../fix.patch` EXIT=0; build EXIT=0; test EXIT=0 (pass).
- `cmake --build build --target zene` EXIT=0.
- `bash tests/complexity-gate.sh --check` EXIT=1 (pre-existing baseline entries unrelated to TapTempo; no TapTempo entry in output).
- `bash tests/file-length-gate.sh --check` EXIT=1 (pre-existing baseline entries unrelated to TapTempo; no TapTempo entry in output).
