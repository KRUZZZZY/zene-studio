ALREADY-FIXED

# Cluster 5313 — the audio reader can spin while the tuning writer is paused

The finding is real against the historical `SessionTuning::commit()` order
(publish `m_front` first, clear `WriterBit` afterwards). It is already fixed on
this branch: the writer now clears the claim bit **before** it publishes the
slot, and writers are serialised by `m_writeMutex`, so the front slot a reader
can observe never carries `WriterBit`.

## Evidence (this branch)

- `src/core/SessionTuning.cpp:282-283` — the order is now
  `m_slotStates[back].fetch_and(~WriterBit, ...)` then
  `m_front.store(back, ...)`. The newly published slot is readable before any
  reader can see it.
- `src/core/SessionTuning.cpp:255-256` — `commit()` holds `m_writeMutex` across
  claim, write, claim-release and front publication. This is what makes the
  reorder safe: no second writer can claim the just-released slot between the
  clear and the store (the concern the earlier SKIPPED-SCOPE attempt raised).
- `include/SessionTuning.h:234-251` — `acquireReadSlot()` still spins while the
  slot it is looking at carries `WriterBit`, but that slot is never the
  published front under the fixed order; it revalidates `front` before
  returning, so a reader never touches a slot a writer has recycled.
- `include/SessionTuning.h:236-249` and `:253-256` — the reader path is atomics
  only (no allocation, mutex, wait or logging).
- The fix landed earlier on the train as commits `83d43cd991` (reorder +
  writer mutex), `1f45bc547f` (test seam) and `818834a232` (seam after
  publication); `git merge-base --is-ancestor 818834a232 HEAD` exits 0.
- The regression test is
  `tests/src/core/SessionTuningPublicationTest.cpp:98`
  `readerReturnsWhileWriterIsPausedAfterPublication()`. It pauses the writer at
  the publication seam (`setPublicationBoundaryHookForTest`, called from
  `src/core/SessionTuning.cpp:288`) and requires a reader thread to return, in
  the window, with the new frequency and zero allocations
  (`SessionTuningPublicationTest.cpp:112-119`).

## Commands and EXIT codes (logs in ../logs, files named below)

| # | command | EXIT | log |
|---|---------|------|-----|
| 1 | `cmake --build build --target SessionTuningPublicationTest SessionTuningTest -j4` | 0 | build-1.log |
| 2 | `ctest -R '^SessionTuningPublicationTest$' --output-on-failure` (from build/tests) | 0 | test-1.log |
| 3 | `git diff -- src/core/SessionTuning.cpp > .../logs/fix.patch` (made after moving the clear below the seam; see note) | 0 | — |
| 4 | `git apply -R .../logs/fix.patch` (restore the fixed order) | 0 | — |
| 5 | `git apply .../logs/fix.patch` (restore the historical publish-before-clear order, with the seam between them) | 0 | — |
| 6 | `cmake --build build --target SessionTuningPublicationTest -j4` | 0 | build-3-red.log |
| 7 | `ctest -R '^SessionTuningPublicationTest$' --output-on-failure` | **8** (test FAIL) | test-3-red.log |
| 8 | `git apply -R .../logs/fix.patch` (restore the fix) | 0 | — |
| 9 | `cmake --build build --target SessionTuningPublicationTest SessionTuningTest -j4` | 0 | build-4-green.log |
| 10 | `ctest -R '^SessionTuning(Publication)?Test$' --output-on-failure` | 0 | test-4-green.log |
| 11 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | gates-1.log |

`test-3-red.log` shows exactly the regression assertion failing:
`FAIL! : ...readerReturnsWhileWriterIsPausedAfterPublication() ... reader timed
out while the writer was paused after publication`, `Totals: 3 passed, 1
failed`, ctest `1 tests failed out of 1`, exit 8.
`test-4-green.log`: `SessionTuningTest Passed`, `SessionTuningPublicationTest
Passed`, `100% tests passed out of 2`, exit 0.

Note on the revert: a first naive swap (front store before the clear) left the
seam after the clear and the test passed (`test-2-red.log`, exit 0). That is
correct — it does not reproduce the historical order, because the hook must sit
between the publish and the clear for a reader to observe the flagged front.
`fix.patch` therefore models the actual old order (store, seam, clear), and
only then does the test go red.

## Test matrix considered

- **Normal path**: `reset()`, `setNoteFrequency()`, `setState()`,
  `feedScale()`/`feedKeymap()` all reach `commit()`; the test's successful
  commit and its stress slot cover repeated publication.
- **Failure / paused writer**: the seam holds the writer after publication; the
  reader must return without touching the writer. RED (old order) times out,
  GREEN (fixed order) returns the new 450 Hz table.
- **Exhaustion**: a writer scans `TableCount - 1` candidate slots and retries if
  all are reader-held (control-thread only, `SessionTuning.cpp:257-277`); the
  stress test runs 2000 commits/reads against this path.
- **Reconfiguration**: `reset()` returns an inactive 12-TET table; the
  `SessionTuningTest` family still passes.
- **Audio-thread constraints**: the reader test asserts
  `s_readerAllocations == 0` via `AllocationProbe`
  (`SessionTuningPublicationTest.cpp:54-61,118`); no mutex, allocation or log
  was added to the audio path.
- **Platforms**: atomics and `std::thread` only; no POSIX-only calls in the
  changed code or test.

## Self-check

1. **RED with the product change reverted / GREEN with it.** Reverting the
   reorder (store, seam, clear = the old order) and rebuilding makes
   `SessionTuningPublicationTest` fail with exit 8 (`test-3-red.log`); restoring
   it rebuilds clean and passes with exit 0 (`test-4-green.log`, both
   `SessionTuningTest` and `SessionTuningPublicationTest`). I did not author
   the test this session: it is the regression added by the earlier fix
   commits, and this session verifies it catches the bug.
2. **Determinism.** The test uses the compiled-in
   `setPublicationBoundaryHookForTest` seam: the writer is stopped at a fixed
   point (after `m_front` publication), the reader thread is started and joined
   there, and no sleep or random delay drives the outcome. The 2 s bound only
   distinguishes an unbounded spin (a liveness bug cannot be asserted without
   some bound); the allocation assertion uses the injected `AllocationProbe`
   counter. There is no timing race between the two sides: the writer cannot
   proceed until the reader thread is observed.
3. **Cause and completeness.** The changed code is the only producer of
   `m_front`/`m_slotStates` (`SessionTuning.cpp:257-292`); the reader is the
   only consumer (`include/SessionTuning.h:234-256`). Clearing before
   publishing removes the flagged-front window; `m_writeMutex` preserves the
   no-recycle guarantee the old order relied on. Every mutation path
   (`setState`, `setNoteFrequency`, `reset`, `feedScale`, `feedKeymap`) goes
   through the same `commit()`.
4. **No collateral damage.** The table contents, JSON snapshot, active flag,
   source cap, scale/keymap provenance and refusal rules are untouched. No
   limit or validation was added, so no caller is newly rejected. Saved data
   format is unchanged; `SessionTuningTest` (the existing family) passes.
5. **ALREADY-FIXED evidence.** `src/core/SessionTuning.cpp:282-283` (clear
   before publish), `include/SessionTuning.h:234-251` (reader protocol),
   `src/core/SessionTuning.cpp:255-256` (writer serialisation), and the
   regression test above.

## Other findings

None reported here; the unrelated bookkeeping in the surrounding train is out
of scope for this lane.
