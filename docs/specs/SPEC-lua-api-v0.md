<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-13).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, specs/SPEC-lua-api-v0.md
    sha256   : 8ffcec1cd6dd50594d0776776140bcef57238054b3dc94a4f7217de8c4fa6c2c
    bytes    : 7937
    why this file: the "specs/" citation class: cited by 6 places including CMakeLists.txt, docs/LUA-API-STABILISATION.md and the Lua test suite
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# SPEC: Lua Scripting API v0

> **Task:** AI-KOS task #561 (program `lmms-fl-replacement-program`, mission `lmms-collab-scripting-mission`)
> **Status:** Draft · **Version:** 1.0 · **Written:** 2026-09-08
> **Sources:** findings-collab-scripting.md (Renoise XRNX analysis, sandbox precedents), REPORT.md plan P7, direct source verification against clone 4e677cb

---

## 1. Goals & Non-Goals

**Goals:** v0 Lua API enabling generative MIDI/pattern manipulation scripts; safe (sandboxed, versioned); the Renoise XRNX model as gold-standard precedent (100+ API classes, stability without crashes).

**Non-goals (v0):** no direct audio-thread scripting (command queue only); no arbitrary C bindings; no GUI scripting; no audio-buffer DSP in Lua (that's the WASM track).

## 2. Binding Layer Decision

| Criterion | sol2 | LuaBridge |
|---|---|---|
| C++20 | Native, heavy template use of modern C++ | Solid; older but maintained |
| Maintenance | Active, wide adoption | Maintained, smaller surface |
| Compile-time cost | Heavy (header-only, slows builds) | Lighter |
| Runtime registration cost | Fast | Fast |

**Decision: LuaBridge.** Rationale: LMMS builds already compile slowly (large Qt surface); sol2's template weight would add measurable build time for no capability we need in v0. LuaBridge covers usrsct-style class/method binding cleanly. Revisit if metatable-heavy API grows past ~50 classes.

## 3. Class Inventory v0 (~28 classes, all PROPOSAL wrappers)

Wrapped classes verified to exist in clone 4e677cb. `Engine` accessors: include/Engine.h:69-79 — `getSong()` (:69), `mixer()` (:71), `patternStore()` (:74), `projectJournal()` (:79).

### Transport/Song
| Class | Wraps | Key methods |
|---|---|---|
| `Song` | include/Song.h | `addPatternTrack()`, tempo, transport play/stop, `saveProject()` |
| `Transport` | Song's transport surface | `play()`, `stop()`, `position()` |
| `PatternStore` | include/PatternStore.h:64 — `class PatternStore : public TrackContainer` | `numPatterns()`, iteration |

### Pattern/Note
| Class | Wrapped | Key methods |
|---|---|---|
| `PatternClip` | include/PatternClip.h:37 | `addNote(note)`, `removeNote(note)`, `notes()` |
| `Note` | include/Note.h:267-282 fields | `key()`, `setKey()`, `length()`, `setLength()`, `pos()`, `volume()` |
| `NoteBuilder` | wraps Note ctors (PROPOSAL) | fluent `at(key).pos(t).len(n).vol(v)` |

### Instrument/Track
| Class | Wrapped | Key methods |
|---|---|---|
| `InstrumentTrack` | include/InstrumentTrack.h | `instrumentName()`, `volume()`, `panning()` |
| `Instrument` | existing Instrument base | `name()`, parameter access via AutomatableModel |
| `Track` | include/Track.h | `name()`, `type()`, clip iteration |
| `FloatModel`/`BoolModel` | include/AutomatableModel.h | `value()`, `setValue()` — the automation surface |
| `MidiEvent` (emit) | existing MIDI event types | `noteOn`, `noteOff` construction for MIDI-out scripts |

### Project/File (sandboxed)
| Class | Wrapped | Notes |
|---|---|---|
| `ProjectFile` (PROPOSAL) | ProjectJournal access | read/write restricted to project dir |
| `LuaLog` (PROPOSAL) | — | script console output |

(A full 30-class table with per-method signatures is G1 output; this spec fixes the *surface groups* and wrapping policy.)

## 4. Threading Model

- Scripts run on a **dedicated worker thread**, never the audio thread.
- Audio-touching operations go through a **command queue** (SPSC, pre-allocated) consumed by the audio thread at period boundaries — same invariant class as mixer spec §5.
- Scheduling: explicit trigger (menu action / shortcut) in v0. Idle-hook scheduling deferred.
- Realtime violation = command-queue overflow → violation logged, command dropped, script flagged (never blocks audio).

## 5. Sandboxing

- **Lua 5.4**, embedded via LuaBridge.
- **Instruction budget:** `debug.sethook(count)` instruction-count hook per script invocation (findings-collab-scripting.md precedent); budget per script call, exceeded → script aborted, error surfaced.
- **Stdlib:** expose `string`, `table`, `math`; remove `os`, `io`; `require` disabled; `load`/`loadstring` restricted to bytecode-safe mode.
- **File IO:** only through the `ProjectFile` wrapper class, restricted to the project directory.
- No arbitrary C function exposure; only API classes registered.

## 6. API Versioning

- Scripts declare `--! lmms-api 0.1` header; loader rejects incompatible versions with a clear message.
- Deprecation: v0 fields never removed; v0.x additions only. Breaking changes → v1.0 with migration notes.

## 7. Example Scripts (matching board success criteria)

1. **create-pattern.lua** — create pattern, add 16-step hi-hat notes, set velocity accents
2. **generative-bass.lua** — seeded RNG arpeggio in a minor scale over 8 bars
3. **midi-router.lua** — script listens to MIDI-in events (v0: polled buffer) and emits transposed notes on a second track

## 8. Test Plan

- Unit: each API class round-trips against the real engine in a headless LMMS build.
- Sandbox: runaway-loop script halts via instruction hook without engine stall; file-access attempt outside project dir rejected.
- End-to-end: 3 example scripts run against a demo project; results verified by engine state inspection.

## 9. Phased Steps (with gates)

1. **G1** — LuaBridge + Lua 5.4 vendored; `hello.lua` prints via LuaLog in headless build. *Gate: builds, runs.*
2. **G2** — Transport/Song + Pattern/Note classes bound; example 1 works on real project. *Gate: script creates pattern + notes, undo works.*
3. **G3** — Threading + command queue + instruction hook. *Gate: sandbox tests pass.*
4. **G4** — Remaining inventory + versioning + 3 examples shipped. *Gate: board success criteria met.*

## 10. Risks

| Risk | Mitigation |
|---|---|
| Lua thread touches engine state mid-save → corruption | All state ops go through command queue + ProjectJournal journaling |
| Instruction hook evadable via C-bound calls | v0 exposes only whitelisted API classes; no user C bindings |
| Engine internals churn between versions | Wrapper layer isolates scripts from internal renames (clone already renamed Pattern→PatternClip) |

## 11. Open Questions

- **OQ-1:** Idle-triggered scripts (auto-run on transport events) — v0 or v1? (Recommend defer; needs event-system design)
- **OQ-2:** Undo integration depth — per-script single undo step (v1 default) vs transactional?

## 12. Self-review & claim verification (2026-09-08, orchestrator)

Every cited C++ claim was re-verified against the clone @ `4e677cb`:

| Claim | Verified |
|---|---|
| `include/Engine.h` accessors `getSong()` :69, `patternStore()` :74, `projectJournal()` :79 | ✅ exact |
| `include/PatternStore.h:64` `class LMMS_EXPORT PatternStore : public TrackContainer` | ✅ exact |
| `include/PatternClip.h:37` `class PatternClip : public Clip` | ✅ exact |
| `include/Note.h:267-284` fields `m_key, m_volume, m_panning, m_length, m_pos, m_detuning, m_type` | ✅ present |
| Wrapped types exist: Song.h, PatternStore.h, PatternClip.h, Note.h, InstrumentTrack.h, AutomatableModel.h, ProjectJournal.h | ✅ all present |

**Path drift:** the playbook specified `scripting/SPEC-lua-api-v0.md`; the spec lives at
`specs/SPEC-lua-api-v0.md` (this file). No other drift.

**Open-question defaults taken autonomously (reversible, v1-scope):**
- **OQ-1 (idle-triggered scripts):** DEFERRED to v1, per the spec's own recommendation — needs an event-system design that v0 must not pre-empt.
- **OQ-2 (undo depth):** per-script **single** undo step is the v0 default; transactional undo is v1. Reversible without an API break.

## 13. Sources

- Clone 4e677cb: include/Engine.h:69-79 (accessors), include/Song.h, include/PatternStore.h:64, include/PatternClip.h:37, include/Note.h:267-284, include/InstrumentTrack.h, include/NotePlayHandle.h
- findings-collab-scripting.md (Renoise/sandbox/CPython-cost analysis); REPORT.md plan P7
