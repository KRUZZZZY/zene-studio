# Comping: take lanes and the non-destructive composite (task #600)

The engine decision record for the `comp.*` command group. Read this before changing anything
about take lanes: three of the four decisions below are the difference between a comp that can be
changed later and a comp that has already destroyed a take.

Implementing lane: branch `030/w14-comping`, 2026-09-13. The design this follows is
`docs/CLIP-CAPTURE-DESIGN.md` §1.8 (the measured absence), §2.2 (the `Lane` and `CompClip` shapes),
§2.3 (the invariants) and §3 rows 6-7. Where this lane diverged from it, §5 says so and why.

---

## 1. What a take lane IS here

A lane is a **child relationship of the track, not a second track type**, exactly as the design
decides (§2.2). Concretely:

- the takes stay in the track's own clip list; a take is just a clip that carries a **lane tag**,
  `Clip::laneIndex()` (default 0), serialised as the `lane` attribute on the clip's own element
  (design §2.6);
- the lane itself holds only `{id, index, name}` — no audio of its own, and **no mute flag**: per-lane
  audibility is the take clip's own `muted`, which already exists, is already serialised and is
  already what the engine reads. A second mute flag on the lane would be state nothing renders;
  **since ARCH-4 S7 the lane is a first-class ENTITY**: its `id` comes from the project's `lane-`
  id family the moment the lane is added (`ProjectIds`, SPEC-ARCH-4 1.5 R4 — the family S3
  declared and named "entity lands with S7"), and the document carries it in the top-level
  `<z:lanes>` section (§3 below). The `index` stays what it always was: the per-track slot the
  take clips and composite segments reference, stable across save/load (I7);
- **lane indices are stable** (design I7): a removal never renumbers the survivors, and the next
  lane added takes the lowest index the track is not using. `removeLane` re-points every
  composite segment that named the removed lane to the track's **base lane** (the lowest index it
  still has) rather than leaving a hole, because a composite is total over its span (§2 below);
  removing the last lane clears the composite entirely.

`comp.assign` is the one way a clip becomes a take. It refuses a **MIDI** clip with a typed error:
this release's take lanes carry audio takes (the design's own scope line, §3: the container is
type-agnostic, the comp path built here is the audio one), and the lane tag rides
`Clip::saveClipEdits`, which only `SampleClip` calls today.

## 2. What the COMPOSITE is, and what makes it non-destructive

**A composite is a view: an ordered, gapless list of `{begin, end, lane, srcpos}` choices over the
timeline, resolved back onto the take clips the track already holds.** It is not a merged buffer,
not a copy, not a new file. The model stores integers (`include/TakeLane.h`); nothing in it can
open, write, normalise or move a sample.

Three decisions say exactly what that means:

1. **The composite is total over its span and gapless inside it** (design I6). A selection paints:
   `comp.select [begin,end) lane L` cuts that range out of every existing segment (splitting a
   segment that straddles it, keeping its `srcpos` arithmetic consistent) and inserts the new
   choice. There is deliberately **no "un-select"**: an unselected tick inside the span is the base
   lane's, which is the region a comp falls back to when no take was chosen.
