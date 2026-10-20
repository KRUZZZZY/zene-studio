**Verdict: FIXED** - cluster new-p18-57: sndio MIDI writes no longer block the caller of `sendByte()` (the audio thread during playback). Round 2 addresses the Luna review (REWORK): ordinary teardown now drains queued bytes, and a stalled device write is abandoned instead of terminated.

# Cluster new-p18-57: sndio MIDI writes can block the realtime path

## Finding

`src/core/midi/MidiSndio.cpp` (finding line 96; before the fix `sendByte()` was at lines 93-101) called `mio_write()` directly on the caller's thread. The handle is opened blocking (`mio_open(..., 0)`, `MidiSndio.cpp:49,53`), so a stalled or full device held up the caller. `sendByte()` is reached from the playback path: `InstrumentTrackNotes` -> `MidiPort::processOutEvent` -> `MidiClientRaw::processOutEvent` -> `sendByte()`, all on the audio thread.

## Evidence (the real defect, before the fix)

- `git show 2da2ed356:src/core/midi/MidiSndio.cpp` lines 93-101: `sendByte()` ends in `mio_write( m_hdl, &c, 1 );` on the caller's thread.
- Reproducer: `MidiSndioTest::deviceWriteRunsOffTheCallerThread`. On the unfixed code the device write runs on the test thread and the assertion fails (round-1 RED, below).

## The design (round 2)

- **`sendByte()`** (`src/core/midi/MidiSndio.cpp`, `sendByte` body) only does `::write( m_outPipe[1], &c, 1 )` into a pipe whose write end is non-blocking (`openOutputPipe`, sets `O_NONBLOCK`). It never waits on the device. A full pipe drops the byte.
- **The writer** is a `std::thread` running the static `MidiSndio::runWriter(std::shared_ptr<OutputWriter>)`. It is the only code that calls `mio_write()`. It reads the pipe until `read` returns 0, which happens only after the write end is closed and every queued byte has been read. Then it calls `mio_close()` on its own handle and closes its read end. It takes no pointer to `MidiSndio`.
- **`OutputWriter`** (`struct MidiSndio::OutputWriter`, defined in the .cpp) is the writer's share of the state: the handle, the read end, and a `std::promise<void>` for completion. The writer thread and the object share it through `std::shared_ptr`, so the writer stays valid after `MidiSndio` is gone.
- **Ordinary teardown** (`~MidiSndio`): the reader stops as before; then the write end is closed (the end-of-queue mark). The destructor waits up to `writerTeardownGrace` (1000 ms) on `m_writerDone`. If the writer finished, it is joined. The writer has already written every queued byte and closed the handle, so bytes queued before teardown reach the device before the close.
- **Stalled write** (same destructor): if the writer is still inside `mio_write` after the grace, the destructor **detaches** it and does not terminate it and does not close the handle. The writer keeps its handle, drains what is still queued, and closes the handle and read end when the write returns. Nothing in the destructor touches the handle or the object after that point.
- **Reader** `run()` is unchanged. Its own shutdown fallback is also unchanged (see residual risk 1).
- **Declarations** `include/MidiSndio.h`: `struct OutputWriter;`, `static void runWriter(...)`, `m_outPipe[2]`, `std::shared_ptr<OutputWriter> m_output`, `std::thread m_writer`, `std::future<void> m_writerDone`. Includes `<future>` and `<thread>`.
- Why not terminate: `QThread::terminate()` on a thread blocked in `mio_write` did not return in the test (RED, round 2). A `QThread` also cannot be deleted while running (Qt aborts), so the abandoned writer is a `std::thread` that is detached.

## Round-2 tests (`tests/src/core/MidiSndioTest.cpp`)

- `queuedBytesAreDeliveredBeforeTeardownCloses` (barrier version, see round 3): the writer is held in its first device write, two more bytes queue behind it, and the gate opens only after `m_quit` has been observed set, so the queue is provably non-empty when teardown begins. Asserts the close happens after all three bytes were written (`closedBytes == 903C7F`).
- `stalledWriteTeardownDoesNotCloseTheHandleUnderTheWrite`: the fake device blocks the first write (`stallWrites = 1`, `writeStarted` / `writeGate`); two more bytes are queued behind it; the client is destroyed; asserts teardown returned with `closeCalls == 0`, then releases the gate and asserts the writer closed the handle once with `903C7F` delivered.
- The four round-1 tests are unchanged.

## Commands run and exit codes

