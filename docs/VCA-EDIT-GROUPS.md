# VCA / mix-and-edit groups: the control surface, and the phase-locked edit half

Lane `030/vca-editgroups`, branch `030/vca-editgroups`, based on `3956ef589` (the
`release/0.3.0` tip at the time of writing). Owner-31 item 11, "phase-locked
multitrack edit groups"; the release line's own row for it is
`docs/FEATURE-LIST-0.3.0.md` row 4, and the ladder's row is
`V0.3-V0.5-RELEASE-LADDER.md:56` ("group entity landed (`VcaGroup`); edit-group
half to build | no architectural gate").

This is the lane's report: what the feature is, what was added to the engine,
what the fourteen commands are, why each A16 row is classified the way it is,
what is proved and how, and what is deliberately **not** in it. It complements
`docs/VCA-GROUPS.md`, which is the earlier lane's report for the mix group
(task #622) - that document's semantics (M1, M2, S1-S3 and the relative-scaling
arithmetic) are unchanged by this lane and are not restated here.

## 1. What was already there, and what the row owed

`docs/FEATURE-LIST-0.3.0.md` row 4 states the starting point exactly: "the group
*entity* landed (`include/VcaGroup.h`, `Mixer::createVcaGroup`, `VcaGroupTest`)
but there is no `vca.*` group to drive it and the edit-group half is to build; a
group can only be created by editing the project file". Three things were
therefore owed:

1. **a way to reach the entity at all** - the entity is a `QObject` owned by the
   `Mixer` and persisted as `<vcagroup>` inside `<mixer>`; nothing created,
   addressed, filled or configured one except a text editor;
2. **the edit half** - `BACKLOG.md` item 11's "smallest honest version": "an
   **edit** group distinct from the landed mix group, membership by track, where
   a media edit on one member applies to all members at the same source
   position";
3. **the absence lines**, because 0.3.0 has no VCA interface at all and the
   previous sentence ("a group can only be created by editing the project file")
   stops being true the moment the socket can create one.

## 2. The engine half: the edit set and the phase lock

Both live on the existing entity - one `VcaGroup` is a mix group (one fader over
member mixer channels) and, when it carries tracks, an edit group too. That is
the row's own framing: the ladder and the backlog treat "VCAs / mix & edit
groups" as ONE row and item 11 extends it rather than adding a second entity.

* `VcaGroup::editTracks()` / `hasEditTrack` / `addEditTrack` / `removeEditTrack`
  - a set of **stable track ids** (`Track::id()`, the number the track's own
  element carries as `id` and the number `trk-<n>` resolves), kept ascending.
* `VcaGroup::isPhaseLocked()` / `setPhaseLocked` - the group's own switch,
  **on by default** because the lock is what the feature is named for.

### 2.1 Why ids and not pointers

`Song::loadState` restores the **mixer** before the track container ("Load mixer
first to be able to set the correct range for mixer channels",
`src/core/Song.cpp`). At the moment a `<vcagroup>` element is read, the tracks
it names do not exist yet, so an id is the only thing that can be recorded. The
consequence is stated rather than hidden: an id whose track has since been
deleted **stays in the set**, `vca.get_state` reports it in `missing_tracks` and
`vca.edit_move` in `skipped_tracks`, and it leaves the set when somebody says so
with `vca.track_remove`. A group that dropped an id it could not resolve would
be rewriting the caller's own membership behind their back, which is the harder
bug to find.

### 2.2 Persistence

`Mixer::saveSettings` now writes, per group, a `locked` attribute and one
`<edittrack track="N"/>` child per set member (beside the `vca`/`muted`/`soloed`
attributes and the `<member channel="N"/>` children that were already there).
`Mixer::loadSettings` reads `locked` with a default of `1`, so a `<vcagroup>`
written before this lane loads as the locked group it was, and reads each
`<edittrack>` by id, resolving nothing (see §2.1). A pre-0.3.0 LMMS skips both
exactly as it skips the `<vcagroup>` itself, so the grouping still degrades to
"no groups".

## 3. The command group: fourteen ids

Registered by `registerVcaCommands` (`src/core/ControlRegistry.cpp`), which is
the group's only registration point; the halves are separate translation units
for the 500-line file ratchet, the split the folder-track, session, warp, rack,
comp and automation groups follow (this group needed FOUR: the phase-locked move
and the edit set it acts on were one file until it reached 521 lines).

| id | file | what it does |
|---|---|---|
| `vca.create` | `ControlCommandsVca.cpp` | create a group; returns its `vca-<n>` id |
| `vca.remove` | `ControlCommandsVca.cpp` | delete a group (exactly reversible, §5) |
| `vca.list` | `ControlCommandsVca.cpp` | every group with its whole state (read) |
| `vca.get_state` | `ControlCommandsVca.cpp` | one group's whole state (read) |
| `vca.rename` | `ControlCommandsVca.cpp` | rename a group |
| `vca.set_gain` | `ControlCommandsVcaMix.cpp` | the group fader (0..2, unity 1.0) |
| `vca.set_mute` | `ControlCommandsVcaMix.cpp` | mute = a published gain of zero |
| `vca.set_solo` | `ControlCommandsVcaMix.cpp` | the product's exclusive solo over the members |
| `vca.assign` | `ControlCommandsVcaMix.cpp` | put a mixer channel in the group |
| `vca.unassign` | `ControlCommandsVcaMix.cpp` | take a mixer channel out |
| `vca.set_phase_lock` | `ControlCommandsVcaEditSet.cpp` | switch the edit lock |
| `vca.track_add` | `ControlCommandsVcaEditSet.cpp` | add a track to the edit set |
| `vca.track_remove` | `ControlCommandsVcaEditSet.cpp` | remove a track from the edit set |
| `vca.edit_move` | `ControlCommandsVcaEdit.cpp` | THE feature: the phase-locked move |

### 3.1 The naming decision, and why

* **The group is `vca.*`, not `editgroup.*` or `mixgroup.*`.** One entity carries
  both halves (the row is "VCAs / mix & edit groups"), the task the entity came
  from is called `VcaGroup`, and the A16 table and the honesty gates compare ids
  with `docs/FEATURE-LIST-0.3.0.md`'s "Command group / ids" column, which names
  the group `vca.*`. A second prefix would make the same object reachable under
  two names and split the contract rows across them.
* **Verbs are the object's own**, not a codec: `create`/`remove` rather than
  `add`/`delete`, because every other container in this surface
  (`mixer.add_channel`, `track.add`) adds a CHILD to something that already
  exists, while a group is a top-level object of the mix - the same distinction
  `comp.lane_add` and `rack.add_chain` make for nested things.
* **`set_gain`, not `set_volume`.** `mixer.set_volume` is a channel's own fader;
  a group's fader is published to members as a separate relative factor, and the
  two numbers mean different things. The result reports both `volume` (the
  fader) and `gain` (what the members are actually scaled by, which is 0 while
  the group is muted), so a caller never has to guess which one it read.
* **`assign`/`unassign` for the mix membership, `track_add`/`track_remove` for
  the edit set.** The two memberships live on different objects and are refused
  for different reasons (a channel can be in at most one group; a track can be
  in as many edit sets as there are groups), and the verb forms make which one a
  call touches unambiguous. `vca.assign` is deliberately NOT spelled
  `vca.member_add`: a member of a VCA is a channel, and the row's own vocabulary
  for the edit half is "membership by track".
* **The id form is `vca-<n>`**, where n is the group's own id
  (`VcaGroup::id()`, preserved across save/load). That is the grammar every
  other addressable object here uses (`ch-<n>`, `trk-<n>`, `clip-<n>`,
  `dev-<n>`) and it is chosen for the same reason: the id a caller holds stays
  valid when a sibling is created or removed, so an agent never re-derives an
  address from a position. The formatter lives in
  `src/core/ControlCommandsVcaShared.h` because this group is its only producer
  and its only consumer.

### 3.2 The correspondence rule (the design decision in the feature)

`vca.edit_move` takes a clip and a position. The clip is the **anchor** and goes
to exactly the position asked for; every other member of the group's edit set
contributes **the clips that overlap the anchor's pre-command span**, each moved
by the SAME DELTA. Then all of them - anchor included - are one undo step.

* **A delta, not an absolute.** Moving every member onto the anchor's new
  position would destroy a deliberate offset between two members (a room mic a
  few ticks late, a doubled take). Applying the delta keeps every offset
  identical, which is what makes it a *lock* and not a snap-to-grid.
* **Overlap, not equality of position.** A take that is 3 ticks out still needs
  to move with the others; requiring an exact start match would silently leave
  it behind. The anchor's span is at least one tick wide, so a zero-length clip
  is still a correspondence.
* **A member with nothing in the span is reported, not an error.** It is normal
  for one input of a multitrack take to be empty; it comes back in
  `unlocked_tracks`. A member whose track is gone comes back in
  `skipped_tracks`. Neither is silently counted as a success.
* **Refusals happen before anything is written**: the lock is off, the anchor's
  track is not in the edit set, or the set has fewer than two live tracks - each
  is a typed `refused` that names the command that fixes it
  (`vca.set_phase_lock`, `vca.track_add`).

## 4. Honest limits

1. **One media edit is propagated: a clip move.** Trim, slip, split and fades on
   a locked group are not propagated. The entity carries the membership and the
   lock, and the rule (§3.2) is implemented for the edit a multitrack take needs
   first - sliding a whole take against the rest of the song. Saying "edit
   groups" without this sentence would be claiming far more than is built.
2. **A deleted track stays in the edit set** until `vca.track_remove`, and is
   reported as `missing_tracks` / `skipped_tracks` (§2.1). This is a decision
   about not rewriting a caller's membership, not an omission.
3. **A clip id is index-derived.** `clip-<n>` is the clip's ordinal in
   arrangement order and a move re-sorts a track's clips, so `vca.edit_move`'s
   inverse is the **checkpoint**, not a replayed `clip-<n>` id. The recorded
   `inverseArgs` address the anchor by the id the caller used and are reported
   for audit only.
4. **`vca.set_solo`'s undo does not restore `MixerChannel::m_muteBeforeSolo`**,
   which is transient and is not part of the project file. It is re-derived on
   the next solo action - the same limit `track.set_solo` states in its own
   transaction text.
5. **No interface, and no Lua binding.** There is no VCA strip, no group menu,
   no member list and no phase-lock toggle in `src/gui/`, and the socket (plus
   the MCP bridge over it) is the only way to reach any of this. That is the
   `docs/KNOWN-LIMITATIONS.md` line.
6. **The edit half is not audibly proved by this lane.** The mix half's
   audibility was measured before it (`VcaGroupTest` renders and measures a dB
   delta, bit-exactly reversible). This lane's end-to-end proof drives the model
   through the socket and reads state back; it does not re-render.

## 5. Reversibility (SPEC A16)

The full argument is in the twelve rows of
`src/core/ControlReversibilityTableVca.cpp` and the two `not_mutating` rows in
`ControlReversibilityTablePassive.cpp`; the summary is that **no command here
can lean on a checkpoint of the group itself**, because a `VcaGroup` is a
`QObject` owned by the `Mixer` rather than a `JournallingObject` with an id on
the journal's object map (its `<vcagroup>` element belongs to the MIXER's state,
and a Mixer checkpoint would destroy and recreate every channel). Every row
instead names one of two things:

