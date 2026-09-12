# Mixer concurrency and ordering fixes (post-alpha/mixer-concurrency)

**Scope.** Fixes for the mixer concurrency and ordering defects that the external audit
("Mixer concurrency and ordering audit", lines 1–162 of
`feedback/external-feedback-2026-09-11.md`) reported and that the grader
(`feedback/grade-A-mixer-concurrency.md`) confirmed live at the current tip: **D1, D2(ii),
D2(iii), D3, D4, D5, D6**. D2(i) was already fixed by our own `44430848a`; D7, D8 and D9 are
deliberately untouched and are listed under "Not fixed, and why" below.

**Branch, base.** `post-alpha/mixer-concurrency`, branched from `post-alpha/v0.2`
(`0c23587d2`). Every one of these defects is inherited from upstream master (the same bare
swaps and the same `bool m_muted` are in `origin/master`), so this is a fork improvement, not a
fork regression.

**Method.** Every defect was reproduced *before* the fix and re-checked after it:

* a new regression test, `tests/src/core/MixerConcurrencyTest.cpp` (registered in
  `tests/CMakeLists.txt`, 8 slots), which either asserts on rendered audio (D1, D2(ii),
  D2(iii), and the "the change waits for the period" slot for D3/D4/D5) or drives the real
  writer on a second thread against the real reader (D3, D4, D5, D6);
* a **ThreadSanitizer** build both before and after
  (`-DCMAKE_BUILD_TYPE=Debug -DWANT_DEBUG_TSAN=ON`, i.e. the tree's own
  `WANT_DEBUG_TSAN` switch), with each slot run on its own so one crash cannot mask another.

**How to reproduce the sanitizer evidence.** ThreadSanitizer needs ASLR disabled on this
kernel (`vm.mmap_rnd_bits` is too high for libtsan's shadow layout, which otherwise aborts with
`FATAL: ThreadSanitizer: unexpected memory mapping`):

```
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DWANT_DEBUG_TSAN=ON -DWANT_QT6=ON -DUSE_WERROR=OFF
cmake --build build-tsan --target MixerConcurrencyTest -j4
cd build-tsan/tests
QT_QPA_PLATFORM=offscreen setarch -R env TSAN_OPTIONS="halt_on_error=0 suppressions=/tmp/mixconc.supp" \
  ./MixerConcurrencyTest <slot>
```

The suppression file covers three **pre-existing, out-of-scope** classes only, each by exact
function name: the worker thread's `volatile bool m_quit` teardown race
(`AudioEngineWorkerThread::quit`), the `QWaitCondition` destroyed while that thread is leaving
`wait()`, and QTest's own watchdog teardown (`QTest::qRun`). Nothing under `Mixer.cpp`,
`EffectChain.cpp`, `AudioBusHandle.cpp` or the test itself is suppressed; an earlier revision
of the suppression file used `race:lmms::AudioEngineWorkerThread::run`, which also matched
reports whose *other* stack contained a worker frame and therefore hid real mixer races — that
is why the patterns are the narrowest fully-qualified names that cover those three classes.

---

## D1 — the mute latch was read before it was decided (high severity, one mute click)

* **The line.** `Mixer::masterMix` latched `ch->m_muted = ch->m_muteModel.value()` and drained
  that latch in the **same pass** (`src/core/Mixer.cpp:1373`–1374 at the tip). `processed()`
  reads the latch of every channel this one sends to (`src/core/Mixer.cpp:170`, `:186`), so a
  *muted* channel looking at a **higher-indexed** receiver tested the **previous period's**
  value. `m_muted` was a plain `bool` (`include/Mixer.h:87`) written by the render thread and
  read by workers, and it was never initialised in `MixerChannel`'s constructor.
