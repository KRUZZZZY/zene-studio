FIXED

# cluster new-p9-276: WinMM MIDI output allocates and can wait on the audio thread

## Verdict

REAL. The finding named the per-event `QStringList` build in
`MidiWinMM::processOutEvent()`; the first fix on this branch removed that list but still
called `midiOutShortMsg()` inline. Microsoft documents that `midiOutShortMsg()` can wait for
the device (`https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midioutshortmsg`),
so the audio thread could still block inside the driver. The old regression passed even
with the routing publication disabled because it drove a private helper instance, not
production publication, subscription and playback.

## Root cause and fix

- `src/core/midi/MidiWinMM.cpp:63-72` (`processOutEvent`) now only packs the event and hands
  it to `MidiWinMMOutput::processOutEvent()`. It no longer calls `midiOutShortMsg()`.
- `include/MidiWinMMOutput.h:176-213` is the production playback path: one published-routing
  snapshot load, a scan of a POD stop list, and one bounded queue push per destination. No
  allocation, free, lock, wait or log. A refused push is counted (`:203`) and never blocks.
- `include/MidiWinMMOutput.h:282-296` (`takeAndSend`) and `:298-308` (`run`) hand each queued
  command to `MidiWinMMSink::send()` on the output worker. `MidiWinMM::WinMMSink`
  (`include/MidiWinMM.h:149-157`) is the only caller of `midiOutShortMsg()`.
- `include/MidiWinMMOutput.h:134-143` (`clearDevices`) refuses new submissions, waits for an
  in-flight submit, advances a generation so queued old commands are discarded, waits out an
  in-flight sink call, then republishes an empty routing. `MidiWinMM::closeDevices()`
  (`src/core/midi/MidiWinMM.cpp:228-254`) calls it before closing any output handle.
- `include/MidiWinMMOutput.h:145-173` owns the same removal-then-push subscription semantics
  as the old `MidiWinMM::subscribeWritablePort`, `applyPortMode` and `removePort`; the
  Windows adapter delegates to it (`src/core/midi/MidiWinMM.cpp:77-108`, `:144-153`).
- `include/MidiWinMMOutRouting.h:150-...` `buildRoutes()` now takes const port identities and
  still emits routes in device-map order with the old name-search multiplicity.
- `include/MidiWinMM.h:126-130` reports the queue overflow count through the existing
  `MidiClient::outputQueueOverflowCount()` seam.

The device and subscription maps moved into the platform-neutral output class, so the
regression drives exactly the publication/subscription/playback code production owns.
`MidiWinMM` is now the thin WinMM handle adapter around that class.

## Test matrix

| Case | Test | Evidence |
|---|---|---|
| Normal routing, order, multiplicity | `publishedRoutingDrivesTheQueueToTheSubscribedDevices` | two devices sharing a name both receive; a duplicate subscription sends once per device; a name with no device and an unsubscribed port send nothing |
| Failure / exhaustion | `queueFullDropsNewestAndCounts` | `QueueCapacity` accepted in order, next 3 refused, `overflowCount()==3`, then submission resumes after a drain |
| Reconfiguration / unsubscribe | `subscriptionChangesTakeEffectImmediately` | unsubscribe, re-subscribe, `removePort`, `applyPortMode(false)` and `applyPortMode(true)` all take effect on the next event |
| Audio-thread allocation | `submissionIsAllocationFree` | a control allocation is observed first; 1000 production submissions show `tlAllocationCount==0` and `tlFreeCount==0` |
| Off-audio driver I/O | `workerSendsOffTheSubmittingThread` | the threaded output's sink records a thread id different from the submitting thread |
| Teardown, queued sends | `clearDevicesDiscardsQueuedCommandsAndStopsSends` | queued commands are discarded, new submissions are refused, reopening works |
| Teardown, in-flight send | `clearDevicesWaitsForInFlightSink` | a gated sink holds the worker inside the driver; `clearDevices()` is proven still waiting, then completes after the gate is released |
| Windows build requirement | Win32 stub type-check of `MidiWinMM.cpp` | `c++ -fsyntax-only` with the build's flags and a minimal stub: EXIT 0 |

The tests use a manual pump for the deterministic cases and semaphores (not sleeps or clocks)
for the two threaded cases. The manual pump runs the same `takeAndSend()` body as the worker.

## Commands and exits (logs in `~/.config/kads/zene/state/runs/new-p9-276.r2/logs`)

Baseline before the change (the old regression was blind):

```text
cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure   # EXIT 0 (test-1-baseline.log)
```

RED, product diff reversed:

```text
git diff -- include/MidiWinMMOutput.h include/MidiWinMMOutRouting.h include/MidiWinMM.h src/core/midi/MidiWinMM.cpp > .../logs/fix.patch   # EXIT 0
git apply -R .../logs/fix.patch                                                # EXIT 0
CCACHE_DISABLE=1 cmake --build build --target MidiWinMMShortMessageTest -j4    # EXIT 0 (build-12-red.log)
cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure   # EXIT 8 (test-12-red.log; 7 passed, 7 failed)
```

GREEN, product diff restored:

```text
git apply .../logs/fix.patch                                                   # EXIT 0
CCACHE_DISABLE=1 cmake --build build --target MidiWinMMShortMessageTest -j4    # EXIT 0 (build-13-green.log)
cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure   # EXIT 0 (test-13-green.log)
```