* a **live checkpoint on a model** - the group's fader, its mute, the composite
  solo step (the group's flag, every other group's flag and every channel's mute
  as ONE undo step, the shape `track.set_solo` uses), and - for
  `vca.edit_move` - one live Clip checkpoint per moved clip, merged into one
  step by `ControlRegistry::runHandler` -> `ProjectJournal::mergeCheckpointsFrom`
  so one `control.undo` returns every member; or
* a **recorded ACTION step** (`control::addUndoStep`) for state that is not a
  model at all: a name, a membership list, a lock flag, a group's existence.
  `vca.remove` is worth singling out: the delete is EXACTLY reconstructible
  (id, name, fader, mute, solo, lock flag, member channels, edit tracks), unlike
  `mixer.remove_channel`, because a group holds scalars, flags and two id lists
  and nothing else in the mix refers to it. Both `vca.create` and `vca.remove`
  resolve the group **by id** at undo time and never capture a pointer: a later
  `vca.remove` (undone first, in LIFO order) destroys the object and the restore
  creates a different one with the same id.

## 6. Proof

* **`tests/src/core/ControlVcaCommandsTest.cpp`** (registered QTest
  `ControlVcaCommandsTest`): the fourteen ids and their schemas; the group's
  contract rows and their classes; junk arguments as typed refusals with nothing
  created; the fader's measured effect (members scaled, and no member's own
  fader written); membership single-valued (a second group, master and a
  non-member all refused); `vca.remove` restored field by field by one undo; the
  phase-locked move across two tracks with ONE undo returning both; a member
  outside the span reported unlocked and left untouched; and the three lock
  refusals. It also exercises the entity half directly on a scratch `Mixer`:
  the edit set's ordering and refusal rules, the `locked` attribute and the
  `<edittrack>` children in the saved XML, the round trip through
  `Mixer::saveSettings`/`loadSettings`, and a legacy `<vcagroup>` with no
  `locked` attribute loading as locked with no edit set.
* **`tests/control-vca-commands.py`** (registered ctest `ControlVcaCommands`):
  the same claims end to end against the real `zene` binary over
  `--control-socket`, including the save/open round trip, which is what proves
  the edit set can only have come from the file (the in-memory model is moved
  elsewhere first).
* **`tests/src/core/VcaGroupTest.cpp` is deliberately NOT extended by this
  lane.** It is grandfathered in `tests/file-length-baseline-all.tsv` at 1022
  lines and `tests/file-length-gate.sh` runs with `FILE_LINE_TOLERANCE=0`, so a
  single added line regresses Gate 7 in the whole-tree scope - a gate this lane
  may not weaken. The entity-half coverage that was to land there landed in this
  lane's own registered test instead (the scratch-Mixer slot above), which is
  measured in the fork scope and is well under the 500-line limit.
