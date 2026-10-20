FIXED

# Cluster new-p10-380-port-port — SID reads uninitialized samples when generation falls short

Port of `bugs/qwen-new-p10-380-port` (commit 60f62ebe1) onto the newest line (conflict was in `plugins/Sid/SidInstrument.cpp`,
where `7f9f90d31` moved the fill into `sidplayback::fillBuffer`). Re-applied by hand; the older FALSE-POSITIVE / NEEDS-CONTEXT
commits (5c0704a8d, ec1cd6d24) are superseded by the measurement below.

## Decision: REAL (not already fixed on the newest line)

- `plugins/Sid/SidInstrument.cpp:241-246` `sid_fillbuffer` returns `sidplayback::fillBuffer(...)`, the number of samples produced
  (`plugins/Sid/SidPlaybackScheduler.h`, `fillBuffer` returns `total`; it stops early when the cycle budget runs out).
- `plugins/Sid/SidInstrument.cpp:271` `short buf[frames];` is an uninitialised stack VLA. Before the fix the return value was
  discarded (the call at the old `:374`) and the loop converted all `frames` entries, so any frames the emulator did not write
  became stale stack bytes as audio.
- The short fill is reachable: the SID's sampling parameters are fixed when the note's plugin data is created
  (`:255-262`), but `samplerate`/`delta_t` are recomputed every period from the live device rate (`:251`, `:269`). If the output
  rate rises under a living note (44.1 kHz -> 96 kHz via `AudioDevice::setSampleRateForTesting`, `include/AudioDevice.h:83`, the
  same path an export-rate change takes) the SID needs ~clockrate/oldRate cycles per sample while the budget shrinks, so only
  part of the period is produced. Measured RED: tail frame 224 read -0.0449219 / 0.580078 (stale stack bytes) instead of 0.

## Fix (plugins/Sid/SidInstrument.cpp:375-380)

Keep the produced count, clamp it to `[0, frames]`, zero-fill `buf[produced, frames)` before the short-to-float conversion. At a
constant rate `produced == frames`, so the fill is empty and output is unchanged. Realtime: `std::clamp`/`std::fill` on the
existing stack buffer only; no allocation, lock, wait or logging. `sid_fillbuffer` has a single caller (`playNoteImpl`), so there
is no sibling path. `#include <algorithm>` added. No control command added.

## Regression test

`tests/src/plugins/SidInstrument.cpp` (ctest `SidInstrument`): creates a note at 44.1 kHz, raises the device rate to 96 kHz under
the same living note, poisons the stack with 0xA5 (noinline helper) and checks the last 32 frames of the period are exact
zeros. No timing or sleeps. Registered in `tests/CMakeLists.txt` (LMMS_TESTS, offscreen environment list, `add_dependencies(SidInstrument sid)`),
`tests/fork-sources.txt`, `tests/all-sources.txt`; `tests/upstream-modifications.txt` line for `plugins/Sid/SidInstrument.cpp` extended.

## Evidence (all logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-380-port-port.r1/logs)

Build env note: `ccache` could not use `/run/user/1000/ccache-tmp` in this sandbox, so builds ran with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10380`.
`cmake --build build --target sid` first re-generated the build tree for the new test registration (cmake configure itself is blocked by the lane guard).

| Step | Command | Exit | Result |
|---|---|---|---|
| RED (x3, unfixed code) | `cd build/tests && ctest -R '^SidInstrument$' --output-on-failure` (test-red-1..3.log) | 8 | `FAIL! rateRiseShortFillLeavesTailSilent: un-written tail frame 224 channel 0 is -0.0449219, not silence` |
| GREEN (fixed) | same, after `cmake --build build --target SidInstrument sid -j4` (build-4.log, exit 0) (test-green-1.log) | 0 | 1/1 passed |
| 4b revert | `git apply -R fix.patch`, rebuild (exit 0), same ctest (test-4b-red.log) | 8 | FAIL, tail frame 224 is 0.580078 |
| 4b reapply | `git apply fix.patch`, rebuild (exit 0), `ctest -R '^SidInstrument$|PluginPortsMigration'` (test-4b-green.log) | 8 | `SidInstrument` Passed; `PluginPortsMigrationTest` Failed only in `nonRealtimeLadspaEffectsAreInvalid` ("SWH Decimator test fixture was not loaded", a LADSPA fixture unrelated to SID, in `ppm-sid.log`); `sidShortSegmentsKeepRegisterWritesOrdered` (the sibling SID regression from 7f9f90d31) PASS |

Affected-family tests executed: 2 ctest tests (`SidInstrument`, `PluginPortsMigrationTest`), 1 + 1 SID slots pass; `PluginPortsMigrationTest`'s one failing slot is the missing SWH fixture, not touched by this change.

Gates: see the Self-check section.

Files committed: `plugins/Sid/SidInstrument.cpp`, `tests/src/plugins/SidInstrument.cpp`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
`tests/all-sources.txt`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p10-380-port-port.md`.
