FIXED

# cluster new-p9-276: WinMM MIDI output allocates and can wait on the audio thread

## Verdict

REAL. The finding named the per-event `QStringList` build in `MidiWinMM::processOutEvent()`.
The first fix removed that list but still called `midiOutShortMsg()` inline, and Microsoft
documents that call can wait for the device
(`https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midioutshortmsg`).
The product is now: published routing snapshot -> bounded queue -> worker thread -> driver.

## Root cause and fix

- `src/core/midi/MidiWinMM.cpp:63-70` `processOutEvent` packs the event and calls
  `MidiWinMMOutput::processOutEvent()`. `midiOutShortMsg()` now appears only in
  `include/MidiWinMM.h:154` (`WinMMSink::send`), reached only from the output worker.
- `include/MidiWinMMOutput.h:169-206` is the audio-thread path: one snapshot load, a scan of
  POD routes, one bounded push per destination. No allocation, free, lock, wait or log.
  Queue full: the command is dropped and counted (`:196`), submission never blocks, the other
  destinations are still tried. Pushes leave in order, so routing order and multiplicity hold.
- `include/MidiWinMMOutput.h:127-136` `clearDevices()` refuses new submissions, waits for an
  in-flight submit, bumps the generation (queued old commands are discarded), waits out an
  in-flight sink call, then republishes empty routing. `src/core/midi/MidiWinMM.cpp:244` calls
  it before any output handle is closed; `:297` publishes the open devices.
- `include/MidiWinMM.h:126-130` reports the overflow count via `outputQueueOverflowCount()`.
- Subscription semantics (`include/MidiWinMMOutput.h:139-166`) are the old remove-then-push ones.

## Test matrix (`tests/src/core/MidiWinMMShortMessageTest.cpp`)

The test is unconditional: no `__has_include`, `#ifdef`, `#if`, `QFAIL`, `QSKIP`, sleep or
`ZENE_HAVE_WINMM_BOUNDED_OUTPUT` (searched this session; the only match for any term is a
comment containing "sleep" at `:85`). It compiles against every behavioural variant, so a
reverted fix is a QCOMPARE/QVERIFY failure, not a compile error.

| Case | Slot (line) |
|---|---|
| Normal path, order, multiplicity, destinations | `publishedRoutingDrivesTheQueueToTheSubscribedDevices` :209 |
| Unsubscribe, re-subscribe, removePort, applyPortMode | `subscriptionChangesTakeEffectImmediately` :255 |
| Queue saturation, drop+count, resume | `queueFullDropsNewestAndCounts` :306 |
| Allocation-free submission (operator new and malloc watch, Qt-storage control) | `submissionIsAllocationFree` :336 |
| Driver I/O off the submitting thread | `workerSendsOffTheSubmittingThread` :390 |
| Teardown, queued sends | `clearDevicesDiscardsQueuedCommandsAndStopsSends` :410 |
| Teardown, in-flight sink call | `clearDevicesWaitsForInFlightSink` :438 |

## Mutation patches and runner (`tests/mutations/new-p9-276/`)

API-preserving patches of `include/MidiWinMMOutput.h`: `inline-send`, `allocating-search`
(the original bug: QStringList build+search in `processOutEvent`), `publish-noop`,
`no-generation-bump`, `no-inflight-wait`, `overflow-uncounted`, `overflow-drains-inline`.
`run-mutations.py` applies each to the header (backing it up and always restoring it), builds
with `CCACHE_DISABLE=1`, asserts the build succeeds, runs the slots and asserts exactly the
predicted slots print `FAIL!` with a QCOMPARE/QVERIFY reason. Slots that would hang under a
mutant (they wait for a worker or gate the mutant removed) are skipped and each skip is printed.
Round 3 fixed one runner bug: a relative `--build-dir` made the binary path wrong
(first attempt `mutations-run-3.log` EXIT=1, FileNotFoundError, no mutant ran); the runner now
resolves both paths. This runs on the worktree header, not a copy, because the build tree is
tied to it; the original is restored and compared (`cmp`) at the end.

## Commands and exits (logs in `~/.config/kads/zene/state/runs/new-p9-276.r3/logs`)

