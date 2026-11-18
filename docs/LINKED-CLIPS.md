# Linked / smart clips (`clip.link_*`) — the relation, the decision, and what propagates

Zene Studio 0.3.0-alpha, feature-list row 6 (section 1), board task #645. Engine
half: `include/ClipLinks.h` (the relation: the group id on the clip, the group,
the mirror) and `src/core/ClipLinks.cpp` (its implementation). Clip-side state:
`Clip::linkId()` / `Clip::setLinkId()`, persisted by `Clip::saveClipEdits` /
`Clip::loadClipEdits` as the `link` attribute of the clip's own element. Surface:
`src/core/ControlCommandsClipLink.cpp` (`clip.link_create`, `clip.link_remove`,
`clip.link_make_unique`) and `src/core/ControlCommandsClipLinkState.cpp`
(`clip.link_get_state`, `clip.link_sync`) — two translation units because the group has five ids and the
file-length ratchet is not moved for a new feature — plus the helpers both halves
share in `include/ControlClipLinkSupport.h`.

**One-line summary.** Linked clips share notes, name and colour; each instance
keeps its own start, length and loop window. A geometry edit changes only the
selected instance, while a note edit in any member updates the shared notes.
The relation survives save/reload, and **Make Unique** detaches one instance
with its own content copy.

---

## 1. The decision: a persisted group id plus a write-through mirror

**Taken, and recorded in the code as well as here** (`include/ClipLinks.h`, the
`ClipLinks` namespace comment): the relation is

* a **group id** — `Clip::linkId()`, an int on the clip, `0` meaning "unlinked",
  written to the clip's OWN element as `link="<n>"` only when the clip is a
  member, and read back with the same **reset-on-absence** rule the take lane
  already follows (`lane`); plus
* a **write-through mirror** — when a member's content changes, the edited
  clip's note list is copied onto every other member that differs, in the same
  step, under one journal checkpoint covering every member written.

It is **not a shared content object**, and it is **not copy-on-write**.

**Why not one aliased `NoteVector` shared by N clips.** Sharing the list through
a pointer would give instant propagation, and it was rejected for four concrete
reasons, in the order they matter here:

1. **A load-order re-linking step.** Aliasing makes the *relation* live in a
   registry that a load must rebuild after every member exists; a file whose
   members are read in one order and re-linked in another is exactly the thing a
   save/reload round trip has to prove. With the group id on each member, every
   clip's element is self-describing: the group is whatever the file says it is,
   and no second pass exists to get wrong.
2. **Blast radius.** The note list is read as a value by `MidiClip::play`, by the
   piano roll (`NoteView` holds `Note*` into it), by the comping lanes
   (`Track::takelanes`, `docs/COMPING.md`), by `NoteTransform`'s quantise and by
   the clip's own serialisation. Aliasing it changes the lifetime rule of every
   one of those at once; a mirror changes none of them.
3. **Content and placement stay separable.** A link shares its notes, name and
   colour. Each member keeps its own start, length, source offset and loop
   window, fades, gain, mute and take lane. That is what makes a link a smart
   clip rather than a single shared arrangement window.
4. **Divergence becomes observable.** With aliasing, disagreement between
   members is impossible by construction — and therefore unreportable when a
   project file, a paste path or a future feature produces one anyway. Here
   `clip.link_get_state` reports each member's content fingerprint verdict
   (`in_sync` / `divergent`) and `clip.link_sync` repairs it.

**Why not copy-on-write.** Copy-on-write means "share until someone edits, then
DETACH" — the opposite of this feature: an edit to one member must be seen by
all. A write here therefore fans OUT; the detach is its own command
(`clip.link_remove`), and it is deliberate every time.

**What a save carries, and why a reload is trivially consistent.** Members are
written in sync (that is what a mirror does), so every member's own element
carries the same note list and the same group id; a reload rebuilds the group
from the members' elements alone, with no extra project state and no repair
step. A file whose members were made to disagree by hand loads as it stands, and
the disagreement is *reported* rather than silently repaired.

---

## 2. What propagates and what does not

| edit | propagates? | why |
|---|---|---|
| `note.add`, `note.remove`, `note.move`, `note.resize`, `note.velocity_set` and other note edits | **yes** | edits to the shared note list are mirrored to every linked MIDI clip |
| `clip.link_sync` | **yes** — that is its job | the explicit repair/propagation verb writes one member's notes to the group |
| `clip.link_create` | **yes**, once | a new member adopts the anchor's notes, name and colour, so the group starts in sync |
| name and colour edits | **yes** | name and colour are shared presentation state |
| `clip.trim`, `clip.resize`, arranger edge drags and `vca.edit_trim` | **no** | only the edited instance's start, length and source window change; other linked instances keep their geometry |
| `clip.move`, `clip.slip`, loop start/length/offset edits | **no** | placement and playback windows describe how that instance plays the shared content |
| `clip.split`, the knife, range/VCA split, loop slicing and overlap trimming | **only the edited instance** | the right piece keeps the original link id, so both pieces show the shared notes through their own windows; other instances remain untouched |
| `clip.link_make_unique` | **no further propagation** | the selected instance keeps its own copy of the current notes, name and colour; its link id is cleared, and a final remaining member's link is dissolved |
| the arranger's "clear notes out of bounds" | **yes**, for notes only | it edits the shared note list; each peer's independent position, length and offset stay unchanged |
| `clip.set_fade`, `clip.set_gain`, `clip.crossfade` | no | the level and fade shape belong to each instance |
| `track.set_mute` / `set_solo`, take lane | no | these remain per-track or per-instance state |

