FIXED

# Cluster new-p14-241 — remote VST period path takes a mutex on the shared block

## Finding

`RemoteVstPlugin::process()` locked the shared-memory block for every audio
period with `tryLockShm()` / `unlockShm()`, while the non-realtime
`updateInOutCount()` took the same `std::mutex` to invalidate the block before
reallocating it. A host that misses the try-lock therefore emits a silent
period. The old non-realtime writer could block the GUI thread on that mutex,
and the audio path still performed a mutex try_lock; the audio thread never
took the blocking lock.

Pre-fix REAL evidence:

- `plugins/VstBase/RemoteVstPlugin.cpp:1104` — `if( !tryLockShm() )` on the
  period path.
- `plugins/VstBase/RemoteVstPlugin.cpp:319-332` — `lockShm()`, `tryLockShm()`,
  `unlockShm()`, `isShmValid()`, `setShmIsValid()` helpers.
- `plugins/VstBase/RemoteVstPlugin.cpp:482` — `std::mutex m_shmLock;` beside
  `bool m_shmValid;`.
- `plugins/VstBase/RemoteVstPlugin.cpp:1819-1823` — `updateInOutCount()` takes
  `lockShm()`, clears validity and unlocks before `delete[]`/reallocation.
- `plugins/VstBase/RemotePluginClient.h:293-297` and `:340-364` — the period
  entry point and buffer plumbing that run under that lock.

## Fix

New `plugins/VstBase/VstShmHandoff.h` (`lmms::vst::ShmHandoff`) implements a
reader/owner protocol with `std::atomic` only:

- A reader calls `beginRead()`, which increments the reader count and then
  checks validity; an invalid block is refused after decrementing again.
- `endRead()` releases the reader.
- The owner calls `invalidate()`, clears validity and yields until the reader
  count drains to zero, then resizes the block; `validate()` republishes it.

All operations are `seq_cst`, and `static_assert`s require the atomics to be
always lock-free. `RemoteVstPlugin` replaces `std::mutex m_shmLock` and
`m_shmValid` with `vst::ShmHandoff m_shmHandoff`; `process()` uses
`beginRead()`/`endRead()`, `updateInOutCount()` uses `invalidate()` before
freeing and reallocating, and the `IdChangeSharedMemoryKey` handler uses
`validate()` instead of `setShmIsValid(true)`. The `<mutex>` include and the
five lock helpers are removed.

Final product scan:

```
$ rg -n "lockShm|tryLockShm|unlockShm|m_shmLock|m_shmValid" plugins/VstBase/RemoteVstPlugin.cpp
(no matches)
```

## Test matrix

Device-independent, native test sources that scan/drive the seams:

| Scenario | Test |
| --- | --- |
| normal read window | `VstShmHandoffTest::normalRead` |
| invalid block refused | `VstShmHandoffTest::invalidReadRefused` |
| in-flight reconfiguration drains held readers | `VstShmHandoffTest::invalidateWaitsForHeldReaderTest` |
| repeated reconfiguration / exhaustion | `VstShmHandoffTest::reconfigurationCycles` |
| read path allocates nothing | `VstShmHandoffTest::readPathDoesNotAllocate` |
| read path is lock-free | `VstShmHandoffTest::readPathIsLockFree` |
| mutated-input proof of the drain | `VstShmHandoffTest::mutantWithoutDrainIsCaught` |
| source scan: period path has no mutex | `VstMidiEventBatchTest::processPeriodPathTakesNoMutex` |
| source scan: mutant scanner rejects a lock | `VstMidiEventBatchTest::mutexScannerRejectsMutatedBody` |
| early return opens a read window | `VstMidiEventBatchTest::processEarlyReturnsSilenceOutputs` |
| allocation-free MIDI path stays | `VstMidiEventBatchTest::processUsesAllocationFreeMidiSort` |
| per-instance scratch stays | `VstMidiEventBatchTest::midiScratchIsPerInstance` |

Supported platform: the handoff is standard C++11 atomics with
`is_always_lock_free` static assertions; the test runs on the native Linux
`build/tests` tree and has no POSIX-only path assumptions.

`VstShmHandoffTest` is header-dependent: it includes the product header, so it
fails to build (not merely to run) when `plugins/VstBase/VstShmHandoff.h` is
reversed.

## RED / GREEN logs

- p2 RED: `ctest -R '^VstMidiEventBatchTest$' --output-on-failure` →
  `Totals: 11 passed, 2 failed`; `FAIL! ... processEarlyReturnsSilenceOutputs()
  'beginRead >= 0' returned FALSE` and `FAIL! ... processPeriodPathTakesNoMutex()
  '!body.contains("lockShm")' returned FALSE` (log `test-2-red.log`).
- p3 GREEN: `VstMidiEventBatchTest` 1/1 Passed (log `test-3-green.log`).
- p4 GREEN: `VstMidiEventBatchTest` and `VstShmHandoffTest` 2/2 Passed (log
  `test-4-green.log`).
