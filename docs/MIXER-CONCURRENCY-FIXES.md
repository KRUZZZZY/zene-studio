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

**How to reproduce the sanitizer evidence.** The runner and its suppression file ship in the
tree, so the numbers below are reproducible with three commands:

```
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DWANT_DEBUG_TSAN=ON -DWANT_QT6=ON -DUSE_WERROR=OFF
cmake --build build-tsan --target MixerConcurrencyTest -j4
bash tests/run-mixer-concurrency-tsan.sh build-tsan /tmp/mixer-concurrency-tsan
```

`tests/run-mixer-concurrency-tsan.sh` runs each slot on its own and writes a
`SUMMARY.txt` with the exit code, verdict, `MIXCONC_*` evidence line and every report summary;
`tests/mixer-concurrency-tsan.supp` covers three **pre-existing, out-of-scope** classes only,
each by exact function name: the worker thread's `volatile bool m_quit` teardown race
(`AudioEngineWorkerThread::quit`), the `QWaitCondition` destroyed while that thread is leaving
`wait()`, and QTest's own watchdog teardown (`QTest::qRun`). Nothing under `Mixer.cpp`,
`EffectChain.cpp`, `AudioBusHandle.cpp`, `Mixer.h` or the test itself is suppressed; the file
also names, and deliberately does not suppress, the parked-worker `JobQueue` race described
under "Not fixed, and why". An earlier revision of the suppression file used
`race:lmms::AudioEngineWorkerThread::run`, which also matched reports whose *other* stack
contained a worker frame and therefore hid real mixer races — that is why the patterns are the
narrowest fully-qualified names that cover those three classes.

Note the runner needs `setarch -R`: this kernel's ASLR entropy (`vm.mmap_rnd_bits`) is too high
for libtsan's shadow layout, which otherwise aborts with
`FATAL: ThreadSanitizer: unexpected memory mapping` before any test starts. The runner does that
internally.

---

## The sanitizer evidence, before and after, slot by slot

Both runs below are the same build directory (`build-tsan`), the same runner and the same
suppression file; the only difference is the four production files. "before" is
`0c23587d2` (via `git checkout 0c23587d2 -- <the four files>`), "after" is the branch tip.

