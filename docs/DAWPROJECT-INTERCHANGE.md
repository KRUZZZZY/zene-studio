# DAWproject interchange — what this build reads, writes, and cannot carry

Feature row 37 of `docs/FEATURE-LIST-0.3.0.md` ("DAWproject import / export"). This is the
document the release contract asks for when it asks **which version of a published format was
implemented, what the mapping is, and what it cannot carry**. The code it describes:

| where | what |
|---|---|
| `include/DawProjectInterchange.h` | the model, the four conversions, the convention as data |
| `src/core/DawProjectZip.cpp` | the container: a ZIP of STORE entries, hand-written writer, bounds-checked reader |
| `src/core/DawProjectModel.cpp` | tick/beat arithmetic, the `contentType` vocabulary, equality, the digest, the JSON view, `dawProjectConvention()` |
| `src/core/DawProjectWrite.cpp` | model → `project.xml` |
| `src/core/DawProjectRead.cpp` | `project.xml` → model: the document walk, the version check, the typed refusals |
| `src/core/DawProjectReadTracks.cpp` | the track/structure walk, its own TU behind `DawProjectReadShared.h` |
| `src/core/DawProjectSession.cpp` | the session ↔ model conversions |
| `src/core/ControlCommandsDawProject.cpp` | the four `dawproject.*` command ids |
| `src/core/ControlReversibilityTableDawProject.cpp` | their four A16 rows |
| `tests/src/core/DawProjectInterchangeRoundTripTest.cpp` | the registered proof (ctest `DawProjectInterchangeRoundTripTest`) |

---

## 1. The version of the published format, read and cited

DAWproject is published by **Bitwig** at `https://github.com/bitwig/dawproject`. Read on
**2026-09-15** at `main` = `ee4dcdde75940f30e14e55401a26955a58b8322b` (2025-07-12).

**The version, in the format's own words** (`README.md`, section "Status", line 29):

> The format is version 1.0 and is stable.

**The container and encoding** (`README.md`, section "Format Specification", lines 56-59):

> * File Extension: `.dawproject`
> * Container: ZIP
> * Format: XML (`project.xml`, `metadata.xml`)
> * Text encoding: UTF-8

**The version attribute is REQUIRED on the root element** (`Project.xsd`, line 56 and line 109):

```xml
<xs:element name="Project" type="project"/>
  <xs:attribute name="version" type="xs:string" use="required"/>
```

