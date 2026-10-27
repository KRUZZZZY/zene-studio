IMPLEMENTED

# Q050-488 - Cooperative worker shutdown and explicit callback ownership

Branch `feat/050-Q050-488-oc`, cut from `050/m0-train-17` (`639e1d23e`). Design, inventory and limits:
`docs/050-features/Q050-488.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-488.r1/logs/`.

**What changed.** Only the ALSA raw-MIDI client (`include/MidiAlsaRaw.h`, `src/core/midi/MidiAlsaRaw.cpp`).
Its worker polled the device with a 10 s timeout while the destructor waited 1 s and then called
`QThread::terminate()`, so every close of an idle device took 1000 ms and killed the thread. The worker now
also polls a wake self-pipe (the MidiAlsaSeq pattern). `stopWorker()` is idempotent: it sets an atomic flag,
wakes the worker and joins it. Device handles, the poll array and the pipe are released only after the join.
A startup failure after open releases everything and starts no worker. `sendByte` ignores a device that
never opened. The new test is `tests/src/core/MidiAlsaRawShutdownTest.cpp`, which fakes libasound's rawmidi
entry points with a pipe.

**Deliberately not changed.** Per the scope and the Q050-479/Q050-483 coordination:
- OSS, sndio (retiring, Q050-479) and RemotePlugin (retiring, Q050-483): no change, because no issue was
  reproduced that needs containment.
- MidiJack and `AudioDevice::stopProcessingThread`: inventoried as the next adapter stage, with no
  reproduction harness yet.
- Plugin-host and scripting callbacks: stage 4, which needs its own ownership design.

## Acceptance

| # | Criterion | Evidence (command -> exit code) |
| --- | --- | --- |
| 1 | Deterministic reproduction; repeated close of a blocked worker within a justified bound | `MidiAlsaRawShutdownTest::closingAWorkerBlockedOnInputIsBounded` (5 rounds, < 500 ms each; the bound is justified in the test and the feature doc). Fixed: `ctest -R "MidiAlsaRawShutdownTest\|MidiOutQueueTest" --output-on-failure` -> `0`, 3 runs out of 3. Old behaviour: `./MidiAlsaRawShutdownTest closingAWorkerBlockedOnInputIsBounded` -> `1` (`close took 1000 ms`). |
| 2 | Release after the worker stops; duplicate stop; partial startup failure | `duplicateStopIsHarmless`, `partialStartupFailureReleasesOnce` (3 rows) and `failedOpenStartsNothingAndDropsOutput`. Each checks that the worker was not running at `snd_rawmidi_close` and that no closed handle was used. Fixed `0`. Old behaviour: `1`; `1` / `134` / `1`; `1`. |
| 3 | TSan/ASan, no added suppressions | ASan+UBSan build of the test TU and `MidiAlsaRaw.cpp` (`-fsanitize=address,undefined`), linked against the existing objects. Run with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1` -> `0`, 10/10, no suppressions (`test-5-asan.log`). **TSan not run**: it does not run on this box. The race reasoning is in the feature doc, row 3. |
| 4 | Audio-thread entry allocation/lock free; delivery and timeouts preserved | `sendByteStaysAllocationFree` (allocation probe: 0 allocations, 0 frees, direct write) and `inputStillReachesTheParser` -> `0`. `MidiOutQueueTest` (incl. `AlsaRaw_ClosesHandlesAfterWorkerExitsOnPollError`) -> `0`. The 10 s poll timeout is unchanged. |
| 5 | Platform shutdown regression; unavailable coverage reported | Linux/ALSA mocked blocking poll -> `0`. JACK, WinMM, CoreMIDI, OSS and sndio are not covered and not changed (reasons in the feature doc). |
| 6 | Plan names surviving/retiring paths and prerequisites; no competing modernization | Inventory table in `docs/050-features/Q050-488.md`. The diff touches no OSS, sndio, libsoundio or RemotePlugin file. |

**Negative controls.**
1. Full revert of the two product files -> build exit `2` (the test cannot compile without `stopWorker`;
   `build-3-negative-full-revert.log`).
2. The original files plus a `stopWorker()` that is the old destructor body -> build `0`, then each case run
   separately -> `1, 1, 1, 134, 1, 1, 0, 0` (`test-3-negative-per-function.log`). The two `0`s are the
   preservation checks (delivery and the allocation probe), which should pass either way.

Restored afterwards. Rebuilt -> `0`. Tests -> `0`.

## Self-check

- Build: `cmake --build build --target zene MidiAlsaRawShutdownTest MidiOutQueueTest -- -j4` -> `0` (`build-6.log`).
  `CCACHE_TEMPDIR=/tmp/...` was needed: the configured ccache temp dir is outside the write-jail.
- Tests: `ctest -R "MidiAlsaRawShutdownTest|MidiOutQueueTest" --output-on-failure` from `build/tests` -> `0`, 2/2 tests, three runs.
- Gates: `feature_check.py` -> PASS on all 8 rows (`gates-2.log`). Two of the 8 are ratchet gates
  (complexity, file-length): "no new item in your files". The script was run from a `/tmp` copy because its
  log directory (`<worktree>/../*-feature-check`) is outside the write-jail. The only change in the copy is
  the log path.
- Registration: test in `tests/CMakeLists.txt` (ALSA block) and `tests/fork-sources.txt`. Both MidiAlsaRaw
  files in `tests/upstream-modifications.txt`. `tests/all-sources.txt` was re-derived; the recipe prints REPRODUCES.
- Commands/metadata: none added or changed, so there was no snapshot regeneration and no reversibility class
  to set. GUI/agent: no new user action; the change is reached through the existing quit paths (File > Quit,
  `control.quit`).
- Realtime: the audio-thread path (`sendByte`) still writes directly, with no allocation, lock, wait or
  signal. Joins and releases happen in the destructor on the main thread.
- Projects/render: no format or audio path change.
- Portability: the code is inside `LMMS_HAVE_ALSA` (Linux only, POSIX `pipe`/`fcntl`); no Qt-6-only API.
- Not done: TSan; a full-tree sanitizer build; the full ctest suite (forbidden by the brief); stages 3-4 beyond the inventory.
