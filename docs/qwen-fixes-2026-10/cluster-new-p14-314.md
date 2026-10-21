# Cluster new-p14-314 (batch: new-p16-230, new-p14-313) — the SMF note reader

Verdict: **FIXED** — all three clusters of this batch.

new-p14-314: FIXED
new-p16-230: FIXED
new-p14-313: FIXED

All three findings sit in one pure reader, `readSmfNotes()`
(`src/core/SmfNoteRead.cpp`, declared in `include/SmfNoteRead.h`), whose only caller is the
MIDI import path `src/core/ControlCommandsClipImportFile.cpp:152`. Each bug got its own
deterministic regression slot in a new test file, `tests/src/core/SmfNoteReadTest.cpp`
(`ClipImportFileTest`, the import path the reader serves, is at the 500-line file-length cap),
its own proof (fix patch reverted -> EXIT 8, restored -> EXIT 0), and its own commit pair
(test+seam, then fix). Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-314.r1/logs`.

## Evidence

### new-p14-314 — closing an overlapping note moved the whole queue behind it

- Cause (pre-fix `src/core/SmfNoteRead.cpp:159`): `noteMessage()` closed the oldest open note of a
  pitch with `queue.erase(queue.begin())` on a `std::vector`. The pairing is correct FIFO, but a
  vector's front erase moves every element behind the erased one, so each close of a many-deep
  overlap paid for the whole queue and reading a track of many overlapping notes of one pitch was
  quadratic.
- Fix: the per-pitch queue is a `std::deque` (`src/core/SmfNoteRead.cpp:151,179`) and the close is
  `pop_front()` (`src/core/SmfNoteRead.cpp:198`), which moves nothing. Pairing, `closeAll()`,
  running status and every other reader behaviour are unchanged.
- Test seam (committed before the fix, so the fix touches only the container): every queue element
  is a `QueuedNote` that counts its own copy/move (`src/core/SmfNoteRead.cpp:116-140`);
  `smfNoteQueueShiftCount()` exposes the cumulative count (`include/SmfNoteRead.h:76`,
  `src/core/SmfNoteRead.cpp:362`). The count is wired whatever holds the queue, so a shuffling
  removal cannot return without the test failing — reverting only the fix patch restores both the
  vector and the counting elements, and the count fires again.
- Test: `SmfNoteReadTest::closingOverlappingNotesOfOnePitchDoesNotShiftTheQueue`
  (`tests/src/core/SmfNoteReadTest.cpp:84`). First it pins FIFO pairing with fixed bytes (three
  overlapping C4 notes: velocities 100/64/80, starts 0/5/15, lengths 10/15/5, endTick 20 — the
  first note-off closes the oldest). Then it opens and closes 4000 notes of one pitch and asserts
  the queue element moves stay within `2 * overlap`.
- RED: `closing 4000 overlapping notes of one pitch moved 8002095 queue elements; the linear bound
  is 8000` (SmfNoteReadTest.cpp:114 at that run) — `ctest` EXIT=8
  (`test-3-red-new-p14-314.log`, `test-5-family-at-red.log`). GREEN: EXIT=0
  (`test-6-green-new-p14-314.log`).

### new-p16-230 — the end-of-track marker was not validated

- Cause (pre-fix `src/core/SmfNoteRead.cpp:183`): `skippedEvent()` set `walk->ended` when it saw
  the `0x2F` meta event without checking the event's length or that nothing followed it. A marker
  carrying a payload was accepted, and any event behind the marker — a note included — belonged to
  no track while the file still read as well formed up to the marker. Both are refusals, not
  truncations, per SMF 1.0.
- Fix: `src/core/SmfNoteRead.cpp:223-227` — the marker must carry no payload and be the track's
  last event, else the track is refused. Presence stays optional: a file that merely loses its end
  marker (a truncated cut) still reads, the contract `ClipImportFileTest`'s cut loop pins.
- Siblings/callers checked: the only other end-of-track parser in the tree is the fork's own
  `src/core/SmfInterchangeReader.cpp`, which has held this contract since it was written
  (`readEndOfTrack`, :212-220; `sawEndOfTrack` + `atEnd` refusal, :346-358) — it is unchanged, and
  the note reader now matches it. `SmfInterchange` writes the marker on export (unchanged).
  `readSmfNotes()`'s sole caller is `ControlCommandsClipImportFile.cpp:152` (import), which
  surfaces the refusal message and writes nothing on `!ok`.
- Test: `SmfNoteReadTest::anEndOfTrackEventMustBeZeroLengthAndTheLastEventOfItsTrack`
  (`tests/src/core/SmfNoteReadTest.cpp:131`): a note lying past the marker is refused, a marker
  with a payload is refused, and a clean one-note file still reads (start 0, length 24).
- RED: `!past.ok ... a note lying past the end-of-track marker was read` (SmfNoteReadTest.cpp:131
  at that run) — EXIT=8 (`test-9-red-new-p16-230.log`, proof `test-11-proof-230-reverted-red.log`).
  GREEN: EXIT=0 (`test-10-green-new-p16-230.log`, proof `test-12-proof-230-restored-green.log`).

### new-p14-313 — still-open notes were not counted against the note cap

- Cause (pre-fix `src/core/SmfNoteRead.cpp:215,218`): `readTrack()` enforced `SmfNoteMaxCount`
  (200000) against the output list only — `out->size() > SmfNoteMaxCount` after each event, and
  once more after `closeAll()` had pushed the still-open notes. A note-on that never closes does
  not reach the output list until its track ends, so a file of unclosed note-ons could hold a queue
  deeper than the cap and was refused only after every one of them had been materialised: the bound
  held on paper while the reader paid past it in memory first.
- Fix: the walk counts its still-open notes (`TrackWalk::openCount`, `src/core/SmfNoteRead.cpp:149`)
  beside the closed ones, refuses the open that would pass the cap (`noteCapHit`, :150 and :182-185;
  the close decrements it at :199, `closeAll()` resets it at :172), and `readTrack()` answers a
  tri-state (`TrackResult`, :252-268) so `readChunks()` keeps both messages it always had, exactly:
  `the MIDI file holds more than 200000 notes` for the cap, `track %1 of the MIDI file is malformed`
  for anything else (:314-322).
- Test: `SmfNoteReadTest::openNotesAreCountedAgainstTheNoteCapBeforeTheyAreMaterialised`
  (`tests/src/core/SmfNoteReadTest.cpp:165`). The two files differ by one note: exactly the cap and
  the cap plus one, both unclosed note-ons. The reader's own allocation count
  (`tests/src/core/AllocationProbe.h`, a count and never a clock) must clearly differ, because the
  fixed reader refuses the over-cap file mid-queue and never pays the full-cap output list.
- RED: `reading the cap plus one allocated 12533 times, against 12533 for exactly the cap` —
  EXIT=8 (`test-13-red-new-p14-313.log`, proof `test-15-proof-313-reverted-red.log`). GREEN:
  EXIT=0 (`test-14-green-new-p14-313.log`, proof `test-16-proof-313-restored-green.log`).

## Summary table

| Cluster | RED command (cwd `build/tests`) | RED exit | GREEN command | GREEN exit | Proof (revert -> restore) | Family |
| --- | --- | ---: | --- | ---: | --- | --- |
| new-p14-314 | `ctest -R '^SmfNoteReadTest$' --output-on-failure > …/test-3-red-new-p14-314.log 2>&1` | 8 (8,002,095 moves vs bound 8,000) | `ctest -R '^(ClipImportFileTest\|SmfNoteReadTest)$' --output-on-failure > …/test-6-green-new-p14-314.log 2>&1` | 0 | 8 (`test-7-proof-reverted-red.log`) -> 0 (`test-8-proof-restored-green.log`), patch apply/build exits all 0 (`proof-new-p14-314.log`) | 2 CTest tests, 2/2 |
| new-p16-230 | `ctest -R '^SmfNoteReadTest$' --output-on-failure > …/test-9-red-new-p16-230.log 2>&1` | 8 (a note past the marker was read) | `ctest -R '^(ClipImportFileTest\|SmfNoteReadTest)$' --output-on-failure > …/test-10-green-new-p16-230.log 2>&1` | 0 | 8 (`test-11-proof-230-reverted-red.log`) -> 0 (`test-12-proof-230-restored-green.log`), all patch/build exits 0 (`proof-new-p16-230.log`) | 2 CTest tests, 2/2 |
| new-p14-313 | `ctest -R '^SmfNoteReadTest$' --output-on-failure > …/test-13-red-new-p14-313.log 2>&1` | 8 (cap+1 and cap both 12,533 allocations) | `ctest -R '^(ClipImportFileTest\|SmfNoteReadTest)$' --output-on-failure > …/test-14-green-new-p14-313.log 2>&1` | 0 | 8 (`test-15-proof-313-reverted-red.log`) -> 0 (`test-16-proof-313-restored-green.log`), all patch/build exits 0 (`proof-new-p14-313.log`) | 2 CTest tests, 2/2 |

Affected family: 2 CTest tests — `SmfNoteReadTest` (3 regression slots, test #507) and
`ClipImportFileTest` (the import path end to end: placement in LMMS ticks, running status,
zero-velocity offs, every truncated cut, flipped bytes, 3000 noise rounds, both cap refusals,
the drag-in refusals). The final family run is 2/2 green (`test-16-proof-313-restored-green.log`),
and `ClipImportFileTest` stayed green under the seam commit and through every proof flip.

Proof procedure, per cluster: save the product diff
(`git diff -- src/core/SmfNoteRead.cpp include/SmfNoteRead.h > …/fix-<cluster>.patch`), then
`git apply -R` + rebuild (`cmake --build build --target zene SmfNoteReadTest ClipImportFileTest`)
+ family ctest -> EXIT=8 with the cluster's own failure message; `git apply` + rebuild + ctest ->
EXIT=0. Every `git apply` and build in the three proofs returned EXIT=0
(`proof-new-p14-314.log`, `proof-new-p16-230.log`, `proof-new-p14-313.log`).

### Ratchet gates (`feature_check.py <worktree>`, run from the worktree root)

| Run | Log | Exit |
| --- | --- | ---: |
| baseline, before any edit | `gate-1-baseline.log` | 0 |
| after the test+seam commit | `gate-2-pre-commit1.log` | 0 |
| after the new-p14-314 fix | `gate-3-pre-commit2.log` | 0 |
| after the new-p16-230 test | `gate-4-pre-commit3.log` | 0 |
| after the new-p16-230 fix | `gate-5-pre-commit4.log` | 0 |
| after the new-p14-313 test | `gate-6-pre-commit5.log` | 0 |
| after the new-p14-313 fix (pre-final-commit) | `gate-7-pre-commit6.log` | 0 |

`tests/all-sources-reproduce.sh` prints `REPRODUCES` after the new test file was registered
(`tests/src/core/SmfNoteReadTest.cpp` in `tests/fork-sources.txt` and `tests/all-sources.txt`);
`SmfNoteRead.cpp` is 367 lines and the test file 199, both under the 500-line cap; no function in
either file is above the complexity gate's CCN 10.

## Files committed

- `4ad1c91f0` test(new-p14-314): `tests/src/core/SmfNoteReadTest.cpp` (new), `tests/CMakeLists.txt`
  (registers the test), `tests/fork-sources.txt`, `tests/all-sources.txt`,
  `tests/upstream-modifications.txt` (the `tests/CMakeLists.txt` ledger entry), and the seam:
  `src/core/SmfNoteRead.cpp`, `include/SmfNoteRead.h`.
- `023af783a` fix(new-p14-314): `src/core/SmfNoteRead.cpp`.
- `901e6f6ed` test(new-p16-230): `tests/src/core/SmfNoteReadTest.cpp`.
- `88ba28044` fix(new-p16-230): `src/core/SmfNoteRead.cpp`.
- `0abcc4c13` test(new-p14-313): `tests/src/core/SmfNoteReadTest.cpp`.
- `55ad2e92b` fix(new-p14-313): `src/core/SmfNoteRead.cpp`.
- this report: `docs/qwen-fixes-2026-10/cluster-new-p14-314.md`.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.** Yes, proven
   per cluster by reverting that cluster's fix patch (`git apply -R`), rebuilding and running the
   family: new-p14-314 EXIT=8 (`8002095` queue moves vs the bound `8000`) -> restored EXIT=0;
   new-p16-230 EXIT=8 (`a note lying past the end-of-track marker was read`) -> EXIT=0;
   new-p14-313 EXIT=8 (`the cap plus one allocated 12533 times, against 12533 for exactly the
   cap`) -> EXIT=0. The three proof logs record every apply/build/test exit code; all applies and
   builds returned 0. Each RED was also run before its fix commit, on its own (logs
   `test-3-red-new-p14-314.log`, `test-9-red-new-p16-230.log`, `test-13-red-new-p14-313.log`).
2. **The tests are deterministic: no wall-clock timing, sleeps or races.** No clock anywhere. The
   seams: new-p14-314 reads `smfNoteQueueShiftCount()` deltas — element moves counted by the queue
   elements themselves — against the fixed bound `2 * overlap` on a fixed 4000-note byte stream;
   new-p16-230 uses three fixed byte streams; new-p14-313 compares
   `AllocationProbe.h`'s thread-local allocation counts of two fixed files that differ by one note
   (noise budget 6 against a real difference of a full output list's growth; measured RED delta
   0). All slots are single-threaded QtTest slots; nothing is shared or timed.
3. **The fix is at the cause and every caller and sibling was checked.** new-p14-314: the only
   per-pitch open-note queue is `TrackWalk::open`; both removal paths (note-off close, closeAll)
   go through it. new-p16-230: every end-of-track site was grepped — the only other parser,
   `SmfInterchangeReader.cpp`, already held the contract and is unchanged; `SmfInterchange`
   only writes the marker on export; `readSmfNotes()`'s sole caller
   (`ControlCommandsClipImportFile.cpp:152`) surfaces the refusal and writes nothing on `!ok`.
   new-p14-313: the cap constant is read only by this file; the output-list checks remain (they
   still catch a multi-track overshoot); both refusal messages are byte-identical to before via
   the tri-state.
4. **No collateral damage.** No project-file format is touched — the reader parses external MIDI
   at import only. `ClipImportFileTest`, the import path end to end, is green in every family run
   (final 2/2), including its cut loop (a file that merely loses its end marker still reads),
   flipped-bytes and noise rounds, both cap refusals, and the exactly-the-cap file, which still
   reads: the new check refuses only past the cap and only with the message the reader already
   used. The EOT rules refuse only malformed markers and events behind a marker. The deque close
   keeps FIFO pairing (pinned by the new test and by the existing running-status/velocity cases).
   The only new steady-state cost is one relaxed atomic increment per open-queue element move,
   on the import path — never the audio thread.
5. **ALREADY-FIXED / FALSE-POSITIVE:** not applicable — all three verdicts are FIXED.
