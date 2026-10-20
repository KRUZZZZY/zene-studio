**Verdict: FIXED** - cluster new-p18-57: sndio MIDI writes no longer block the caller of `sendByte()` (the audio thread during playback).

# Cluster new-p18-57: sndio MIDI writes can block the realtime path

## Finding

`src/core/midi/MidiSndio.cpp` (finding line 96; the tree before this fix had `sendByte()` at lines 93-101) called `mio_write()` directly on the caller's thread. The handle is opened blocking (`mio_open(..., 0)`, `MidiSndio.cpp:49,53`), so a stalled or full device held up the caller. `sendByte()` is reached from the playback path: `InstrumentTrackNotes` -> `MidiPort::processOutEvent` -> `MidiClientRaw::processOutEvent` -> `sendByte()`, all on the audio thread.

## Evidence (the real defect, before the fix)

- `git show HEAD:src/core/midi/MidiSndio.cpp` lines 93-101: `sendByte()` ends in `mio_write( m_hdl, &c, 1 );` on the caller's thread.
- The reproducer is `MidiSndioTest::deviceWriteRunsOffTheCallerThread` (`tests/src/core/MidiSndioTest.cpp:110-119`). On the unfixed code the device write runs on the test thread, and the assertion at `MidiSndioTest.cpp:117` fails.

## The fix

- `sendByte()` (`src/core/midi/MidiSndio.cpp:144-154`) now only does `::write( m_outPipe[1], &c, 1 )` into a pipe whose write end is non-blocking (`openOutputPipe`, `MidiSndio.cpp:45-61`, sets `O_NONBLOCK` at line 51). It never waits on the device. A full pipe drops the byte.
- A new writer thread, `m_writer` (`QThread::create`, `MidiSndio.cpp:94-95`), is the only code that calls `mio_write()` (`writeQueuedBytes()`, `MidiSndio.cpp:159-185`, the `mio_write` call at line 177). It reads the pipe, checks `m_quit` after each read, and writes the bytes in order.
- Destructor (`MidiSndio.cpp:99-132`): the reader is stopped as before; then the writer is woken with one byte, joined with the same 1000 ms wait and `terminate()` fallback as the reader, and only then is `mio_close` called. The reconnect test's check that the handle is closed after the reader stops still holds (`MidiReconnectTest::sndioHandleIsClosedWhenClientIsDestroyed` passes).
- Declarations: `include/MidiSndio.h:72-82` (`writeQueuedBytes`, `m_outPipe[2]`, `std::unique_ptr<QThread> m_writer`); `#include <memory>` at line 34.
- The reader thread `run()` is unchanged (`MidiSndio.cpp:188` onward).

## Commands run and exit codes

