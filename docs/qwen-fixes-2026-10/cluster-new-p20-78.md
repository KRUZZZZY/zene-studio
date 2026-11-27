FIXED

# cluster new-p20-78: full sndio output pipe dropped arbitrary MIDI bytes

new-p20-78: FIXED (round 3: the round-2 review and its diagnostic checkpoint are addressed below)

## Decision

REAL. On the legacy code (`git show 9913d36d53:src/core/midi/MidiSndio.cpp`, `sendByte()`),
each byte went to a non-blocking pipe with
`[[maybe_unused]] const ssize_t queued = ::write( m_outPipe[1], &c, 1 );`. When the pipe
was full, the byte was discarded. The round-3 legacy run shows the result:
`overflowReleasesEverySoundingNoteAfterDraining` fails on that code at `'!malformed'`,
because the stream the device received contains a cut frame (log 19).

## The fix (current line numbers)

| Review point | Where it is handled |
| --- | --- |
| Stuck note after a refused Note Off; explicit overflow policy | `include/MidiSndio.h:75` documents the policy. When the queue overflows, every event is refused and counted until everything accepted before the overflow is delivered. The writer then releases every note it has sounded on the device, held notes included, with `0x80\|ch key 00`, and only then accepts events again. A producer records only the flag and the drop count (`submit`, `src/core/midi/MidiSndio.cpp:108`, flag at `:116`). The writer keeps a plain 2048-bit `sounding` bitmap that only it touches. `trackSounding` (`:128`) is called from `deliver` (`:150`) after a complete, successful write: 0x9n with velocity above 0 sets a bit, and 0x8n or 0x9n with velocity 0 clears it. `recoverOverflow` (`:178`) runs while `overflow` is still set, writes a release for every bit, and clears `overflow` as its last step (`:200`). |
| Partial write then failure freed the handle under the reader | `deliver` (`:150`) treats a short write confirmed by `mio_eof()` as terminal and sets `failed`; per mio_open(3) errors are fatal, https://man.openbsd.org/mio_open.3. The writer then keeps consuming until `closing`. `mio_close` (`:361`) runs only after the loop exits, and the destructor stores `closing` (`:251`) only after the reader thread has stopped. Unchanged from round 2. |
| Later sends after a terminal failure went unreported | `submit` (`:108`) refuses and counts once `failed` is set. The writer is still running, so it counts any event that raced past that check. Unchanged from round 2. |
| Teardown could count, not write, a final event | `runWriter` (`:336`) reads `closing` before it pops (`:340`) and exits only on that earlier `closing` plus an empty pop. Unchanged from round 2. |
| Test sampled live output; legacy run did not lose bytes | Every overflow test now judges `closedBytes`, the stream captured by the fake `mio_close` after the writer has finished. The main test sends 40000 three-byte events while the writer is held at a fixed write number. That is more than a 64 KiB pipe holds, so the legacy code cuts a frame every time (log 19, `'!malformed'`). |

Concurrency argument (comments above `OutputWriter` and `runWriter`). TSan does not run on
this box, so this is reasoned, not measured. `overflow`, `pushing` and `failed` use
seq_cst.
- **The flag is visible once no push is in flight.** A refusing producer sets
  `overflow` before it leaves the `pushing` window.
- **When the queue is truly drained.** Suppose the writer sees `overflow`, then
  `pushing == 0`, then an empty pop. Then every push that started earlier has finished
  and is visible to the pop. That also covers a slot that a push has reserved but not
  yet published, which `MpscQueue::pop` reports as empty. Every later push sees
  `overflow` and is refused.
- **Why releases cannot cut a new note.** `overflow` stays set until the last release
  is written, so nothing accepted after the overflow can be cut by a release.
- **Single-thread state.** `sounding` is touched only by the writer thread.

