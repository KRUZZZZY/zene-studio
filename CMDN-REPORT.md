# Lane report — `post-alpha/cmd-notes`: the notes / clips / tracks command group

Lane worktree: `$W/zene-pa-cmdnotes` (`$W = .../projects/lmms-fl-research`), branch
`post-alpha/cmd-notes`, base `post-alpha/agent-control-surface` @ **6b01b98eb** (verified with
`git rev-parse HEAD` before any edit).

This lane adds the piano-roll and song-editor half of `AGENT-TOOLING.md` §5's existing-capability
table (the `8 / 61` song-arrangement and `6 / 46` piano-roll cells of `AGENT-SURFACE-INVENTORY.md`
— the biggest practical gap an agent hits when it tries to *edit* music rather than read it).

## What was added

21 commands, all `group.verb`, all with an args schema, a result schema, a
`requires: []` declaration (headless-safe, SPEC A13), a handler that runs on the UI thread through
`ControlRegistry::invoke` → `runOnUiThread`, and stable ids (`trk-<n>`, `clip-<n>`, `note-<n>`)
accepted by every command that targets them and returned by both `get_state`s:

| group | commands |
|---|---|
| `note` | `add` `remove` `move` `resize` `velocity_set` `select` |
| `roll` | `get_state` (the piano-roll view of a clip: notes + the clip they belong to) |
| `clip` | `add` `move` `resize` `split` `delete` `duplicate` `select` |
| `track` | `add` `remove` `rename` `set_mute` `set_solo` `set_arm` |
| `arrangement` | `get_state` (every track with its clips and the stable ids) |

New files: `include/ControlEdit.h`, `src/core/ControlEditSupport.cpp`,
`src/core/ControlCommandsArrangement.cpp`, `src/core/ControlCommandsClip.cpp`,
`src/core/ControlCommandsNotes.cpp`, `tests/src/core/ControlEditCommandsTest.cpp`.
Modified: `include/ControlRegistry.h` + `src/core/ControlRegistry.cpp` (three new
`register*Commands` calls), `src/core/CMakeLists.txt` (source list), `tests/CMakeLists.txt`
(`ControlEditCommandsTest` + its offscreen environment), `tests/fork-sources.txt` (five new
entries appended under a labelled block), `tests/control-socket-integration.py` (the editing flow).

## Evidence

```
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON     -> CONFIGURE_EXIT=0   (73.9 s)
cmake --build build -j4                                        -> BUILD_EXIT=0       (0 errors)
cd build/tests && QT_QPA_PLATFORM=offscreen ctest              -> 100% tests passed, 0 tests failed out of 28   CTEST_EXIT=0
```

`ControlEditCommandsTest` (new, in-process, guiless) and `ControlSocketIntegration` (the existing
external-client test, extended) are both green. The external client drives a real `lmms` binary over
the AF_UNIX socket with the documented recipe (offscreen + `--config` with
`<audioengine audiodev="Dummy (no sound output)"/>` + `HOME`/`XDG_*` in a temp dir, polling
`control.ping` until `engine_ready`).