All commands are from the worktree unless noted. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-57.r1/logs/`.

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| build attempt 1 (env issue, not code) | `cmake --build build --target MidiSndioTest -j4` | `build-red-1.log` | 2 (ccache could not write to `/run/user/1000/ccache-tmp`) |
| build RED (unfixed code) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p18-57 cmake --build build --target MidiSndioTest -j4` | `build-red-2.log` | 0 |
| **RED** | `cd build/tests && ctest -R '^MidiSndioTest$' --output-on-failure` | `test-red-1.log` | **8** (`FAIL! : MidiSndioTest::deviceWriteRunsOffTheCallerThread() 'writerThread != QThread::currentThread()' returned FALSE`, `MidiSndioTest.cpp(117)`; `Totals: 5 passed, 1 failed`) |
| build GREEN (first) | `CCACHE_TEMPDIR=... cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `build-green-1.log` | 2 (`std::unique_ptr<QThread>` = `QThread*`: `QThread::create` returns a raw pointer; fixed by `m_writer.reset(...)`) |
| build GREEN | `CCACHE_TEMPDIR=... cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `build-green-2.log` | 0 |
| **GREEN** | `cd build/tests && ctest -R '^(MidiSndioTest\|MidiReconnectTest)$' --output-on-failure` | `test-green-1.log` | **0** (2/2 passed) |
| 4b: save product patch | `git diff -- src/core/midi/MidiSndio.cpp include/MidiSndio.h > logs/fix.patch` | - | 0 |
| 4b: revert product | `git apply -R logs/fix.patch` | - | 0 |
| 4b: rebuild reverted | `CCACHE_TEMPDIR=... cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `build-4b-reverted.log` | 0 |
| **4b RED (reverted)** | `cd build/tests && ctest -R '^MidiSndioTest$' --output-on-failure` | `test-4b-reverted.log` | **8** (same assertion, `MidiSndioTest.cpp(117)`; `5 passed, 1 failed`) |
| 4b: restore product | `git apply logs/fix.patch` | - | 0 |
| 4b: rebuild restored | `CCACHE_TEMPDIR=... cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `build-4b-restored.log` | 0 |
| **4b GREEN (restored)** | `cd build/tests && ctest -R '^MidiSndioTest$' --output-on-failure` | `test-4b-restored.log` | **0** (1/1 passed) |
| family build | `CCACHE_TEMPDIR=... cmake --build build --target MidiReconnectActionTest ControlMidiReconnectSharedTest MidiSndioTest MidiReconnectTest -j4` | `build-family-1.log` | 0 |
| **family** | `cd build/tests && ctest -R 'Midi(Sndio\|Reconnect)' --output-on-failure` | `test-family-1.log` | 8 (4 of 5 passed; the failing one is `ControlMidiReconnect`, see below) |
| family per binary | `./MidiSndioTest`, `./MidiReconnectTest`, `./MidiReconnectActionTest`, `./ControlMidiReconnectSharedTest` (offscreen) | stdout | 6 passed / 15 passed / 4 passed / 9 passed, 0 failed (QTest `Totals` lines) |
| `ControlMidiReconnect` on reverted code | `git apply -R logs/fix.patch`, `cmake --build build --target zene`, `ctest -R '^ControlMidiReconnect$'`, `git apply logs/fix.patch` | `test-ctrl-orig.log` | 0 (passed) |
| `ControlMidiReconnect` with fix, solo | `ctest -R '^ControlMidiReconnect$'` x2 | `test-ctrl-fixed-1.log`, `-2.log` | 0, 0 (passed both) |
| gates (raw commands, see note) | `bash tests/fork-sources-gate.sh` | `gate-fork-sources-1.log` | 0 |
| | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce-1.log` | 0 |
| | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression-1.log` | 0 |
| | `tests/scripted/check-namespace` | `gate-check-namespace-1.log` | 0 |
| | `bash tests/no-tautology-gate.sh` | `gate-no-tautology-1.log` | 0 |
| | `python3 tests/doc-figures-gate.py` | `gate-doc-figures-1.log` | 0 |
| | `bash tests/complexity-gate.sh --check` | `gate-complexity-1.log` | 1 (6 items in other files; none in the sndio files) |
| | `bash tests/file-length-gate.sh --check` | `gate-file-length-1.log` | 1 (15 items in other files; none in the sndio files) |
| **feature check** | `python3 -I /tmp/fc-p18-57/feature_check.py <worktree>` (see note) | `feature-check-run-1.log` | **0** (`RESULT: PASS`, all 8 rows PASS) |

**Note on the feature check.** `feature_check.py` puts its logs in `<worktree>/../<name>-feature-check`, i.e. `projects/zene-studio-program/zene-qwen-new-p18-57-feature-check`. That folder is not writable from this session (`PermissionError` on `mkdir`; the attempt's log is `gate-feature-check-1.log`). I did not edit the shared script. I ran a copy at `/tmp/fc-p18-57/feature_check.py` that differs in exactly two lines: the log folder (now `logs/feature-check` in this run's log folder) and `ROOT` (so it reads the shared `FIX-BASE` and the already-cached base listing `gate-baselines/2da2ed356...json`). Its logic is otherwise identical, including the ratchet rule that a red complexity or file-length item only counts if it is in a changed file. None is.

