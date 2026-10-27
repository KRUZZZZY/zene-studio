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

### submit_check.py answers

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> Q050-488` -> `1`
(`submit-check-1.log`). Its only FAIL is "gates (feature_check.py)".

**The gates FAIL is environmental, not a gate result.** The original script exits `1` with
`PermissionError: ... zene-feat-q050-488-feature-check`: its log directory sits beside the worktree, which
this lane's write-jail refuses (`gates-3-original.log`). The `/tmp` copy, with only the log path changed,
runs the same 8 gates on the final HEAD -> `0`, RESULT: PASS (`gates-4.log`). The loop runs the original
outside the jail.

**The three WARNs (GUI path, GUI test, agent command).** This unit adds no user-facing action. It changes
how an existing device closes. That close is reached only through the existing quit paths (File > Quit,
and the registered `control.quit`), which are unchanged. No command or GUI file needed to change.

**The "?" items:**

- **Fails reverted / passes restored.** Yes, see "Negative controls" above:
  - full revert: build `2`;
  - old behaviour: `1, 1, 1, 134, 1, 1` for the six fix tests, `0, 0` for the two preservation checks;
  - restored: build `0`, ctest `0` three times.
- **Each criterion proven by a test I ran.** Yes, see the acceptance table: test name and exit code per row.
  For row 3, TSan was not run (it does not run on this box). ASan+UBSan was run -> `0`.
- **One undo step / reversibility.** Not applicable. No edit, command or journal step is added or changed.
- **Audio thread.** The only function on the audio path is `MidiAlsaRaw::sendByte`, reached from
  `MidiClientRaw::processOutEvent`. It gains one null-pointer compare, and is otherwise the same
  `snd_rawmidi_write` it always made. `sendByteStaysAllocationFree` shows 0 allocations and 0 frees.
  `stopWorker`, the destructor, `closeDevice` and `closeWakePipe` run only on the thread that deletes the
  MIDI client (`AudioEngine::~AudioEngine`, main thread). The worker `run()` is a low-priority MIDI input
  thread, not the audio thread.
- **Projects/render.** No load, save or render code was touched, and no project data changed.
- **Portability.** The code is inside `#ifdef LMMS_HAVE_ALSA`, which is Linux only, so POSIX `pipe`/`fcntl`/`poll`
  are available. It uses no Qt-6-only API: `QThread::wait(unsigned long)` exists in Qt 5.15 and 6. There are
  no windows.h macro names, no `std::filesystem`, and none of `small`/`near`/`far`/`interface`/`min`/`max` as
  identifiers.