Round 2 logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-57.r2/logs/`. Round-1 logs are in `.../new-p18-57.r1/logs/` (the same folder name is kept for the round-1 section below).

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| R2 RED (reviewed code, new tests) | `cd build/tests && ctest -R '^MidiSndioTest$' --output-on-failure` | `test-red-r2-1.log` | 8 (`queuedBytesAreDeliveredBeforeTeardownCloses`: `closedBytes` empty, `MidiSndioTest.cpp:170`; `stalledWriteTeardownDoesNotCloseTheHandleUnderTheWrite`: destructor hung until the QTest watchdog, 300 s) |
| R2 build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p18-57 cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `build-green-r2-1.log` | 0 |
| **R2 GREEN** | `cd build/tests && ctest -R '^(MidiSndioTest\|MidiReconnectTest)$' --output-on-failure` | `test-green-r2-1.log` | **0** (2/2 passed) |
| R2 stability | `./MidiSndioTest` x3 (offscreen) | stdout | 8 passed, 0 failed each run (1.30 s) |
| 4b: save product patch | `git diff -- src/core/midi/MidiSndio.cpp include/MidiSndio.h > logs/fix-r2.patch` | - | 0 |
| 4b: revert to reviewed code | `git apply -R logs/fix-r2.patch` | - | 0 |
| 4b: rebuild reverted | `cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `build-4b-reverted-r2.log` | 0 |
| **4b RED drain (reverted)** | `./MidiSndioTest queuedBytesAreDeliveredBeforeTeardownCloses` | `test-4b-reverted-r2-drain.log` | **1** (`closedBytes` empty; `MidiSndioTest.cpp:170`) |
| **4b RED stall (reverted)** | `./MidiSndioTest stalledWriteTeardownDoesNotCloseTheHandleUnderTheWrite` (`timeout 420`) | `test-4b-reverted-r2-stall.log` | **134** (`Test function timed out` at 300 s: teardown blocked on `terminate()`/`wait()`) |
| 4b: restore | `git apply logs/fix-r2.patch` | - | 0 |
| 4b: rebuild restored | `cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `build-4b-restored-r2.log` | 0 |
| **4b GREEN (restored)** | `./MidiSndioTest` | `test-4b-restored-r2.log` | **0** (8 passed, 0 failed) |
| R2 family build | `cmake --build build --target MidiReconnectActionTest ControlMidiReconnectSharedTest MidiSndioTest MidiReconnectTest zene -j4` | `build-family-r2-1.log` | 0 |
| **R2 family** | `cd build/tests && ctest -R 'Midi(Sndio\|Reconnect)' --output-on-failure` | `test-family-r2-1.log` | **0** (5/5 passed, including `ControlMidiReconnect`) |
| gates (raw commands, see note) | `bash tests/fork-sources-gate.sh` / `bash tests/all-sources-reproduce.sh` / `bash tests/no-upstream-regression-gate.sh` / `tests/scripted/check-namespace` / `bash tests/no-tautology-gate.sh` / `python3 tests/doc-figures-gate.py` | `gate-<name>-r2.log` | 0 / 0 / 0 / 0 / 0 / 0 |
| | `bash tests/complexity-gate.sh --check` | `gate-complexity-r2.log` | 1 (6 items in other files; 0 mention MidiSndio) |
| | `bash tests/file-length-gate.sh --check` | `gate-file-length-r2.log` | 1 (15 items in other files; 0 mention MidiSndio) |
| **feature check** (copy, see note) | `python3 -I /tmp/fc-p18-57/feature_check.py <worktree>` | `feature-check-run-r2.log` | **0** (`RESULT: PASS`, all 8 rows PASS) |
| whitespace | `git diff --check` | - | 0 |

**Note on the feature check.** `feature_check.py` puts its logs in `<worktree>/../<name>-feature-check`, which is not writable from this session (`PermissionError`). I did not edit the shared script. I run a copy at `/tmp/fc-p18-57/feature_check.py` that differs in exactly two lines: the log folder, and `ROOT` (so it reads the shared `FIX-BASE` and the already-cached base listing for `2da2ed356`). Its logic is otherwise identical, including the rule that a red complexity or file-length item only counts when it is in a changed file. None is.

**Note on build.** ccache could not write under `/run/user/1000`. Builds used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p18-57`. No code change was involved.

## Round 3: deterministic ordinary-drain regression

Review round 2 (REWORK) asked that `queuedBytesAreDeliveredBeforeTeardownCloses` prove, deterministically, that bytes are still queued when teardown begins. The round-2 version queued bytes without a barrier, so the writer could consume them before teardown and the test could pass with the bug present.

