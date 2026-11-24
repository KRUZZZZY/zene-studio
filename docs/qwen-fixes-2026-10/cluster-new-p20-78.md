FIXED

# cluster new-p20-78: full sndio output pipe dropped arbitrary MIDI bytes

## Decision

REAL. At the starting HEAD, `src/core/midi/MidiSndio.cpp:169` was:

```cpp
[[maybe_unused]] const ssize_t queued = ::write( m_outPipe[1], &c, 1 );
```

`sendByte()` is called once per byte by `MidiClientRaw::processOutEvent()`.
When the non-blocking pipe was full, `write()` returned `-1`/`EAGAIN`, the
result was ignored, and exactly that byte disappeared. A note-on or note-off
could therefore reach the device with one byte missing.

## Fix

- `src/core/midi/MidiSndio.cpp:159` now stages the bytes of one event on the
  calling thread. `processOutEvent()` at line 170 encodes that event once and
  pushes the complete event into a bounded MPSC queue.
- `include/MidiSndio.h:90` sets the queue bound to 1024 whole events.
  `src/core/midi/MidiSndio.cpp:189` refuses the whole event when the queue is
  full and increments `dropped`; it never stores a fragment.
- `src/core/midi/MidiSndio.cpp:196` reports refused plus undelivered events
  through the existing `outputQueueOverflowCount()` contract used by
  `midi.retro_capture_status`. No command was added.
- `src/core/midi/MidiSndio.cpp:206` keeps the single device-writing thread,
  writes one event completely, retries `EINTR`, counts a device-refused event,
  and stops after a partial write rather than putting the next event after a
  torn message. Ordinary teardown (`:130`) drains queued events; a stalled
  write is still abandoned rather than terminated (`:139`).

## Test matrix

| Path | Regression/coverage | Result |
| --- | --- | --- |
| Normal encoding and ordering | `queuedMessagesReachTheDeviceInSendOrder` | PASS |
| Failed device open | `failedOpenDoesNotWrite` | PASS |
| Queue exhaustion | `fullOutputQueueRefusesWholeEventsAndCountsOverflow` | PASS with fix, FAIL without it |
| Ordinary teardown/reconfiguration | `queuedBytesAreDeliveredBeforeTeardownCloses` | PASS |
| Stalled device write/teardown | `stalledWriteTeardownDoesNotCloseTheHandleUnderTheWrite` | PASS |
| Audio-thread constraint | `processOutEvent()`/`sendByte()` only touch thread-local bytes and one `MpscQueue::push`; no allocation, lock, wait, log or system call. The blocking `mio_write()` is on `runWriter()`. | Design inspection |
| Build gate | Test registered only under `if(LMMS_HAVE_SNDIO)` in `tests/CMakeLists.txt:1976`; this build has `LMMS_HAVE_SNDIO`. | PASS |

The exhaustion test does not use elapsed time as an oracle. It stalls the fake
device with `writeStarted`/`writeGate`, sends exactly the queue capacity, then
one more complete note-off. It proves both the count (`1`) and that the refused
`80 7F 00` never appears; the device receives exactly the accepted whole events.

## Commands and EXIT codes

