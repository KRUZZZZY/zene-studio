# Space Bunny batch 06 — `core` lane

Six Luna findings, each checked against the current tree
(`bugs/bunny-core-2026-10-05`, head before this batch `ddbaebe5d`). Line numbers
are from the current tree, not the revisions the findings quoted.

## Summary

| Cluster | Verdict | Commit |
| --- | --- | --- |
| 5337 — Model accessors return an unregistered Lua class | FIXED | `947ea07ac` |
| 5347 — Ordinary allocation failures reported as budget exhaustion | FIXED | `652e17d66` |
| 5357 — Recorder overflow can leave a performance unlandable | FIXED | `eedbc68d5` |
| 5312 — Recording overflow silently removes audio and shifts the take | REAL-UNTESTED | none |
| 3508 — Recording starts a thread and allocates on the audio path | SKIPPED-SCOPE | none |
| 5335 — Mixer and effect wrappers can dereference deleted objects | SKIPPED-SCOPE | none |

### Cluster 5337 — Model accessors return an unregistered Lua class
Verdict: FIXED
Evidence: `include/ScriptDawBindings.h:89-91,150` declared `AutomatableModel* gainModel/muteModel/soloModel`
and `AutomatableModel* parameter(int)`, and `src/core/ScriptDawBindings.cpp:451-459` /
`src/core/ScriptDawEffects.cpp:158-163` returned the raw `&m_channel->m_volumeModel` etc.
`AutomatableModel` has no `beginClass` anywhere (confirmed with a bounded `rg -n -l 'beginClass' src`),
so LuaBridge raised "The class is not registered in LuaBridge" out of the script; the registered
classes are the wrappers `LuaFloatModel`/`LuaBoolModel` (`src/core/ScriptBindings.cpp:337,347`).
Fix: the accessors now return the pooled registered wrappers, as the instrument-track bindings
already do (`LuaInstrument::parameterModel`), with the wrapper types forward-declared in
`ScriptDawBindings.h` and `ScriptBindings.h` included by both `.cpp` files.
Commit: 947ea07ac
Test: `ScriptDawBindingTest::modelAccessorsReturnRegisteredLuaWrappers`
Red/green: `cmake --build build --target zene ScriptDawBindingTest` → EXIT=0;
`cd build/tests && ctest -R '^ScriptDawBindingTest$' --output-on-failure` → EXIT=8
(`lua: (string):3: The class is not registered in LuaBridge`) before the fix, EXIT=0 after.
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 5347 — Ordinary allocation failures are reported as budget exhaustion
Verdict: FIXED
Evidence: `include/ScriptMemoryBudget.h:107-114` incremented the same `refused` counter on a real
`realloc` failure as on a budget refusal, and `src/core/ScriptEngine.cpp:141-146` reported every
`LUA_ERRMEM` through `budgetExceededMessage`. A system OOM therefore produced a false
"memory budget exceeded" diagnosis.
Fix: a separate `failed` counter; realloc failure now increments only `failed`, and a new
`memoryErrorMessage()` names a system allocation failure when `refused == 0 && failed > 0`,
keeping the existing budget wording otherwise. The `script.run` result schema is unchanged
(`memory_refusals` still counts budget refusals only).
Commit: 652e17d66
Test: `ScriptMemoryBudgetTest::aSystemAllocationFailureIsNotReportedAsBudgetExhaustion`
Red/green: with the old counting restored in place, `cmake --build build --target zene ScriptMemoryBudgetTest` → EXIT=0,
`cd build/tests && ctest -R '^ScriptMemoryBudgetTest$' --output-on-failure` → EXIT=8
(`'system.refused == 0' returned FALSE`); after the fix EXIT=0. Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 5357 — Recorder overflow can leave a performance permanently unlandable
Verdict: FIXED
Evidence: `include/SessionArrangementRecorder.h:104` held `Capacity = 64` while
`include/SessionScheduler.h:217` allows `MaxActiveSlots = 64` concurrently launched slots and a launch
is TWO events (start + stop). 64 starts therefore fill the ring, every later stop is dropped by
`push()` (`include/SessionArrangementRecorder.h:124-137`), and
`src/core/ControlCommandsSessionRecordLand.cpp:126-143` refuses a pass that still sees open starts while
consuming nothing — and neither `setArmed(false)` nor a scheduler reset clears the ring, so that
performance could never be landed.
Fix: the ring is sized for one whole worst-case performance, `Capacity = 2 * MaxActiveSlots`
(`include/SessionArrangementRecorder.h:104`), so every launch the scheduler can run has room for its
stop. The bounded drop-and-count behaviour itself is unchanged.
Commit: eedbc68d5
Test: `SessionArrangementRecordTest::recorderCapacityCoversAWorstCasePerformance`
Red/green: `cmake --build build --target zene SessionArrangementRecordTest` → EXIT=0;
`cd build/tests && ctest -R '^SessionArrangementRecordTest$' --output-on-failure` → EXIT=8
(`'SessionArrangementRecorder::Capacity >= 2 * SessionScheduler::MaxActiveSlots' returned FALSE`) before
the fix, EXIT=0 after. Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 5312 — Recording overflow silently removes audio and shifts the take
Verdict: REAL-UNTESTED
Evidence: the mechanism is real — `include/SampleRecordAccumulator.h:125-129` counts dropped frames
(`framesDropped()` → `SampleFrameRingBuffer::overflowCount()`) but nothing in
`src/core/SampleRecordHandle.cpp` (whole file) or the sample-track recording path reads it, so a take
built from the surviving frames is short by exactly the dropped duration and later audio lands earlier.
The bounded drop is itself by design (`include/SampleRecordAccumulator.h:56-77`), and only the separate
`TakeRecorder`/`TrackRecorder` path surfaces a count (`include/TakeRecorder.h:78`,
`src/core/ControlCommandsRecorder.cpp:71`).
No change made: the trigger requires the drain thread to be starved for longer than ~0.34 s of ring
(`include/SampleRecordAccumulator.h:81-84`), which cannot be provoked deterministically headlessly, so
there is no RED test. Surfacing it would need the take-building path to insert the missing silence and a
way to report the overflow to the user — a schema/UI change outside this lane.
Commit: none
Test: none
Red/green: none