The test now works like this (`tests/src/core/MidiSndioTest.cpp`, `queuedBytesAreDeliveredBeforeTeardownCloses`):

1. `stallWrites = 1`; create the client; assert `isRunning()` (the reader is up).
2. `sendByte(0x90)`; wait on `writeStarted`, so the writer is provably blocked inside its first device write.
3. `sendByte(0x3C)`, `sendByte(0x7F)`: both are queued behind the blocked write.
4. Start teardown (`delete midi`) on its own thread, so this thread can watch the object.
5. Spin with `QThread::yieldCurrentThread()` until `midi->m_quit` is true, which the destructor sets first. The loop has a 5 s failure bound; it is not a sleep.
6. Only then `writeGate.release()`. Join the teardown thread and assert `closedBytes == 903C7F` at close.

The gate cannot open before `m_quit` is set, so the two bytes are in the pipe when teardown starts. If teardown discards them, the close check fails.

Residual timing assumption in step 5, recorded honestly: the spin reads the object's `m_quit` until it sees it. If the spinning thread were not scheduled for longer than the 1000 ms teardown grace after `m_quit` was set, the destructor would finish first and that last read would touch freed memory. Under this test the grace expiring means the gate has not been released, so the spin cannot have finished. The window needs a full second of starvation of the spinning thread, and I have not seen it. Removing it would need a test seam in production code, which I did not add. Say so if you want that seam.

Round-3 evidence (logs in `runs/new-p18-57.r3/logs/`):

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| R3 GREEN (round-2 product, barrier test) | `cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4`; `./MidiSndioTest` | `build-green-r3-1.log`, `test-green-r3-1.log` | build 0; run 0 (8 passed, 0 failed) |
| **R3 RED (round-1 product, barrier test)** | `git diff 8a6b5a0f8 HEAD -- include/MidiSndio.h src/core/midi/MidiSndio.cpp > logs/r2-product.patch`; `git apply -R logs/r2-product.patch`; rebuild; `./MidiSndioTest queuedBytesAreDeliveredBeforeTeardownCloses` x5 | `test-4b-reverted-r3-drain-1..5.log` | build 0; each run **1** (`closedBytes` is `"\x90"`, `MidiSndioTest.cpp:191`; `3C 7F` discarded) |
| R3 restore | `git apply logs/r2-product.patch`; rebuild; `./MidiSndioTest` | `build-4b-restored-r3.log`, `test-4b-restored-r3.log` | build 0; run 0 (8 passed); drain test alone x5 each 0 |
| R3 family build | `cmake --build build --target MidiReconnectActionTest ControlMidiReconnectSharedTest MidiSndioTest MidiReconnectTest zene -j4` | `build-family-r3-1.log` | 0 |
| **R3 family** | `ctest -R 'Midi(Sndio\|Reconnect)' --output-on-failure` | `test-family-r3-1.log` | **0** (5/5 passed, including `ControlMidiReconnect`) |
| gates (raw commands) | `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures` | `gate-<name>-r3.log` | 0 / 0 / 0 / 0 / 0 / 0 |
| | `bash tests/complexity-gate.sh --check` | `gate-complexity-r3.log` | 1 (6 items in other files; 0 mention MidiSndio) |
| | `bash tests/file-length-gate.sh --check` | `gate-file-length-r3.log` | 1 (15 items in other files; 0 mention MidiSndio) |
| **feature check** (copy, see note) | `python3 -I /tmp/fc-p18-57/feature_check.py <worktree>` | `feature-check-run-r3.log` | **0** (`RESULT: PASS`, all 8 rows PASS) |
| whitespace | `git diff --check` | - | 0 |

The round-2 stalled-write test is unchanged and still passes on the round-2 product code (8/8 with it). Its reverted-code result (EXIT 134, watchdog timeout) is in the round-2 table above.

## Round-1 record (kept for history)

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| R1 RED (unfixed code) | `ctest -R '^MidiSndioTest$' --output-on-failure` | `test-red-1.log` | 8 (`deviceWriteRunsOffTheCallerThread` fails at `MidiSndioTest.cpp:117`) |
| R1 GREEN | `ctest -R '^(MidiSndioTest\|MidiReconnectTest)$'` | `test-green-1.log` | 0 (2/2) |
| R1 4b | reverted: `test-4b-reverted.log` 8; restored: `test-4b-restored.log` 0 | | |
| R1 family | `ctest -R 'Midi(Sndio\|Reconnect)'` | `test-family-1.log` | 8 (`ControlMidiReconnect` flaked once; see below) |

