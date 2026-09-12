# The MIDI-learn cross-thread race: evidence, fix, and what is not proven

Branch: `post-alpha/midi-race` (based on `post-alpha/midi-learn` `22617a93c`).
Fix commits: `838e38c22` (the fix and its tests) and this document's commit.
The feature's own write-up is `docs/MIDI-LEARN.md`; it said plainly that the
binding was made on the MIDI input thread and named the follow-up ("defer the
whole binding to the GUI thread"). This is that follow-up, done and tested.

---

## 1. The defect, with the lines

Two MIDI input paths feed the learn, and both are MIDI input threads, not the GUI
thread and not the audio thread:

| Call site | Thread |
| --- | --- |
| `src/core/midi/MidiAlsaSeq.cpp:570` in `MidiAlsaSeq::run()` (`src/core/midi/MidiAlsaSeq.cpp:457`) | `MidiAlsaSeq` is a `QThread` (`include/MidiAlsaSeq.h:48`), started with `start(QThread::IdlePriority)` (`src/core/midi/MidiAlsaSeq.cpp:119`) — a dedicated sequencer read thread |
| `src/core/midi/MidiClient.cpp:244` in `MidiClientRaw::processParsedEvent()` (`:240`), reached from `MidiClientRaw::parseData()` (`:234`) | the raw clients' read threads: `MidiAlsaRaw::run()` (`src/core/midi/MidiAlsaRaw.cpp:103`, `start(QThread::LowPriority)` at `:55`, read loop calling `parseData()` at `:159`), `MidiJack::JackMidiRead()` (`src/core/midi/MidiJack.cpp:180`, called from the JACK process callback — a non-Qt thread) → `parseData()` at `:197`, and the OSS/Sndio readers |

The write, as it stood at `22617a93c`:

- `src/core/MidiLearn.cpp:143` — `target->setControllerConnection(connection)`, called
  from `handleMidiEvent()`.
- → `src/core/AutomatableModel.cpp:488-498` `AutomatableModel::setControllerConnection()`:
  `m_controllerConnection = c;` (`:490`) — a plain `ControllerConnection*`
  (`include/AutomatableModel.h:410`), no atomic, no lock — then two
  `QObject::connect` calls on the model (`:493`, `:495`) and `emit dataChanged()` (`:497`).

The same write done from the GUI thread, plus the connected signal, is read
elsewhere:

- **GUI thread** (paint and menu paths): `src/gui/AutomatableModelView.cpp:96-98`
  (`model->controllerConnection()->getController()`), `:215-217`, `:242-244`;
  `src/gui/widgets/FloatModelEditorBase.cpp:529-530`.
- **Audio render thread**: `src/core/AutomatableModel.cpp:548`, inside
  `AutomatableModel::controllerValue()` (`:504`) —
  `m_controllerConnection->getController()->isSampleExact()`; reached from
  `value()` (`include/AutomatableModel.h:159-166`).
- GUI-thread predicates: `src/core/AutomatableModel.cpp:140-145`,
  `include/AutomatableModel.h:123`.

So the feature moved upstream's unsynchronised pointer write (and a `QObject::connect`
on a GUI-thread-owned `QObject`, and a `dataChanged` emission) onto a foreign thread
that the GUI and the audio thread read concurrently. The lane that wrote it called it
"a real race" and said so in `docs/MIDI-LEARN.md` §6 — that section is now updated to
point here.

Two more foreign-thread touches rode along on the same path and are also gone:
`Engine::getSong()` (old `src/core/MidiLearn.cpp:111`) and
`MidiPort::readablePorts()` (old `:133`), the latter walking the `MidiClient`'s port
list, which the GUI mutates.

## 2. The fix: which thread writes what now

`MidiLearn::handleMidiEvent()` (`src/core/MidiLearn.cpp:156`) now splits by thread:

- **On the application ("GUI") thread** — `onGuiThread()` (`:62`) compares
  `QThread::currentThread()` with `QCoreApplication::instance()->thread()` — it builds
  the binding immediately, exactly as before. Nothing is deferred for a GUI-thread
  caller, which is what keeps the existing immediate-semantics tests honest.
- **On any other thread** — `requestBinding()` (`:131`): two relaxed `int` stores
  (channel, controller number) followed by one release store of
  `m_pendingBinding`. No `new`, no `std::function`, no Qt event, no `QObject` touched,
  no lock, no wait. It then spends the learn (`setEnabled(false)`), so a burst of CCs
  while armed cannot produce a second request — the slot is single-shot by
  construction.
- **The GUI thread delivers** — `applyPendingBinding()` (`:201`) exchanges the flag
  (acquire) and runs `bindFocusedControl()` (`:220`) there: `MidiController`,
  `ControllerConnection`, `target->setControllerConnection()`,
  `Song::setModified()`, the disarm and the binding count.