### Cluster 3508 — Recording starts a thread and allocates on the audio path
Verdict: SKIPPED-SCOPE
Evidence: `src/core/SampleRecordHandle.cpp:45-47` constructs the accumulator in the handle constructor
(`std::make_unique<SampleRecordAccumulator>`), whose constructor allocates the ring, the drain scratch and
the shared take and starts a `std::thread` (`include/SampleRecordAccumulator.h:89-96`); that handle is
created from `SampleTrack::play` on the audio thread (`src/tracks/SampleTrack.cpp:205`; the same
constructor notes it at `src/core/SampleRecordHandle.cpp:56-57`).
Out of scope by the realtime rule: moving construction and thread start off the audio thread means
pre-creating and owning the accumulator in the track/clip, which is a design change (and a lifetime one)
rather than a local defect fix.
Commit: none
Test: none
Red/green: none

### Cluster 5335 — Mixer and effect wrappers can dereference deleted objects
Verdict: SKIPPED-SCOPE
Evidence: `LuaMixerChannel`/`LuaEffect` keep raw `m_channel`/`m_chain`/`m_effect` pointers
(`include/ScriptDawBindings.h:105` region) with no lifetime guard, so a wrapper that outlives its target
can dereference freed memory. What the finding overstates is the effect-wrapper case:
`resolveLiveEffect` (`src/core/ScriptDawEffects.cpp:51-63`) re-validates the effect against the live chain
on every access, so a removed effect yields `nullptr` and an error rather than a dangling read.
Out of scope: closing the channel case needs stable-id re-resolution or wrapper invalidation on removal —
a binding-lifetime design change (rule 8 territory for the wrapper surface), not a local fix.
Commit: none
Test: none
Red/green: none

Leftovers: cluster 5312 remains unfixed and untested — the overflow count is available on the accumulator
but no recording path consumes it, and a deterministic headless reproduction of a starved drain thread was
not found; it needs a follow-up that decides where the missing duration is materialised and how an
overflow is surfaced to the user.