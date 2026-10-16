# Cluster new-p12-229 — Lua object views can dereference deleted project objects

**Verdict:** NEEDS-CONTEXT
**Difficulty (finding):** hard / needs_context
**Confidence:** high (the described hazard is reachable) — but the fix is architectural, not a point fix.

## Summary

The finding is *real in principle*: the Lua binding views (`LuaTrack`,
`LuaInstrumentTrack`, `LuaPatternClip`, `LuaInstrument`, `LuaFloatModel`,
`LuaBoolModel`) hold raw non-owning pointers (`Track*`, `MidiClip*`,
`AutomatableModel*`, `BoolModel*`, `Instrument*`) with no invalidation
mechanism. And a script *can* delete the object a saved view points at,
because `track.remove` / `clip.delete` are reachable from a script and are
applied on the apply side **synchronously with a Lua getter** via
`flushCommandsForRead()`.

However, the only correct remediation the finding itself names — "Use
invalidatable handles or generation-checked IDs for project objects" — is a
large, cross-cutting architectural change (it touches `Track`, `MidiClip`,
`Instrument`, `AutomatableModel`, every `Lua*` wrapper, the pool allocators in
`ScriptBindings`, and the `newXxx()` factories). It also requires a **design
decision** about the contract for a view whose target has been removed
(return `nil`/error? throw a Lua error? a typed "invalid" sentinel?), which is
not specified anywhere in the finding or the spec. That is beyond a
behavior-preserving point fix, and inventing the contract would violate
"never invent a behavior change when the contract is unclear."

## Evidence (exact file:line)

1. **Raw pointers, no invalidation.** `include/ScriptBindings.h:222`
   `Track* m_track{nullptr};` (inside `LuaTrack`); `:239`
   `LuaPatternClip(MidiClip* clip, ...)` storing `MidiClip* m_clip`;
   `:184` `LuaInstrumentTrack(InstrumentTrack* track)`; `:127`/`LuaFloatModel`
   `AutomatableModel*`; `:150` `LuaBoolModel` `BoolModel*`; `:597`
   `LuaInstrument(Instrument*)`. None of these getters/setters check whether the
   pointee is still alive beyond a `nullptr` test.

2. **Getters flush the command queue, then dereference.**
   `src/core/ScriptBindings.cpp:578-581` (`LuaInstrumentTrack::name`):
   ```cpp
   if (!m_track) { return QString(); }
   ScriptEngine::instance()->flushCommandsForRead();
   return m_track->name();
   ```
   Same pattern: `:603-605` `volume()`, `:620-623` `panning()`, `:675`
   `LuaTrack::name()`, `:744` `LuaPatternClip::name()`, `:763` `lengthTicks()`,
   `:777-779` `noteCount()`, `:785` `note()`, etc. All flush, then dereference
   `m_track` / `m_clip`.

3. **`flushCommandsForRead()` applies pending commands on the apply side during
   a run.** `src/core/ScriptEngine.cpp:727-746`. While a script runs, the apply
   pump (`:404-429`) is active, so a getter's flush releases `m_applyRequest`
   and the pump calls `applyCommands()` -> `applyCommand()`, which runs **all**
   queued commands, including destructive `RegistryCall` ops.

4. **`RegistryCall` runs a control command that can delete the target.**
   `src/core/ScriptDawEdit.cpp:286-288`:
   ```cpp
   case ScriptDawBindings::Op::RegistryCall:
       ScriptCommandBridge::applyRequest(...command.object0);
   ```
   `track.remove` (`src/core/ControlCommandsArrangement.cpp:204`) and
   `clip.delete` (`src/core/ArrangementEdits.cpp:53` `deleteClip`) are registered
   control commands reachable from Lua through `zene.command(...)` (the
   `RegistryCall` DawEdit op), and they free the `Track*` / `MidiClip*`.

5. **Concrete UAF sequence** (why it is reachable, not theoretical):
   ```lua
   local t = lmms.track(0)                 -- pools LuaTrack -> TrackA*
   zene.command("track.remove", {track=0})-- enqueues RegistryCall to free TrackA
   local n = t:name()                     -- getter flushes -> apply side frees TrackA
                                          -- then `m_track->name()` reads freed memory
   ```
   `LuaNote` is the *only* safe wrapper — it stores a value copy of `Note`
   (`include/ScriptBindings.h:53-57`), so note views are not affected.

## Why no minimal fix was written

- A `nullptr` check cannot detect a *freed* (non-null) pointer; detecting
  liveness requires a generation/epoch tag on the project object, i.e. the
  architectural change the finding names.
- The contract for "method called on a view of a removed object" (return
  `nil`? error? throw?) is undefined; picking one is a design decision that
  must come from the API spec owners, not from this bug-fix pass.
- Per the task rules a bug fix must be behavior-preserving and small; an
  invalidation scheme is neither, and there is no existing test family that
  pins the desired post-removal behavior.

## Commands run (investigation only — no code change, so no RED/GREEN)

Inspected (read-only), exit 0:
- `rg -n "enum class Type" include/ScriptEngine.h` — confirmed the ScriptCommand
  set has **no** `RemoveTrack`/`RemoveClip`; deletion of tracks/clips is only
  reachable via the `DawEdit`/`RegistryCall` op (control surface), not the
  direct Lua command enums.
- `rg -n "flushCommandsForRead" src/core/ScriptBindings.cpp` — confirmed all
  getters flush before dereferencing.
- `rg -n "RegistryCall" src/core/ScriptDawEdit.cpp` and inspection of
  `ScriptEngine::runOnWorker`/`flushCommandsForRead` — confirmed destructive
  control commands are applied on the apply side during a run, inline with a
  getter.

No build/test was executed because there is no code change to verify (no fix
was made). A regression test cannot be written without first deciding the
post-removal contract; a test that asserts "does not crash" on freed memory is
flaky UB and would not be accepted.

## Files touched
None (no product code changed). This is a NEEDS-CONTEXT report only.