A split is one undo step. The left and right pieces stay in the original link group; they do not form a second group.
The arranger draws a small chain marker on linked clips, and the marker is exposed in the clip's accessible name.
The MIDI clip context menu offers **Make Unique**, and `clip.link_make_unique` exposes the same undoable action on
the control surface.

A hand-edited project file or an edit path outside the shared note entry points can leave divergent note lists.
`clip.link_get_state` reports the divergence and `clip.link_sync` repairs it.

The linked-clips entries in `docs/KNOWN-LIMITATIONS.md` and
`docs/RELEASE-NOTES-v0.3.0-alpha.md` describe the visible marker, Make Unique
action and per-instance geometry rule.

**Audio clips cannot be members in 0.3.0.** A link group's content channel is a
note list, so `clip.link_create` refuses a `SampleClip`, typed
(`invalid_args`, "…a link group shares a note list, and this clip has none").
An audio link would need a shared *sample window* rather than a note list, and
that is a different feature; refusing is the honest answer, not a silent
one-member group.

---

## 3. The commands

| id | args | result | class (SPEC A16) |
|---|---|---|---|
| `clip.link_create` | `clip` (string, required), `clips` (array of clip ids, required, non-empty) | `group`, `members`, `size`, `content`, `notes`, `adopted`, `mirrored` | `true_inverse`, reversible |
| `clip.link_remove` | `clip` (string, required) | `removed`, `group`, `remaining`, `dissolved` | `true_inverse`, reversible |
| `clip.link_make_unique` | `clip` (string, required) | `removed`, `group`, `remaining`, `dissolved`, `made_unique` | `true_inverse`, reversible |
| `clip.link_get_state` | `clip` (string, optional) | `groups[]` (`group`, `members`, `size`, `content`, `content_members`, `notes`, `reference`, `in_sync[]`, `divergent[]`), `count`, and with `clip`: `linked` | `not_mutating` |
| `clip.link_sync` | `clip` (string, required) | `clip`, `group`, `members`, `divergent_before`, `written_count`, `content` | `true_inverse`, reversible |

Behaviour worth stating because it is not inferable from the names:

* **A link needs two ends.** `clip.link_remove` or `clip.link_make_unique` that leaves ONE member dissolves
  the relation entirely (that member loses its tag too), so no group of one
  outlives its last pair. `clip.link_get_state` on that member reports
  `linked: false`.
* **Groups do not merge.** Naming a clip that is already a member of a
  *different* group is a typed `refused` that tells the caller to
  `clip.link_remove` it first: silently merging two groups would overwrite one
  group's content with the other's.
* **A copy of a member is a member.** `Clip`'s copy constructor carries the link
  id and the `link` attribute rides `saveClipEdits`, so a split, a duplicate or a
  paste of a linked clip yields another member of the same group (with its own
  placement). `clip.link_make_unique` is the explicit detach action.
* **`link.*` is a different feature.** `docs/LINK-SYNC.md`'s `link.*` group is
  Ableton-Link-style session tempo/beat sync; this group's ids keep the `clip.`
  prefix for exactly that reason.

---

## 4. Reversibility (SPEC A16)

Rows live in `src/core/ControlReversibilityTableVerbs.cpp` beside `clip.trim`'s
and `clip.slip`'s. Link creation, removal, Make Unique, and content sync use live
`ProjectJournal` checkpoints over the clip state they change. The facts that
make those rows true inverses are:

1. `link` is written **only** when the clip is a member and `loadClipEdits`
   **resets to 0 when the attribute is absent** — so the checkpoint taken before
   a FIRST link restores "unlinked" exactly (the same reset-on-absence rule that
   makes `clip.trim`'s first edit reversible, and the opposite of the authored
   `srcin`/`srcout` window pair).
2. The checkpoint is taken over **every member** the command writes
   (`ProjectJournal::addJournalCheckPoint(QVector<JournallingObject*>)`), so
   the state restored is the whole relation, not one end of it.
3. The registry's `mergeCheckpointsFrom()` folds every checkpoint a handler
   pushed into **one undo step**, so ONE `control.undo` (or one Ctrl+Z) takes the
   whole group back — asserted by
   `ClipLinkTest::undoRestoresEveryMemberOfTheGroup()`.

---

## 5. Proof, and how to re-run it

The registered ctests, run from `<build>/tests` (LMMS rule 7):

```bash
cd <build>/tests && ctest -R 'ClipLinkTest|LinkedClipGroupEditsTest|MidiClipLinkedEditTest' --output-on-failure > log 2>&1; echo EXIT=$?
```

`ClipLinkTest` drives the command surface through `ControlRegistry::invoke` and
covers registration, shared state, Make Unique, undo, save/reload and typed
refusals. `LinkedClipGroupEditsTest` and `MidiClipLinkedEditTest` cover the
independent geometry rule in command and GUI paths. The former apply-to-all
trim/split assertions were changed to assert the owner's 2026-10-09 reversal;
they were not weakened.

---

## 6. Bounds

1. **Audio clips cannot be linked** (§2).
2. **The mirror writes the whole note list**, not a delta. A one-note edit to a
   linked clip with a large shared list rewrites every member's list; the work is
   bounded by the group's size and the note count.
3. **Geometry stays independent during content writes.** A note edit never
   changes a peer's start, length, offset, loop window or auto-resize setting.
4. **One edit is one undo step.** Control commands, arranger edits and note
   gestures checkpoint the source and every member written as one step.
