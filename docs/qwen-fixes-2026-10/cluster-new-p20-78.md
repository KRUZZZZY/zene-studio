FIXED

# cluster new-p20-78: full sndio output pipe dropped arbitrary MIDI bytes

new-p20-78: FIXED (round 2: every point the round-1 review raised is addressed below)

## Decision

REAL. On the legacy code (`git show 9913d36d53:src/core/midi/MidiSndio.cpp`, `sendByte()`),
each byte went to a non-blocking pipe with
`[[maybe_unused]] const ssize_t queued = ::write( m_outPipe[1], &c, 1 );`. When the
pipe was full, that byte disappeared: a message could be cut, or a whole Note Off could
be lost, which leaves the note sounding. The legacy red run below shows both:
`refusedNoteOffIsReleasedAfterOverflowDrains` and
`fullOutputQueueRefusesWholeEventsAndCountsOverflow` fail on that code.

## Round-1 review points and how round 2 answers them

| Review point | Round-2 change (current line numbers) |
| --- | --- |
| A refused Note Off had no recovery, so its note stayed stuck | `OutputWriter::noteRefused` (`src/core/midi/MidiSndio.cpp:130`) records a refused Note Off (0x8n, or 0x9n with velocity 0) in a fixed 16x128 bitmap of lock-free `atomic<uint64_t>`. The refusal also sets `overflow` (`submit`, `:111`). While `overflow` is set, every event is refused, so no Note On can get ahead of the recovery. When the writer sees `overflow`, with no push in flight and the queue empty, `recoverOverflow` (`:171`) clears the flag, takes the bitmap and writes `80\|ch key 00` for each recorded note. That is at most 2048 messages. |
| A partial write followed by failure called `mio_close` while the reader still used `m_hdl` | `deliver` (`:146`) treats a short write as terminal when `mio_eof()` confirms it (mio_open(3): errors are fatal, and afterwards only `mio_close`/`mio_eof` work, https://man.openbsd.org/mio_open.3). It then sets `failed`. The writer never leaves its loop because of a failure. It keeps consuming until `closing`, and `mio_close` (`:347`) runs only after the loop ends. The destructor stores `closing` (`:239`) only after the reader thread has stopped. |
| After a terminal failure, later events went into an abandoned queue and were never counted | `submit` (`:111`) refuses an event and counts it in `dropped` once `failed` is set. An event that raced past the check is popped by the still-running writer and counted in `undelivered` (`deliver`, `:146`). |
| Teardown could count, rather than write, an event published between the empty pop and the `closing` check | `runWriter` (`:322`) reads `closing`, `overflow` and `pushing` before it pops (`:326`). It leaves only when the `closing` it saw is true and the pop is empty. `closing` is stored after the producers are gone, so an empty pop at that point is final, and every accepted event is written. |
| The overflow test sampled live bytes, and its legacy run did not show byte loss | Every overflow test now checks `closedBytes`, the stream captured by the fake `mio_close` after the writer has finished. The new stuck-note test sends 40000 three-byte events while the writer is held, which is more than any default pipe holds. The legacy code therefore loses bytes deterministically (legacy red, below). |

Producer path (audio thread, `processOutEvent` `:275` → `submit` `:111`): it stages the
bytes in a thread_local array, does two lock-free RMWs on `pushing`, loads two atomics,
makes one `MpscQueue::push` CAS, and on refusal does at most one `fetch_or` and a store.
There is no allocation, lock, wait, log or system call. The `static_assert`s require the
atomics to be always lock-free.

Concurrency argument (comment above `runWriter`). TSan does not run on this box, so this
is reasoned, not measured. All protocol atomics use seq_cst.
- **Recording before leaving the window:** a refusal records its bit and `overflow`
  before it leaves the `pushing` window.
- **When recovery is safe:** suppose the writer sees `overflow`, then `pushing == 0`,
  then an empty pop. Then every push that started earlier has finished and is visible
  to the pop. Every later push sees `overflow` and is refused. So the recovery lands
  after everything accepted before the refusal, and before everything accepted after
  the flag is cleared. Waiting for `pushing == 0` also covers a slot that a push has
  reserved but not yet published, which `MpscQueue::pop` reports as empty.
- **Flag before bitmap:** the writer clears `overflow` before it takes the bitmap. A
  refusal that sets a bit after the take also sets the flag again, so the next round
  handles it.

## Test matrix (`tests/src/core/MidiSndioTest.cpp`)

| Path | Test | Fix | Round-1 code (HEAD before this change) | Legacy pipe code (9913d36d53) |
| --- | --- | --- | --- | --- |
| Overflow + recovery of a sounding note | `refusedNoteOffIsReleasedAfterOverflowDrains` (:301) | PASS | FAIL `sounding.isEmpty()` (:329) | FAIL count 0 vs 38977 (:320) |
| Exhaustion, refused event never written | `fullOutputQueueRefusesWholeEventsAndCountsOverflow` (:271) | PASS | PASS | FAIL count 0 vs 1 (:289) |
| Terminal write failure, handle lifetime, later sends counted | `terminalWriteFailureKeepsTheHandleUntilTheReaderStops` (:337) | PASS | FAIL `closeCalls` 1 vs 0 (:348) | FAIL `writerReacted` never released (:347) |
| Final enqueue just before teardown | `eventQueuedJustBeforeTeardownIsDelivered` (:364) | PASS | PASS (see note) | PASS |
| Normal order, failed open, off-thread write, teardown drain, stalled teardown | existing tests | PASS | PASS | PASS |
| sndio link stub for the second test that links MidiSndio.cpp | `tests/src/core/MidiReconnectTest.cpp` gets an `mio_eof` stub | PASS | n/a | n/a |

How each test stays deterministic:
- **No timing oracles.** The tests use no sleeps and no elapsed-time assertions.
- **Stalls go through the fake device.** `stallWrites`/`writeGate` hold the writer
  inside a fake `mio_write`.
- **The failure test has a fixed sync point.** `failAfterBytes` makes the device accept
  1 byte and then report EOF/EIO. The test waits on `writerReacted`, which is released
  by the fake `mio_eof` or `mio_close`, whichever the writer reaches first. Round-1
  code reaches `mio_close` first, so `closeCalls == 1` there every time.
- **Bounded waits only.** `tryAcquire(..., 5000)` and the 5 s `overflowCountReaches`
  loop only wait for conditions that must become true. They never decide the result.

Note on `eventQueuedJustBeforeTeardownIsDelivered`: with the fix its outcome does not
depend on the interleaving. The round-1 interleaving it guards against (pop finds the
queue empty → event published → `closing` stored → `closing` read) cannot be forced
without a seam in the writer's idle path, so this test is coverage and does not fail on
round-1 code. The ordering fix rests on the code argument above.

## Commands and EXIT codes

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r2/logs/`. Builds ran in the
worktree, ctest and test binaries in `build/tests`.

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| Green build (Sonnet) | `cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `01-build-green.log` | 0 |
| Green, 20 repeats (Sonnet) | `ctest -R '^(MidiSndioTest\|MidiReconnectTest)$' --repeat until-fail:20 --output-on-failure` | `03-test-green-exact.log` | 0 |
| Green build after the final product edit | `cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `05-build-green.log` | 0 |
| Green test | `ctest -R '^(MidiSndioTest\|MidiReconnectTest)$' --output-on-failure` | `06-test-green.log` | 0 |
| 4b: save and reverse product diff | `git diff -- src/core/midi/MidiSndio.cpp include/MidiSndio.h > /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r2/logs/fix.patch` | `fix.patch` | 0 |
| 4b: red build | `cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `07-build-red.log` | 0 |
| **4b: RED** | `ctest -R '^MidiSndioTest$' --output-on-failure` | `08-test-red.log` | **8** |
| 4b: red functions | `./MidiSndioTest` | `09-red-functions.log` | 2 (10 passed, 2 failed) |
| 4b: restore | `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r2/logs/fix.patch` | — | 0 |
| 4b: green build | `cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `10-build-green2.log` | 0 |
| **4b: GREEN** | `ctest -R '^MidiSndioTest$' --output-on-failure` | `11-test-green2.log` | **0** |
| Legacy: reverse to 9913d36d53 | `git diff 9913d36d53 -- src/core/midi/MidiSndio.cpp include/MidiSndio.h > .../legacy.patch && git apply -R .../legacy.patch` | `legacy.patch` | 0 |
| Legacy build | `cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `12-build-legacy-red.log` | 0 |
| **Legacy RED** | `./MidiSndioTest` | `13-legacy-red-functions.log` | **3** (9 passed, 3 failed) |
| Legacy restore | `git apply .../legacy.patch` | — | 0 |
| Final build | `cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4` | `14-build-final.log` | 0 |
| Final tests | `ctest -R '^(MidiSndioTest\|MidiReconnectTest)$' --output-on-failure` | `15-test-final.log` | 0 (2/2) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p20-78` | `16-gates.log` | 0 |

`02-test-green.log` (EXIT=8) used the looser regex `MidiSndio|MidiReconnect`. That regex
also matched `ControlMidiReconnectSharedTest` and `MidiReconnectActionTest`, which this
build does not include ("Not Run"). The exact-name runs above replace it.

Gate table (`16-gates.log`, EXIT=0):

```text
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

1. **Reverse proof.** Reversing only the product hunks of this round (`fix.patch`) gave
   `EXIT=8` (`08-test-red.log`). Two tests failed there: `refusedNoteOffIsReleasedAfterOverflowDrains`
   and `terminalWriteFailureKeepsTheHandleUntilTheReaderStops`. Restoring the hunks gave
   `EXIT=0` (`11-test-green2.log`). The legacy pipe code fails 3 tests (`13-legacy-red-functions.log`).
2. **Determinism.** See the test matrix notes. Each red failure follows from the code
   under test, not from scheduling. The one test that cannot fail on round-1 code
   (`eventQueuedJustBeforeTeardownIsDelivered`) is labelled as coverage.
3. **Cause and completeness.** Both producers, the audio path and the clock path through
   `processNonAudioOutEvent()`, reach `processOutEvent` → `submit`. The writer is the only
   thread that calls `mio_write` and `mio_close`. `outputQueueOverflowCount()` keeps its
   contract (refused + undelivered) and its one consumer (`src/core/ControlCommandsMidi.cpp:275`),
   and its doc comment now describes the recovery. The other test that links
   `MidiSndio.cpp` (`MidiReconnectTest`) has the new `mio_eof` stub and passes.
4. **No collateral damage.** No project or save format changed and no command was added.
   Below the queue bound the event encoding and order are unchanged. A recovery note-off
   is sent only for a Note Off that was refused. Sending it for a note that was not
   sounding does nothing.
5. **Realtime.** The audio-thread path is listed above: no allocation, lock, wait, log or
   syscall. The bitmap and counters are fixed members of `OutputWriter`, allocated once
   in the constructor.
6. **Sibling commands.** No `ControlCommands*`, `ControlRegistry`,
   `ControlReversibilityTable` or command snapshot file is touched, so there is no
   command sibling to update.
7. **Registration.** `tests/upstream-modifications.txt` updates the reasons on the
   existing `src/core/midi/MidiSndio.cpp` and `include/MidiSndio.h` entries. Both test
   files are already in `tests/fork-sources.txt`. The fork-sources gate passes.
8. **Verdict evidence.** The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE. REAL
   rests on the legacy `sendByte()` ignoring the result of `::write()` (quoted under
   Decision) and on the legacy red run (`13-legacy-red-functions.log`, 3 failures).
   Product code tested: commit `5999bead02` (proof manifest `proof.json`). The completion
   check (`17-submit-check.log`) gave EXIT=0, RESULT: PASS.

Delegation: Sonnet implemented the designed product change and the four tests and ran
the first green builds (logs 01–04). Haiku ran the green, 4b red/green and legacy red
build/test sequence (logs 05–15). I designed the fix and reviewed both diffs. I also moved
the refusal record inside the `pushing` window, a gap Sonnet flagged, and wrote the
registration update, the gate run (16), this report and the commit. DeepSeek was not used.

## Other findings

- `src/core/midi/MidiOss.cpp:198` has the same byte-granular non-blocking pipe pattern
  (`::write()` result ignored) in the separate OSS backend. It is not a caller of the
  sndio code and is left for its own finding.
- The reader (`MidiSndio::run`) and the writer use one `mio_hdl` from two threads at once
  (`mio_read` and `mio_write`). Upstream already did this. This round does not change it.
