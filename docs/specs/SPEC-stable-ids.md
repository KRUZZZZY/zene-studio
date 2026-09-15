# SPEC-stable-ids.md — the stable-id contract (Zene Studio 0.3.0-alpha)

## Scope

The agent surface addresses objects by stable ids. Slice 1 (0.3.0-alpha, row 50) made `trk-<n>` persistent. Slice 2 (row 51) makes the remaining four document-object families persistent and reclassifies `dev-<n>` as what it always was.

## The contract — one line per family

| family | persistence | form | document element |
|---|---|---|---|
| `trk-` | persistent | `trk-<n>` | `<track id="n">` attribute |
| `clip-` | persistent | `clip-<n>` | `<midiclip|sampleclip|patternclip|automationclip id="n">` attribute |
| `note-` | persistent | `note-<n>` | `<note id="n">` attribute |
| `ch-` | persistent | `ch-<n>` | `<mixerchannel id="n">` attribute |
| `fx-` | persistent | `fx-<n>` | `<effect id="n">` attribute |
| `dev-` | catalogue | `dev-<n>` | not a document identity: build's plugin catalogue selector |

## Rules

**R1 — An id names an object, not a position.** The number is assigned once, at construction, by `ProjectIds::allocate()`, and never changes while the object lives.

**R2 — Persistence.** The id is written into the project file as an `id` attribute on the object's own element and read back on load. A file that carries one keeps it; a legacy file that does not keeps the constructor's deterministic assignment (the load walks elements in document order). `ProjectIds::noteLoadAssignment()` counts the upgrade.

**R3 — No rebirth.** `ProjectIds::observe(id)` raises the counter above every id that has been handed out or loaded, so a retired id can never be allocated to a different object.

**R4 — A copy is a new object.** `Note::clone()`, `Clip::Clip(const Clip&)` and every copy path allocate a fresh id. Two objects wearing one id would make the id an ambiguous address, which the contract forbids.

## Proof

`tests/control-stable-ids-slice2.py` — driven through the control socket against the real binary, asserts that the ids reported by `arrangement.get_state`, `roll.get_state`, `mixer.get_state` and `dsp.get_state` are identical before and after a `project.save` / `project.open` cycle, and that a sibling's delete does not renumber any persistent family.
