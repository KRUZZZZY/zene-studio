# Space Bunny fixer lane `core` — batch-03

Worktree: `zene-bunny-core`, branch `bugs/bunny-core-2026-10-05`. Six findings reviewed against the
current tree. No product code was changed; no finding admitted a minimal fix that could be proven RED
headlessly. One cross-cutting note is recorded before the sections: cluster 72 from batch-01 said rule 6
forbids any snapshot publication or synchronization on the audio path. That was too broad and was
corrected in `docs/bunny-fixes-2026-10-05/core/batch-01.md` (commit 46ce2a2c5); rule 6 forbids
allocation, locking and unbounded growth on audio-thread callbacks, and bounded atomics or a
preallocated snapshot publication can comply. The verdicts below do not rely on the over-broad reading.

Verdicts: FIXED 0, FALSE-POSITIVE 2, REAL-UNTESTED 2, SKIPPED-SCOPE 2.

### Cluster 4170 — `std::span` breaks the stated C++17 build

Verdict: FALSE-POSITIVE
Evidence: the project is C++20, declared at `src/CMakeLists.txt:25` (`SET(CMAKE_CXX_STANDARD 20)`, under
the comment "Enable C++20") with `CMAKE_CXX_STANDARD_REQUIRED ON` at `CMakeLists.txt:319`. `std::span`
at `include/InstrumentTrack.h:150` is not a local deviation: 45 files under `include/` and `src/` use it
(`PlayHandle.h:115`, `AudioBuffer.h:220`, `TempoMap.h:168`, `Sample.h:64`, `AudioEngine.h:397` and 41
more), so it is the codebase-wide idiom, not the changed `playNote` signature the report singles out.
There is no C++17 build to break; the premise of the finding is wrong for this tree.
Commit: none
Test: none
Red/green: none

### Cluster 4252 — ALSA shutdown flag is a data race

Verdict: REAL-UNTESTED
Evidence: `include/MidiAlsaSeq.h:159` declares `volatile bool m_quit`; the destructor writes it at
`src/core/midi/MidiAlsaSeq.cpp:214` and `run()` reads it at `:719` and `:743` from the worker thread.
`volatile` does not synchronize, so the report's core claim is correct. The second half of the claim is
not: `wait()` cannot block indefinitely here, because `run()` blocks in `poll()` with a bounded
`EventPollTimeOut` of 250 ms (`src/core/midi/MidiAlsaSeq.cpp:43`, used at `:727`) and re-checks `m_quit`
on every wake, so the loop is left within one poll interval. The unbounded join at `:227` is a
deliberate, documented trade (`:215-226`) that replaced a bounded budget which caused an unconditional
`qFatal("QThread: Destroyed while thread is still running")` / SIGABRT in `Engine::destroy`. Making the
flag atomic is a one-word change, but a data race cannot be made deterministically RED headlessly (it
needs ThreadSanitizer plus a live ALSA sequencer, which this box does not have — the CI MIDI client is
the `MidiClientRaw` fallback), so per procedure no product change was made.
Commit: none
Test: none
Red/green: none

### Cluster 5137 — MIDI controller settings race with incoming events

Verdict: REAL-UNTESTED
Evidence: the audio-side reader is `MidiController::processInEvent`
(`src/core/midi/MidiController.cpp:77`), reached from `MidiPort::processInEvent` at
`src/core/midi/MidiPort.cpp:177`; it reads `m_softTakeoverEnabled`, `m_softTakeoverCaptured`
(`:91-92`), `m_softTakeoverTarget` (`:126,133-134`) and `m_feedbackEnabled` (`:102`). The writers are
control-thread: `setSoftTakeoverEnabled` (`:149-156`) and `setFeedbackEnabled` (`:169-211`) are called
from `src/core/ControlCommandsController.cpp:321,405`, `src/core/ControlCommandsControllerMap.cpp:87-88`
and `src/core/ControllerSurface.cpp:190,199`. The fields are plain `bool`/`float`
(`include/MidiController.h:149-151,166`) with no atomic and no lock, so the report's race is real. It is
the same shape as batch-01 cluster 5526 and cannot be made deterministically RED without ThreadSanitizer
and a racy playback/edit harness; no product change was made. Note the fix direction is the same one
batch-01 identified for `AutomationClip`: these are candidates for `std::atomic`, which rule 6 permits
(bounded atomics, no allocation, no locking).
Commit: none
Test: none
Red/green: none

