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

**R5 — A copy payload carries no identity.** Rule R4 is checked at the reader, because a copy payload (the clip drag/copy DataFile, `Track::clone()`'s temporary document, an instrument or device preset, a `plugin.state_*` document) writes the SOURCE object's attributes verbatim — `id` included — and the copy is built by *re-loading* that element, not by calling a copy constructor. `ProjectIds::isDocumentElement()` is the single test both the writers and the readers use: the object's own element name is identical in a document and in a payload, so only what WRAPS it distinguishes the two. A clip or a note built from a payload keeps the id its constructor handed out, and the payload's id (which names the still-live original) is ignored.

**R6 — The journal id is not the stable id.** `trk-`/`clip-`/`note-`/`ch-`/`fx-` are `ProjectIds` numbers, written as the `id` attribute of the object's element. The journal id (`jo_id_t`, written as a `<journallingObject id="N">` CHILD element) is a separate, per-session number that automation endpoints and undo steps are recorded against. A `saveState` override in the inheritance chain of `Clip` must therefore call `JournallingObject::saveState`, never `SerializingObject::saveState` directly — the latter drops the journal node from every element the build writes and takes the whole undo mechanism with it (`docs/UNDO-BOUNDS.md`).

## Consequences

**Row 75 — undo of a deleted track.** A deleted track is restored by re-loading its element (`Track::loadTrack` deletes every clip and re-creates it), so the ids of every object in it come out of the checkpoint. Before this slice the track came back as its `trk-<n>` while its clip came back as `clip-0` where it had been `clip-1`: the clip id was the clip's arrangement ordinal, and the deletion had renumbered that space. The point of R1/R2/R3 is that the restored clip answers with the id it had, so an id cached across the delete still names the same clip.

## Proof

`tests/control-stable-ids-slice2.py` — driven through the control socket against the real binary, asserts that the ids reported by `arrangement.get_state`, `roll.get_state`, `mixer.get_state` and `dsp.get_state` are identical before and after a `project.save` / `project.open` cycle (with the model MOVED in between, so the post-open ids can only have come from the file), that the saved document carries an `id` attribute on each family's element and no `dev-` selector, that a sibling's delete does not renumber any persistent family, that `track.remove` + `control.undo` restores the deleted track with the same `clip-` and `note-` ids (the row-75 case above), and that `control.id_contract` declares the six families with their persistence and live counts.