Raw transcript of the whole run, including the editing flow and the transaction list:
`CMDN-TRANSCRIPT.md` beside this file (produced by running the test script directly against
`build/lmms`; ctest's own copy only shows output on failure).

### The editing flow (verbatim request/response)

```
-> {"id":24,"cmd":"arrangement.get_state","args":{},"proto":1}
<- {"id":24,"ok":true,"result":{"clip_count":1,"clips":[{"auto_resize":true,"id":"clip-0","index_in_track":0,"length":1536,"muted":false,"name":"Beat/Baseline 0","note_count":null,"position":0,"selected":false,"track":"trk-0"}],"selected_clip":"","track_count":1,"tracks":[{"clip_count":1,"clips":["clip-0"],"id":"trk-0","index":0,"muted":false,"name":"Pattern 0","selected":false,"soloed":false,"type":"pattern"}]}}
-> {"id":25,"cmd":"roll.get_state","args":{"clip":"clip-0"},"proto":1}
<- {"error":{"kind":"refused","message":"clip-0 has no note list: it sits on a pattern track, and the piano roll edits MIDI clips (an instrument track's clip)"},"id":25,"ok":false}
```

(… the full sequence is in `CMDN-TRANSCRIPT.md`: `track.add` → `clip.add` → `control.undo`
(proving `clip.add` reverses) → `clip.add` again → `note.add` ×2 → `note.move` → `note.resize` →
`note.velocity_set` → `note.select` → `roll.get_state` read-back → `note.remove` → `control.undo` →
read-back → `clip.delete` → `control.undo` → read-back → `clip.move`/`resize`/`split`/`duplicate` →
typed errors → `track.remove` dry_run + real.)

### The transaction split (verbatim from `control.transactions`)

```
project.open           reversible=false snapshot only: no pre-load project snapshot is kept in this slice
mixer.add_channel      reversible=false snapshot only: ProjectJournal has no checkpoint for mixer channel creation
mixer.set_volume       reversible=true  ProjectJournal (MixerChannel volume model checkpoint)
project.save           reversible=false snapshot only: the engine keeps no previous file revision
track.add              reversible=false snapshot only: the product's own track add/remove path keeps no journal checkpoint (TrackContainerView::createTrackView / deleteTrackView have theirs commented out in this tree), so the ProjectJournal cannot create or destroy a track; the before-state is recorded so the change can be re-applied by hand
track.rename           reversible=true  ProjectJournal (Track checkpoint)
track.set_mute         reversible=true  ProjectJournal (Track mute BoolModel checkpoint)
track.set_solo         reversible=false partial: the ProjectJournal (Track checkpoint) reverses this track's solo flag, but the same action writes the mute state of every track (the solo model's dataChanged is connected to Track::toggleSolo by TrackView) and one checkpoint covers one object, so a single control.undo does not reverse the whole action; the before-state recorded here is a snapshot of every track
clip.add               reversible=true  ProjectJournal (Track checkpoint: Track::restoreState re-loads the track's serialized clips, which is how the GUI's own clip add/delete/split paths reverse themselves)
clip.select            reversible=false selection is control-surface state, not project state: the GUI keeps its selection in views (QGraphicsItem state) and the project file has no field for it, so the ProjectJournal has no checkpoint to reverse
note.add               reversible=true  ProjectJournal (MidiClip checkpoint: MidiClip::loadSettings clears and re-loads the clip's note list, which is the mechanism the piano roll's own note edits reverse with)
note.move              reversible=true  ProjectJournal (MidiClip checkpoint: ...)
note.resize            reversible=true  ProjectJournal (MidiClip checkpoint: ...)
note.velocity_set      reversible=true  ProjectJournal (MidiClip checkpoint: ...)
note.select            reversible=false selection is control-surface state, not project state: Note::setSelected is piano-roll view state and the project file has no field for it, so the ProjectJournal has no checkpoint to reverse
note.remove            reversible=true  ProjectJournal (MidiClip checkpoint: ...)
clip.delete            reversible=true  ProjectJournal (Track checkpoint: ...)
clip.move              reversible=true  ProjectJournal (Clip checkpoint: Clip::restoreState re-loads the clip's position/length)
clip.resize            reversible=true  ProjectJournal (Clip checkpoint: ...)
clip.split             reversible=true  ProjectJournal (Track checkpoint: ...)
clip.duplicate         reversible=true  ProjectJournal (Track checkpoint: ...)
track.remove (dry run) reversible=false dry_run preview: nothing was changed
track.remove           reversible=false snapshot only: ... the journal cannot create or destroy a track ...
```

Every `reversible=true` claim above is exercised by a real `control.undo` in the test, not asserted
from the mechanism string: undo after `track.set_mute`, after `note.remove`, after `clip.add`, after
`clip.delete`. 15 of the 21 new commands are reversible:true, 3 are reversible:false with a stated
reason (`track.add`, `track.set_solo`, `track.remove`), the two `*.select` commands are
reversible:false with a stated reason, and `track.set_arm` is a typed `refused` (nothing to record
because nothing changed — the same shape `mixer.set_pan` has).

### Which engine checkpoints exist — the honest split

The product's own GUI already checkpoints exactly these objects, and the commands reuse that:

| mechanism | where the product does it | commands here |
|---|---|---|
| `MidiClip::addJournalCheckPoint()` | `PianoRoll.cpp` (add/move/resize/velocity) | `note.add` `remove` `move` `resize` `velocity_set` |
| `Clip::addJournalCheckPoint()` | `ClipView.cpp` (clip move/resize) | `clip.move` `clip.resize` |
| `Track::addJournalCheckPoint()` | `TrackContentWidget.cpp` (create clip), `ClipView.cpp` (remove clip, split), `TrackOperationsWidget.cpp` (track ops) | `clip.add` `clip.delete` `clip.split` `clip.duplicate`, `track.rename`, `track.set_solo` (partial) |
| the track's mute `BoolModel` checkpoint | `mixer.set_volume`-style model checkpoint | `track.set_mute` |
| **nothing** | `TrackContainerView::createTrackView` / `deleteTrackView` both have their `addJournalCheckPoint()` **commented out**, and `TrackOperationsWidget::removeTrack` deletes the track with no checkpoint | `track.add`, `track.remove` |

## Findings worth keeping

1. **`track.remove` crashed the offscreen instance** (`Connection reset by peer`, SIGSEGV right
   after the command answered). Cause: the product removes a track through its *view*
   (`TrackContainerView::deleteTrackView` removes the view, deletes it, then deletes the track);
   deleting the `Track` directly leaves the `TrackView` pointing at freed memory. The command now
   takes the product's order: when a display is present it calls
   `getGUI()->songEditor()->m_editor->deleteTrackView(view)` for the track's view, and only falls
   back to `delete track` when there is no view (guiless). This is the second "the model is not the
   whole story" trap this lane hit; the first is below.
2. **`set_solo` is not a one-bit write.** `TrackView` connects the solo model's `dataChanged` to
   `Track::toggleSolo()`, which unmutes the soloed track and mutes every other track. So a
   programmatic `setSolo()` in a display-ful instance performs the *whole* solo action. The command
   therefore (a) reports the mute/solo state of every track in its result, (b) records a
   cross-track snapshot and marks itself `reversible:false` with the reason, and (c) calls
   `toggleSolo()` itself only when `gui::getGUI() == nullptr`, so a guiless run does the same thing
   a display-ful one does (calling it in both would apply it twice and corrupt
   `m_mutedBeforeSolo`). The unit test asserts the headless case, the external-client test the
   display case.
3. **`track.set_arm` has no engine path.** Record arm in this tree lives on the prototype
   `MultiTrackRecorder` (`AudioEngine::recorder()`, two capture streams keyed by input channel,
   task #556), not on `lmms::Track`. The command is registered with full schemas and returns a
   typed `refused` naming that, rather than inventing a per-track flag (and a project-format
   change) to look complete.
4. **`clip-<n>` is one ordinal space over the whole arrangement** (tracks in song order, clips
   inside a track sorted by start position), so a clip id needs no track qualifier. Like the mixer
   lane's `ch-<n>` these ids are index-derived: they are stable for the state a `get_state` returned
   them for, but they are **not persisted** — the project file has no id attribute for a clip or a
   note. `note-<n>` is the index in the clip's position-sorted note list, so `note.move` can
   renumber other notes; its result reports the note's new id for exactly that reason.
5. **A fresh instrument track has no instrument** (`InstrumentTrack` is constructed with
   `m_instrument == nullptr`), so the arrangement an agent builds is real, serialized model state
   but the track renders silence until something loads an instrument. `plugin.load` is the plugins
   lane's command group, not this one — stated rather than papered over.
6. The fixture's single song clip is a `PatternClip` (pattern track), which has no note list:
   `roll.get_state` and `note.*` refuse it by type with a message naming the track type. The
   PatternStore's pattern columns are **not** addressable yet (`clip-<n>` enumerates the song's
   clips only); a pattern-editor clip is a separate id space a later lane can add.

## Governance notes

- `tests/fork-sources.txt` is an append-only ledger shared with three sibling lanes; this lane only
  appended a labelled block at the end (5 entries). If this branch is merged with a conflicting
  version of that file, **take a union of entries, never a line union** — no structural token was
  removed or reordered here.
- No upstream-inherited production file was touched (build files are exempt by Gate 6): the four new
  `src/core/Control*` files and the header are fork-NEW, and `include/ControlRegistry.h` /
  `src/core/ControlRegistry.cpp` are already fork-NEW. `tests/upstream-modifications.txt` needs no
  new entry.
- Every new source is under 500 lines (Gate 7) and every function is small (Gate 4's CCN ≤ 10
  target): `ControlEditSupport.cpp` 375, `ControlCommandsArrangement.cpp` 462,
  `ControlCommandsClip.cpp` 459, `ControlCommandsNotes.cpp` 475, `include/ControlEdit.h` 134.

## Final state

```
worktree      : $W/zene-pa-cmdnotes   (branch post-alpha/cmd-notes, base 6b01b98eb)
commit        : the branch head; message "feat(control): notes/clips/tracks command group for the
                agent surface (SPEC A11-A16)" (read the hash with `git log --oneline -3`)
configure     : cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON            -> CONFIGURE_EXIT=0
build         : cmake --build build -j4                                                -> BUILD_EXIT=0 (0 errors)
ctest         : cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure   -> 100% tests passed, 0 tests failed out of 28   (CTEST_EXIT=0)
gates         : no-upstream-regression-gate.sh -> PASS (0)   file-length-gate.sh --check -> PASS (0)
                complexity-gate.sh --check     -> PASS (0)   duplication-gate.sh --check -> PASS, 1.39% (budget 5%)
```

Nothing was pushed. The tree contains `build/` (git-ignored) and is left ready for
`git log --oneline -3` / `ctest` reproduction from the paths above.