`MidiLearnGui` owns the delivery: a 10 ms `QTimer` (`src/gui/MidiLearnGui.cpp:58-66`,
`BindPollIntervalMs` at `:44`), started by `setArmed(true)` and stopped when the learn
ends, whose timeout runs `applyPendingBinding()` and then reconciles the armed state.

**Why a GUI-side poll and not a queued hand-off.** The task asked for the codebase's
established idiom for posting to the GUI thread. The tree's idiom is
`QMetaObject::invokeMethod(obj, lambda, Qt::QueuedConnection)`
(`src/core/ScriptEngine.cpp:418`) or a queued signal/slot connection — and both
allocate a `QMetaCallEvent` (or a post-event node) **on the calling thread**, i.e. on
the MIDI input thread, which is the one thing this change exists to stop. Posting
anything to the GUI thread from the MIDI thread costs an allocation; there is no
allocation-free post in Qt. So the hand-off is a lock-free slot plus a GUI-side
`QTimer`, which is also the tree's existing idiom for GUI-thread periodic work
(`include/CPULoadWidget.h:69`, `src/gui/widgets/SimpleTextFloat.cpp:48-50`,
`include/StepRecorder.h:97`).
The trade is up to 10 ms of latency on a one-shot user gesture, in exchange for a MIDI
input thread that allocates nothing.

**What each thread allocates:**

| Thread | Work per CC | Allocates |
| --- | --- | --- |
| MIDI input | 1 atomic load when disarmed; otherwise 2 int stores + 1 bool store + 1 bool store | **nothing** |
| GUI thread (timer tick) | 1 atomic exchange when idle; on a request, the whole binding | `MidiController`, `ControllerConnection`, Qt's own event/timer internals |

The input path is free of locks and waits. `static_assert`s in
`src/core/MidiLearn.cpp:44-47` pin the atomics the input thread touches as
lock-free, so a platform where `std::atomic<bool>` grew a lock would fail to build
rather than silently put a lock on that thread.

Behaviour with learn unarmed is unchanged: one atomic load and `return false`. The
branch's 512-CC flood case (`MidiLearnTest::LearnOffIgnoresTheSameCc`) still passes
untouched.

## 3. Ownership and lifetime: what guarantees the deferred bind's object is alive

The short answer is that the question is answered by construction — nothing that could
die is ever handed to the other thread.

1. **What crosses threads is data, not an object.** The hand-off is two `int`s in a
   lock-free slot. There is no pointer whose lifetime has to be negotiated, extended
   or guarded across the hand-off, so there is no window in which the MIDI input
   thread holds a reference to a model.
2. **The target is resolved on its owning thread, at drain time.**
   `MidiLearn::setFocusTarget()` (`src/core/MidiLearn.cpp:88`) stores a
   `QPointer<AutomatableModel>` (`include/MidiLearn.h:131`), and
   `bindFocusedControl()` reads it on the GUI thread. If the model died between the CC
   and the drain — the GUI thread deleted the track, closed the project, unloaded the
   plugin — the `QPointer` has already self-nulled (`~QObject` clears it), the drain
   finds `target == nullptr` and drops the request. What happens then is defined, not
   accidental: no binding is created, the binding count does not move, the seam stays
   where it was, and the learn (already spent by the claim) stays disarmed.
3. **The MIDI input thread never reads the target pointer**, only the
   `m_focusTargetSet` flag (`include/MidiLearn.h:130`), which is why the `QPointer`
   can stay GUI-thread-only without being a new shared object.
4. **The rest of the objects are looked up at drain time on the GUI thread.**
   `Engine::getSong()` (`src/core/MidiLearn.cpp:237`, previously read on the MIDI
   thread) and the model's `MidiPort`s are read where they live; a null `Song` drops
   the request the same way a null target does.