`ControlMidiReconnect` failed once in the round-1 family run with `the engine received 0 of the 4 events the BOUND client played`. The script drives the real binary on the ALSA sequencer and never reaches `MidiSndio`. It passed on the reverted code and in every later run, including round 2 (5/5). I read it as a timing flake on a loaded machine.

## Judgement calls and residual risk

1. **Reader fallback still terminates.** `~MidiSndio` still calls `terminate()` on the reader if it does not stop within 1000 ms, as it did before this cluster. The reader's loop polls every 100 ms and only calls `mio_read` after `POLLIN`, so this should not fire in practice, but it is the same shape of risk the review raised for the writer. I did not change the reader; say so if it should move to the same abandon path.
2. **`sendByte()` makes one `write(2)` syscall.** The realtime rule forbids waits and locks on the audio path. The write is non-blocking, takes no user-space lock, and allocates nothing. Waking the writer without a syscall would need polling, which I judged worse.
3. **Bounded buffering with drop.** The pipe holds the kernel default (64 KiB). When the device has stalled long enough to fill it, new bytes are dropped. The old code blocked instead; dropping can leave a stuck note, but only after a stall that already made output unusable, and it never hangs the audio thread.
4. **Teardown timing.** `writerTeardownGrace` is 1000 ms. The stalled-write test waits for that grace, because the writer is blocked for its whole duration. The outcome does not depend on timing; only the test's runtime does (about 1.3 s).
5. **Abandoned writer outlives the object.** After a stalled teardown the detached thread keeps its handle until the device write returns, then closes it. If the device never returns, the handle and thread are leaked until process exit. That is the cost of not terminating inside sndio.
6. **Concurrent handle use.** The reader calls `mio_read` and the writer calls `mio_write` on one handle. The old code already did this from the audio thread. Not changed.
7. **`sendByte()` is not called during destruction.** `AudioEngine` joins its audio workers before it deletes `m_midiClient` (`src/core/AudioEngine.cpp:395-417`), so no `sendByte()` runs once the destructor starts. That is the contract the pipe's write end relies on.
8. **Siblings not changed.** `MidiAlsaRaw::sendByte` (`snd_rawmidi_write`, `src/core/midi/MidiAlsaRaw.cpp:108-111`) and `MidiOss::sendByte` (`putChar`, `src/core/midi/MidiOss.cpp:90-93`) also write synchronously on the caller's thread. Not in this finding; they would need their own cluster.

## Self-check

- **The regression tests FAIL with the product change reverted and PASS with it (exit codes).** Round 3: drain test on the round-1 product EXIT 1 in 5 of 5 runs; restored round-2 product: `MidiSndioTest` EXIT 0, 8/8, and the drain test alone EXIT 0 in 5 of 5 runs. Round 2: reverted drain EXIT 1, stall EXIT 134 (watchdog timeout at 300 s). Round 1: reverted EXIT 8 on `MidiSndioTest.cpp:117`, restored EXIT 0.
- **The tests are deterministic.** The device is injected: `mio_open`, `mio_write`, `mio_read`, `mio_pollfd`, `mio_revents` and `mio_close` are fakes in `MidiSndioTest.cpp`. Synchronisation uses `QSemaphore`s that the fake releases (`bytesWritten` per byte accepted, `writeStarted` when a stalled write begins, `closed` per close). The drain test's barrier is the observed `m_quit` flip (round 3, residual timing assumption above). Each test waits on exactly what it expects. The only time bounds are `tryAcquire(n, 5000)` and the 5 s failure bound on the `m_quit` spin, which return as soon as the event happens, and the production grace in residual risk 4. No sleeps. The stall test asserts `closeCalls == 0` only while the writer is provably blocked, because `writeStarted` was acquired first.
- **The fix is at the cause and complete.** `mio_write` appears only in `runWriter` (`MidiSndio.cpp:192`). `mio_close` appears in `runWriter` (`MidiSndio.cpp:200`) and in the constructor's pipe-failure path (`MidiSndio.cpp:102`), where no writer exists yet. The destructor no longer closes the handle. The ordinary path closes the handle before the destructor returns, which keeps the reconnect test's shutdown checks true (`MidiReconnectTest` 15 passed, 0 failed, re-run this round). The failed-open path is unchanged. Callers of `sendByte()` are unchanged.
- **No collateral damage.** No saved-data format, no control command (no `ControlCommandsSnapshot` change), no limit that other callers hit. Behaviour changes are residual risks 2, 3 and 5. Project files are unaffected.
- **Verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed.**