```text
GREEN   CCACHE_DISABLE=1 cmake --build build --target MidiWinMMShortMessageTest -j4   # EXIT 0 (build-30-green.log)
        cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure    # EXIT 0 (test-30-green.log)
RED     git apply tests/mutations/new-p9-276/allocating-search.patch                    # EXIT 0 (product reverted to the original allocating algorithm)
        CCACHE_DISABLE=1 cmake --build build --target MidiWinMMShortMessageTest -j4   # EXIT 0 (build-31-red.log)
        cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure    # EXIT 8 (test-31-red.log)
              FAIL! submissionIsAllocationFree() at MidiWinMMShortMessageTest.cpp(382): MallocWatch::allocations 3000, expected 0; 13 others pass
        git apply -R tests/mutations/new-p9-276/allocating-search.patch                 # EXIT 0, git status clean
GREEN   CCACHE_DISABLE=1 cmake --build build --target MidiWinMMShortMessageTest -j4   # EXIT 0 (build-32-green.log)
        cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure    # EXIT 0 (test-32-green.log)
SIBLING cmake --build build --target MidiOutQueueTest -j4 ; ctest -R 'MidiOutQueue|MidiWinMM'   # EXIT 0, EXIT 0 (build-33-sibling.log, test-33-family.log)
MUTANTS python3 tests/mutations/new-p9-276/run-mutations.py --build-dir build --scratch <logs>/mutations-4 --jobs 4   # EXIT 0 (mutations-run-4.log)
        RESULT: PASS - every mutant built and failed exactly the predicted slots
        inline-send: Discards, Routing, Full, Alloc, Subscribe, OffThread fail (InFlight skipped: hangs in the gated inline call)
        allocating-search: submissionIsAllocationFree only
        publish-noop: Discards, Routing, Full, Alloc, Subscribe fail (OffThread, InFlight skipped: nothing routed)
        no-generation-bump: clearDevicesDiscardsQueuedCommandsAndStopsSends only
        no-inflight-wait: clearDevicesWaitsForInFlightSink only
        overflow-uncounted, overflow-drains-inline: queueFullDropsNewestAndCounts only
        baseline and restored-baseline: no failing slot
        ctest after the runner                                                          # EXIT 0 (test-36-after-mutations.log)
WIN32   c++ -fsyntax-only <build flags> -DLMMS_HAVE_WINMM -I<logs>/winmm-shim src/core/midi/MidiWinMM.cpp   # EXIT 0 (build-34-winmm-typecheck.log, empty)
        same command without the stub include dir (negative control)                    # EXIT 1 "windows.h: No such file" (build-35-winmm-noshim-control.log)
```

The RED step reverses the allocation fix via the mutation patch rather than `git apply -R` of
the whole product diff: the unconditional test needs the `MidiWinMMOutput` API, so the
pre-fix source could only fail to compile, which proves nothing. The full-reversal variants
are the `inline-send` and `publish-noop` mutants above (the old test passed with publication
disabled; this one fails 5 slots).

The Win32 stub headers (`windows.h`, `mmsystem.h`) are lane scratch in
`<logs>/winmm-shim`, not in the repo. **The full Windows link is NOT verified here**: only
`MidiWinMM.cpp` was syntax-checked against a stub on Linux; the real build and link remain the
hosted Windows job. The portable core (`MidiWinMMOutput.h`) is compiled and tested on Linux.

## Gates

```text
python3 .../feature_check.py <worktree>   # EXIT 1 (feature-check-2.log): fork-sources and all-sources-reproduce FAIL,
                                          # tests/all-sources.txt lacked the committed tests/src/core/MallocWatch.h
(added that one line, in the recipe's C-collation position)
python3 .../feature_check.py <worktree>   # EXIT 0, RESULT: PASS (feature-check-3.log)
```

## Self-check

- **Fails without the fix, passes with it.** RED EXIT 8 / GREEN EXIT 0 above; the
  inline-send and publish-noop mutants fail six and five slots respectively.
- **Determinism.** The deterministic slots use `drainForTesting()` (the worker's own
  `takeAndSend()` body on the test thread); the threaded slots use semaphores, no sleeps or
  clocks. The earlier round ran 20 repeats green; this round ran the suite 5 times green
  (baseline, green, after-mutations, restored baseline, sibling family).
- **Cause and completeness.** `processOutEvent`, `subscribeWritablePort`, `applyPortMode`,
  `removePort`, `closeDevices`, `openDevices` and the destructor delegate to the new class;
  `MidiClient::sendFromNonAudioThread()` reaches the same function and the queue is
  multi-producer. Sibling `MidiOutQueueTest` passes.
- **Collateral damage.** No project/save format, model or control command changed; device
  order, remove-then-push subscription semantics and name-search multiplicity are as before.
  The only new limit is the WinMM queue capacity, which counts and drops instead of blocking.
- **Races.** Local TSan does not run here. The handshake (accepting flag, in-flight counters,
  generation, seq_cst) is reasoned in `MidiWinMMOutput.h:59-79` and exercised by the
  in-flight slot and the `no-inflight-wait` / `no-generation-bump` mutants; it is not
  TSan-verified.
- **Control commands.** None changed; no `ControlCommandsSnapshot` canary moves.

## Round 4: macOS 10.13 sync availability and dead code (this session)