**Note on build.** The first build failed inside ccache (`/run/user/1000/ccache-tmp` not writable). Every build after that used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p18-57`. No code change was involved.

## Evidence table

| Item | Result |
| --- | --- |
| RED command and exit | `ctest -R '^MidiSndioTest$'` on unfixed code: EXIT 8, `deviceWriteRunsOffTheCallerThread` fails at `tests/src/core/MidiSndioTest.cpp:117` |
| GREEN command and exit | `ctest -R '^(MidiSndioTest\|MidiReconnectTest)$'` on fixed code: EXIT 0, 2/2 passed |
| 4b (revert then restore) | reverted: EXIT 8, same assertion; restored: EXIT 0 |
| Affected family, tests executed | `Midi(Sndio\|Reconnect)`: 5 ctest entries; 4 pass; `ControlMidiReconnect` fails under the parallel family run (see below). QTest totals: MidiSndioTest 6, MidiReconnectTest 15, MidiReconnectActionTest 4, ControlMidiReconnectSharedTest 9 passed, 0 failed |
| Gate exit codes | the 6 simple gates: 0; complexity: 1, file-length: 1 (pre-existing items in other files); feature check (copy): 0, `RESULT: PASS` |
| Files committed | `include/MidiSndio.h`, `src/core/midi/MidiSndio.cpp`, `tests/src/core/MidiSndioTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p18-57.md` |

## Flake observed (not caused by this change)

In the family run, `ControlMidiReconnect` failed with `events played through the binding arrive, and only through it FAILED (1): the engine received 0 of the 4 events the BOUND client played` (`test-family-1.log`, lines 20-22). The script drives the real binary on the ALSA sequencer (`engine : 'ALSA-Sequencer'`); it never reaches `MidiSndio`. The same test passed on the reverted code (EXIT 0), and passed twice on its own with the fix. I read it as a timing flake on a loaded machine. I did not change it, and I did not reproduce it in the three solo runs.

## Judgement calls and residual risk

1. **`sendByte()` makes one `write(2)` syscall.** The realtime rule forbids waits and locks on the audio path. The write is non-blocking: it never waits on the device, takes no user-space lock, and allocates nothing. The kernel takes its own short pipe lock. Waking a writer without a syscall would need the writer to poll on a timer, which I judged worse. Reviewers who want zero syscalls on the audio path should say so.
2. **Bounded buffering, with drop.** The pipe holds the kernel default (64 KiB). When the device has stalled long enough to fill it, new bytes are dropped. The old code never dropped bytes; it blocked. Dropping can leave a stuck note or a torn message, but only after a stall that already made output unusable, and it cannot hang the audio thread.
3. **Shutdown discards queued bytes.** `writeQueuedBytes()` checks `m_quit` after each read, so bytes still queued when the MIDI client is destroyed are not written. The old code had nothing queued, so this is new behaviour, only at shutdown.
4. **Concurrent handle use.** The reader calls `mio_read` and the writer calls `mio_write` on the same handle. The old code already did this with the audio thread writing while the reader read. I did not change that.
5. **Siblings not changed.** `MidiAlsaRaw::sendByte` (`snd_rawmidi_write`, `src/core/midi/MidiAlsaRaw.cpp:108-111`) and `MidiOss::sendByte` (`putChar`, `src/core/midi/MidiOss.cpp:90-93`) write synchronously on the caller's thread too. They are not in this finding, so I did not touch them. They are the same kind of risk and would need their own cluster; I did not confirm whether their handles are opened blocking.
6. **Pipe over the wasm SPSC ring.** I considered reusing `src/wasm/WasmSpscRingBuffer.h` but it is in `lmms::wasm` and needs a wake-up anyway, so the pipe is both the queue and the wake-up with less code.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Yes. Reverted: `ctest -R '^MidiSndioTest$'` EXIT 8, `MidiSndioTest.cpp(117)` (`writerThread != QThread::currentThread()` returned FALSE). Restored: EXIT 0. Initial RED on the unfixed tree: EXIT 8, same assertion.
- **The test is deterministic: no wall-clock timing, sleeps or races.** The test injects the device: `mio_open`/`mio_write`/`mio_read`/`mio_pollfd`/`mio_revents`/`mio_close` are fakes in `MidiSndioTest.cpp` (`deviceOpens`, `deviceBytes`, `writerThread`). The synchronisation hook is `QSemaphore bytesWritten`, released once per byte the fake device accepts; the test waits on exactly the number of bytes it sent. The only time bound is `tryAcquire(n, 5000)`: an upper limit on the wait that returns the moment the device has the bytes, not a timing assertion. The writer thread id is compared with the caller's, not measured. The reader's 100 ms poll in `run()` is never asserted on. Note: the fake `mio_write` stores `writerThread` before releasing permits, and the test reads it only after acquiring them, so there is no data race.
- **The fix is at the cause and complete.** `mio_write` now appears in exactly one place in the product tree (`src/core/midi/MidiSndio.cpp:177`, in `writeQueuedBytes()`), run only on `m_writer`. Callers of `sendByte()` are unchanged (`MidiClientRaw::processOutEvent`). The destructor closes the handle only after both threads are joined, so the reconnect test's shutdown check holds. The `m_hdl == nullptr` guard and the failed-open path are unchanged (`failedOpenDoesNotWrite` passes). Siblings: see judgement call 5.
- **No collateral damage.** No saved-data format, no control command (so no `ControlCommandsSnapshot` change), no new limit that other callers hit. The only new behaviour is judgement calls 2 and 3 above. Old projects load as before because nothing about project files changed.
- **Verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed.**