* **The fix.** Split the pass: latch every channel for the period first, then act
  (`src/core/Mixer.cpp`, both loops over `m_mixerChannels`), and make the flag
  `std::atomic<bool>` with an explicit `m_muted(false)` in the constructor. The latches are
  stored with `memory_order_relaxed`; this period's value reaches a worker through the job
  queue's release/acquire pair (`ThreadableJob::queue()` → `process()`), which orders every
  store made in the latch pass before the job becomes visible — that is the "a worker queued
  for the period sees the value the latch decided" requirement, and splitting the pass means
  no channel is in the queue while the latch is being written.
* **Proof.** `MIXCONC_D1` in `MixerConcurrencyTest::muteLatchIsDecidedBeforeDependenciesAreCounted`:
  channels 1 and 3 both send into channel 2 (channel 1 stays muted, channel 3 carries the
  signal). Mute the receiver for three periods, then un-mute it while the lower-indexed muted
  sender stays muted and render one period. Mid-graph, channel 2's only proof of life is
  channel 3 - and channel 3's audio reached the master only if channel 2 was counted.

  | | before | after |
  |---|---|---|
  | `transition_period_peak` | **0.000000** | **0.500000** |
  | `next_period_peak` | 0.500000 | 0.500000 |
  | slot verdict | `FAIL!` | `PASS` |

  A silent period through a live signal path, followed by normal audio - exactly the
  deterministic one-period dropout D1(a) describes, and it is gone.