2. **Resolution is a pure function of the composite.** `TakeLaneModel::resolve(tick)` answers
   `(lane, srcpos)` for a tick, or nothing at all outside the span — never a default lane.
   `takeAt()` / `resolveSource()` then map that tick onto the take clip of that lane **through
   `Clip::sourceFrameAt()`**, the mapping seam the clip wave froze (#611, design §2.4). So the
   frame the comp reads comes from the take's own mapping; a lane with no take covering the tick is
   reported **`unresolved`** rather than guessed at.
3. **`srcpos` is recorded and reported, not yet applied.** It is the intended slip into the lane's
   take (0 = that take's own start, which is what a plain "use this lane here" selection means).
   No playback path consumes the composite in this release, so nothing can apply it yet — see §4.

**The non-destructive property, stated as the thing the test measures:** after every `comp.*`
command, after a save and after a load, the takes' **files** and the takes' **in-memory buffers**
are byte-identical (sha256), while what a tick resolves to changes when the selection changes.
`tests/src/core/TakeLaneTest.cpp` asserts exactly that pair; a comp that copied or rewrote a
take would pass every field-level assertion in that file and fail those two.

## 3. The project-file shape (and the one trap in it)

**ARCH-4 S7 (2026-09-22): lane state moved out of `<track>` into the top-level `<z:lanes>`
section** — the migration row's `<takelanes>` children + clip `lane` attribute → `<z:lane id>` on
the track's lane list, clips referencing it (SPEC-ARCH-4 2; census rows 3/4). There are now two
shapes, one per context, and the difference is the compatibility rule: **in a project file, new
state goes only in top-level namespaced sections — never as a child of `<track>`, never as a new
`<track>` attribute** — because an older build's `Track::loadTrack` coerces an unrecognised
`<track>` child into a real Clip (SPEC-ARCH-4 5.2 risk 1).

The **document** form, written by `Song::saveProjectFile` only when some track's model is not
empty (design I9 at document scope: a project that never comped serialises byte for byte as it
always did — section, `z:` binding and all):

```xml
<zene-project ... xmlns:z="urn:zene:core:1" next-id="418">
  <head .../>
  <song>
    <trackcontainer>
      <track type="2" name="gtr" id="3">
        <sampleclip pos="0" len="384" src="take1.wav" lane="1"/>  <!-- the take tag, unchanged -->
      </track>
    </trackcontainer>
    <z:lanes v="1">
      <z:tracklanes track="3">                        <!-- the track's lane list, by track id -->
        <z:lane id="12" index="0" name="take 1"/>     <!-- the lane ENTITY (lane- id family) -->
        <z:lane id="13" index="1"/>
        <z:segment begin="0"   end="192" lane="1" srcpos="0"/>  <!-- srcpos only when non-zero -->
        <z:segment begin="192" end="384" lane="0"/>
      </z:tracklanes>
    </z:lanes>
  </song>
</zene-project>
```

The **payload** form: a journal checkpoint and a copy payload (clone, drag) are not project files
— `ProjectJournal` serialises into a `JournalData` document and neither ever reaches
`DataFile::write` — and every `comp.*` undo step restores *through* `Track::loadTrack`, so outside
a project save the lanes are still written as the track's `<takelanes>` child (with the same
`z:lane`/`z:segment` children inside), now **unmarked**. `Track::saveTrack` chooses between the
two on `Song::isSavingProject()`.

- **The `metadata="1"` marker is gone** (migration row: *"`metadata="1"` markers dropped — the
  coercion they guard no longer exists"*). It had two jobs. `DataFile::write`'s `cleanMetaNodes()`
  **deleted** every marked element, so the marked `<takelanes>` never reached a disk at all
  (measured pre-S7: lane names and the composite were lost by every `project.save`; only the
  clips' `lane` attributes survived), and it guarded pre-#600 readers against a child they had no
  branch for. S1b's claim-or-preserve split closes the coercion for unknown names, and the
  document form no longer lives under `<track>` at all, so neither job remains.
- **The clip reference does not move.** The `lane` attribute on the clip keeps its name and its
  per-track index meaning (migration's "clips referencing it": the index now resolves against
  first-class `<z:lane id>` entities instead of an anonymous slot). Changing the value to a global
  id would make an older build read `lane="13"` as *index 13*, and would break byte identity for
  every file that carries the attribute — the S7 row owes exactly that identity, so the vocabulary
  stays.
- **The selection is the composite.** There is one persisted list, not a selection plus a derived
  composite: `comp.select` writes the composite directly and `comp.rebuild` normalises/extends it
  (§4). Persisting both would create a second source of truth that can disagree with the first.
- **Reset on absence, both places — unchanged, riding the s4 schemas.** `Track::loadTrack` clears
  the model before reading (a track element with no `<takelanes>` leaves an empty model;
  `TakeLaneModel::loadSettings` clears first too, so a document whose `<z:lanes>` section omits a
  track leaves that track empty), and `Clip::loadClipEdits` resets the lane tag to 0 when the
  attribute is absent (`CLIP_RESTORE_SCHEMA`, `"lane"` → `"0"`). A journal checkpoint restores by
  re-loading, so state that survived its own absence could never be undone — the trap the
  `zene-control-command-group` skill records, and the test covers both halves.
- **Lane ids follow the clip-id rule** (SPEC-stable-ids R4/R3): allocated when the lane is added,
  taken from the file's `id` on load, or counted as a load assignment when absent; the file's
  `next-id` is floored by what the section carries.
- **No `UPGRADE_METHODS` entry** is needed: the loader is read-both (the `<takelanes>` payload
  branch stays, `<z:lanes>` is claimed in `Song::restoreNamedSection` only in the exact shape this
  writer produces — anything else is preserved verbatim as unclaimed, S1's rule), and no existing
  attribute changes meaning. An older build reading a new file ignores `<z:lanes>` — it cannot
  keep the lanes across its own re-save (the spec's stated impossibility for newer content), but
  it grows no phantom clip, which is what risk 1 asks of this change; the negative control
  (`LanesUpconvertTest::newModelStateIsNeverWrittenUnderATrack`, run RED with state planted under
  `<track>`, see `docs/s7-logs/negative-control.md`) is the executable form of that claim.

## 4. The command group: `comp.*`

**Naming, decided.** A NEW group `comp`, not `clip.*`. Take lanes and the composite are
**track**-scoped (a lane group belongs to a track; the composite spans several clips on several
lanes), while `clip.*` is clip-scoped — and `clip.*` already has three translation units. The
design's own command sketch (`AGENT-TOOLING.md`: `take.lane_create`, `comp.region_select`) splits
the same feature across two prefixes; this lane keeps **one** prefix so the whole feature is
addressable from `comp.` and every id has exactly one A16 row to review. The divergence is that
`take.lane_create` is `comp.lane_add`.

| id | args | class | what it does |
|---|---|---|---|
| `comp.lane_add` | `track`, `name`? | `true_inverse` (Track checkpoint) | adds a lane, returns its index; the recorded inverse op is the real command `comp.lane_remove` |
| `comp.lane_remove` | `track`, `lane` | `true_inverse` (Track checkpoint) | removes a lane; segments that named it fall back to the base lane; the last lane clears the composite |
| `comp.lane_list` | `track`? | `not_mutating` | every track's lanes (or one track's) with the clips assigned to each |
| `comp.assign` | `clip`, `lane` | `true_inverse` (Clip checkpoint) | tags an audio clip as a take of that lane; the recorded inverse op is the real command `comp.assign` with the previous lane |
| `comp.select` | `track`, `begin`, `end`, `lane`, `srcpos`? | `true_inverse` (Track checkpoint) | per-segment selection: paints `[begin,end)` with that lane |
| `comp.rebuild` | `track`, `begin`?, `end`? | `true_inverse` (Track checkpoint) | sorts, merges continuous same-lane neighbours, and — given a span — clamps the composite to exactly `[begin,end)` and fills every gap with the base lane |
| `comp.get_state` | `track` | `not_mutating` | lanes + composite + what each segment resolves to (take clip and source frame, `bound` or `unresolved`) |

Refusals are typed and happen **before** the journal checkpoint, so a refused call writes nothing
and leaves no undo step behind: an empty or reversed range (`invalid_args`), a lane the track does
not have (`not_found`, naming the lanes it does have), a MIDI clip (`refused`), a half-given span
(`invalid_args`).

## 5. What is NOT built here (read this before assuming)

- **Nothing renders a composite.** No playback path, no `SampleTrack::play` branch, no play handle
  reads the composite: a comp sounds exactly like the track's clips as they lie. `resolve()` and
  `resolveSource()` are the engine's answer to "which take supplies this tick", and they are what a
  future render path consumes — this lane stops at the model, the surface and their proof.
- **No interface at all.** No lane geometry, no lane header, no lane handle, no comping gesture,
  and no waveform drawing of the composite: `src/gui/` is untouched, and the release notes and
  `docs/KNOWN-LIMITATIONS.md` carry the one-line absence.
- **No MIDI comping** (design §3: note-level merging across takes is a different edit), **no
  audition** (`comp.audition` of the design's sketch) and **no flatten** (the destructive bounce a
  comp can end in; deliberately absent while nothing renders).
- **The `srcpos` slip is not applied** by anything (§2.3).
- **Divergence from the design's `CompClip`**: the design sketches the composite as a new clip
  subclass (`compclip`) with child `<region>` elements, loaded through the clip path. This lane
  puts the composite on the **track** instead, because `Track::loadTrack` dispatches a clip element
  by the *track's* default clip type rather than by the element's node name — a new clip node name
  would need that dispatch changed and a `createView()` for a type the GUI never draws. The design's
  own acceptance test ("4 takes → comp of 2 regions → reload → identical composite; the source
  takes' file hashes unchanged") is exactly what the two comping test files prove
  (`tests/src/core/TakeLaneTest.cpp` for the take hashes and the element round trip,
  `tests/src/core/TakeLaneCompTest.cpp` for the composite itself),
  which is the reason the divergence is acceptable rather than a shortcut.

## 6. The proof

`tests/src/core/TakeLaneTest.cpp` (registered ctest `TakeLaneTest`; the take-lane half - the lane
list, the takes' audio and the project-file shape - proves claims 1, 6, 7 and 8) and
`tests/src/core/TakeLaneCompTest.cpp` (registered ctest `TakeLaneCompTest`; the composite half -
the selection model, the `comp.*` surface and its A16 rows - proves claims 2, 3, 4, 5, 9 and 10),
both offscreen Qt and both built on the shared fixtures in `tests/src/core/TakeLaneTestSupport.h`:
plus, since ARCH-4 S7, `tests/src/core/LanesUpconvertTest.cpp` (registered ctest
`LanesUpconvertTest`; the document layer - claim 11 below):

1. lane indices are stable and the lowest free index is reused;
2. an invalid selection is refused and writes nothing;
3. a selection splits what it overwrites, and the composite stays ordered and gapless;
4. removing a lane re-points its segments to the base lane; removing the last lane clears the comp;
5. resolution follows the selection, and a tick outside the span has no answer;
6. **the non-destructive property**: the resolved take clip and frame follow the selection, and the
   take files and buffers are byte-identical (sha256) after select/rebuild/save/load;
7. the `<takelanes>` element round-trips, and an element without it `loadTrack`s to an EMPTY model;
8. a track that never comped writes no element, and a clip with no lane writes no attribute
   (reset-on-absence at both levels);
9. the group driven through the registry, including every typed refusal;
10. `control.undo` takes a `comp.select` / `comp.rebuild` / `comp.lane_remove` back through the
    Track checkpoint, and every `comp.*` id has an A16 row with a non-empty reason and mechanism.
11. **the S7 document layer** (`LanesUpconvertTest`): lane state lands in the top-level
    `<z:lanes v="1">` section — entities with `lane-` ids, the track's list keyed by track id,
    composite beside them — and never as a child of `<track>`; the section round-trips (ids,
    names, segments, clip references included) and a save/load/save of the lane-using project is
    byte-identical; a project that uses none of this writes no section, no `<takelanes>` and no
    `xmlns:z`; and the RISK 1 control names any new-model element a writer puts under `<track>`
    (its RED run with the plant is `docs/s7-logs/negative-control.md`).

What the test does **not** prove, stated rather than implied: it does not render audio, so it
cannot show a comp's waveform; `TakeLaneTest`/`TakeLaneCompTest` themselves still round-trip the
Track element (the journal-checkpoint shape), while the ON-DISK `.mmp` save/load of lane state is
`LanesUpconvertTest`'s claim 11; and nothing here says anything about the seven-platform CI
matrix, which is CI-only.
