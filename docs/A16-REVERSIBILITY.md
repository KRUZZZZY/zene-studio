# SPEC A16 - the reversibility contract of the agent surface (task #623)

**Status:** implemented and tested on `post-alpha/reversibility`, branched from the gate-clean
integrated tree `post-alpha/agent-surface-integration` @ `149068c06`.

This is the DESIGN + IMPLEMENTATION record. The measured baseline it starts from is
`ableton-gap/A16-STATUS-MEASURED.md` (36 mutating commands exercised, 17 `reversible: true`); where
this table disagrees with that measurement, the row says so and section 2 argues it.

---

## 1. The classification table for EVERY registered command

One row per registered command. The table is DATA in
`src/core/ControlReversibilityTable.cpp` - one file, so it can be reviewed as a table and so an
anti-drift test (`tests/src/core/ReversibilityContractTest.cpp`) can compare it against the
registry in both directions: every registered command has a row, and every row names a registered
command.

The wire names are `true_inverse`, `snapshot`, `irreversible` and `not_mutating`. **70 commands are
registered; this table has 71 rows** - the 70 plus `project.restore_revision`, which this change
ADDS (SPEC A16 deliverable 4: the file-level restore path, reachable through the control surface).

The three classes SPEC A16 names apply to the 40 commands that write something. The other 31 write
nothing - a read-only inspector, a refused handler, or `control.undo` itself - and are classed
`not_mutating`, because a command that changes no project state is not "reversible", it is
*not a mutation*. Grouping counts:

| class | rows |
|---|---|
| `true_inverse` | 30 |
| `snapshot` | 5 |
| `irreversible` | 3 |
| `not_mutating` | 34 (of which 3 are declared-mutating refusals and 2 are selection commands) |
| **total** | **72** |

### 1.1 `true_inverse` - a live checkpoint on the engine's own undo stack

Three flavours of ONE checkpoint, all unwound by `ProjectJournal::undo()` - the same call the GUI's
Edit->Undo (Ctrl+Z) makes:

* **plain** - one object's serialized XML (a Track, a Clip, an `AutomatableModel`, the `Song`);
* **composite** - several objects restored together in ONE pop, so a command that writes N objects
  still costs one Ctrl+Z (`track.set_solo`);
* **action** - a recorded inverse OPERATION, for a change with no live state to put back (a created
  or deleted Track/MixerChannel/AutomationTrack/Effect, a scalar in a subsystem the engine does not
  journal, a config value, a file revision).