* **Residual risk.** The `std::atomic` removes the undefined behaviour on the flag. The
  *scheduling* window the grader called D1(b)/I7 - a worker parked between
  `queueReadyWaitCond->wakeAll()` and `globalJobQueue.run()`, from the previous period - is
  **not** fixed: it lives in `AudioEngineWorkerThread`, not in the mixer, and it is visible in
  this lane's own evidence as a `JobQueue::reset` vs `JobQueue::run` race that is unrelated to
  any of D1–D6 (it is the grader's UNVERIFIABLE #1, and a run of this test observed it). With
  the latch now atomic, a late worker reads a well-defined value; it may read a stale one, and
  the double-increment D1(b) describes remains possible in that window.

## D2(ii) — a muted sender replayed its last sidechain block (medium severity)

* **The line.** `44430848a` deleted the unreachable muted branch of `MixerChannel::doProcessing`
  and taught `masterMix`'s muted branch to advance the *incoming* rings
  (`advanceSilence`, `src/core/Mixer.cpp:1380`–1387 at the tip) - but not to clear the
  *outgoing* sidechain intermediates, which is the other half of the same deleted branch.
  `clearIntermediate()` therefore appeared exactly once in the file, in the receiver's
  non-deferred path.
* **The fix.** In the same muted branch, clear the channel's outgoing intermediates:
  `for (MixerSidechainRoute* route : ch->m_sidechainSends) { route->clearIntermediate(); }`.
  Placement matters: it runs before `ch->processed()`, so a receiver woken by the muted sender
  already sees the cleared buffer.
* **Reachability, stated precisely.** A non-deferred route is consumed *and cleared* by its
  receiver in `sumSidechainInputs`, so a block can only go stale if the receiver did not run
  the period the sender wrote it. The construction therefore mutes the **receiver** while the
  sender plays (nothing consumes the intermediate), then un-mutes the receiver and mutes the
  sender in one period. The sender deliberately sits at a **higher index** than the receiver:
  with a lower-indexed muted sender, D1's stale latch means the receiver is not scheduled at
  all, which masks this defect instead of exhibiting it.
* **Proof.** `MIXCONC_D2ii` in `mutedSenderDoesNotReplayItsStaleSidechainTap`, with a
  `TapProbeEffect` on the receiver reading the sidechain buffer the mixer hands its chain:

  | | before | after |
  |---|---|---|
  | `tap_before_mute` | 0.500000 | 0.500000 |
  | `tap_in_transition_period` | **0.500000** (stale) | **0.000000** |
  | `probe_calls` | 1 | 1 |
  | slot verdict | `FAIL!` | `PASS` |

  `probe_calls > 0` is asserted too, so the result cannot be a sleeping effect that was never
  called.

## D2(iii) — a deferred receiver looped the same stale block forever (medium severity)

* **The line.** `prepareMasterMix()` commits every deferred route's intermediate
  unconditionally, once per period (`src/core/Mixer.cpp:1174`–1180), and a deferred receiver
  reads that snapshot. An intermediate that is never cleared is therefore re-committed every
  period - the looping buzz the audit described.
* **The fix.** The same `clearIntermediate()` loop as D2(ii): one loop covers both, because a
  muted channel writes no taps and its `doProcessing()` - where the clear used to happen - never
  runs.
* **Proof.** `MIXCONC_D2iii` in `mutedSenderStopsFeedingADeferredReceiver`: a regular send
  1→2 anchors the ordering, the sidechain 2→1 closes a cycle through it and is deferred
  (`route->deferred()` is asserted), and a probe on channel 1 reads the committed tap.

  | | before | after |
  |---|---|---|
  | `tap_before_mute` | 0.500000 | 0.500000 |
  | `tap_in_mute_period` | 0.500000 | 0.500000 |
  | `tap_three_periods_later` | **0.500000** (looping) | **0.000000** |
  | slot verdict | `FAIL!` | `PASS` |

  The mute period legitimately still carries the pre-mute tap: a deferred route is one period
  late **by construction**, and `prepareMasterMix()` commits the snapshot before the muted
  branch runs. The defect was that it never stopped; the test asserts the tap has stopped
  while only *documenting* (not asserting equality with zero for) that first late period.

## D3 — `createChannel()` grew the graph under the render thread (high severity, Add channel)

* **The line.** `Mixer::createChannel` (`src/core/Mixer.cpp:564`–583 at the tip) did
  `m_mixerChannels.push_back(...)` and `resizeLatencyScratch(...)` with no guard, while the
  render thread iterates both (`masterMix`, `updateLatencyCompensation`, `resolveLatency`).
  The only lock on that path was the *nested* one, taken after the vectors had already been
  reallocated.
* **The fix.** Bracket the body with `requestChangeInModel()`/`doneChangeInModel()`, the idiom
  every other topology writer in the file already uses; the mutex is recursive, so the nested
  `clearChannel() → createChannelSend() → createRoute()` lock is fine. `createBusChannel()`'s
  `setIsBus()` (read by the render thread in `mixToChannel` and by the latency pass) gets the
  same treatment.
* **Proof (a), sanitizer.** `creatingAChannelIsSerialisedWithTheRenderPeriod` renders on one
  thread while a second thread adds channels.

  | | before | after |
  |---|---|---|
  | TSAN races reported | **25** | **1** |
  | reports naming the code under test | `_M_realloc_insert` (vector.tcc:521/522), `operator delete`, `std::vector<MixerChannel*>::size()` vs `Mixer.cpp:568` | none — the sole remaining report is the pre-existing `AudioEngineWorkerThread` `JobQueue::reset`/`run` race |
  | slot verdict | `PASS` (the failure is the race) | `PASS` |

  Before the fix the report is exactly the audited hazard: `Mixer::createChannel()`'s
  `_M_realloc_insert` freeing the array that `Mixer::masterMix` was iterating.
* **Proof (b), no sanitizer needed.** `topologyChangesWaitForTheRenderPeriodToEnd` holds the
  render period's change mutex open and asks whether a `createChannel()` on another thread gets
  through anyway: before the fix the vector **grows while the period is in flight** (the
  assertion fails and the reallocation has already happened), after the fix the call waits and
  lands when the period ends. This slot fails on a plain build.
* **Residual risk.** `createChannel()` can now block a GUI thread for up to one render period.
  That is the same contract `deleteChannel()` already had.

## D4 — `moveChannelLeft()` reordered and renumbered under the render thread (high severity, move left/right)

* **The line.** `Mixer::moveChannelLeft` did `qSwap(m_mixerChannels[index], m_mixerChannels[index-1])`
  plus two `setIndex()` calls (`src/core/Mixer.cpp:800`, `:803`–804 at the tip) with no lock,
  while `masterMix`'s latch loop walks the vector and `updateLatencyCompensation` reads the
  order and each channel's index through `MixerRoute::senderIndex()`.
* **The fix.** Take the change mutex for the whole operation (after the early-return guard, so
  the lock/unlock pair is balanced), as `deleteChannel()` does.
* **Proof (a), sanitizer.** The slot drives `moveChannelLeft`/`moveChannelRight` on a second
  thread while the render loop reads the channel order and index under its period mutex (the
  read the audit names).

  | | before | after |
  |---|---|---|
  | TSAN races reported | **3** | **2** |
  | reports naming the code under test | `Mixer.cpp:800` (`qSwap`) and `Mixer.cpp:803` (`setIndex`) vs `MixerChannel::index()` (`include/Mixer.h:112`), reached from `Mixer::resolveLatency` (`Mixer.cpp:1224`) | none — both remaining reports are the pre-existing `AudioEngineWorkerThread` classes |
  | slot verdict | `PASS` (the failure is the race) | `PASS` |

* **Proof (b), no sanitizer needed.** The `topologyChangesWaitForTheRenderPeriodToEnd` probe:
  before the fix `moveChannelLeft()` reorders the vector while the period is in flight; after
  the fix it waits, and the move lands at the period boundary (asserted both ways).
* **Honest limit.** On the first attempt this race was *not* reported by TSAN even though
  `moveChannelLeft` was executed ~595,000 times against ~600 render periods (verified by
  counting effective swaps through the public accessor). The reports only appeared once the
  test's render loop read the channel order explicitly under the period mutex; `masterMix`'s
  own iteration of the same vector was not reported on this harness even though the same kind
  of access *was* reported for D3. I could not explain that asymmetry, so D4's sanitizer
  evidence rests on the test's explicit render-thread read - which is the same read
  `updateLatencyCompensation` performs - plus the grader's line-by-line source verification,
  rather than on a report naming `masterMix`.
* **Residual risk.** Same as D3: a channel move can block the GUI thread for up to one period.

## D5 — bare `std::swap` in `EffectChain::moveUp/moveDown` (low severity, GUI-reachable)

* **The line.** `EffectChain::moveDown`/`moveUp` swapped two vector elements with no guard
  (`src/core/EffectChain.cpp:213`, `:226`) while a worker range-fors `m_effects` in
  `processAudioBuffer` (`src/core/EffectChain.cpp:247`, `:272`). A one-period double-process or
  skip, no memory unsafety (the size does not change).
* **The fix.** Bracket the find-and-swap with
  `requestChangeInModel()`/`doneChangeInModel()`, the same idiom `appendEffect`,
  `removeEffect` and `clear` already use in that file.
* **Proof (a), sanitizer.** `reorderingEffectsIsSerialisedWithTheRenderPeriod` reorders every
  channel's chain while the render loop runs periods.

  | | before | after |
  |---|---|---|
  | TSAN races reported | **2** | **1** |
  | reports naming the code under test | `EffectChain::moveDown` (`EffectChain.cpp:213`, `std::swap`) vs `EffectChain::processAudioBuffer` (`EffectChain.cpp:272`, the range-for) | none — the sole remaining report is the pre-existing `AudioEngineWorkerThread` race |
  | slot verdict | `PASS` (the failure is the race) | `PASS` |

  The reader in that report is the real reader: the render thread's inline job execution inside
  `masterMix` → `startAndWaitForJobs()`.
* **Proof (b), no sanitizer needed.** The `topologyChangesWaitForTheRenderPeriodToEnd` probe for
  `moveDown` (see above). The chain's post-condition is asserted too: its cached latency - the
  order-independent, membership-sensitive invariant of a three-effect chain - is unchanged, so
  the reorder moved order, not membership.
* **Residual risk.** None identified beyond the added lock at the period boundary.

## D6 — `AudioBusHandle::m_playHandles`: appended under the lock, iterated without it (high severity, a crash)

* **The line.** `AudioBusHandle::doProcessing` range-fors `m_playHandles` with
  **nothing held** (`src/core/AudioBusHandle.cpp:140`), while `addPlayHandle` appends under
  `m_playHandleLock` (`:272`–273) and `removePlayHandle` erases under the same lock
  (`:279`–280). `AudioEngine::addPlayHandle` takes no change mutex, so the writer is a real
  live-note path (MIDI thread, `Piano` widget, preset preview).
* **The fix.** Take `m_playHandleLock` for the iteration, so reader and writer share one lock
  instead of one holding it and the other not.
* **Proof, sanitizer.** `addingAPlayHandleIsSerialisedWithTheIterator` iterates
  `handle.doProcessing()` on one thread while another adds and removes the same handle.

  | | before | after |
  |---|---|---|
  | TSAN races reported | **2** | **0** |
  | reports naming the code under test | `AudioBusHandle::doProcessing` (`AudioBusHandle.cpp:140`, unguarded `QList::end()`) vs `AudioBusHandle::addPlayHandle` (`:273`) / `removePlayHandle` (`:283`, `QList::erase`) | none |
  | slot verdict | `PASS` | `PASS` |
  | exit code | 66 (reports) | **0** |

  With the fix this is the only slot with **zero** sanitizer output at all — the cleanest
  before/after of the set.
* **Residual risk (stated, not hidden).** Taking a mutex on the audio path trades a
  use-after-free for a bounded wait: if a MIDI/GUI thread holds `m_playHandleLock` it can delay
  the worker that is mixing that track (and therefore the period). The critical section being
  guarded is an append/erase on a pointer list; the alternative - a lock-free snapshot of the
  list published to readers - is a larger change and was out of this lane's scope. This is the
  fix the grader prescribed, and it removes the crash.

---

## Not fixed, and why

* **D2(i)** — already fixed by our own `44430848a` before this lane started (the dead muted
  branch was deleted and the ring advance moved into `masterMix`). Nothing to do.
* **D7** (in-place send/route updates bypass the lock) and **D8** (`MixerView::deleteChannel` →
  `clearChannel` writes buffers before the locked delete) — the grader rates both benign and
  says "no action needed"; neither is a correctness defect. Not touched.
* **D9** (`AudioPortsModel::setPin`/`setChannelCounts` without the lock) — the grader narrowed
  this to "serialised with that job, not concurrent": the reallocation runs on the plugin's own
  worker thread inside its period, so the raw-UB framing was weaker than the audit's. The
  residual exposure is the *other* readers, and the grader's own action item is
  "`requestChangesGuard()` in `setChannelCounts` **if** the readers can ever run on another
  worker - that is the one thing still to establish". It was not established, so no code
  changed; this is also `AudioPortsModel` territory rather than the mixer.
* **D10** (model callbacks running control-thread code on the render thread) — an observation,
  not a defect, and out of scope.
* **The `AudioEngineWorkerThread` races this lane observed.** Both the parked-worker
  `JobQueue::reset` vs `run()` race (the grader's UNVERIFIABLE #1 / D1(b) precondition) and the
  `volatile bool m_quit` teardown race are real and still present. They are engine
  worker-thread defects, not mixer defects, they were not on the graded fix list, and fixing
  them means changing `AudioEngineWorkerThread`'s scheduling contract - a separate lane's
  business. They are named here rather than suppressed silently.
* **The `m_peakLeft/m_peakRight` residual** the grader identified in the D2(i) fix (the deleted
  branch used to zero the peaks) is left alone: it is not on the fix list, the GUI's decay
  already covers it in normal use, and changing it would be a behaviour change this lane is
  explicitly not supposed to make.
