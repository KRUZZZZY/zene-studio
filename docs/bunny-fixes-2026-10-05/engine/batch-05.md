# Engine batch 05 — MIDI clock and MIDI learn

Worktree `zene-bunny-engine`, branch `bugs/bunny-engine-2026-10-05`. Six findings,
two fixed with regression tests, four closed without a product change.

| Verdict | Count | Findings |
| --- | --- | --- |
| FIXED | 2 | 4241, 4243 |
| ALREADY-FIXED | 1 | 4244 |
| FALSE-POSITIVE | 1 | 565 |
| SKIPPED-SCOPE | 1 | 4247 |
| REAL-UNTESTED | 1 | 5147 |

---

### Cluster 4241 — Changing the MIDI clock destination leaves the old destination subscribed

Verdict: FIXED

Evidence: `src/core/MidiClock.cpp:184-196` subscribed the named port and replaced
`m_masterPort` without unsubscribing the port that was there before.
`src/core/midi/MidiPort.cpp:360-382` sets `m_writablePorts[port] = subscribe` for
the named entry only and never clears another one, so both destinations kept
receiving. Real. `restoreMaster()` (`src/core/MidiClock.cpp:227-238`) only
unsubscribed when the restored port was empty, so undoing a switch to a named port
could leave both subscribed. Fixed by unsubscribing the previous destination before
subscribing the new one; `restoreMaster()` is now the exact inverse because both
halves of the setting are subscriptions.

Commit: 147175eed

Test: `MidiClockTest::changingTheClockDestinationUnsubscribesTheOldOne`

Red/green: `cmake --build build --target zene MidiClockTest` EXIT=0 (both times).
`cd build/tests && ctest -R '^MidiClockTest$' --output-on-failure` -> EXIT=8,
`FAIL '!clock->masterSubscribedTo("zene-a")' returned FALSE (the previous
destination must be unsubscribed)`, 14 passed / 2 failed. After the fix the same
command -> EXIT=0, 15/15.
`ctest -R '^ControlClockCommands$'` -> EXIT=0. Gates: complexity EXIT=0,
file-length EXIT=0.

The test asserts the subscription map itself, through a new control-thread
diagnostic `MidiClock::masterSubscribedTo()` (`include/MidiClock.h:275-277`), not
the `masterEnabled` flag. `MidiClient::subscribeWritablePort()` is a no-op
(`src/core/midi/MidiClient.cpp:95`), so the port's own `writablePorts()` map is
exactly what the clock wrote.

---

### Cluster 4243 — Re-enabling clock master during playback misses the transport edge

Verdict: FIXED

Evidence: `src/core/MidiClock.cpp:456-457` returns from `processAudioPeriod()`
while the master is off, before `trackTransport()` at :462, so `m_transportWasRunning`
kept the value it had when the master was switched off. `trackTransport()`
(`src/core/MidiClock.cpp:397-416`) only emits an edge when the running flag
CHANGES, so re-enabling mid-playback saw no rise and resumed the pulses with no
Song Position Pointer and no CONTINUE. Real. Fixed by clearing the tracked
transport state on a real change of master mode in `setMasterEnabled()` — on the
control thread, so the audio path is untouched. A repeat of the same call is not a
mode change and must not invent a second edge, which the test asserts too.

Commit: 7adf17592

Test: `MidiClockTest::reEnablingTheMasterDuringPlaybackEmitsTheTransportEdge`

Red/green: `cmake --build build --target zene MidiClockTest` EXIT=0.
`cd build/tests && ctest -R '^MidiClockTest$' --output-on-failure` -> EXIT=8,
`FAIL 'emittedCount(Continue) >= 1u' returned FALSE (no CONTINUE on re-enable:
off=6 before=6 monitor=clock)`, 14 passed / 2 failed. After the fix the same
command -> EXIT=0, 16/16. Gates: complexity EXIT=0, file-length EXIT=0.

The RED run above was taken with both new cases present and neither fix applied
(the diagnostic accessor was in place so the suite would compile and fail on its
assertions, not on a missing declaration). The test asserts the emitted ORDER from
the monitor — position then CONTINUE, never a START — rather than a bare counter.

---

### Cluster 4244 — First MIDI Clock use can allocate on the audio thread

Verdict: ALREADY-FIXED