Review found `include/MidiWinMMOutput.h` (`<semaphore>`, `std::counting_semaphore<2> m_wake`) and
the test (`<semaphore>`, `std::binary_semaphore`/`std::counting_semaphore`) reintroducing the
cluster-ci-61ad26e4f8b135f9 defect: Apple libc++ marks those unavailable below macOS 11 and the
product floor is 10.13 (`MacosSyncAvailabilityCheck`, ctest #830, EXIT 8 on those lines).

- `include/MidiWinMMOutput.h`: `<semaphore>` removed. `m_wake` is now a
  `std::atomic<std::uint32_t>` wake word driven by `lmms::workerwake` (`AudioWorkerWake.h`, the
  tree's futex / `__ulock` wake that never locks or allocates). `requestWake()`: after the
  `m_wakePending` exchange, `fetch_add(1, release)` then `workerwake::notifyOne`. `stop()`:
  `fetch_add(1, release)` then `workerwake::notify` before `join()`, unconditional.
  `run()` loads the word (acquire) BEFORE the stop check, the `m_wakePending.store(false)` and
  the drain, and ends the iteration with `workerwake::wait(m_wake, seen)`, so a wake posted
  between drain and wait moves the word and the wait returns at once (no lost wakeup). The stop
  check sits after the load on purpose: `stop()` stores `m_stop` then moves the word with
  release, so a worker that sees the moved word also sees `m_stop`; a worker that loaded the old
  word waits on a word that is about to change. (Deviation from the instruction text: the
  `fetch_add`s are `release`, not `relaxed`, to make that argument hold; no cost on x86.)
- `processOutEvent()` now reads the snapshot through `MidiWinMMOutRouting::forEachDevice()`,
  which was dead code; the class comment ("the playback path reads through it") is now true.
- `tests/src/core/MidiWinMMShortMessageTest.cpp`: `<semaphore>` dropped;
  `lmms::testsupport::BinarySemaphore` (`tests/src/core/PortableSync.h`) for `arrived`,
  `entered` and `release`. GatedSink performs one `acquire()`; an extra failure-path `release()`
  is idempotent for a binary permit, so it stays harmless.
- Mutation patches `allocating-search`, `inline-send`, `overflow-uncounted`,
  `overflow-drains-inline` were regenerated for the lambda-based loop (they edit the same
  semantics as before); `publish-noop`, `no-generation-bump`, `no-inflight-wait` applied as is.
- Races: local TSan does not run here; the wake protocol above is argued, not TSan-verified.

## Round 4 commands and exits (logs in `~/.config/kads/zene/state/runs/new-p9-276.r4/logs`)

```text
cmake --build build --target zene MidiWinMMShortMessageTest -j4                  # first try EXIT 2 (build-40.log): one stop() call
                                                                                 # still used the removed m_wake.release(); fixed
cmake --build build --target zene MidiWinMMShortMessageTest -j4                  # EXIT 0 (build-41.log)
cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure     # 6 runs, EXIT 0 each (test-51.log .. test-56.log)
cd build/tests && ctest -R MacosSyncAvailabilityCheck --output-on-failure        # EXIT 8 (test-57.log): the only FAIL lines are the six
        '<X>Test.moc: No such file' lines (tests never built in this tree); the six
        MidiWinMMOutput.h / MidiWinMMShortMessageTest.cpp lines are gone
python3 tests/mutations/new-p9-276/run-mutations.py --build-dir build --scratch <logs>/mutations-1 --jobs 4
        # EXIT 0 (mutations-run-1.log): RESULT: PASS, every mutant built and failed exactly the
        # predicted slots through QCOMPARE/QVERIFY; baseline and restored baseline pass
RED     git apply tests/mutations/new-p9-276/allocating-search.patch                    # EXIT 0
        CCACHE_DISABLE=1 cmake --build build --target MidiWinMMShortMessageTest -j4   # EXIT 0 (build-60-red.log)
        cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure    # EXIT 8 (test-60-red.log)
              FAIL! submissionIsAllocationFree(): Compared values are not the same, MidiWinMMShortMessageTest.cpp(382)
        git apply -R tests/mutations/new-p9-276/allocating-search.patch                 # EXIT 0
GREEN   CCACHE_DISABLE=1 cmake --build build --target MidiWinMMShortMessageTest -j4   # EXIT 0 (build-61-green.log)
        cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure    # EXIT 0 (test-61-green.log)
```

## Re-verification (round 3 session, before the round-4 change; logs in the r3/r4-earlier folders)

```text
CCACHE_DISABLE=1 cmake --build build --target MidiWinMMShortMessageTest -j4   # EXIT 0 (build-40-green.log)
cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure    # EXIT 0, 1/1 passed (test-40-green.log)
python3 tests/mutations/new-p9-276/run-mutations.py --build-dir build --scratch <logs>/mutations-5 --jobs 4   # EXIT 0 (mutations-run-5.log)
        RESULT: PASS - every mutant built and failed exactly the predicted slots (same slot sets as above)
rebuild + ctest after the runner                                                # EXIT 0, EXIT 0, 1/1 passed (build-41-after.log, test-41-after.log)
python3 .../submit_check.py <worktree> new-p9-276                               # EXIT 0, RESULT: PASS - submit (submit-check-4.log)
```


## Other findings

- ccache direct mode kept a stale object once when a header was removed and restored, so the
  RED/GREEN and mutation builds use `CCACHE_DISABLE=1`. Local tooling effect only.

Delegation: Haiku built and ran the GREEN/RED/GREEN cycle, the sibling family, the Win32-stub
type-check with its negative control, and the mutation runner (after I fixed its relative-path
bug), and reported exit codes; I verified the log tails and wrote the runner fix, report and
manifest myself.