| slot (defect) | before: verdict / races | after: verdict / races | what the before-run named |
|---|---|---|---|
| `muteLatchIsDecidedBeforeDependenciesAreCounted` (D1) | **`FAIL!`** / 1 | `PASS` / 2 | `Mixer.cpp:1370` = the parked-worker `JobQueue::reset` race, **not** the `m_muted` race (see the D1 residual note) |
| `mutedSenderDoesNotReplayItsStaleSidechainTap` (D2(ii)) | **`FAIL!`** / 1 | `PASS` / 1 | same parked-worker class |
| `mutedSenderStopsFeedingADeferredReceiver` (D2(iii)) | **`FAIL!`** / 0 | `PASS` / 0 | — |
| `topologyChangesWaitForTheRenderPeriodToEnd` (D3/D4/D5, plain-build) | **`FAIL!`** / 2 | `PASS` / 0 | `Mixer.cpp:568` (`createChannel`'s `push_back`) vs the test's `numChannels()` inside the period |
| `creatingAChannelIsSerialisedWithTheRenderPeriod` (D3) | `PASS` / **143** | `PASS` / 1 | `Mixer.cpp:568`/`:570`/`:573` (the `push_back` and `resizeLatencyScratch`) vs `Mixer.cpp:1164`, `:1191`–`:1194`, `:1222`–`:1250` (`updateLatencyCompensation`/`resolveLatency`), `:1438`–`:1447` (masterMix's reset loop), `:1414` (`startAndWaitForJobs`) |
| `movingAChannelIsSerialisedWithTheRenderPeriod` (D4) | `PASS` / **5** | `PASS` / 1 | `Mixer.cpp:800` (the `qSwap`) and `:803`/`:804` (`setIndex`) vs `Mixer.cpp:1224`/`:1229`/`:1291` (`resolveLatency` reading `MixerRoute::senderIndex()`) and `:1164` |
| `reorderingEffectsIsSerialisedWithTheRenderPeriod` (D5) | `PASS` / **8** | `PASS` / 1 | `EffectChain.cpp:213` (`moveDown`'s `std::swap`) and `:226` (`moveUp`'s) vs `EffectChain.cpp:272` (the worker's range-for in `processAudioBuffer`) |
| `addingAPlayHandleIsSerialisedWithTheIterator` (D6) | `PASS` / **4** | `PASS` / **0** | `AudioBusHandle.cpp:140` (the unguarded range-for) vs `AudioBusHandle.cpp:272`/`:273` (`addPlayHandle` appending *under* the lock) |

In the **after** run, every remaining report in every slot is one of the two pre-existing
classes and **no report names `Mixer.cpp`, `EffectChain.cpp`, `AudioBusHandle.cpp` or
`Mixer.h`**:

```
$ grep -E '^SUMMARY: ThreadSanitizer' after-tsan/*.log | sed 's|.*/||'
... in lmms::AudioEngineWorkerThread::JobQueue::reset(...)      # the parked-worker window
... in operator delete(void*, unsigned long)                    # AudioEngineWorkerThread teardown
```

The single most legible before/after is D6, verbatim:

```
before — WARNING: ThreadSanitizer: data race
  Read of size 8 at 0x7fffffffce20 by main thread:                 # the reader, nothing held
    #0 QArrayDataPointer<lmms::PlayHandle*>::end()  qarraydatapointer.h:108
    #1 QList<lmms::PlayHandle*>::end()              qlist.h:588
    #2 lmms::AudioBusHandle::doProcessing()         src/core/AudioBusHandle.cpp:140
  Previous write of size 8 at 0x7fffffffce20 by thread T26 (mutexes: write M0):
    #0 QtPrivate::QPodArrayOps<lmms::PlayHandle*>::erase()  qarraydataops.h:198
    #1 QList<lmms::PlayHandle*>::remove(...)                qlist.h:771
    #2 QList<lmms::PlayHandle*>::erase(...)                 qlist.h:865
    #3 QList<lmms::PlayHandle*>::erase(...)                 qlist.h:604
    #4 lmms::AudioBusHandle::removePlayHandle(...)  src/core/AudioBusHandle.cpp:283
  Thread T26 created by main thread
  SUMMARY: ThreadSanitizer: data race .../src/core/AudioBusHandle.cpp:140 in lmms::AudioBusHandle::doProcessing()

after  — EXIT=0, zero ThreadSanitizer output for this slot
```

and D3's reallocation, verbatim:

```
before — WARNING: ThreadSanitizer: data race
  Write of size 8 at 0x720400000440 by thread T26:
    #0 operator delete(void*, unsigned long)  tsan_new_delete.cpp:150
    #1 std::__new_allocator<lmms::MixerChannel*>::deallocate()  new_allocator.h:172
    ...
    #5 std::vector<lmms::MixerChannel*>::_M_realloc_insert<...>()  vector.tcc:519
    #8 lmms::Mixer::createChannel()  src/core/Mixer.cpp:568
  Previous read of size 8 at 0x720400000440 by main thread (mutexes: write M0):
    #0 lmms::Mixer::masterMix(lmms::SampleFrame*)  src/core/Mixer.cpp:1444

after  — 1 report, and it is the parked-worker `JobQueue::reset` one
```

Slot races are counted per run and ThreadSanitizer groups repeats, so the counts vary between
runs of the same binary; the *identities* above are stable and are the point.

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
  | TSAN races reported | **143** | **1** |
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
  | TSAN races reported | **5** | **1** |
  | reports naming the code under test | `Mixer.cpp:800` (`qSwap`) and `Mixer.cpp:803` (`setIndex`) vs `MixerChannel::index()` (`include/Mixer.h:112`), reached from `Mixer::resolveLatency` (`Mixer.cpp:1224`) | none — the single remaining report is the pre-existing `AudioEngineWorkerThread` class |
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
  | TSAN races reported | **8** | **1** |
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
  | TSAN races reported | **4** | **0** |
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

---

## Build, tests, gates, behaviour preservation

**Commits** (branch `post-alpha/mixer-concurrency`, base `0c23587d2` = `post-alpha/v0.2`;
grouped per file/mechanism rather than strictly per defect, because D1–D4 live in one file and
two hunks):

```
test(mixer)+docs: the mixer concurrency regression tests, the fix report, the declared divergences
fix(AudioBusHandle): iterate m_playHandles under m_playHandleLock                        (D6)
fix(EffectChain): serialise moveUp/moveDown with the render period                       (D5)
fix(mixer): decide the mute latch before counting deps, clear muted senders'
            sidechain taps, serialise the topology writers            (D1, D2(ii)/(iii), D3, D4)
```

**CI build and ctest.** `JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4` with the
CI's exact `CMAKE_OPTS` plus the documented deviation the script prints for this box
(`-DWANT_QT6=ON`, no Qt5 development files):

| | base (`git stash` of the four fixed files) | fixed |
|---|---|---|
| configure | `EXIT=0` | `EXIT=0` |
| build | `EXIT=0` | `EXIT=0` |
| ctest (from `build/tests`) | **`EXIT=8` — 96% tests passed, 1 failed out of 26** (`9 - MixerConcurrencyTest`) | **`EXIT=0` — 100% tests passed, 0 failed out of 26** |

The base failure is the new test and only the new test, on a plain `RelWithDebInfo` build with
no sanitizer:

```
MIXCONC_D1 transition_period_peak=0.000000 next_period_peak=0.500000
FAIL!  : ...muteLatchIsDecidedBeforeDependenciesAreCounted() 'maxAbs(transition) > 1.0e-6f' returned FALSE.
MIXCONC_D2ii tap_before_mute=0.500000 tap_in_transition_period=0.500000 probe_calls=1
FAIL!  : ...mutedSenderDoesNotReplayItsStaleSidechainTap() 'std::fabs(tapAfterMute) < 1.0e-9f' returned FALSE.
MIXCONC_D2iii tap_before_mute=0.500000 tap_in_mute_period=0.500000 tap_three_periods_later=0.500000
FAIL!  : ...mutedSenderStopsFeedingADeferredReceiver() 'std::fabs(tapLater) < 1.0e-9f' returned FALSE.
FAIL!  : ...topologyChangesWaitForTheRenderPeriodToEnd() '!grewWhileRendering' returned FALSE.
```

**Gates.** Run on the committed tree; exit codes unpiped. `tests/run-all-gates.sh` numbering.

| gate | command | exit | result |
|---|---|---|---|
| 1 unit tests | `bash tools/local-ci.sh --build-dir build --jobs 4` → ctest | `0` | 26/26 (base: 1 failed) |
| 2 coverage | not run | — | opt-in; needs a separate full `build-coverage` build. The only file it would newly measure is the new test (no entry floor). Deliberately skipped, not reported as a pass. |
| 3 no tautologies | `bash tests/no-tautology-gate.sh` | `0` | PASS — every registered test file has slots and real assertions |
| 4 complexity | `bash tests/complexity-gate.sh --check` | `0` | PASS — no regressions |
| 5 mutation | `bash tests/mutation-gate.sh --max-mutants 8` | `0` | PASS — kill score 87.5% ≥ 80% (8-mutant sample, not the full 30) |
| 6 upstream regression | `bash tests/no-upstream-regression-gate.sh` | `0` | PASS — every change to inherited code is declared (31 files in the ledger) |
| 7 file length | `bash tests/file-length-gate.sh --check` | `0` | PASS — no regressions |
| 8 duplication | `bash tests/duplication-gate.sh` | `0` | PASS — 1.05% duplicated lines (budget 5%) |
| 9 fork sources | — | — | **does not exist on this branch.** `tests/fork-sources-gate.sh` is only on lanes descended from `post-alpha/gate-debt`; this lane has no such script, so no Gate 9 result is claimed. |

**One declared deviation:** Gate 7's 500-line ratchet would flag the new test file (677 lines).
It is exempted in `tests/file-length-exempt.txt` (a file this lane creates) with the reason
stated there — a hand-written 8-slot test whose comments are what make each scenario
reproducible; splitting it would also invalidate the sanitizer log names quoted above. Delete
the line and the gate reports the file; that is the intended behaviour if a reviewer disagrees.

**Behaviour preservation.** Renders of an in-tree mixer project
(`data/projects/shorties/Crunk(Demo).mmp`: 4 mixer channels, 2 effects, audiofileprocessor
tracks, no mute activity) through `lmms render … -f wav -s 48000 -a`:

| comparison | runs | frames | differing frames | max │Δ│ | Δ dB |
|---|---|---|---|---|---|
| base vs base (run-to-run floor, same build) | 3 | 1,594,880 | 0 | **0 LSB** | 0.00000 |
| fixed vs fixed (run-to-run floor, same build) | 3 | 1,594,880 | 0 | **0 LSB** | 0.00000 |
| **base vs fixed** (3 pairs) | 6 | 1,594,880 each | **0** | **0 LSB** | 0.00000 |

So the render is **sample-identical** before and after the fix, against a measured run-to-run
floor of **0 LSB** on the same build - stronger than the LSB tolerance the task asked for, and
not a byte-identity claim. Note for the record: this box's renders were *not* bit-reproducible
in an earlier lane's investigation; in this configuration (`RelWithDebInfo`, 48 kHz, float WAV,
`QT_QPA_PLATFORM=offscreen`) they are, and the floor was measured rather than assumed. One
base and zero fixed runs aborted during teardown (`QThread: Destroyed while thread is still
running`) *after* writing a complete, identical WAV - a pre-existing renderer-exit flake, not
a defect introduced here.

Why the fix is behaviour-preserving *by construction* on this project: it has nothing muted or
soloed during the render, so D1's latch ordering and D2's sidechain clears are no-ops for it,
and D3–D6 change only locking. D1's and D2's intended behaviour changes are the ones the
`MIXCONC_D1`/`MIXCONC_D2ii`/`MIXCONC_D2iii` assertions exhibit.

**Slots that could only be proven under a sanitizer.** D3, D4, D5 and D6 do not change the
audio of a correctly scheduled period, so there is nothing to assert on but "the access is
ordered": their slots are expected to be silent failures on a plain build and were the
sanitizer reports listed above. D1, D2(ii), D2(iii) and the D3/D4/D5 "topology waits" slot fail
on a plain build, which is why the base ctest run above is red.

**Slots that could only be proven behaviourally.** D1's data race on `m_muted` was *not*
observed: the race pair (render-thread latch write vs worker read) needs the parked-worker
window (the grader's D1(b) precondition, UNVERIFIABLE #1) before it can be concurrent at all,
and this harness never caught it. The atomic conversion is therefore argued from the memory
model - relaxed stores in the latch pass, published by the job queue's release/acquire pair -
not from a report.

**Expected-but-noisy test output.** The D3 slots mutate the mixer from a test thread, and
`MixerChannel`'s models are parented to a `Mixer` that lives on the main thread, so Qt prints
`QObject: Cannot create children for a parent that is in a different thread` once per created
channel. It is a warning, not a failure, and it is inherent to testing an API whose real caller
is the GUI thread; it was left visible rather than filtered.