5. **Ownership of what the bind creates is unchanged.** `ControllerConnection` owns
   MIDI controllers (`m_ownsController`, `src/core/ControllerConnection.cpp:129-130`)
   and the target model deletes the connection
   (`src/core/AutomatableModel.cpp:77-80`). The controller stays deliberately
   parentless — now for a different reason than before (it used to be "do not splice a
   QObject from the wrong thread", which no longer applies): re-parenting it would
   change where it sits in the project's object graph for no gain.
6. If the application is tearing down, the pending request dies with the process; the
   delivering timer is a member of `MidiLearnGui`, so it cannot outlive the object
   whose slot it calls.

## 4. The proof

Two new test binaries, each registered in `tests/CMakeLists.txt` and
`tests/all-sources.txt`.

`tests/src/core/MidiLearnThreadTest.cpp` (QTEST_GUILESS_MAIN, same conditions as the
existing `MidiLearnTest`):

- `BindingIsCreatedOnTheGuiThreadNotTheMidiInputThread` — arms learn the way the menu
  does and focuses a control, then delivers the CC through
  `MidiLearn::handleMidiEvent()` **from a `std::thread`** (a non-Qt thread, which is
  what `MidiJack`'s callback is). Assertions after the join, which are the proof: the
  model has **no** controller connection, `hasPendingBinding()` is true, the binding
  count has not moved and `lastBindingThreadId()` is unchanged. If the write were
  still on the input thread, the first of those fails. The binding is then delivered
  by the production path — `MidiLearnGui`'s GUI-thread timer, no direct drain call —
  and the test asserts the connection exists, that
  `lastBindingThreadId() == QThread::currentThreadId()` (the seam the bind itself
  sets, `src/core/MidiLearn.cpp:269`), that it differs from the delivering thread's
  id, that channel/controller round-trip (1-based port numbering), that the learn
  disarmed, and that the connection drives the model.
- `TargetGoneBeforeTheDeferredBindDropsTheRequest` — the lifetime case: the CC arrives
  from the MIDI thread, the model is `delete`d before the drain, and the drain then
  drops the request. Asserts the `QPointer` is already null, `applyPendingBinding()`
  returns false, no binding was counted and the seam did not move. With the old raw
  pointer in a slot this test would be a use-after-free.
- Negative control, in this tree's established shape: with
  `MIDILEARN_RACE_CONTROL_INVERTED=1` the central assertion flips to the pre-fix
  expectation ("the write happened on the MIDI input thread") and the run must fail.

`tests/src/gui/MidiLearnGuiTest.cpp` — the only MIDI-learn test with a `QApplication`,
because a `QAction` cannot be constructed without a `QGuiApplication` (it segfaults
inside `QAction::QAction` under a bare `QCoreApplication`; confirmed with `gdb`). Its
`main` defaults `QT_QPA_PLATFORM` to `offscreen` when no platform and no display are
configured, so a bare `ctest` on a headless box works; the repo's gate runner exports
`offscreen` itself (`.github/workflows/quality-gates.yml:85`,
`tests/run-all-gates.sh` Gate 1), and the repo already has two `QTEST_MAIN` tests
(`tests/src/plugins/PluginPortsMigrationTest.cpp:730`, `AudioPluginTest.cpp:438`).

Commands and results, exit codes measured unpiped:

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
  configure EXIT=0   (log: build/configure.log)
  build EXIT=0       (log: build/build.log)
  ctest EXIT=0       (log: build/ctest.log)
  ctest totals: 100% tests passed, 0 tests failed out of 28
  deviation: Qt5 development files not found: added -DWANT_QT6=ON
  local-ci: overall exit=0

$ grep -n MidiLearn build/ctest.log
   1/28 Test #10: MidiLearnThreadTest ..............   Passed    3.77 sec
  10/28 Test  #9: MidiLearnTest ....................   Passed    3.32 sec
  28/28 Test #25: MidiLearnGuiTest .................   Passed    2.93 sec

$ cd build/tests && ./MidiLearnThreadTest          # EXIT=0
  PASS   : BindingIsCreatedOnTheGuiThreadNotTheMidiInputThread()
  PASS   : TargetGoneBeforeTheDeferredBindDropsTheRequest()
  Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 3572ms

$ ./MidiLearnGuiTest                               # EXIT=0
  PASS   : MenuTickIsReconciledWithoutReopeningTheMenu()
  Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 2792ms

$ ./MidiLearnTest                                  # EXIT=0 (untouched file)
  Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 5168ms
```

The two inverted controls, which must fail and therefore show the checks can fail:

```
$ MIDILEARN_RACE_CONTROL_INVERTED=1 ./MidiLearnThreadTest      # EXIT=1
  FAIL!  : BindingIsCreatedOnTheGuiThreadNotTheMidiInputThread()
           'target.controllerConnection() != nullptr' returned FALSE.
           Loc: [tests/src/core/MidiLearnThreadTest.cpp(155)]
  Totals: 3 passed, 1 failed

$ MIDILEARN_NEGATIVE_CONTROL_INVERTED=1 ./MidiLearnTest         # EXIT=1
  Totals: 7 passed, 1 failed
```

The existing tests were **not edited** to make room for any of this:
`git diff 22617a93c -- tests/src/core/MidiLearnTest.cpp` is empty, it still passes
8/8, and its inverted control still fails as designed.

Gate registrations, and the gate exit codes on this commit (`fork-sources-gate` and
`no-upstream-regression-gate` were re-run on it; `run-all-gates` was measured on the
fix commit `838e38c22` — the only later change is this `.md`, which none of those
nine gates scans):

```
$ bash tests/fork-sources-gate.sh               # EXIT=0
  scanned 1098 tracked source file(s) ... 104 fork-NEW, 995 inherited upstream, 0 stale
  PASS: every tracked source in scope is registered

$ bash tests/no-upstream-regression-gate.sh     # EXIT=0
  PASS: every change to upstream-inherited code since 01148947ea4d8bdb05c237942758d61acc867223
        is declared (35 file(s) in the ledger)

$ bash tests/run-all-gates.sh                   # EXIT=3
  1 ctest PASS | 2 coverage SKIP | 3 no-tautology PASS | 4 complexity PASS
  5 mutation PASS | 6 upstream-regression PASS | 7 file-length PASS
  8 duplication PASS | 9 fork-sources PASS
  RESULT: PASS-WITH-SKIPS (exit 3) — 1 of 9 gates did not run
```

Exit 3 from `run-all-gates.sh` is "every gate that ran passed, but at least one was
skipped" (Gate 2, coverage, needs `--with-coverage`), which is the gate runner's
defined incomplete-run result, not a failure.

Two registrations came with the fix because the branch was failing its own gates
without them, and neither is cosmetic:

- `tests/src/core/MidiLearnTest.cpp` (from `22617a93c`) was in **no** scope manifest:
  Gate 9 named it as `NOT IN tests/fork-sources.txt` and exited 1 before this change.
  Both MIDI-learn test sources are now in `tests/all-sources.txt`, which is where this
  repo keeps test sources (fork-NEW *production* sources go in `tests/fork-sources.txt`;
  nothing new was added under `src/` or `include/`).
- The midi-learn feature's four upstream-inherited files —
  `include/MainWindow.h`, `include/MidiController.h`, `src/core/midi/MidiAlsaSeq.cpp`,
  `src/core/midi/MidiClient.cpp` — were changed by `5d6ccdf1f` and never declared, so
  Gate 6 exited 1 on this branch. They are now in
  `tests/upstream-modifications.txt` with reasons read off the diff. I did not touch
  those four files; the ledger was simply incomplete for the branch, which is exactly
  what Gate 6 is for.

The three gate-suite commits needed to run `tests/fork-sources-gate.sh` at all are
cherry-picked from `post-alpha/lufs-wire` (`879e251ef`, `9763a185c`, `d96db9e4c`,
tests/CI only, `-x` provenance in the messages): this lane's base (19:21 today) predates
Gate 9 (21:02 today), so the requested command did not exist in the worktree.

## 5. The Edit ▸ MIDI Learn tick

Small and local, so fixed rather than left. The action was only ever re-read by
`MainWindow::updateMidiLearnAction()` on the Edit menu's `aboutToShow`
(`src/gui/MainWindow.cpp:359`), so after a learn completed elsewhere the tick stayed
set until the menu was reopened. `MidiLearnGui::syncArmedState()`
(`src/gui/MidiLearnGui.cpp:84`) now runs on the same bind timer and, when the learn is
no longer armed, unchecks the action, clears the focus target, stops the timer and
removes the event filter — so the tick is reconciled within one poll interval.

No `MainWindow` change was needed for this (it would have pulled an upstream-inherited
file into the diff); the GUI half already held the action pointer.

## 6. What is NOT proven

- **The MIDI input call sites are compiled, not driven.** The CC in the tests is
  delivered by a `std::thread` calling the same entry point the clients call; no ALSA
  sequencer, raw client or JACK callback is exercised (`Engine::init(true)` forces
  `MidiDummy`). The mapping "MidiAlsaSeq::run() / processParsedEvent() are the only
  callers" is from reading the tree, not from a run.
- **No allocation counter.** "The MIDI input thread allocates nothing" rests on
  reading `requestBinding()`: two `int` stores, one `bool` store, one `bool` store —
  no `new`, no `QEvent`, no `std::function`, no container. That is a code-reading
  claim, not a measured one; the repo's gold standard (an allocation counter) is not
  applied here.
- **The 10 ms delivery latency is not asserted.** The tests assert delivery happens
  (via `QTRY_VERIFY`'s window), not how quickly; a regression that made delivery take
  a second would still pass.
- **The target-removal case is proven via the `QPointer` seam**, not under ASan: the
  test asserts the pointer is already null and the request dropped. It does not prove
  a hypothetical raw-pointer implementation would crash.
- **The drain is exercised through a real timer only in the shapes above** — the
  app's own arm path (`MidiLearnGui::setArmed`), not a full `MainWindow` with a real
  event loop and an unplugged device.
- **The menu tick is proven with a bare `QAction` under the offscreen platform**, not
  inside a real, open menu.
- **Two Qt behaviours are relied on and not tested here**: a destroyed `QObject`
  nulling its `QPointer`, and `QTimer` timeouts running on the thread that created the
  timer. Both are documented Qt guarantees and used elsewhere in the tree.
- **The threaded path is not covered by the fork coverage ratchet** in any special
  way; the new tests join the suite like any other.