### Cluster 3286 — Reopening a MIDI surface can apply events from its previous port

Verdict: FALSE-POSITIVE
Evidence: the report's premise — that `close()` and `open()` are called on the same live object so
`m_inbox` survives a port change — does not hold in this codebase. `close()` is called only from the two
derived destructors (`src/core/RemoteControls.cpp:79` and `src/core/LaunchBindings.cpp:112-115`), and
both `connect()` factories build a brand-new object and open it once:
`src/core/RemoteControls.cpp:91-95` does `s_remote.reset()` then `std::make_unique<RemoteControls()` then
`remote->open(...)`, and `src/core/LaunchBindings.cpp:122-123` does `std::make_unique<LaunchBindings()`
then `bindings->open(...)`. `open()` has no other caller (the only three occurrences of the surface
`open(` are the declaration at `include/MidiSurfaceInput.h:57` and those two factory calls), so there is
no close-then-reopen sequence on one object and no queue that outlives its port. The stale-event window
the finding describes is unreachable.
Commit: none
Test: none
Red/green: none

### Cluster 4232 — Surface input drops queued events without reporting them

Verdict: SKIPPED-SCOPE
Evidence: the observation is accurate — `include/MidiSurfaceInput.h:71-72` does
`m_eventsIn.fetch_add(1, ...)` before `(void)m_inbox.push(item)`, and `MpscQueue::push` returns false
when the 256-slot queue is full (`include/MpscQueue.h:52-53`), so a burst inflates the counter while
events are dropped. The suggested fix ("expose them in surface status") requires a new status field, and
the only surface status result is `RemoteControls::state()` (`src/core/RemoteControls.cpp:168-183`, which
already emits `events_in` at `:182`), consumed as the `remote.state` command result at
`src/core/ControlCommandsRemote.cpp:88-92`. Adding a key to that result changes a command's result
schema, which rule 8 reserves for the parent's ControlCommandsSnapshot canary. Counting drops without
exposing them would be dead state. Left alone.
Commit: none
Test: none
Red/green: none

### Cluster 5173 — Liveness tokens do not protect modulation targets from concurrent destruction

Verdict: SKIPPED-SCOPE
Evidence: the check-then-use window is real. `drivingSource` tests the token at
`src/core/ModulationLayerPublisher.cpp:189` (`!publisher.targetAlive(i, entry.token)`) and the caller
then writes through the raw `entry.model` at `:222`, so a target destroyed between the two is a
use-after-free. But the token scheme is the documented, deliberate alternative to `QPointer`: the header
comment at `include/ModulationLayer.h:36-46` states that a `QPointer` cannot be copied on the audio
thread at all (shared refcounted block; the copy can free on the audio thread — the 2026-09-28 survey
G1), which is why raw pointers plus `std::atomic<std::uint32_t>` liveness tokens
(`include/ModulationLayer.h:367-371,421`) were chosen. The fix the report asks for — deferring target
destruction until audio readers exit, or otherwise pinning the target — is a lifetime-scheme redesign of
the audio path, beyond a minimal patch and in the territory rule 6 guards. Left alone.
Commit: none
Test: none
Red/green: none

Leftovers: clusters 4252 and 5137 are genuine data races that remain unfixed because neither can be made
deterministically RED headlessly; both point at the same remedy — `std::atomic` on the offending fields
(`include/MidiAlsaSeq.h:159`, `include/MidiController.h:149-151,166`), which rule 6 permits as bounded
atomics with no allocation or locking. They should be closed by a ThreadSanitizer run or an
atomics-under-concurrent-edit harness rather than from this report lane. Cluster 4232 needs a decision
from the parent on the `remote.state` result schema before the drop counter can be exposed.