All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r1/logs/`.

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| Baseline build | `cmake --build build --target MidiSndioTest -j4` | `build-0-baseline.log` | 0 |
| Baseline test | `ctest -R '^MidiSndioTest$' --output-on-failure` (from `build/tests`) | `test-0-baseline.log` | 0 |
| Preliminary RED build | `cmake --build build --target MidiSndioTest -j4` | `build-1-red.log` | 2 (test needed `TimePos.h`) |
| Preliminary RED build | `cmake --build build --target MidiSndioTest -j4` | `build-1b-red.log` | 2 (unconfigured allocation probe; probe removed) |
| RED build | `cmake --build build --target MidiSndioTest -j4` | `build-2-red.log` | 0 |
| **RED test** | `ctest -R '^MidiSndioTest$' --output-on-failure` | `test-1-red.log` | **8** (`overflowCount` was `0`, expected `1`) |
| GREEN build | `cmake --build build --target MidiSndioTest -j4` | `build-3-green.log` | 0 |
| **GREEN test** | `ctest -R '^MidiSndioTest$' --output-on-failure` | `test-2-green.log` | **0** |
| Header rebuild | `cmake --build build --target zene MidiSndioTest -j4` | `build-4-all-green.log` | 0 |
| GREEN family | `ctest -R '^MidiSndioTest$' --output-on-failure` | `test-3-green-all.log` | 0 |

### Reverse-patch proof (step 4b)

| Command | Log/output | EXIT |
| --- | --- | --- |
| `git diff -- include/MidiSndio.h src/core/midi/MidiSndio.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r1/logs/fix.patch; echo EXIT=$?` | `fix.patch` written | 0 |
| `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r1/logs/fix.patch; echo EXIT=$?` | product change reversed | 0 |
| `cmake --build build --target zene MidiSndioTest -j4` | `build-5-reversed.log` | 0 |
| `ctest -R '^MidiSndioTest$' --output-on-failure` | `test-4-reversed-red.log` | **8** |
| `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r1/logs/fix.patch; echo EXIT=$?` | product change restored | 0 |
| `cmake --build build --target zene MidiSndioTest -j4` | `build-6-restored.log` | 0 |
| `ctest -R '^MidiSndioTest$' --output-on-failure` | `test-5-restored-green.log` | **0** |

The reversed RED failure was again
`Actual (outputQueueOverflowCount()): 0`, `Expected: 1` at
`tests/src/core/MidiSndioTest.cpp:207`.

### Gates

Command:

```text
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p20-78
```

Saved as `gates-1.log`, unpiped `EXIT=0`:

```text
feature-check kads-new-p20-78 (base fa4a5f2e7, 655 changed files; logs in /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p20-78/build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

1. **Reverse proof.** Reversing only the product hunks gave `EXIT=8` on
   `MidiSndioTest`; restoring them gave `EXIT=0`. Both are in `test-4-reversed-red.log`
   and `test-5-restored-green.log`.
2. **Determinism.** The test injects the device stall through the existing
   fake `mio_write()` seam (`stallWrites`, `writeStarted`, `writeGate`) and uses
   the exact 1024-event bound. `tryAcquire(..., 5000)` is only a hang guard; the
   pass/fail assertions are on queue count and the exact byte stream. There is
   no sleep or elapsed-time assertion in the test.
3. **Cause and completeness.** The production caller of `sendByte()` within
   `MidiClientRaw::processOutEvent()` now goes through the whole-event override
   at `src/core/midi/MidiSndio.cpp:170`; the non-audio clock path enters the
   same virtual `processOutEvent()` through
   `MidiClientRaw::processNonAudioOutEvent()`, so its one-byte real-time
   messages are queued whole too. `outputQueueOverflowCount()` is the existing
   overflow contract and needs no sibling command. `MidiOss` is an independent
   backend; its analogous issue is recorded below rather than silently changed
   outside this finding.
4. **No collateral damage.** No project or save format changed. The new bound
   applies only to undelivered output events waiting for the sndio writer; a
   full queue refuses and counts the newest whole event, while callers under
   the bound see the same event encoding and ordering. The fix keeps the old
   pre-open no-write behaviour and the old teardown/stalled-write ownership
   rules.
5. **Verdict evidence.** REAL is evidenced by the starting-HEAD code at
   `src/core/midi/MidiSndio.cpp:169` and the RED run. This is not an
   ALREADY-FIXED or FALSE-POSITIVE claim.
6. **Sibling commands.** This commit changes no `ControlCommands*`,
   `ControlRegistry`, `ControlReversibilityTable` or command snapshot file.
   `git show --stat --oneline HEAD` lists exactly the five files above. The
   `app.*`/`arrangement.*` ids in the pre-submit question come from the
   train-wide merge-base diff, not from this fix; no command was added or
   changed, so there is no command sibling to update.

## Other findings

- `src/core/midi/MidiOss.cpp:198` has the same byte-granular non-blocking pipe
  pattern (`::write()` result ignored) in the separate OSS backend. It is not a
  caller of the changed sndio code and was left for its own finding.
