# Linked / smart clips (`clip.link_*`) — the relation, the decision, and what propagates

Zene Studio 0.3.0-alpha, feature-list row 6 (section 1), board task #645. Engine
half: `include/ClipLinks.h` (the relation: the group id on the clip, the group,
the mirror) and `src/core/ClipLinks.cpp` (its implementation). Clip-side state:
`Clip::linkId()` / `Clip::setLinkId()`, persisted by `Clip::saveClipEdits` /
`Clip::loadClipEdits` as the `link` attribute of the clip's own element. Surface:
`src/core/ControlCommandsClipLink.cpp` (`clip.link_create`, `clip.link_remove`,
`clip.link_get_state`, `clip.link_sync`).

**One-line summary.** Two clips share one source — the clip's content, its note
list — so an edit to one is seen by all of them; the relation is written to each
member's own element, so it survives a save/reload; and unlinking is an
operation, not a side effect of an edit.

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
3. **Content and placement stay separable.** What a link shares is the
   *content*; where each member plays it — position, length, source offset,
   fades, gain, mute, name, colour, take lane — stays per-member. That is what
   makes a link a smart clip and not a rename, and it is only expressible if the
   members keep their own objects.
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
| `note.add`, `note.remove` | **yes** | they reach `MidiClip::addNote` / `removeNote`, which mirror |
| `note.move`, `note.resize`, `note.velocity_set` | **yes** | the verbs mirror after the in-place edit (the same call the piano roll's own drag points do not make) |
| `clip.link_sync` | **yes** — that is its job | the explicit repair/propagation verb: one member's content is written onto the group |
| `clip.link_create` | **yes**, once | a new member ADOPTS the anchor's content, so a fresh group starts in sync |
| `clip.move`, `clip.resize`, `clip.trim`, `clip.slip` | no | position, length and source offset describe where a member plays the shared content |
| `clip.set_fade`, `clip.set_gain`, `clip.crossfade` | no | the level/fade shape is a member's own |
| `track.set_mute` / `set_solo`, a member's name or colour, its take lane | no | per-member presentation |
| a piano-roll gesture that does not pass through the note entry points (an in-place drag that only re-sorts), a hand-edited project file, a Lua edit of the note list | no | `clip.link_get_state` reports the drift and `clip.link_sync` repairs it |

The one line that ships in the two release documents is
`docs/KNOWN-LIMITATIONS.md`'s and `docs/RELEASE-NOTES-v0.3.0-alpha.md`'s
"linked clips" entry, and `ClipLinkTest::theOneLineUiAbsenceIsWrittenDown()`
reads both files from the built tree and fails if either loses the claim.

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
| `clip.link_get_state` | `clip` (string, optional) | `groups[]` (`group`, `members`, `size`, `content`, `content_members`, `notes`, `reference`, `in_sync[]`, `divergent[]`), `count`, and with `clip`: `linked` | `not_mutating` |
| `clip.link_sync` | `clip` (string, required) | `clip`, `group`, `members`, `divergent_before`, `written_count`, `content` | `true_inverse`, reversible |

Behaviour worth stating because it is not inferable from the names:

* **A link needs two ends.** `clip.link_remove` that leaves ONE member dissolves
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
  placement), and `clip.link_remove` is the "make unique" operation.
* **`link.*` is a different feature.** `docs/LINK-SYNC.md`'s `link.*` group is
  Ableton-Link-style session tempo/beat sync; this group's ids keep the `clip.`
  prefix for exactly that reason.

---

## 4. Reversibility (SPEC A16)

Rows live in `src/core/ControlReversibilityTableVerbs.cpp` beside `clip.trim`'s
and `clip.slip`'s, for the same reason those two are `true_inverse` rows: what a
link changes is the clip's own serialized state. The mechanism is the engine's
live `ProjectJournal` checkpoint, and the three facts that make it a real inverse
are:

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

One registered ctest, run from `<build>/tests` (LMMS rule 7):

```bash
cd <build>/tests && ctest -R ClipLinkTest --output-on-failure > log 2>&1; echo EXIT=$?
```

It drives the whole feature through `ControlRegistry::invoke` — the release's
own door — and covers, in this order: the four ids with both schemas
(`requiredCommandsAreRegistered`); the relation and the propagation of every
content verb in both directions, plus the placement edits that must NOT
propagate (`anEditToOneMemberIsSeenByAll`); the unlink, its dissolve rule, the
detach being real and the relink (`unlinkDetachesAndKeepsItsContent`); **the save
/ reload round trip** — the `link` attribute in the saved file, the group still
there after `loadProject`, the reloaded group still propagating, and a second
save writing the same relation (`theLinkSurvivesSaveAndReload`); the typed
refusals (`refusalsAreTypedAndNothingIsHalfWritten`); one undo restoring every
member (`undoRestoresEveryMemberOfTheGroup`); the A16 rows
(`theA16ContractHasARowForEachId`); and the two one-line UI-absence notes
(`theOneLineUiAbsenceIsWrittenDown`).

---

## 6. What is NOT here

1. **No UI.** There is no link badge, no "Edit shared source" gesture and no
   linked-clip colour in this release; the relation is created, read, repaired
   and broken through the socket only (the one line in
   `docs/KNOWN-LIMITATIONS.md`).
2. **Audio clips cannot be linked** (§2).
3. **The mirror writes the whole note list**, not a delta. A one-note edit to a
   linked clip with a large shared list rewrites every member's list; the cost
   is bounded by the group's size, not by the edit's. Unconditional-by-design,
   because a delta-based mirror is where divergence would come from.
4. **A member with auto-resize on re-sizes to the propagated content**, because
   that is the engine's own rule for a clip whose notes changed
   (`MidiClip::updateLength()`); a member that was manually resized
   (`autoresize=0`) keeps its length.
5. **Undo granularity outside the command path.** Inside the command path every
   member written is one step (point 3 of §4). A GUI gesture that calls the note
   entry points directly — drawing a note in the piano roll of a linked clip —
   takes its own checkpoint per object the journal sees, so a human's Ctrl+Z
   there may need more than one press. That is the pre-existing behaviour of a
   gesture that is not one command, not a change this feature made.