Evidence: `src/core/MidiClock.cpp:99-110` is a lazily built singleton, so the
report's premise about `instance()` was correct in principle. It is already built on
a non-audio thread: `src/core/Song.cpp:134` calls `MidiClock::instance()` in the
Song constructor, on the song-building thread, immediately under a comment citing
RealtimeSanitizer run 36789477810 and naming this exact `new` as the thing the run
caught. `src/core/Song.cpp:316-320` calls `processAudioPeriod()` from
`processNextBuffer()`, so by the time the audio thread reaches the clock the
singleton already exists.

Commit: none

Test: none

Red/green: none

---

### Cluster 4247 — MIDI clock input ignores the selected source port

Verdict: SKIPPED-SCOPE

Evidence: `src/core/MidiClock.cpp:291-294` stores `m_slavePort` and nothing reads
it; `src/core/MidiClock.cpp:466-470` `handleInputMessage()` takes no source
identity, so the setting cannot be honoured; `src/core/midi/MidiAlsaSeq.cpp:771-783`
dispatches the clock family before the destination lookup, with a comment stating
this is deliberate ("The clock family is SYSTEM-wide ... a slave follows a clock from
wherever it arrives"). The gap is real, but the reported fix — "pass source identity
into clock input handling and reject messages from other ports" — changes the
`handleInputMessage()` signature and requires every MIDI driver (ALSA sequencer,
ALSA raw, JACK, OSS, and the client entry points) to supply and carry a source id,
including platform-only backends this lane cannot exercise headlessly. That is a
driver-interface change, not a behaviour-preserving minimal fix, and it is larger
than the ~150 changed lines this lane allows. Left alone.

Commit: none

Test: none

Red/green: none

---

### Cluster 565 — MIDI-learn event handling calls Qt from the JACK process callback

Verdict: FALSE-POSITIVE

Evidence: `src/core/MidiLearn.cpp:62-69` `onGuiThread()` calls
`QCoreApplication::instance()` and `QThread::currentThread()`. Both are pointer /
thread-local reads in Qt6 — no allocation, no lock, no logging — so the realtime
rule is not violated by them, which is what the finding's "may incur hidden work"
concern rests on. The path is also gated before it is reached:
`src/core/MidiLearn.cpp:156-197` reaches `onGuiThread()` only after `isEnabled()`,
the `MidiControlChange` type test and the focus-target check, so an unarmed engine
does one relaxed atomic load per event. The JACK route is real
(`src/core/midi/MidiJack.cpp:185-210` -> `src/core/midi/MidiClient.cpp:270` ->
`handleMidiEvent()`), but every event on it was already handed to Qt on that thread
before this call, so removing these two reads would not move the path off Qt.

Commit: none

Test: none (already covered by `MidiLearnTest` / `MidiLearnThreadTest`, which prove
the deferred bind runs on the GUI thread rather than the MIDI input thread)

Red/green: none

---

### Cluster 5147 — Simultaneous MIDI inputs can overwrite the learn request

Verdict: REAL-UNTESTED

Evidence: `src/core/MidiLearn.cpp:137-146` is a genuine TOCTOU — it tests
`m_pendingBinding` with a relaxed load and returns if set, then writes the two
payload words, then sets the flag with a release store. Two input threads can both
pass the test and both write `m_pendingChannel` / `m_pendingController`, so the
resulting binding can pair one event's channel with the other event's controller.
The window is a handful of instructions between :137 and :146, and only reachable
from two MIDI drivers delivering a CC simultaneously while learn is armed.

No product change made. A deterministic headless RED for this needs the race to
actually happen, which means either a real two-thread race or a stress loop that
fails intermittently — and an intermittently failing test is not a regression test.
The obvious fix in the finding ("reserve the slot with a compare-exchange before
writing its payload") is also wrong as written: `applyPendingBinding()`
(`src/core/MidiLearn.cpp:205-213`) claims the slot with
`m_pendingBinding.exchange(false, acquire)` and would read the flag before the
payload of a CAS-claimed slot was written, trading one race for a worse one. The
correct minimal design is a single packed `std::atomic<quint32>` payload (0 = empty,
otherwise `channel | controller << 8`) claimed with one `exchange` — a header and
source change in `include/MidiLearn.h:143-145` — but it ships only with a test, and
the test cannot be made deterministic.

Commit: none

Test: none

Red/green: none

---

Leftovers: cluster 5147 is a real data race left in place — the fix is a packed
atomic payload in `include/MidiLearn.h` plus `src/core/MidiLearn.cpp`, and it needs
a deterministic test that does not exist yet. Cluster 4247 needs a source-identity
parameter plumbed through every MIDI driver, which is a driver-interface change
rather than a bug fix. Nothing was half-applied, and no test, gate or ratchet was
weakened.