**What this build implements.** `DawProjectMajorVersion = 1`, `DawProjectMinorVersion = 0`,
and `DawProjectVersionAttribute = "1.0"` written into `<Project version="1.0">`
(`include/DawProjectInterchange.h`). The reader accepts a file whose `version` is `1.0` or any
`1.x` and **refuses another MAJOR version by name** (`dawproject.read` → `Refused`: *"the
document declares DAWproject version N; this module implements 1.0"*), rather than reading a
document whose semantics it does not know.

**GAP: none claimed.** The format's own version is 1.0, this module writes 1.0 and reads 1.0;
no part of the published 1.0 schema is implemented as a dialect. What this build does **not**
implement is *optional content within* 1.0 — that is the list in section 7, and it is stated as
loss rather than left implicit. `dawproject.convention` reports the version, the container, the
tick rule and the stated losses **as data on the wire**, so a client can read the same claims
this document makes without reading the source.

The format's own support list (`README.md`, section "DAW Support", line 175) names exactly six
DAWs at 1.0 — Bitwig Studio 5.0.9, PreSonus Studio One 6.5, Steinberg Cubase 14, Steinberg
Cubasis 3.7.1, Steinberg VST Live 2.2, n-Track Studio v10.2.2. **That list is the compatibility
claim**: a container this module writes is written to the same published schemas those read.

---

## 2. The element tree the writer emits

Every element and attribute name below is declared in `Project.xsd`, except the two in
`metadata.xml`, which are declared in `MetaData.xsd`. **Measured, not asserted**: the writer's
source was parsed for every `writeStartElement`/`writeAttribute` name it can emit and each name
checked against the two schemas — **23 of 23** project names and **27 of 27** attribute names
are declared (`MetaData` and `Comment` are the two from `MetaData.xsd`). The order is the
schema's own sequence:

```xml
<Project version="1.0">                                    Project.xsd:56,109
  <Application name="Zene Studio" version="0.3.0-alpha"/>  :88, :114-115 (both required)
  <Transport>                                              :118
    <Tempo min="10" max="999" unit="bpm" value="140" id="id0" name="Tempo"/>   :120, :129-132
    <TimeSignature numerator="4" denominator="4" id="id1"/>                    :121, :201-202
  </Transport>
  <Structure>                                              (choice of Track | Channel)
    <Channel role="master" solo="false" audioChannels="2" id="mixer0" name="Master">
      <Volume min="0" max="2" unit="linear" value="1" id="id2" name="Volume"/>  :298-312
      <Mute value="false" id="id3" name="Mute"/>
    </Channel>
    <Track contentType="notes" loaded="true" id="track0" name="Bass" color="#a2eabf">
      <Channel solo="false" id="strip1" name="Bass" destination="mixer0">
        <Volume .../>  <Mute .../>  <Pan min="0" max="1" unit="normalized" value="0.4" .../>
      </Channel>
    </Track>
  </Structure>
  <Arrangement id="id6">                                   :215
    <Lanes timeUnit="beats" id="id7">                      :228, :253
      <Lanes track="track0" id="id8">                      :254 (xs:IDREF)
        <Clips id="id9">                                   :549
          <Clip time="0" duration="4" playStart="0" name="Intro">   :516, :533-536
            <Notes id="id10">                              :506
              <Note time="0" duration="0.25" channel="0" key="60" vel="0.75" rel="0.75"/>  :483, :498-503
            </Notes>
          </Clip>
        </Clips>
      </Lanes>
    </Lanes>
    <TempoAutomation timeUnit="beats" unit="bpm" id="id11">   :249 (abstract timeline)
      <Target parameter="id0"/>                               :707, :724 (xs:IDREF)
      <RealPoint time="0" value="140" interpolation="hold"/>  :656, :660-661
    </TempoAutomation>
    <TimeSignatureAutomation timeUnit="beats" id="id12">
      <Target parameter="id1"/>
      <TimeSignaturePoint time="0" numerator="4" denominator="4"/>  :693, :697-698
    </TimeSignatureAutomation>
  </Arrangement>
  <Scenes/>                                                  (empty: see LOSSY #1)
</Project>
```

`metadata.xml` is written as `MetaData.xsd` declares it (`MetaData.xsd:4`, `:20`):

```xml
<MetaData><Comment>Exported by Zene Studio 0.3.0-alpha as DAWproject 1.0.</Comment></MetaData>
```

**A stored entry, not a deflated one.** The container is a ZIP whose entries use the **STORE**
method (method 0). Two reasons, both engine-level: `DataFile`'s `qUncompress` reads zlib's
*framed* format and cannot produce a raw DEFLATE stream, so compression would mean a new
dependency; and a stored entry is a valid entry in every ZIP reader, while the format's spec
constrains the *entries*, not the compression method.

---

## 3. What the writer refuses rather than rounds

The format can express all of these; this engine cannot read them back or place them, so writing
them would produce a file that claims something the session is not. Each is a typed refusal
carrying the offending value:

1. **A tempo outside 10..999** — the engine's own bounds (`TempoMap`, `Song::setTempo`), also
   written into `<Tempo min max>`.
2. **A metre whose denominator is not a power of two** — the `TimeSignaturePoint` the engine
   cannot restore.
3. **A clip whose lane has no track id** — an orphan clip has nowhere to land.
4. **A relative path on `dawproject.export`**, and an **existing file without `overwrite`** —
   argument semantics, refused with `InvalidArgs` / `Refused` rather than written anyway.
5. **An id the schema would reject** (empty, or not an NCName) **or one the model repeats** — the
   writer substitutes a generated `id<n>` rather than emit duplicate `xs:ID`s. See LOSSY #11.

On the read side the refusals are the mirror: a file that is not a ZIP, a ZIP with no
`project.xml` entry, a document with no `<Project>` root, a `<Application>` with no name, and a
document declaring another MAJOR version.

---

## 4. Tracks, strips and the join between them

**`<Structure>` is a choice of `Track | Channel`** (`Project.xsd`), and an LMMS `MixerChannel`
is not a property of one track — it is a **summing strip several tracks may feed**. So the mixer's
strips are written as **bare `<Channel>` elements first**, in mixer order, and each track's own
`<Channel>` names the strip it feeds with `destination`, which `Project.xsd:310` declares as an
**`xs:IDREF`**.

The model carries the join in three fields (`DawProjectMixerChannel`, `DawProjectTrack`):

- `DawProjectMixerChannel::index` — the LMMS mixer index the strip came from (`-1` for a
  file), the **conversion-time key**;
- `DawProjectMixerChannel::id` — the document's own id, the IDREF target. The session path
  names them `mixer<n>`; a file's own ids are kept (LOSSY #11);
- `DawProjectTrack::destinationChannelId` — what `destination` said (the id the model carries),
  plus `DawProjectTrack::mixerChannelIndex`, the strip the model was joined to.

**Reading joins by IDREF, writing joins by index.** A model read from a file records both: the
id the document used and the ordinal of the strip it resolved to. On write, `destinationFor()`
prefers the **mixer index** (the session path, where ids are generated per session) and falls
back to the **id** (the file path), so an export→import→export cycle keeps pointing the same
tracks at the same strips. A track whose strip cannot be found is written **without**
`destination` rather than with a dangling IDREF — an unresolvable IDREF is an invalid document.

**The sharing is what cannot be carried** (LOSSY #7): the format joins one track to one strip,
so two LMMS tracks feeding one `MixerChannel` import as two strips, or as one — either way the
*sharing* is gone, and `mixerSharingLost` counts it.

---

## 5. The attribute set, element by element

| element | attribute | `Project.xsd` | value this build writes |
|---|---|---|---|
| `Project` | `version` | :109, required | `1.0` |
| `Application` | `name`, `version` | :114-115, both required | the product name and version (`lmmsversion.h`) |
| `Tempo` | `min`,`max`,`unit`,`value`,`id`,`name` | :129-132 | `10`,`999`,`bpm`, the tempo, generated id |
| `TimeSignature` | `numerator`, `denominator`, `id` | :201-202 required | the global metre |
| `Channel` (strip) | `audioChannels`, `role`, `id`, `name`, `solo` | :309-312 | `2`, `master`/`submix`/`regular`, the strip's id |
| `Volume` | `value`, `unit`, `min`, `max`, `id`, `name` | :307, :129-132 | `linear`, `0`, `2` — the **mixer fader's** scale |
| `Mute` | `value`, `id`, `name` | :166 | the strip's mute |
| `Track` | `contentType`, `loaded`, `id`, `name`, `color` | :266-271, :342 | `loaded="true"` always (see LOSSY #5) |
| `Channel` (track) | `solo`, `id`, `name`, `destination` | :310-312, :477 | `destination` is the IDREF of section 4 |
| `Volume` (track) | as above | :307 | the **track's** volume ÷ 100 (LMMS 0..200, unity 100) |
| `Pan` (track) | `value`, `unit`, `min`, `max` | :299 | `normalized` `0..1`, from the track's panning model |
| `Lanes` (outer) | `timeUnit` | :253 | `beats` |
| `Lanes` (per track) | `track`, `id` | :254 IDREF | the track's id — this is the lane↔track join |
| `Clip` | `time`, `duration`, `playStart`, `name`, `color` | :533-536 | beats, six decimals |
| `Note` | `time`, `duration`, `channel`, `key`, `vel`, `rel` | :498-503 | beats; `key` 0..127; `vel`/`rel` 0..1 |
| `Target` | `parameter` | :724 IDREF | the id of the parameter the timeline drives |
| `RealPoint` | `time`, `value`, `interpolation` | :660-661 | `hold` — LMMS' tempo map holds steps, it has no curve to write |
| `TimeSignaturePoint` | `time`, `numerator`, `denominator` | :697-698 | the metre map's points |
| `MetaData`/`Comment` | — | `MetaData.xsd:4`, `:20` | the product and version that exported |

**Closed vocabularies** the writer keeps to (`Project.xsd`): `contentType` is one or more of
`audio|automation|notes|video|markers|tracks` (:809-818) — **six** values; `timeUnit` is
`beats|seconds` (:764-767) and this build writes `beats`; `unit` includes `linear|normalized|bpm`
(:750-762); `interpolation` includes `hold` (:820+).

**The tick rule, as data.** One beat **is** one quarter note, and `TimePos::ticksPerQuarterNote()
== 48`, so one LMMS tick is exactly 1/48 beat. Times are written with **six decimals**: the
maximum error of the round trip through a decimal is `0.5e-6 × 48 = 2.4e-5` ticks, far below the
half-tick that would land on a neighbouring tick — so reading a written value back **cannot**
move a note, and "the model did not change" is a statement about the format, not about luck. A
*foreign* time not on that grid is rounded onto it and counted in `rounded_times` (LOSSY #9).

---

## 6. The two directions

```
session ──dawProjectModelFromSong──▶ model ──dawProjectXmlFromModel──▶ project.xml ──┐
   ▲                                 │                                    writeDawProject (ZIP)
   │                                 │                                               │
   └──applyDawProjectModel───────────┴────────────dawProjectModelFromXml◀── readDawProject
```

`modelFromSong` never fails (a session always describes a document) and fills a
`DawProjectLossReport`; `applyModelToSong` replaces the session and fails **only** when a track
type cannot be created. `dawProjectModelFromXml` refuses with the model untouched.
`DawProjectLossReport`'s fields are the measurement (section 7): every loss is a **count or a
flag a test can assert on**, so "lossy" is measured, not claimed.

`dawProjectModel` equality (`operator==`) compares **every** field the model carries: the
application and version, the tempo and metre, the mixer channels, each track (id, name, colour,
contentType, typeName, the lost-content flag, the channel id, solo, mute, the volume with its
*two* presence flags, the pan with its presence flag, the destination id and clips), each clip
(time, duration, playStart, name, colour, notes) and each note (time, duration, channel, key,
vel, rel), plus the two point lists. That is what the proof compares — **not the file, not its
hash**.

---

## 7. The lossy mappings, all eleven

The format cannot carry 1-6; the engine has 7-8 that the format cannot express; 9-11 are
properties of the conversion itself. Each line names **where it is counted**, so a caller of
`dawproject.export` / `dawproject.import` reads the same list as data.

| # | loss | direction | counted in |
|---|---|---|---|
| 1 | **Audio clips and their media** — no media file is ever copied, and a `SampleClip`'s content is not carried; automation clips and their content likewise. A `MidiClip`'s **notes** are carried; nothing else about it is. Scenes and clip slots, loop points, fades and clip gain are also not written (`<Scenes/>` is empty). | export | `audioClipsSkipped`, `automationClipsSkipped`, `fadesNotWritten`, `sendsNotWritten` |
| 2 | **Device and plug-in state** — `<Devices>`, their parameters and any automation of them. | export | `devicesNotWritten`, `automationClipsSkipped` |
| 3 | **A map event carrying BOTH halves becomes TWO points** — the format has one timeline per half (tempo, time signature), LMMS' `TempoMap` can carry a tempo and a metre change at the same tick. Nothing is lost: the pair is split and re-joined by tick on the way back. | both | `splitMapEvents` |
| 4 | **Folder nesting** — LMMS' track list is flat, so a folder's children are written as tracks at the container root; the folder **marker** survives as a `tracks`-contentType track, the *relation* does not. | export | `folderChildrenLost` |
| 5 | **Track types** — LMMS' nine types onto the format's six `contentType` values (section 5). `instrument` and `pattern` → `notes`; `sample` → `audio`; `automation` and `hidden_automation` → `automation`; `folder` → `tracks`; `event` and `video` have **no counterpart** and are written with an empty `contentType`. The mapping is many-to-one: two LMMS types can come back as the same type. | both | `unmappedTrackTypes` |
| 6 | **Mixer pan, and two volume scales** — the format has one `<Volume>` per `Channel`, LMMS has two: a `MixerChannel` fader (0..2, unity 1.0) goes on the bare strip `<Channel>`, the track's own volume (0..200, unity 100) on the track's `<Channel>`, divided by 100. `<Pan>` comes from the **track's** panning model, which only `InstrumentTrack` exposes — a track type with none has no pan written and is counted. | both | `panNotWritten` |
| 7 | **Mixer routing and sharing** — `destination` names the strip one track feeds; LMMS' wider `MixerRoute` graph, its pre/post-fader flags, and *which* tracks share one strip are not carried (the format joins one track to one strip). | both | `routingLost`, `mixerSharingLost` |
| 8 | **Tempo bounds** — written as 10..999 and a file outside them is refused (section 3), so an out-of-range tempo is a refusal rather than a loss. | both | — (refusal) |
| 9 | **Time values are beats** — a *foreign* time not on LMMS' 48-per-beat grid is rounded onto it. Times this build writes are exact (section 5). | import | `roundedTimes` |
| 10 | **The track type's NAME is not in the document** — the format carries `contentType`, and the name is **derived from it on read**, so a model whose `typeName` is spelled another way (`"Instrument"` for the canonical `"instrument"`) comes back in the canonical lowercase spelling. The mapping is case-insensitive in both directions, so the *type* is never lost — only its spelling. | both | — (not a count: it is a spelling) |
| 11 | **An unusable or repeated id is replaced by a generated `id<n>`** — `xs:ID` must be unique in the document and be an NCName (`Project.xsd:150`), so an id that is empty, starts with a digit, or repeats cannot be written through. **Ids the model does carry do round-trip**: the writer emits the model's own strip and track ids (`mixer0`, `strip1`, `track0`, …) and points every IDREF at *them*, which is what makes the export→import→export cycle stable. | both | — (the writer's `elementId`) |

**Losses 10 and 11 were found by the round-trip proof, not by reading the code** — see
section 9. Before them the writer renumbered every track and strip to `id<n>`, so a model's own
ids did not survive the trip and `destination` pointed at an id the writer had invented.

---

## 8. The control surface: ids, schemas, A16 rows

Four commands, all in group `dawproject` (`src/core/ControlCommandsDawProject.cpp`), registered
by `registerDawProjectCommands` from `ControlRegistryRegistrations.cpp`, declared in
**`include/ControlRegistryGroups.h`** (not `ControlRegistry.h`, which is at its file-length cap).

| id | args schema | result schema | A16 class |
|---|---|---|---|
| `dawproject.convention` | `{}` (none) | `format_name`, `format_version`, `major_version`, `minor_version`, `container`, `project_entry`, `metadata_entry`, `text_encoding`, `time_unit`, `ticks_per_quarter`, `tick_rule`, `zip_method`, `content_type_vocabulary`, `stated_losses` | `not_mutating` |
| `dawproject.export` | `path` (required), `overwrite` | `path`, `bytes`, `sha256`, `format_version`, `application_name`, `application_version`, `track_count`, `mixer_channel_count`, `clip_count`, `note_count`, `tempo_point_count`, `meter_point_count`, `split_map_events`, `model_digest`, `loss` | `not_mutating` |
| `dawproject.read` | `path` (required) | `format_version`, `application_name`, `application_version`, `has_metadata`, `title`, `track_count`, `mixer_channel_count`, `clip_count`, `note_count`, `tempo_point_count`, `meter_point_count`, `split_map_events`, `unused_entries[]`, `model_digest`, `loss`, `model` (the whole model as JSON) | `not_mutating` |
| `dawproject.import` | `path` (required) | `path`, `format_version`, `track_count`, `mixer_channel_count`, `clip_count`, `note_count`, `tempo`, `numerator`, `denominator`, `tempo_point_count`, `meter_point_count`, `map_active`, `model_digest`, `loss` | `true_inverse`, reversible, recorded-action checkpoint |

**The A16 rows** live in their own file, `src/core/ControlReversibilityTableDawProject.cpp`,
joined into `reversibilityRowTable()` with **one** entry (the join list in
`ControlReversibilityTable.cpp` is at its cap). The class of each row comes from the **row**, not
from the file it lives in.

**Why export and read are `not_mutating` although they touch the filesystem:** the claim is about
the **session**, not about the disk. They write or read a file *outside* the project and change
no session state, so there is nothing an inverse could restore — the same class `project.save`
and `interchange.smf_export` carry, for the same reason. An existing file without `overwrite`
is refused as argument semantics, not undone.

**Why import is a recorded-action `true_inverse` although `project.open` is not one:** an import
replaces the whole session — every track, the tempo map, the global tempo and metre, and each
track's mixer strip — so the undo must carry the whole captured session: every track's own XML
taken **while the track was still alive**, the mixer strips, the tempo map and the two globals,
recreated through `control::restoreTrackFromXml` (the project loader's own path) and written back
through `addStructuralUndoStep`. The Song's own checkpoint cannot serve: `TrackContainer::saveSettings`
carries neither the mixer strips nor a track that has been destroyed. The captured document counts
against the undo byte budget, and **an import whose session cannot be captured is refused before
anything is replaced** — never performed unreversibly.

**The A16 histogram is a measurement.** Measured on this tree with
`ZENE_TELEMETRY_ENABLED` (this build's `build/lmmsconfig.h`), no wasmtime, no `WANT_STEM_SPLIT`:

```
MEASURED rows=287 true_inverse=152 snapshot=21 irreversible=6 not_mutating=108
```

which is the tested base `285 / 152 / 21 / 6 / 106` plus the two `telemetry.*` rows this
configuration compiles in — the figure this page's lane measured, kept as that lane's record.

**That lane's figure is history, not this release's number** (2026-09-16, board card #677): the table's
size is published ONCE, in the `A16-HISTOGRAM` block of `docs/RELEASE-NOTES-v0.3.0-alpha.md`, and
`tests/src/core/ReversibilityContractTest.cpp::theTableHistogramIsTheDocumentedOne()` now re-derives the
histogram from the live table on every run and READS that published figure, failing when the two differ
— there is no constant in the test to re-measure by hand any more, and the option deltas of a
configuration are measurements declared in the same published block. This build's 4 rows contribute 3
`not_mutating` + 1 `true_inverse`.

---

## 9. The proof

**Registered ctest:** `DawProjectInterchangeRoundTripTest`
(`tests/src/core/DawProjectInterchangeRoundTripTest.cpp`, registered in `tests/CMakeLists.txt`
with `QT_QPA_PLATFORM=offscreen`). Five claims:

1. **The round trip compares the MODEL** — a model with tracks, clips, notes, mixer channels,
   tempo points and metre points is written to a real `.dawproject` container, read back, and
   compared with `operator==`; the ids and the IDREF between a track and its strip are asserted
   **by name** as well, so a document that is renumbered fails with the id it changed.
2. **The session round trip** — model → apply to a real `Engine` → extract → compare, asserting
   the expected **loss counts** rather than an exact match of the lossy fields.
3. **The control command reads the model back** — `dawproject.read` returns the model JSON, the
   counts and the format version.
4. **One undo** — `dawproject.import` replaces the session and one `control.undo` restores the
   previous session through the recorded action checkpoint.
5. **Typed refusals** — a missing file, a file that is not a ZIP, a ZIP with no `project.xml`,
   and a relative path on export.

**What was measured here.** The round trip's MODEL half (claims 1, 3, 5) was measured against the
engine directly, without the Engine/registry the ctest needs, because the writer, the reader, the
container and the model have no engine dependency — a harness that links only
`DawProjectModel/Zip/Write/Read/ReadTracks` and authors the *same* model the ctest authors:

```
$ ./dawp-proof ; echo EXIT=$?
authored: tracks=2 clips=2 notes=2 mixer=2 tempoPoints=1 meterPoints=1
wrote: path=/tmp/dawp-proof.dawproject bytes=2690 sha256=37eadd1db1a1158a...
read: formatVersion=1.0 application=Zene Studio 0.3.0-alpha hasMetaData=yes tracks=2 ...
MODEL EQUAL: operator== says the read model is the authored model
DIGEST SENSITIVE: a changed note key moves the digest
DIGEST SENSITIVE: tempo moves the digest
REFUSAL (missing file): cannot read ... No such file or directory
REFUSAL (not a ZIP): ... is too short to be a ZIP container
REFUSAL (no project.xml): ... has no 'project.xml' entry, so it is not a DAWproject container
failures=0
EXIT=0
```

and the container itself, for the ids:

```
$ python3 -c "import zipfile,re; print(re.findall(r'id=\"([^\"]+)\"', zipfile.ZipFile('/tmp/dawp-proof.dawproject').read('project.xml').decode()))"
['id0','id1','mixer0','id2','id3','mixer1','id4','id5','track0','strip1','id6','id7','id8','track1',
 'strip2','id9','id10','id11','id12','id13','id14','id15','id16','id17','id18','id19']
destination: ['mixer0','mixer1']     track refs: ['track0','track1']
```

The model's own `mixer0`/`track0`/`strip1` are the document's ids and every IDREF points at them
(LOSSY #11). The digest is **not** a hash of the file: it is a line-per-entity rendering of the
model, and it moves when a note key or the tempo moves — asserted, so "the models are equal"
cannot be satisfied by two blanks.

**NOT verified in this lane:** the ctest itself was **not executed** — it constructs a full
`Engine` and the lane's provider window did not allow a build of that size (see the report's
"not verified" section). The session half (converts through `Song`, `Mixer`, `TempoMap`,
`InstrumentTrack`) is therefore proven by reading only, and the merge tip must run the ctest.

---

## 10. Limitations

**UI absence — one line: DAWproject import / export is drivable through the socket, not from the
interface.** Nothing in `src/gui/` writes or reads a `.dawproject` container:
`dawproject.convention`, `dawproject.export`, `dawproject.read` and `dawproject.import` are
reachable through `--control-socket` only. The same line is carried in
`docs/KNOWN-LIMITATIONS.md` and `docs/RELEASE-NOTES-v0.3.0-alpha.md`.

Further limits, stated rather than implied:

- **No media.** An audio clip's file is never copied into the container, so a container this build
  writes is a *score* of the session's note content (LOSSY #1) — not a self-contained project.
- **No devices.** Plug-in and device state is not written, so an import produces a session of
  bare instrument tracks.
- **`<Scenes/>` is always empty** — the clip launcher is not modelled.
- **An export is refused**, not approximated, when a tempo leaves 10..999 or a metre's
  denominator is not a power of two.
- **Two volume scales** are exposed on the wire as the format expresses them (LOSSY #6); a
  client that reads `loss` must know which `<Volume>` it is looking at (the bare strip `<Channel>`
  is the mixer fader, the track's `<Channel>` is the track's own).