Production route publication disabled (`return;` at the top of `publishRoutes()`), running the
non-blocking routing tests:

```text
CCACHE_DISABLE=1 cmake --build build --target MidiWinMMShortMessageTest -j4    # EXIT 0 (build-14-nopublish.log)
cd build/tests && ./MidiWinMMShortMessageTest publishedRoutingDrivesTheQueueToTheSubscribedDevices \
  subscriptionChangesTakeEffectImmediately queueFullDropsNewestAndCounts \
  submissionIsAllocationFree clearDevicesDiscardsQueuedCommandsAndStopsSends  # EXIT 5 (test-14-nopublish.log; 2 passed, 5 failed)
```

Restored and green again:

```text
CCACHE_DISABLE=1 cmake --build build --target MidiWinMMShortMessageTest -j4    # EXIT 0 (build-15-restored.log)
cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --output-on-failure   # EXIT 0 (test-15-restored.log)
```

Determinism and siblings:

```text
cd build/tests && ctest -R '^MidiWinMMShortMessageTest$' --repeat until-fail:20 --output-on-failure  # EXIT 0 (test-7-repeat.log)
CCACHE_DISABLE=1 cmake --build build --target MidiOutQueueTest -j4             # EXIT 0 (build-17-midioutqueue.log)
cd build/tests && ctest -R 'MidiOutQueue|MidiWinMM' --output-on-failure         # EXIT 0 (test-17-family.log)
```

Windows adapter type-check (stub was lane scratch under `logs/winmm-shim`, now removed):

```text
c++ -fsyntax-only ... -DLMMS_HAVE_WINMM -I<stub> src/core/midi/MidiWinMM.cpp    # EXIT 0 (build-19-winmm-typecheck.log)
```

Gates:

```text
python3 .../feature_check.py <worktree>                                        # EXIT 0, RESULT: PASS (feature-check-1.log)
python3 .../submit_check.py <worktree> new-p9-276                             # EXIT 1 before commit: dirty tree + ASK items (submit-check-1.log)
```

The first submit check ran before the commit by design; its only FAIL was the intentionally
dirty working tree, and its ASK items are answered below. The post-commit run on `9440ed23`
printed every item PASS and `RESULT: PASS - submit` (`submit-check-2.log`, EXIT 0).

## Self-check

- **Regression fails with the product change reverted and passes with it (exit codes).**
  RED: `ctest` EXIT 8, 7 failed (`test-12-red.log`). GREEN: `ctest` EXIT 0
  (`test-13-green.log`). The specific publication-disabled experiment also failed: EXIT 5,
  5 of the 5 selected routing tests failed (`test-14-nopublish.log`).
- **Determinism: no wall-clock timing, sleeps or races.** The deterministic cases use
  `drainForTesting()` (the same `takeAndSend()` consumer body, called by the test instead of
  the worker). The two threaded cases use `std::binary_semaphore` handshakes and a test-only
  in-flight observation flag; no `sleep`, no elapsed-time check, and 20 repeated runs passed
  (`test-7-repeat.log`).
- **Cause and completeness: every caller and sibling checked.** `processOutEvent`,
  `subscribeWritablePort`, `applyPortMode`, `removePort`, `closeDevices`, `openDevices` and
  the destructor all delegate to the new class; `MidiClient::sendFromNonAudioThread()` reaches
  the same `processOutEvent()` and is safe because the queue is bounded multi-producer. The
  `MidiOutQueueTest` sibling still passes (`test-17-family.log`). The WinMM translation unit
  was type-checked with the Windows branch enabled (`build-19-winmm-typecheck.log`); the full
  Windows link remains the hosted portability job, which this Linux lane cannot run.
- **No collateral damage.** No save/load schema, model, project file or control command
  changed. The device-list order is still `QMap<HMIDIOUT,QString>` order
  (`src/core/midi/MidiWinMM.cpp:277-293`), subscription removal semantics are the old
  remove-then-push, and routing multiplicity is the old name-search multiplicity. The only
  limit added is the WinMM output queue's fixed capacity; it refuses excess WinMM commands
  (counted) instead of blocking, and no other caller's limit changes.
- **ALREADY-FIXED / FALSE-POSITIVE item.** Not applicable: the verdict is FIXED. The
  pre-fix code still called `midiOutShortMsg()` inline (`src/core/midi/MidiWinMM.cpp` before
  this change), and the review reproduced that the old test passed with publication disabled.
- **Sibling commands in the changed command files.** No control command file or handler was
  changed. The listed `app.*` and `arrangement.*` commands are unrelated to WinMM output;
  `src/core/ControlCommands*.cpp` and the command snapshot are untouched, so no
  `ControlCommandsSnapshot` canary moves.

## Other findings

- ccache's direct mode kept a stale object across the RED experiment when
  `MidiWinMMOutput.h` was removed and then restored: `__has_include` had seen the file absent.
  The recorded RED/GREEN runs therefore use `CCACHE_DISABLE=1`. This is a local tooling
  effect, not a product defect; a clean train build is unaffected.
- The real Windows build/link is not run in this Linux lane. The adapter was type-checked
  against a minimal Win32 stub, and the portable core is compiled and tested on Linux.
