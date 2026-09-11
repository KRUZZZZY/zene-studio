# Script-defined devices — design (NOT implemented)

> **Task:** #613 (Lua API stabilisation), scope item 2.
> **Status:** design only. No script-defined device was attempted. This document
> exists so the next lane starts from evidence rather than from a wish, and so
> nobody reads the stabilisation work as having delivered one.

## 1. Why this was not attempted

Scope item 2 asks for "the smallest honest version" of a device a script
defines and the engine runs. The evidence says there is no small honest version
on today's architecture, and the two cheap-looking versions are both dishonest:

* **Audio-rate `process(buffer, frames)` callback** — forbidden. The v0 spec
  excludes it by name: *"no direct audio-thread scripting (command queue only);
  … no audio-buffer DSP in Lua (that's the WASM track)"*
  (`specs/SPEC-lua-api-v0.md` §1). It would also break the realtime rule: Lua
  allocates, and the tree's rule is no allocation, locking or unbounded growth on
  audio-thread paths.
* **A device that is really "run this script again"** — the engine has no way to
  hold a callback between runs (§2.1), so "the engine runs your `process`" would
  in practice mean re-executing the script body on every trigger, repeating its
  side effects. That is a half-wired device, which the task explicitly forbids.

Landing a real one is a small project, not a slice: it needs a persistent
registry, a trigger the architecture does not have, a context object, a
persistence story for save/load, and a discovery/install path — five design
decisions, three of which the v0 spec deliberately deferred.

## 2. What the architecture actually is (evidence)

### 2.1 A Lua state lives for exactly one run — there is nowhere to keep a callback

`ScriptEngine::ScriptWorker::run()` (`src/core/ScriptEngine.cpp:85`, run body
`:96-132`) calls `luaL_newstate()` at the start of a run and `lua_close(L)` at the
end, on purpose: *"a fresh state per script means no script can observe another
script's globals and sandbox configuration cannot drift"*. The worker itself is
permanent, the state is not.

Consequence: a `function` value a script hands to the engine is invalid the
moment the run ends. Any device registry must keep its own long-lived
`lua_State`, and must therefore answer: whose state, which thread owns it, and
what happens to it when the sandbox rules change (today `openSandbox()` at
`src/core/ScriptEngine.cpp:138` and `ScriptBindings::registerAll()` at
`src/core/ScriptBindings.cpp:276` are both run per fresh state).

### 2.2 The only trigger surface is "run this source"

`ScriptEngine::runFile()` (`src/core/ScriptEngine.cpp:291`) and
`runString()`/`runOnWorker()` (`:337`) are the entire entry-point set. Nothing in
the audio engine, the mixer or the song's transport calls into the script engine:
`ScriptEngine::audioThreadTick()` (`:578`, declared `include/ScriptEngine.h:168`)
**has no caller anywhere in the tree** — verified with
`grep -rn audioThreadTick src include` — and the only production caller of
`processCommands()` is the headless `--run-script` path
(`src/core/main.cpp:799`). Scripts are otherwise applied by the `autoApply` flush
inside `runOnWorker()`.

Consequence: "the engine can run it" has no existing meaning. A device needs a
trigger design (transport start/stop? period boundary? an explicit host call?),
which is exactly the deferred open question OQ-1 in `SPEC-lua-api-v0.md` §11
("Idle-triggered scripts (auto-run on transport events) — v0 or v1?"). Devices
cannot be designed before it, and folding it in silently would pre-empt a v1
decision.

### 2.3 Mutation already has exactly one path, and it is the right one

Every engine change a script can request is a `ScriptCommand`
(`include/ScriptEngine.h:62-92`, enum at `:64`), queued on a bounded SPSC ring
(`ScriptCommandQueueCapacity = 1024`, `:51`) and applied only by
`ScriptEngine::applyCommand()` (`src/core/ScriptEngine.cpp:445`) on the apply
side. `AddNote`, `SetModelValue`, `AddInstrumentTrack` and `AddCheckPoint`
already cover what a note-generating device would emit, and
`LuaPatternClip::addNoteAt()` (`src/core/ScriptBindings.cpp:835`) is the binding
that reaches it.

Consequence: the *output* half of a device is already built and needs no new
API. This is the part of the design that is genuinely small, and it is why the
recommendation below is a note generator and not an effect.

## 3. Recommended shape, if a lane picks this up

**A script-defined note generator, invoked by the host — not an audio effect.**

1. **A device state.** A second `lua_State` owned by `ScriptEngine`, created with
   the same `openSandbox()` + `registerAll()` pair, kept for the lifetime of the
   process, and touched only on the script worker thread. Reuse, do not
   re-implement: the sandbox rules must not have two versions.
2. **A registry.** `ScriptEngine` gains a name → `{source path, luaL_ref of the
   entry function}` map. A script declares a device the way it declares a
   version — in the header — plus one registration call:
   `lmms.device('arp'):onProcess(function(ctx) … end)`.
3. **A context object.** A new binding class (`LuaDeviceContext`) exposing the
   bounded, non-audio facts a generator may read: bar/step position, tempo,
   block length in ticks, and the clip/track it may write to. Writing goes
   through the existing `ScriptCommand` queue, so the RT rule is untouched.
4. **A trigger.** An explicit host call, `ScriptEngine::invokeDevice(name,
   context)`, run on the apply side (identical thread contract to
   `runOnWorker()`); no audio-thread call site is added. Wiring a transport or
   period trigger is the v1 event-system decision, not this one.
5. **A negative control.** `lmms.device('nope')` must return a null view that
   fails on use, exactly like `LuaPatternStore::track(99)` (see
   `ScriptBindingsTest::trackWrapper`), never throw from C++ and never no-op
   silently.

**Touch points, as of this branch:**

| Concern | File:line |
|---|---|
| Run lifecycle to mirror for a device state | `src/core/ScriptEngine.cpp:85-132` |
| Sandbox setup to reuse | `src/core/ScriptEngine.cpp:138` |
| Binding registration to reuse | `src/core/ScriptBindings.cpp:276` |
| View pools (per-run recycling — a device state must NOT use these) | `src/core/ScriptBindings.cpp:218` (`beginRun`) |
| Command enum / queue | `include/ScriptEngine.h:51-92` |
| Apply side | `src/core/ScriptEngine.cpp:445` (`applyCommand`) |
| Proposed trigger entry point | new, modelled on `src/core/ScriptEngine.cpp:337` (`runOnWorker`) |
| Note output already available | `src/core/ScriptBindings.cpp:835` (`LuaPatternClip::addNoteAt`) |
| Version gate the device package inherits | `src/core/ScriptEngine.cpp:291` (`runFile`) |
| Persistence (a device instance in a saved project) | `src/core/Song.cpp:1215` (`saveProjectFile`), `:1008` (`loadProject`) — **deferred**; a device that cannot be saved must say so |
| Discovery / install | none today; see the package-format gap in `LUA-API-STABILISATION.md` |

## 4. What this document does not claim

* It does not claim the API is ready for devices, or that the shape above is the
  only one — it is the smallest one that keeps the RT rule and the sandbox
  single-sourced.
* It does not describe audio-rate DSP in Lua, which the v0 spec assigns to the
  WASM track.
* It does not claim the trigger question is settled: it is OQ-1.
