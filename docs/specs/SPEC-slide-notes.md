<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-15).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, specs/SPEC-slide-notes.md
    sha256   : d958a1b35c1aa327a98fa654f8c349fa0e4418beac67cf7970f1fa46efbf8fe5
    bytes    : 7059
    why this file: the note.slide_set/note.slide_clear design - cited by include/ControlRegistryGroups.h as the document the portamento flag has come from.
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# SPEC: Native Slide (Portamento) Notes

> **Task:** AI-KOS task #557 (program `lmms-fl-replacement-program`, mission `lmms-pianoroll-uidpi-mission`)
> **Status:** Draft · **Version:** 1.0 · **Written:** 2026-09-08
> **Sources:** findings-gap-analysis.md (issue #3190 lineage), REPORT.md plan P5, direct source verification against clone 4e677cb

---

## 1. Scope & Goals

Native slide/portamento notes in patterns: a note flagged *slide* glides pitch from the previous note to itself over its full duration (FL-style). Per-instrument opt-out for instruments where portamento is meaningless (drums, sample players). Old projects load unchanged.

**Out of scope:** MIDI-out slide CC semantics, per-slide duration curves (that's `DetuningHelper`'s job), slide in the BB editor.

## 2. Current State in Code (verified vs clone 4e677cb)

| Component | Where | Relevance |
|---|---|---|
| `Note` fields | include/Note.h:267-282 — `m_key`, `m_volume`, `m_panning`, `m_length`, `m_pos`, `m_detuning` (shared_ptr<DetuningHelper>), `m_type` | The slide flag lives here |
| Note serialization | src/core/Note.cpp:188-204 `saveSettings`: attributes `key/vol/pan/len/pos/type`; detuning saved as child | Serialization precedent — slide adds one attribute the same way |
| `Note::Type` enum | include/Note.h:118-124 — `enum class Type { Regular = 0, Step }` | **Precedent for a second per-note enum**: add `Slide` as a Type value OR a separate flag. See D-1 |
| `ParameterType` enum | include/Note.h:128+ — per-note automation parameter types; currently "only detuning/pitch bending is supported" | The per-note-automation hook that already exists |
| `DetuningHelper` | include/DetuningHelper.h:34 — `class DetuningHelper : public InlineAutomation` | Existing inline pitch automation = the composition surface |
| Pitch playback | include/NotePlayHandle.h:100 (`m_frequency`), :268 (`m_frequencyNeedsUpdate`), :323, :332 | Where glide interpolation happens |
| Piano roll | src/gui/editors/PianoRoll.cpp (moved from src/gui/ — REPORT.md path stale) | Draw tool + rendering |
| Strum/stacking | src/gui/editors/PianoRoll.cpp:386-390 (`InstrumentFunctionNoteStacking::ChordTable`) | Precedent for note-transform features living near the piano roll |

## 3. Design

### D-1: Flag vs Type (decision required — recommendation: separate bool)
Adding `Slide` to `Note::Type` collides with `Regular/Step` semantics (type describes *what the note is*; slide describes *how it enters*). Recommendation: `bool m_slide` + `slide()`/`setSlide()` accessors, serialized as `"slide":"1"` attribute in `Note::saveSettings` (Note.cpp:188). Absent attribute = false → old projects load unchanged (no DataFile version bump needed since XML attributes are optional by nature; the version chain only matters for structural changes).

### D-2: Playback interpolation
At `NotePlayHandle` (NotePlayHandle.h): when the handle starts for a slide note, it captures the *previous* note's end key in the same pattern (lookup responsibility of `PatternClip`, which owns the `NoteVector` — include/Note.h:284 `using NoteVector = std::vector<Note*>`). Glide implemented by updating `m_frequency` (NotePlayHandle.h:323) per period: linear (v1) interpolation from prev-key frequency to note frequency across the slide note's duration. `m_frequencyNeedsUpdate` (:332) already exists as the invalidation mechanism.

### D-3: Per-instrument opt-out
`Instrument` gets a virtual `bool supportsSlideNotes() const { return true; }` (PROPOSAL). Drum/sample instruments override to false → NotePlayHandle skips glide (plays straight pitch). v1 default: TripleOscillator true, AudioFile processor false.

### D-4: Piano roll
Slide draw tool in src/gui/editors/PianoRoll.cpp: modifier+click or a tool palette entry; render slide notes with a diagonal connector from the previous note's end to the slide note's head (visual precedent: FL).

## 4. Interaction with Pitch-Bend Curves (the real design question)

LMMS already has per-note pitch automation via `DetuningHelper` (InlineAutomation). Three options:

- **(a) Slide overrides detuning:** if a note is slide-flagged AND has detuning, detuning wins (slide ignored). Simple, predictable.
- **(b) Compose:** slide provides base glide; detuning curves apply on top (multiplicative or additive frequency offset). Musically richest.
- **(c) Conflict = warning:** editor flags the combination.

**Recommendation: (b) compose** — slide sets the starting glide target; detuning remains the per-note automation layer on top, exactly as it composes with regular notes today. Rationale: DetuningHelper is already an *offset* on the note frequency (InlineAutomation), so composition falls out of the existing architecture; overriding (a) would silently discard user automation. Edge rule: slide glide runs first; detuning automation begins after glide completes if the curve starts at slide-start (documented, testable).

## 5. Test Plan

| # | Test | Pass criterion |
|---|---|---|
| T1 | Two notes C4→C5, second flagged slide, TripleOscillator | Audible continuous glide; no step at note boundary |
| T2 | Same on one LV2 instrument | Glide present (frequency-driven instruments) |
| T3 | Slide note on instrument with `supportsSlideNotes()=false` | Straight pitch, no glide |
| T4 | Save/reload round-trip | `slide` attribute survives; glide identical after reload |
| T5 | Old project (no slide attributes) | Loads unchanged, byte-comparable rendering |
| T6 | Slide + detuning curve | Glide then curve, per D-4 rule |

## 6. Phased Steps (with gates)

1. **G1** — Note model: `m_slide` + accessors + serialization. *Gate: unit round-trip save/load; T5 passes.*
2. **G2** — Playback: prev-note lookup + linear glide in NotePlayHandle. *Gate: T1 passes on TripleOscillator.*
3. **G3** — Instrument opt-out hook. *Gate: T2, T3 pass.*
4. **G4** — Piano roll tool + connector rendering. *Gate: drawable, renders, T4/T6 pass.*

## 7. Risks

| Risk | Mitigation |
|---|---|
| Prev-note lookup ambiguous at pattern edges | Rule: slide glides from the *nearest preceding note in the same PatternClip*; none → no glide (documented) |
| Frequency update per period costs on polyphonic projects | Glide math is per-handle, O(1) per period; benchmark in G2 |
| instruments with internal pitch handling (e.g. Zyn) may fight glide | Opt-out hook + test in G3; document known exceptions |

## 8. Open Questions

- **OQ-1:** Should glide respect `m_detuning` starting offset as the glide *start* point (full compose) or only compose after glide? (G2 spike decides; D-4 rule is v1 default)
- **OQ-2:** Do MIDI-out instruments need slide → pitch-bend CC emission in v1? (defer; note for MIDI-out mission)

## 9. Sources

- Clone 4e677cb: include/Note.h:118-128, 267-282; src/core/Note.cpp:188-221; include/DetuningHelper.h:34; include/NotePlayHandle.h:100,268,323,332; include/PatternClip.h:37; src/gui/editors/PianoRoll.cpp:386-390; include/lmms_constants.h:38
- Issue #3190 (slide request closed as duplicate 2016 — lineage only; state volatile)