Audio-thread path (`processOutEvent` → `submit`): thread_local staging, two lock-free RMWs
on `pushing`, two atomic loads, one `MpscQueue::push` CAS, and on refusal one store and a
relaxed `fetch_add`. There is no allocation, lock, wait, log or system call
(`static_assert` that `atomic<uint32_t>` is lock-free).

## Test matrix (`tests/src/core/MidiSndioTest.cpp`)

| Path | Test | Fix | Round-2 code (committed HEAD) | Legacy pipe (9913d36d53) |
| --- | --- | --- | --- | --- |
| Overflow drains and releases every sounding note (the refused Note Off's and a held one), exact final stream | `overflowReleasesEverySoundingNoteAfterDraining` (:312) | PASS | FAIL `'sounding.isEmpty()'` (:345) | FAIL `'!malformed'` (:344): byte loss cut a frame |
| Exhaustion: refused event never reaches the device | `fullOutputQueueRefusesWholeEventsAndCountsOverflow` (:280) | PASS | PASS | FAIL count 0 vs 1 (:298) |
| Terminal write failure, handle lifetime, later sends counted | `terminalWriteFailureKeepsTheHandleUntilTheReaderStops` (:358) | PASS | PASS | FAIL `writerReacted` (:368) |
| Final enqueue before teardown | `eventQueuedJustBeforeTeardownIsDelivered` (:385) | PASS | PASS | PASS |
| Normal order, failed open, off-thread write, teardown drain, stalled teardown | existing tests | PASS | PASS | PASS |

The terminal-failure test failed on round-1 code (`closeCalls` 1 vs 0) in round 2. The
round-2 handling of that case is unchanged here.

How the main test stays deterministic:
- **Fixed stall point.** The new fake-device hook `stallOnWriteNumber` (`:45`) is set
  before the client is constructed. It holds the writer inside a chosen device write,
  here write 3.
- **Each Note On is waited for separately.** So Note On 0x3C is write 1 and Note On
  ch1 0x40 is write 2 on both implementations, and the first filler is write 3.
- **No timing oracles.** The test uses no sleeps and no elapsed-time assertions.
  `tryAcquire(..., 5000)` is only a hang guard.
- **Qt 5 and Qt 6.** Indexing uses `int` with `QByteArray::at(int)`, and the file has
  no `qsizetype`.

Test limitation: `eventQueuedJustBeforeTeardownIsDelivered` is coverage. The bad
interleaving (pop finds the queue empty → event published → `closing` stored →
`closing` read) cannot be forced without a seam in the idle path. With the fix, the test's
outcome does not depend on the interleaving.

## Commands and EXIT codes

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r3/logs/`. Builds ran in the
worktree, ctest and test binaries in `build/tests`. Build command for every row marked
"build": `cmake --build build --target zene MidiSndioTest MidiReconnectTest -j4`.

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| Green build (Sonnet) | build | `01-build-green.log` | 0 |
| Green test (Sonnet) | `ctest -R '^(MidiSndioTest\|MidiReconnectTest)$' --output-on-failure` | `02-test-green.log` | 0 |
| Earlier proof (superseded by the test edit below) | same sequence | `03`–`12` | red 8 / green 0 / gates 0 |
| 4b: save and reverse product diff | `git diff -- src/core/midi/MidiSndio.cpp include/MidiSndio.h > /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r3/logs/fix.patch && git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r3/logs/fix.patch` | `fix.patch` | 0 |
| 4b: red build | build | `13-build-red.log` | 0 |
| **4b: RED** | `ctest -R '^MidiSndioTest$' --output-on-failure` | `14-test-red.log` | **8** |
| 4b: red functions | `./MidiSndioTest` | `15-red-functions.log` | 1 (11 passed, 1 failed) |
| 4b: restore | `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-78.r3/logs/fix.patch` | — | 0 |
| 4b: green build | build | `16-build-green.log` | 0 |
| **4b: GREEN** | `ctest -R '^MidiSndioTest$' --output-on-failure` | `17-test-green.log` | **0** |
| Legacy: reverse to 9913d36d53 | `git diff 9913d36d53 -- src/core/midi/MidiSndio.cpp include/MidiSndio.h > .../legacy.patch && git apply -R .../legacy.patch` | `legacy.patch` | 0 |
| Legacy build | build | `18-build-legacy.log` | 0 |
| **Legacy RED** | `./MidiSndioTest` | `19-legacy-functions.log` | **3** (9 passed, 3 failed) |
| Legacy restore | `git apply .../legacy.patch` | — | 0 |
| Final build | build | `20-build-final.log` | 0 |
| Final tests x10 | `ctest -R '^(MidiSndioTest\|MidiReconnectTest)$' --repeat until-fail:10 --output-on-failure` | `21-test-final.log` | 0 (2/2) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p20-78` | `22-gates.log` | 0 (all 8 gates PASS, RESULT: PASS) |

## Self-check

1. **Reverse proof.** Reversing only this round's product hunks (`fix.patch`) gave
   `EXIT=8` (`14-test-red.log`; `overflowReleasesEverySoundingNoteAfterDraining` failed at
   `sounding.isEmpty()`, :345). Restoring them gave `EXIT=0` (`17-test-green.log`). On the
   legacy pipe code, 3 tests fail, the main one at `'!malformed'` (`19-legacy-functions.log`).
2. **Determinism.** See the matrix notes. The test seams are the fake `mio_*` functions,
   `stallOnWriteNumber`/`writeGate`, `failAfterBytes`/`deviceEof`/`writerReacted`, and
   `closedBytes`, which is captured at `mio_close`.
3. **Cause and completeness.** Both producers reach `processOutEvent` → `submit`: the
   audio path and the clock path through `MidiClientRaw::processNonAudioOutEvent`
   (`src/core/midi/MidiClient.cpp:68`). Only the writer calls `mio_write`/`mio_close` and
   touches `sounding`. `outputQueueOverflowCount()` keeps its meaning (refused +
   undelivered) for its one consumer (`src/core/ControlCommandsMidi.cpp:275`).
   `MidiReconnectTest`, the other test that links `MidiSndio.cpp`, passes 10 of 10
   (`21-test-final.log`).
4. **No collateral damage.** No project, save format or command changed. Below the queue
   bound the event encoding and order are unchanged, and no release is sent (the
   exact-stream assertions in `fullOutputQueueRefusesWholeEventsAndCountsOverflow` and the
   send-order tests hold). Releasing held notes happens only when an overflow ends, and the
   header documents it as the policy.
5. **Verdict evidence.** The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE. The
   legacy `sendByte()` code is quoted under Decision, and the legacy red is log 19.
6. **Sibling commands.** No `ControlCommands*`, `ControlRegistry`,
   `ControlReversibilityTable` or command snapshot file is touched, so there is no
   command sibling to update.
7. **Registration.** `tests/upstream-modifications.txt` updates the reasons on the
   existing `src/core/midi/MidiSndio.cpp` and `include/MidiSndio.h` entries (new policy,
   renamed test). The test file is already in `tests/fork-sources.txt`.

Delegation: Sonnet implemented the checkpoint design in the product code, the header
policy and the test changes, including the `stallOnWriteNumber` hook and the Qt 5/6
indexing (logs 01–02). Haiku ran two red/green/legacy/gates sequences (logs 03–12, then
13–21). I reviewed both diffs. I also changed the main test so that each Note On is waited
for and the stream is judged before the count, which makes the legacy run reach byte loss
deterministically. I wrote the registration update, this report and the commit, and ran
the final gate and the completion check. DeepSeek was not used.

## Other findings

- `src/core/midi/MidiOss.cpp:198` has the same byte-granular non-blocking pipe pattern in
  the separate OSS backend. It is left for its own finding.
- The reader (`MidiSndio::run`) and the writer use one `mio_hdl` from two threads at once.
  This predates this change and is not changed here.