| command | rev | reason | mechanism the engine provides | fallback |
|---|---|---|---|---|
| `audio.device_set` | yes | the preference is a scalar in the config file, not in the project | action checkpoint: the recorded undo step writes the previous device name back and saves the config file | - |
| `automation.add_point` | yes | the point lives on an AutomationClip, which is a JournallingObject - EXCEPT on the call that has to create the AutomationClip first | ProjectJournal (AutomationClip checkpoint); on the creating call, an action checkpoint that removes the automation track the command created, so the first point is one undoable step like every later one. DISAGREEMENT with A16-STATUS-MEASURED.md, which records the creating call as reversible:false | - |
| `automation.clear` | yes | the point list is the clip's own state | ProjectJournal (AutomationClip checkpoint) | - |
| `automation.remove_point` | yes | the point list is the clip's own state | ProjectJournal (AutomationClip checkpoint) | - |
| `clip.add` | yes | the clip is created inside a Track, and a Track checkpoint carries every clip it holds | ProjectJournal (Track checkpoint) | - |
| `clip.delete` | yes | same container: the clip list is part of the Track's serialized state | ProjectJournal (Track checkpoint) | - |
| `clip.duplicate` | yes | the duplicate is a second clip in the same Track | ProjectJournal (Track checkpoint) | - |
| `clip.move` | yes | position is a Clip property and the Clip is a JournallingObject | ProjectJournal (Clip checkpoint) | - |
| `clip.resize` | yes | length is a Clip property | ProjectJournal (Clip checkpoint) | - |
| `clip.split` | yes | the split rewrites the clip list of one Track | ProjectJournal (Track checkpoint) | - |
| `mixer.add_channel` | yes | a created MixerChannel has no before-state; the mixer is a JournallingObject but restoring its checkpoint would destroy and recreate every channel, and a MixerView holds those pointers - the same GUI-safety reason the TrackContainer checkpoints are commented out upstream. DISAGREEMENT with A16-STATUS-MEASURED.md, which records this as reversible:false | action checkpoint: the recorded undo step deletes the channel it created (Mixer::deleteChannel), through the same code path mixer.remove_channel uses; a fresh channel carries only defaults, so removing it restores the mixer exactly | - |
| `mixer.set_volume` | yes | the fader is a FloatModel, i.e. a JournallingObject | ProjectJournal (MixerChannel volume model checkpoint) | - |
| `note.add` | yes | the note list belongs to the MidiClip, which is a JournallingObject | ProjectJournal (Clip checkpoint) | - |
| `note.move` | yes | note position is part of the clip's saved note list | ProjectJournal (Clip checkpoint) | - |
| `note.remove` | yes | same clip-owned list | ProjectJournal (Clip checkpoint) | - |
| `note.resize` | yes | note length is part of the clip's saved note list | ProjectJournal (Clip checkpoint) | - |
| `note.velocity_set` | yes | note volume is part of the clip's saved note list | ProjectJournal (Clip checkpoint) | - |
| `plugin.bypass` | yes | the On/Off control is the Effect's enabled model | ProjectJournal (Effect enabled-model checkpoint) | - |
| `plugin.param_set` | yes | the parameter is an AutomatableModel, i.e. a JournallingObject | ProjectJournal (parameter model checkpoint) | - |
| `plugin.preset_load` | yes | same as plugin.state_load: the preset replaces the device's parameters and the previous settings are captured first | action checkpoint: the recorded undo step restores the captured state XML | - |
| `plugin.state_load` | yes | the load replaces the device's settings, and the device's PREVIOUS settings are captured in the transaction's before-state | action checkpoint: the recorded undo step restores the captured state XML through the same controlRestoreDeviceState path plugin.state_load itself uses | - |
| `project.restore_revision` | yes | a revision restore is a file write; before it runs, the file it replaces is rotated into the same bounded revision set | action checkpoint: the recorded undo step restores the revision the restore replaced | - |
| `settings.set` | yes | ConfigManager is not a JournallingObject, so there is no object checkpoint - but the previous value is a bounded scalar | action checkpoint: the recorded undo step writes the previous value back and saves the config file, exactly as the command does | - |
| `track.add` | yes | a created Track has no before-state to restore, so the inverse is the operation, not a snapshot. DISAGREEMENT with A16-STATUS-MEASURED.md, which records this as reversible:false (no checkpoint exists) | action checkpoint: the recorded undo step removes the created track through the product's own TrackContainerView::deleteTrackView path (the path whose checkpoint upstream left commented out); a fresh instrument track carries only defaults, so removing it restores the container exactly. The project-scoped next-id counter is monotonic and is NOT rewound: a re-add gets a fresh trk-<n>, which is the documented limit of this inverse | - |
| `track.remove` | yes | a deleted JournallingObject's journal id resolves to nullptr, so a checkpoint cannot bring it back. DISAGREEMENT with A16-STATUS-MEASURED.md, which records this as reversible:false | action checkpoint: the track's own XML (Track::saveState into a JournalData DataFile) is captured before the delete and the recorded undo step recreates it with Track::create(element, song) - the same call TrackContainer::loadSettings makes. The capture is bounded like the device-state snapshot (64 KiB) | - |
| `track.rename` | yes | the name is part of the Track's own serialized state | ProjectJournal (Track checkpoint) | - |
| `track.set_mute` | yes | the muted flag is a BoolModel, i.e. a JournallingObject of its own | ProjectJournal (Track mute BoolModel checkpoint) | - |
| `track.set_solo` | yes | the action is NOT one object: the solo flag rides the solo BoolModel and Track::toggleSolo (driven from the solo model's dataChanged) writes EVERY other track's mute. DISAGREEMENT with A16-STATUS-MEASURED.md, which records this as reversible:false ("partial: one checkpoint covers one object") | COMPOSITE checkpoint: every track of the song container is snapshotted as ONE checkpoint (ProjectJournal::addJournalCheckPoint(QVector)), so one control.undo and one Ctrl+Z restore the solo flag and every mute together. This is SPEC A16 deliverable 3 | - |
| `transport.seek` | yes | the play head is engine state, not project state, and is not a JournallingObject - so no object checkpoint exists for it | action checkpoint: the recorded undo step calls Song::setPlayPos with the tick the transaction's before-state holds | - |
| `transport.set_tempo` | yes | one scalar on the Song, which is a JournallingObject | ProjectJournal (Song checkpoint): Song::saveState carries the tempo | - |

### 1.2 `snapshot` - the inverse is a bounded recorded state, not a live object

`rev=no` means the recorded state is a MANUAL fallback: an undo attempt is refused, typed, and the
refusal names the fallback rather than pretending.

| command | rev | reason | mechanism the engine provides | fallback |
|---|---|---|---|---|
| `mixer.remove_channel` | no | the channel's full state is in the transaction's before-state, but there is no command that recreates a channel WITH state: mixer.add_channel always makes a default channel | bounded container snapshot: before-state holds the removed channel's name, gain, mute/solo, sends and effects, so the state is not lost - only the automatic replay is missing. DISAGREEMENT with A16-STATUS-MEASURED.md in one respect: the doc lists this command under "not exercised", so its class was unmeasured; this row is the measurement | recreate the channel with mixer.add_channel and re-apply name, gain, mute/solo and sends from before; the effects must be loaded again with plugin.load and their state XML re-applied |
| `plugin.load` | no | two branches: appending an EFFECT has an exact inverse (the instance is new and carries only defaults), but REPLACING an instrument does not - the replaced instrument and its parameter values are gone | the effect branch pushes an action checkpoint that unloads the device it created (plugin.load of an effect is therefore one undoable step). The instrument branch records the replaced plugin NAME only and is NOT reversible: replacing an instrument destroys its state | the instrument branch: reload the previous instrument by dev-<n> (the transaction's before.plugin names it); its parameter values are NOT restored, so treat an instrument replacement as destructive |
| `plugin.preset_save` | no | the command writes a file OUTSIDE the project; the replaced revision is recorded in before.previous_content (bounded) | file-level snapshot: the replaced revision is in before.previous_content | write before.previous_content back with plugin.preset_save, or delete the file when before.replaced was false |
| `plugin.state_save` | no | the command writes a file OUTSIDE the project; the project is untouched, and the file's previous revision is recorded in before.previous_content (bounded) | file-level snapshot: the replaced revision is in before.previous_content, bounded like every other state snapshot | write before.previous_content back with plugin.state_save, or delete the file when before.replaced was false |
| `project.save` | yes | the engine writes the project file in place and keeps no previous revision by default. DISAGREEMENT with A16-STATUS-MEASURED.md, which records this as reversible:false with no file-level inverse | file revision: before the write, the existing file is rotated into a named retention set (keep-3: <file>.rev0, .rev1, .rev2, each capped at 8 MiB, 24 MiB per project). The recorded inverse is the command project.restore_revision, which control.undo dispatches. File-level commands are deliberately NOT put on the GUI undo stack - see docs/A16-REVERSIBILITY.md - because a file is not project state | - |

### 1.3 `irreversible` - no inverse exists in this engine, by nature of the command

An undo attempt on one of these FAILS with the typed `irreversible` error, naming the command, its
class, the engine's own reason and the documented fallback. It never undoes an older command
instead.

| command | rev | reason | mechanism the engine provides | fallback |
|---|---|---|---|---|
| `plugin.unload` | no | the removed Effect's state XML is captured, but recreating the instance would need plugin.load by catalogue id plus a state restore, and the instance id (fx-<n>) is position-derived - the inverse is not one operation the registry can run | none. before holds the device's full state XML (bounded at 64 KiB) plus its plugin name and chain index | write before.state_xml to a file, plugin.load the same dev-<n> onto the same target, then plugin.state_load that file. The chain ORDER is not restored |
| `project.open` | no | loading a project replaces the whole session, and the engine keeps no pre-load snapshot: the previous document, including any UNSAVED edits, is gone | none. The transaction records the previous file path and its sha256 so the caller can see what was displaced | reopen the file named in before.previous_file; unsaved changes to the displaced session are LOST - save first (project.save keeps a revision) if they matter |
| `script.run` | no | a Lua script mutates the engine through its own bindings; the registry sees one command and cannot know what the script wrote | none. A script that wants to be undoable must take its own checkpoint (Lua addCheckPoint()), which ProjectJournal::undo DOES replay - so the record is honest about what it can and cannot cover | run a script that takes its own checkpoint (Lua addCheckPoint()) before it edits; control.undo then replays that checkpoint |

### 1.4 `not_mutating` - nothing to reverse

Read-only inspectors, the three handlers that refuse every call (declared `mutating` but never
writing), the two selection commands, the undo/redo commands themselves, `control.quit`, the
transport run state, and `render.render` (an output artefact).

| command | rev | reason | mechanism the engine provides | fallback |
|---|---|---|---|---|
| `app.version` | no | reads the build identity | no write | - |
| `arrangement.get_state` | no | reads the model | no write | - |
| `audio.device_list` | no | reads the device table | no write | - |
| `automation.get_state` | no | reads the model | no write | - |
| `automation.mode_set` | no | declared mutating, but the handler REFUSES every call: this build has automation modes in the engine but no way to select or persist one (docs/KNOWN-LIMITATIONS.md) | no write happens, so no transaction is recorded | use automation.add_point to write a curve instead |
| `clip.select` | no | selection is control-surface view state: it is not serialized, the GUI keeps its own copy in QGraphicsItem state, and no engine checkpoint can hold it | nothing to inverse in the project. The registry records NO transaction for this command (mutating is false), so a select cannot block or shadow the undo of a real edit; the previous selection is reported in the result so a client can restore the view itself | - |
| `control.commands_list` | no | reads the registry | no write | - |
| `control.ping` | no | liveness probe | no write | - |
| `control.quit` | no | process lifecycle, not a project edit; the response reports whether unsaved changes were discarded so the caller is not surprised | no project state is written | - |
| `control.redo` | no | the inverse of the undo; it re-applies a step, it is not project state | the engine's ProjectJournal | - |
| `control.surface_report` | no | reads the menu/toolbar reflection | no write | - |
| `control.transactions` | no | reads the transaction record | no write | - |
| `control.undo` | no | it IS the inverse applier: it is the thing that reverses another command, so classifying it as a command to be reversed would recurse | the engine's ProjectJournal | - |
| `control.version` | no | reads the version strings | no write | - |
| `dsp.get_state` | no | reads the device chains | no write | - |
| `midi.device_list` | no | reads the MIDI client | no write | - |
| `midi.learn_toggle` | no | the armed flag is GUI/engine mode state (MidiLearn's own enabled flag), not project state: no model, no serialized field and no journal checkpoint is written, so the registry records no transaction | nothing to reverse: calling midi.learn_toggle again is the operation a client calls, and setArmed() keeps the Edit menu tick in step | - |
| `mixer.get_state` | no | reads the mixer | no write | - |
| `mixer.set_pan` | no | declared mutating, but the handler REFUSES every call: this tree has no pan on a MixerChannel, and inventing one would change the mixer's serialization format | no write happens, so no transaction is recorded and control.undo is not blocked by it | none needed: the command is a typed refusal, use track panning (InstrumentTrack/SampleTrack panningModel) or per-note panning |
| `note.select` | no | same view state, per note | same: no transaction, the previous selection is reported in the result | - |
| `plugin.list` | no | reads the device catalogue | no write | - |
| `plugin.param_get` | no | reads a parameter | no write | - |
| `plugin.preset_list` | no | reads a preset directory | no write | - |
| `project.get_state` | no | reads the project state | no write | - |
| `render.render` | no | it writes an OUTPUT ARTEFACT; the session it renders is not modified (it serialises to a temp file and removes it) | the project is unchanged; the rendered file is an output, not project state, and overwriting it is the caller's decision | - |
| `roll.get_state` | no | reads the note list | no write | - |
| `script.list` | no | reads the scripts directory | no write | - |
| `settings.get` | no | reads one config value | no write | - |
| `track.get_state` | no | reads one track | no write | - |
| `track.list` | no | reads the track container | no write | - |
| `track.set_arm` | no | declared mutating, but the handler REFUSES every call: arm state lives on the prototype MultiTrackRecorder, not on lmms::Track | no write happens, so no transaction is recorded | none needed: the command is a typed refusal |
| `transport.get_state` | no | reads the transport | no write | - |
| `transport.play` | no | the transport run state is engine state, not project state, and the registry has never recorded a transaction for it | nothing to reverse: transport.stop is the operation a client calls, and it is available directly | - |
| `transport.stop` | no | same engine run state | nothing to reverse: transport.play is the operation | - |

---

## 2. Reconciliation with `A16-STATUS-MEASURED.md`, and where this disagrees

The measurement recorded 36 exercised mutating commands: 17 `reversible: true`, 19 `false`. This
table keeps the true half and moves 7 of the false half (plus the two selection commands) to
an honest inverse, or to a typed refusal with a named fallback. Every row that moved:

| command | measured baseline | this implementation |
|---|---|---|
| `automation.add_point` | true_inverse | measured: with A16-STATUS-MEASURED.md, which records the creating call as reversible:false | this table: **true_inverse**, reversible=yes |
| `mixer.add_channel` | true_inverse | measured: with A16-STATUS-MEASURED.md, which records this as reversible:false | this table: **true_inverse**, reversible=yes |
| `mixer.remove_channel` | snapshot | measured: with A16-STATUS-MEASURED.md in one respect: the doc lists this command under "not exercised", so its class was unmeasured; this row is the measurement | this table: **snapshot**, reversible=no |
| `project.save` | snapshot | measured: with A16-STATUS-MEASURED.md, which records this as reversible:false with no file-level inverse | this table: **snapshot**, reversible=yes |
| `track.add` | true_inverse | measured: with A16-STATUS-MEASURED.md, which records this as reversible:false (no checkpoint exists | this table: **true_inverse**, reversible=yes |
| `track.remove` | true_inverse | measured: with A16-STATUS-MEASURED.md, which records this as reversible:false | this table: **true_inverse**, reversible=yes |
| `track.set_solo` | true_inverse | measured: with A16-STATUS-MEASURED.md, which records this as reversible:false ("partial: one checkpoint covers one object" | this table: **true_inverse**, reversible=yes |

The classes the measurement listed as "what it would take to make each class reversible" map onto
this design as follows:

| measurement's class | this design's answer |
|---|---|
| no checkpoint exists (`track.add`, `track.remove`, `mixer.add_channel`, the first `automation.add_point`) | **implement the inverse as an operation**: an ACTION checkpoint on the engine's own stack. A created object carries only defaults (remove it); a deleted one is recreated from its own captured XML through `Track::create(element, song)`, the call `TrackContainer::loadSettings` makes. Upstream's commented-out `TrackContainer` checkpoint was NOT the answer: restoring a `TrackContainer` calls `clearAllTracks()` (src/core/TrackContainer.cpp:85-91), which invalidates every `TrackView` a GUI holds - a crash, not an undo. |
| partial: one checkpoint, many objects (`track.set_solo`) | **composite checkpoint**: every song track snapshotted as ONE step. |
| view state (`clip.select`, `note.select`) | the commands are **no longer `mutating`**: they record no transaction, so a select cannot shadow or block the undo of a real edit. The previous selection travels in the result instead. |
| not a `JournallingObject` (`transport.seek`, `settings.set`, `audio.device_set`) | action checkpoints: the scalar inverse is recorded and runs on undo. |
| file-level (`project.save`, `plugin.state_*`, `plugin.preset_*`) | `project.save` keeps a named, bounded revision set and its inverse is the command `project.restore_revision`, which `control.undo` dispatches. `plugin.state_load` / `plugin.preset_load` are action checkpoints (the device's previous XML is restored). `plugin.state_save` / `plugin.preset_save` stay **not reversible automatically**: they write a file outside the project; their previous revision is recorded (`before.previous_content`) and the fallback names how to put it back. |
| replacement / removal (`plugin.load`, `plugin.unload`) | split by branch. **Effect append** is an action step (unload the instance it created; it carries only defaults). **Instrument replacement** is NOT reversible (the replaced instrument's parameter values are gone) and says so. `plugin.unload` is **irreversible**, as the task requires. |
| out-of-band mutation (`script.run`) | **irreversible**, typed refusal, with the honest fallback: a script that wants to be undoable takes its own checkpoint (Lua `addCheckPoint()`), which `control.undo` does replay. |

---

## 3. The transaction record (deliverable 2)

One record per successful mutating command, built by the registry from the handler's
`__transaction` payload after the handler returns (on the UI thread - see section 7).

```
{ "command": "track.set_solo",
  "class": "true_inverse",      <- stamped from THE table, never from the handler
  "before": { ... },            <- the before-state snapshot
  "inverse": { "op": "...",     <- the inverse descriptor
               "applies": "journal" | "command",
               "args": { ... } },
  "reversible": true,           <- the CALL's verdict
  "mechanism": "...",           <- how it is reversed, or why it cannot be
  "bytes": 412 }
```

**Bounds, eviction, and what happens at the cap.**

| | value |
|---|---|
| record count cap | **100** (`control::MaxTransactionRecords`), the same depth as `ProjectJournal::MAX_UNDO_STATES` - so an agent never sees a record for a step it can no longer undo |
| record byte cap | **262144** (256 KiB) of serialised before-state + inverse descriptor + mechanism |
| per-field caps inside `before` | 64 KiB for a device state XML (`ControlCommandsPlugin.cpp`), 64 KiB for a captured track XML (`MaxTrackSnapshotChars`) |
| eviction policy | **FIFO - oldest first.** The newest record (the one `control.undo` reads) is never the one dropped |
| at the cap | the oldest record is evicted and the eviction is **counted and reported**, never hidden: `control.transactions` returns `count`, `retained_bytes`, `cap_records`, `cap_bytes`, `evicted` and `capped` |

**Invariant enforced by the registry, not by convention:** the `class` is stamped from the table.
A handler may record LESS than its class allows (an instrument replacement inside `plugin.load` is
`reversible: false` although the command's class is `snapshot`), and may never record more: if a
handler claims an inverse for a command the contract calls `irreversible`, the claim is dropped and
the mechanism says so.

---

## 4. Aggregation: one agent command is ONE undoable step (deliverable 3)

Two mechanisms, and the SECOND one is the one that makes the rule general. Both are
`ProjectJournal` (the engine's own stack, which the GUI's Edit->Undo drives), so the agent and the
human share one history at one granularity.

**4.1 `ProjectJournal` gained two additive checkpoint flavours** (`include/ProjectJournal.h`):

* `addJournalCheckPoint(const QVector<JournallingObject*>&)` - one checkpoint, N objects, one pop;
* `addJournalAction(undo, redo)` - one checkpoint that runs an operation instead of restoring an
  object.

**4.2 And the registry MERGES the window a command produced into one step.**
`ProjectJournal::undoDepth()` marks the stack before a mutating handler runs;
`ProjectJournal::mergeCheckpointsFrom(mark)` merges everything it pushed into ONE checkpoint (for
each object, its EARLIEST capture wins - that is the pre-command state; every recorded action is
kept in push order).

This is not belt-and-braces, it is the load-bearing part, and it was found by measurement rather
than assumed: **`AutomatableModel::setValue()` pushes a checkpoint of its own on every
non-automated write** (`src/core/AutomatableModel.cpp:303`). So a command that writes N models
leaves N checkpoints - `track.set_solo` writes the solo flag and every other track's mute - and the
explicit checkpoint the handler takes is buried UNDER them. Before the merge, one `control.undo`
after `track.set_solo` restored exactly one track's mute and left the rest of the action in place:
the contract test failed with all three tracks still muted. The merge is what makes the rule hold
for a command the author of the command never thought about.

**The partial case, made honest.** `track.set_solo` writes the solo flag *and* every other track's
mute (`Track::toggleSolo`, driven from the solo model's `dataChanged` on a display; called directly
headless). With the merge, ONE `control.undo` (or one Ctrl+Z - the same call) restores the whole
action. The test asserts on **every** track's mute+solo, not on the one the command named, and
proves the action changed something before it proves the undo restored it - otherwise the test
would pass on a no-op.

**What redo does at a step with no reverse.** An action step whose inverse action is unknown (the
creating call of `automation.add_point`, which would have to rebuild an AutomationTrack) empties the
redo stack instead of leaving an older entry in place: a redo that replays the wrong step is worse
than no redo. The transaction's mechanism says so for the command in question.

**The one deliberate asymmetry.** File-level commands (`project.save`) are NOT on the GUI stack: a
file is not project state, and reverting the user's file behind their back on a Ctrl+Z would be a
surprise. Their inverse is a COMMAND (`project.restore_revision`, `inverse.applies: "command"`),
which `control.undo` dispatches. So `control.undo` after an agent save restores the previous file
revision, while a GUI Ctrl+Z after the same save behaves exactly as it did before this change
(nothing about the file). This is a decision, recorded here, not an oversight.

---

## 5. File-level reversibility (deliverable 4)

`ProjectRevisions.{h,cpp}`, policy **`keep-3`**, applied by `project.save` before it writes:

```
<file>.rev0   the revision the last save replaced
<file>.rev1   the one before that
<file>.rev2   the one before that
```

* **disk bound, two-sided:** one revision is never larger than **8 MiB**
  (`ProjectRevisionPolicy::MaxRevisionBytes`); the set for a project is therefore bounded by
  **3 x 8 MiB = 24 MiB**. A project file over the per-revision cap is NOT copied - a truncated
  project file is a corrupt revision, which is worse than none - and the result reports
  `revision_kept: false` with `revision_skipped` naming the cap.
* the rotation shifts `rev(n-1) -> rev(n)`, stores the file about to be replaced as `rev0`, and
  trims anything past the three slots.
* **the restore path is a command of the surface:** `project.restore_revision {path?, revision}`
  (revision 0-2). It rotates the live file in as the new `rev0` first, so a restore is itself
  recoverable, and it records an action checkpoint whose undo restores what it replaced.
* the recoverable set is **reported, not assumed**: `project.get_state` and both save/restore
  results carry `revisions: {policy, revisions[], count, retained_bytes, max_revision_bytes,
  max_total_bytes}`.
* `control.undo` after `project.save` dispatches `project.restore_revision` (see section 4).

---

## 6. Irreversible-by-nature commands and their honest fallbacks (deliverable 6)

`control.undo` reads the LAST transaction and:

1. if there is none, unwinds the engine journal (the historical behaviour);
2. if the last recorded command is **not reversible**, FAILS with
   `error.kind = "irreversible"` - a new member of the closed error set in `ControlRegistry.h` -
   whose message names the command, its class, the engine's own reason and the documented
   fallback. **The journal is not touched**, so an irreversible command can never make
   `control.undo` silently unwind an older one;
3. otherwise undoes it - by dispatching the recorded inverse command when the inverse is a command,
   else by unwinding the engine's `ProjectJournal`.

The three irreversible commands and what an agent should do instead:

| command | fallback |
|---|---|
| `project.open` | reopen the file named in `before.previous_file` (the record also carries its sha256). UNSAVED changes to the displaced session are lost - save first if they matter. |
| `script.run` | the script takes its own checkpoint (Lua `addCheckPoint()`), which `control.undo` DOES replay. |
| `plugin.unload` | write `before.state_xml` to a file, `plugin.load` the same `dev-<n>` onto the same target, `plugin.state_load` that file. The chain ORDER is not restored. |

`plugin.state_save` / `plugin.preset_save` are ALSO refused an automatic undo (class `snapshot`,
`reversible: false`) - they write a file outside the project - and their fallback is to write
`before.previous_content` back with the same command.

---

## 7. Scope fences honoured

* **No audio-realtime change.** The transaction hook lives entirely in
  `ControlRegistry::invoke()` -> `runOnUiThread()`, i.e. the UI thread, and runs *after* the handler
  returns. The undo actions run from `ProjectJournal::undo()`, which the GUI drives from a menu slot
  (UI thread). Nothing in this change is reachable from the audio thread, and no path here allocates
  or locks in the audio callback. The audio engine's realtime paths are untouched.
* **No readiness/quit change (#626), no id change (#628), no shared vocabulary change.** The id
  grammar, `objectSchema` and the property helpers stay in `ControlVocabulary.{h,cpp}`; this change
  includes that header and adds no copy. The only `ControlRegistry.h` additions to the wire are the
  new error kind and the transaction record's `class`/`bytes` fields.
* `include/ProjectJournal.h` and `src/core/ProjectJournal.cpp` are UPSTREAM-INHERITED and are
  declared, with reasons, in `tests/upstream-modifications.txt`. The change is additive.

## 8. Honest limits (what this does NOT do)

1. **Undo depth is 100 steps** for both the model stack and the record; past it the oldest step is
   evicted and `capped: true` says so. The record does not survive a restart.
2. **`track.remove`'s inverse is bounded at 64 KiB of track XML.** A larger track refuses the
   inverse (typed, with the reason) rather than keeping a truncated one - its before-state names
   what was removed and the fallback is a project revision.
3. **`track.add`'s inverse does not rewind the project id counter.** Re-adding a track assigns a
   fresh `trk-<n>`; the removed id is not reused. Recorded in the transaction's mechanism.
4. **`track.set_solo` does not restore `Track::mutedBeforeSolo`** - it is a transient C++ field, not
   part of the project file, and it is re-derived on the next solo action.
5. **A revision restore does not reload the session.** `project.restore_revision` rewrites the file;
   the in-memory session is untouched until `project.open`. Said so in the command's description.
6. **`plugin.unload` cannot be replayed by the registry** even though its state XML is captured
   (see section 6). The typed refusal is the honest form of that fact.
7. **`plugin.load` of an INSTRUMENT is destructive** (the replaced instrument's values are lost) and
   is recorded `reversible: false` even though the command's declared class is `snapshot`.
8. **`mixer.remove_channel`, `automation.mode_set`, `track.set_arm` and `mixer.set_pan` were NOT
   exercised by the measured baseline** (that doc lists them as "not exercised"); their rows here
   are this lane's measurement, and the two refusal rows record the measured refusal text.
9. **A merged step is only as good as the window it merges.** If a command spawns work that runs
   outside its handler (a queued slot, a worker thread) and that work pushes checkpoints later,
   those land ABOVE the merged step and cost a second undo. Nothing in the current surface does
   that - every handler mutates synchronously - but it is a property to keep, not a guarantee the
   engine can enforce.
10. **The merged step is not persisted.** Close the app and the undo history is gone, as it was
    before this change.

## 9. Defects found while doing this (all measured, all fixed here)

1. **`Track::m_mutedBeforeSolo` was never initialised** and `saveTrack` writes it to the project
   file as an int. The SAME fresh track therefore saved as `mutedBeforeSolo=1` in one run and 48 in
   the next: `StableTrackIdsTest` failed intermittently (2 of 3 runs at one point) and the
   serializer's byte-determinism promise was broken. Fixed in `src/core/Track.cpp` (declared in
   `tests/upstream-modifications.txt`).
2. **`restoreProjectRevision` rotated the live file BEFORE reading the revision it was restoring**,
   so revision 0 after the rotation was the file the restore was meant to discard - the first
   version restored the wrong bytes. The contract test caught it (the byte comparison came back as
   the second save's content). Fixed by staging the revision out first.
3. **A captured track was being taken from the wrong element.** `DataFile`'s root is the project
   element, so `content().firstChildElement()` was `<head>`, not the `<track>`: the "restored" track
   was a phantom built from the head element. Fixed with an explicit snapshot root element.
4. **`control.undo` used to unwind an OLDER checkpoint when the last command had none of its own**,
   which is the pretending SPEC A16 forbids. It now refuses, typed, and the refusal names the
   fallback.