- p4b GREEN: 2/2 Passed before the p5 reverse/restore cycle (log
  `test-4b-green.log`).
- p5 4b RED: 11 passed / 2 failed with the product hunks reversed (log
  `test-red.log`).
- p5 post-restore GREEN: 4/4 Passed — `VstMidiEventBatchTest`,
  `VstShmHandoffTest`, `VstChannelCountTest`, `VstPeMachineTypeTest` (log
  `test-green.log`).
- VST family: 4/4 Passed in the separate family run (log `test-family.log`).

## p4b reversal proof

Product-only reverse/restore cycle, exact commands and EXIT codes
(`LOGS=/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-241.r1/logs`):

| Step | Command | EXIT |
| --- | --- | --- |
| capture product diff | `git diff HEAD -- plugins/VstBase/RemoteVstPlugin.cpp plugins/VstBase/VstShmHandoff.h > $LOGS/fix.patch` | 0 |
| reverse it | `git apply -R $LOGS/fix.patch > $LOGS/reverse-1.log 2>&1` | 0 |
| rebuild reversed | `cmake --build build --target VstMidiEventBatchTest > $LOGS/build-red.log 2>&1` | 0 |
| RED test | `cd build/tests && ctest -R '^VstMidiEventBatchTest$' --output-on-failure > $LOGS/test-red.log 2>&1` | 8 |
| restore | `git apply $LOGS/fix.patch > $LOGS/restore-1.log 2>&1` | 0 |
| rebuild restored | `cmake --build build --target NativeLinuxRemoteVstPlugin64 VstMidiEventBatchTest VstShmHandoffTest VstChannelCountTest VstPeMachineTypeTest > $LOGS/build-green.log 2>&1` | 0 |
| green test | `cd build/tests && ctest -R '^(VstMidiEventBatchTest\|VstShmHandoffTest\|VstChannelCountTest\|VstPeMachineTypeTest)$' --output-on-failure > $LOGS/test-green.log 2>&1` | 0 |

The RED run failed 2 of 13 with the `beginRead >= 0` and `lockShm` assertions
above. After restore, `sha256sum LOGS/fix.patch` was
`a97f149ad466ec2a303c2b183bda088853c60755014f4cb6017db18950929037`, matching
the restored product diff (`git diff HEAD --` the two product paths) exactly.

## Gates

`feature_check.py` on this worktree:

```
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

VST family ctest: 4/4 Passed.

## Other findings

Not fixed here, and outside this packet's scope:

- `RemoteVstPlugin::updateInOutCount()` still publishes the new input/output
  counts before the backing arrays are reallocated, so a reader admitted after
  `validate()` could observe counts that the old arrays do not yet cover.
  The handoff closes the resize/drain race for the block itself, not this
  count/array ordering window.
- `RemoteVstPlugin::doProcessing()` reads the plain (non-atomic)
  `m_inputCount` / `m_outputCount` cross-thread without a synchronisation
  edge.
- `queueMessage()` uses its own separate `pthread_mutex`, which is untouched
  by this change.

## Self-check

- Regression proof: `VstMidiEventBatchTest` FAILS with the product hunks
  reversed (`ctest` EXIT=8, 11 passed / 2 failed, log `test-red.log`) and
  PASSES restored (`ctest` EXIT=0, 4/4 passed, log `test-green.log`). The
  reversed product diff still built, so the failure is at runtime, not a
  compile error.
- Deterministic: the tests use injected reader counts, explicit
  `beginRead()`/`invalidate()`/`endRead()` sequencing and source scans; there
  are no sleeps, wall-clock checks or timing assumptions. `invalidate()` yields
  only until the test's held reader is released.
- Cause and completeness: the cause is the `std::mutex` around
  `m_shmValid`/the shared block. `process()`, `updateInOutCount()` and the
  `IdChangeSharedMemoryKey` handler are the only users of the old helpers; all
  three now use the handoff, and the helpers and `<mutex>` include are gone
  (see the final `rg` above).
- No collateral damage: saved projects and plugin state are unchanged; the
  shared block layout and message protocol are untouched, so old projects load
  and render as before. The change only alters how the block is invalidated and
  read, not its contents or size.
- Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE, so no counter-evidence
  section is needed; the pre-fix `tryLockShm` at line 1104 and the `std::mutex`
  at line 482 are the real defect.
- Sibling commands: this change touches no control-command file, so the
  `app.*` and `arrangement.*` commands listed by the checker (`app.about`,
  `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`,
  `app.tour`, `app.tours`, `app.update_check_get`, `app.update_check_run`,
  `app.update_check_set`, `app.version`, `app.whats_new`,
  `arrangement.fit_tracks`, `arrangement.get_state`) are unmodified and
  unaffected; no command registration or reversibility class changed.
