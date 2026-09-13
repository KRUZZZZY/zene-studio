<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-13).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, AGENT-SURFACE-INVENTORY.md
    sha256   : 319c6350edbf2b49b8e24ca6f0c3d5aa8a67ba322214015fc3f24d3b620a049d
    bytes    : 78465
    why this file: DOC-5 names it; cited by docs/reports/CMDN-REPORT.md, and it is the surface inventory the command groups were built from
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# Zene Studio 0.1.0-alpha — agent-surface inventory

| | |
|---|---|
| Product | Zene Studio 0.1.0-alpha (LMMS fork), repo `KRUZZZZY/zene-studio` |
| Tree inventoried | `projects/lmms-fl-research/zene-remote`, branch `part-c/remote-plugin-slice`, HEAD `2f0cd8689` (the release commit `0c23587d2` plus one docs/CI commit) |
| Date | 2026-09-11 |
| Scope | every user-facing capability with a code anchor in the tree, and every boarded/planned capability in `docs/STATUS.md`, `../ableton-gap/SPEC-zene-studio.md`, `../ableton-gap/PLAN-zene-studio.md` and `../BACKLOG.md` |
| Method | `search_files` / `read_file` over the tree; every row's anchor was machine-checked to resolve to a real file and an in-range line — 441 anchors, 117 distinct files, 0 unresolved |

**The one-line summary.** Zene Studio has three agent-reachable surfaces today: the LMMS CLI actions (`render`, `rendertracks`, `dump`, `compress`, `upgrade`, `makebundle`, `--run-script`, `--import`, `--geometry`, `--config`, `--help`, `--version`), the in-tree `mmpz-git` CLI, and Lua API v0 driven by `--run-script` (`src/core/ScriptBindings.cpp:332-513`). A fourth surface — the `lmms-lab` MCP server — is real but lives outside the app (`mcp-lmms-lab/`), and it can build, test, render, measure and inspect plugins, not edit a session. **No in-app command registry exists**: nothing lets an agent drive or read a *running* DAW, and the mixer, piano roll, plugins, automation, recording and settings surfaces are reachable only through Qt widgets. That is exactly the gap SPEC §7 (A11–A15) is written to close.

## How to read this file

- **Agent-callable today?** is one of `CLI action: <name>`, `Lua binding: <path>`, `MCP tool: <name>`, or `NONE`.
- A capability counts as agent-callable **only** if at least one of those three routes exists today; 'the C++ API exists' and 'a menu item exists' are both `NONE`.
- **Smallest honest agent tool** is one call signature — the minimum that would make that single row drivable. It is not a design; it is a sizing.
- Anchors are `path:line` into `zene-remote/` unless the path starts with `zene-remote/` or `ableton-gap/`, or is a bare `BACKLOG.md`; those are relative to `projects/lmms-fl-research/`.
- Lua API v0 is the only in-app automation surface, and it is deliberately narrow: it binds `Song`, `PatternStore`, `PatternClip`, `Transport`, `Track`, `InstrumentTrack`, `Instrument`, `FloatModel`, `BoolModel`, `Note`, `MidiIn`, `MidiOut`, `ProjectFile` and `LuaLog` — **no mixer channel, effect chain, plugin, send, PDC, automation clip, controller or settings object.** Its clip/note surface is keyed `(patternIndex, trackIndex)` in the `PatternStore` (`src/core/ScriptBindings.cpp:978`), so Song-Editor MIDI clips are not reachable from Lua either.

## Group 1. Transport and playback

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| Start / stop playback (Space) | `src/gui/editors/Editor.cpp:71` | Lua binding: lmms.transport():play() / stop() | — (exists: `lmms.transport():play()`/`stop()`) | now — `docs/STATUS.md` 'Have' |
| Play button action | `src/gui/editors/Editor.cpp:108` | Lua binding: lmms.transport():play() | — (exists) | now |
| Stop button action | `src/gui/editors/Editor.cpp:109` | Lua binding: lmms.transport():stop() | — (exists) | now |
| Pause / unpause toggle | `src/gui/editors/Editor.cpp:80` | NONE | `lmms.transport.pause()` | now (GUI-only) |
| Read playback position | `src/core/ScriptBindings.cpp:455` | Lua binding: lmms.transport():position() | — (exists) | now |
| Query whether transport is rolling | `src/core/ScriptBindings.cpp:454` | Lua binding: lmms.transport():isPlaying() | — (exists) | now |
| Song-level play | `include/Song.h:330` | Lua binding: lmms.transport():play() | — (exists) | now |
| Song-level stop | `include/Song.h:336` | Lua binding: lmms.transport():stop() | — (exists) | now |
| Set playback position in ticks | `include/Song.h:216` | NONE | `lmms.transport.setPosition(ticks)` | now (GUI-only: timeline click) |
| Jump playhead by clicking the timeline | `src/gui/editors/TimeLineWidget.cpp:279` | NONE | `lmms.transport.setPosition(ticks)` | now (GUI-only) |
| Preview one pattern | `include/Song.h:333` | NONE | `lmms.transport.playPattern(patternIndex)` | now (GUI-only) |
| Audition a single MIDI clip (looped) | `include/Song.h:334` | NONE | `lmms.transport.previewClip(clipId, loop)` | now (GUI-only) |
| Set tempo (BPM) | `src/gui/editors/SongEditor.cpp:138` | Lua binding: lmms.song():setTempo(bpm) | — (exists) | now |
| Read tempo (BPM) | `src/core/ScriptBindings.cpp:475` | Lua binding: lmms.song():tempo() | — (exists) | now |
| Set time signature | `src/gui/editors/SongEditor.cpp:149` | NONE | `lmms.song.setTimeSignature(numerator, denominator)` | now (GUI-only); tempo automation/time-sig changes also listed as a product-side gap, `docs/STATUS.md:248` |
| Set master volume | `src/gui/editors/SongEditor.cpp:168` | Lua binding: lmms.song():setMasterVolume(v) | — (exists) | now |
| Set master pitch (whole-song detune in semitones) | `src/gui/editors/SongEditor.cpp:199` | NONE | `lmms.song.setMasterPitch(semitones)` | now (GUI-only) |
| Enable / disable the loop region | `src/gui/editors/TimeLineWidget.cpp:93` | NONE | `lmms.transport.setLoopEnabled(on)` | now (GUI-only) |
| Drag / resize the loop region | `src/gui/editors/TimeLineWidget.cpp:233` | NONE | `lmms.transport.setLoopRegion(startTick, endTick)` | now (GUI-only) |
| 'Set loop begin here' (timeline context menu) | `src/gui/editors/TimeLineWidget.cpp:416` | NONE | `lmms.transport.setLoopBegin(tick)` | now (GUI-only) |
| 'Set loop end here' (timeline context menu) | `src/gui/editors/TimeLineWidget.cpp:424` | NONE | `lmms.transport.setLoopEnd(tick)` | now (GUI-only) |
| Loop edit-mode preference (dual-button / closest / handles) | `src/gui/editors/TimeLineWidget.cpp:435` | NONE | `lmms.settings.set("loop_edit_mode", mode)` | now (GUI-only) |
| Render as loop (`--loop`) | `src/core/main.cpp:496` | CLI action: render -l / --loop | — (exists) | now |
| Metronome on / off | `src/gui/MainWindow.cpp:1221` | NONE | `lmms.transport.setMetronome(on)` | now (GUI-only) |
| Playback auto-scroll (follow the playhead) | `src/gui/editors/TimeLineWidget.cpp:128` | NONE | `lmms.transport.setFollowPlayhead(on)` | now (GUI-only) |
| Position / time read-out (bars:beats:ticks) | `src/gui/editors/SongEditor.cpp:145` | Lua binding: lmms.transport():position() | — (exists: numeric position) | now |
| Switch time display between bars and min:sec | `src/gui/widgets/TimeDisplayWidget.cpp:62` | NONE | `lmms.settings.set("time_display_mode", mode)` | now (GUI-only) |

## Group 2. Song / arrangement editing (clips, tracks, timeline)

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| Add a pattern track | `src/gui/editors/SongEditor.cpp:945` | NONE | `lmms.track.addPatternTrack()` | now (GUI-only) |
| Add a sample track | `src/gui/editors/SongEditor.cpp:948` | NONE | `lmms.track.addSampleTrack()` | now (GUI-only) |
| Add an automation track | `src/gui/editors/SongEditor.cpp:951` | NONE | `lmms.track.addAutomationTrack()` | now (GUI-only; `Song::addAutomationTrack` exists, `include/Song.h:353`) |
| Draw mode | `src/gui/editors/SongEditor.cpp:967` | NONE | `lmms.song.setEditMode("draw")` | now (GUI-only) |
| Knife mode (split clips) | `src/gui/editors/SongEditor.cpp:968` | NONE | `lmms.clip.split(clipId, tick)` | now (GUI-only) |
| Select / move mode | `src/gui/editors/SongEditor.cpp:969` | NONE | `lmms.song.setEditMode("select")` | now (GUI-only) |
| Insert a bar at the playhead | `src/gui/editors/SongEditor.cpp:984` | NONE | `lmms.song.insertBar(tick)` | now (GUI-only) |
| Remove a bar at the playhead | `src/gui/editors/SongEditor.cpp:985` | NONE | `lmms.song.removeBar(tick)` | now (GUI-only) |
| Proportional snap toggle | `src/gui/editors/SongEditor.cpp:1021` | NONE | `lmms.song.setProportionalSnap(on)` | now (GUI-only) |
| Snap / grid granularity (1/16 bar … 8 bars) | `src/gui/editors/SongEditor.cpp:261` | NONE | `lmms.song.setSnap("1/4 Bar")` | now (GUI-only) |
| Horizontal zoom | `src/gui/editors/SongEditor.cpp:850` | NONE | `lmms.song.setZoom(pixelsPerBar)` | now (GUI-only) |
| Rubberband region select across clips | `src/gui/editors/SongEditor.cpp:345` | NONE | `lmms.clip.selectRegion(trackA, trackB, tickA, tickB)` | now (GUI-only) |
| Select all clips | `src/gui/editors/SongEditor.cpp:864` | NONE | `lmms.clip.selectAll()` | now (GUI-only) |
| Clip context menu | `src/gui/clips/ClipView.cpp:1070` | NONE | `lmms.clip.describe(clipId)` | now (GUI-only) |
| Remove clip | `src/gui/clips/ClipView.cpp:1087` | NONE | `lmms.clip.remove(clipId)` | now (GUI-only) |
| Cut clip | `src/gui/clips/ClipView.cpp:1096` | NONE | `lmms.clip.cut(clipIds)` | now (GUI-only) |
| Copy clip | `src/gui/clips/ClipView.cpp:1104` | NONE | `lmms.clip.copy(clipIds)` | now (GUI-only) |
| Paste clip | `src/gui/clips/ClipView.cpp:1111` | NONE | `lmms.clip.paste(track, tick)` | now (GUI-only) |
| Mute clip | `src/gui/clips/ClipView.cpp:1124` | NONE | `lmms.clip.setMute(clipId, on)` | now (GUI-only) |
| Clip colour: change / reset / pick random | `src/gui/clips/ClipView.cpp:1135` | NONE | `lmms.clip.setColor(clipId, hex)` | now (GUI-only; random at `:1137`) |
| Enable / disable clip auto-resize | `src/gui/clips/ClipView.cpp:1140` | NONE | `lmms.clip.setAutoResize(clipId, on)` | now (GUI-only) |
| Undo | `src/gui/MainWindow.cpp:337` | NONE | `lmms.undo()` | now (GUI-only) |
| Redo | `src/gui/MainWindow.cpp:340` | NONE | `lmms.redo()` | now (GUI-only); undo depth/drag coalescing flagged as a product-side gap, `docs/STATUS.md:248` |
| Track operations menu | `src/gui/tracks/TrackOperationsWidget.cpp:271` | NONE | `lmms.track.describe(index)` | now (GUI-only) |
| Clone track | `src/gui/tracks/TrackOperationsWidget.cpp:175` | NONE | `lmms.track.clone(index)` | now (GUI-only) |
| Clear a track's content | `src/gui/tracks/TrackOperationsWidget.cpp:199` | NONE | `lmms.track.clear(index)` | now (GUI-only) |
| Remove track | `src/gui/tracks/TrackOperationsWidget.cpp:212` | NONE | `lmms.track.remove(index)` | now (GUI-only) |
| Track colour: change / reset / pick random | `src/gui/tracks/TrackOperationsWidget.cpp:220` | NONE | `lmms.track.setColor(index, hex)` | now (GUI-only; reset `:234`, random `:242`) |
| Reset clip colours on a track | `src/gui/tracks/TrackOperationsWidget.cpp:251` | NONE | `lmms.track.resetClipColors(index)` | now (GUI-only) |
| Assign a track to a mixer channel | `src/gui/tracks/TrackOperationsWidget.cpp:286` | NONE | `lmms.mixer.assignTrack(trackIndex, channel)` | now (GUI-only) |
| Per-track MIDI input / output port menu | `src/gui/tracks/TrackOperationsWidget.cpp:294` | NONE | `lmms.track.setMidiPorts(index, in, out)` | now (GUI-only) |
| Turn all recording on / off for a track | `src/gui/tracks/TrackOperationsWidget.cpp:298` | NONE | `lmms.record.setArm(trackIndex, on)` | now (GUI-only) |
| Open a MIDI clip in the piano roll | `src/gui/clips/MidiClipView.cpp:199` | NONE | `lmms.edit.openInPianoRoll(clipId)` | now (GUI-only) |
| Set a MIDI clip as the piano-roll ghost clip | `src/gui/clips/MidiClipView.cpp:204` | NONE | `lmms.edit.setGhostClip(clipId, "pianoroll")` | now (GUI-only) |
| Set a MIDI clip as the automation-editor ghost clip | `src/gui/clips/MidiClipView.cpp:210` | NONE | `lmms.edit.setGhostClip(clipId, "automation")` | now (GUI-only) |
| Clear all notes in a MIDI clip | `src/gui/clips/MidiClipView.cpp:218` | Lua binding: lmms.patternClip:clearNotes() (pattern clips only) | — (exists for pattern clips) | now |
| Merge selected clips | `src/gui/clips/MidiClipView.cpp:223` | NONE | `lmms.clip.merge(clipIds)` | now (GUI-only) |
| Clear notes outside the clip bounds | `src/gui/clips/MidiClipView.cpp:230` | NONE | `lmms.clip.clearNotesOutOfBounds(clipId)` | now (GUI-only) |
| Transpose a MIDI clip's selection | `src/gui/clips/MidiClipView.cpp:234` | NONE | `lmms.clip.transpose(clipId, semitones)` | now (GUI-only) |
| Rename a MIDI clip / reset its name | `src/gui/clips/MidiClipView.cpp:240` | Lua binding: lmms.patternClip:setName(name) (pattern clips only) | — (exists for pattern clips) | now |
| Beat clip: add steps | `src/gui/clips/MidiClipView.cpp:248` | NONE | `lmms.clip.addSteps(clipId, n)` | now (GUI-only) |
| Beat clip: remove steps | `src/gui/clips/MidiClipView.cpp:250` | NONE | `lmms.clip.removeSteps(clipId, n)` | now (GUI-only) |
| Beat clip: clone steps | `src/gui/clips/MidiClipView.cpp:252` | NONE | `lmms.clip.cloneSteps(clipId, n)` | now (GUI-only) |
| Pattern clip: open in pattern editor | `src/gui/clips/PatternClipView.cpp:65` | NONE | `lmms.edit.openInPatternEditor(clipId)` | now (GUI-only) |
| Pattern clip: rename / reset name | `src/gui/clips/PatternClipView.cpp:73` | Lua binding: lmms.patternClip:setName(name) | — (exists) | now |
| Automation clip: open in automation editor | `src/gui/clips/AutomationClipView.cpp:163` | NONE | `lmms.edit.openInAutomationEditor(clipId)` | now (GUI-only) |
| Automation clip: clear | `src/gui/clips/AutomationClipView.cpp:170` | NONE | `lmms.automation.clear(clipId)` | now (GUI-only) |
| Automation clip: set / clear recording | `src/gui/clips/AutomationClipView.cpp:179` | NONE | `lmms.automation.setRecording(clipId, on)` | now (GUI-only) |
| Automation clip: flip curve Y (values) | `src/gui/clips/AutomationClipView.cpp:182` | NONE | `lmms.automation.flipY(clipId)` | now (GUI-only; core op `src/core/AutomationClip.cpp:677`) |
| Automation clip: flip curve X (time) | `src/gui/clips/AutomationClipView.cpp:185` | NONE | `lmms.automation.flipX(clipId)` | now (GUI-only) |
| Automation clip: disconnect a controlled model | `src/gui/clips/AutomationClipView.cpp:197` | NONE | `lmms.automation.disconnect(clipId, modelId)` | now (GUI-only) |
| Sample clip: reverse the sample | `src/gui/clips/SampleClipView.cpp:94` | NONE | `lmms.sample.reverse(clipId)` | now (GUI-only) |
| Sample clip: set as automation-editor ghost | `src/gui/clips/SampleClipView.cpp:101` | NONE | `lmms.edit.setGhostClip(clipId, "automation")` | now (GUI-only) |
| Add a note to a pattern clip (headless) | `src/core/ScriptBindings.cpp:446` | Lua binding: lmms.patternClip:addNote(note) | — (exists) | now — note: `LuaPatternClip` is keyed `(patternIndex, trackIndex)` in the PatternStore (`src/core/ScriptBindings.cpp:978`), so Song-Editor MIDI clips are NOT reachable from Lua |
| Add a note at an explicit position (headless) | `src/core/ScriptBindings.cpp:447` | Lua binding: lmms.patternClip:addNoteAt(key,pos,len,vol) | — (exists) | now |
| Remove one note by index (headless) | `src/core/ScriptBindings.cpp:448` | Lua binding: lmms.patternClip:removeNote(index) | — (exists) | now |
| Create a new pattern clip (headless) | `src/core/ScriptBindings.cpp:466` | Lua binding: lmms.patternStore():addPattern() | — (exists) | now |
| Add an instrument track (headless) | `src/core/ScriptBindings.cpp:464` | Lua binding: lmms.patternStore():addInstrumentTrack() | — (exists) | now |
| Pattern editor: new pattern / clone pattern | `src/gui/editors/PatternEditor.cpp:319` | NONE | `lmms.pattern.add()` (clone at `:321`) | now (GUI-only) |
| Pattern editor: add / remove / clone steps | `src/gui/editors/PatternEditor.cpp:336` | NONE | `lmms.pattern.setStepCount(n)` | now (GUI-only; `:334` remove, `:338` clone) |
| Pattern editor: step to next / previous pattern | `src/gui/editors/PatternEditor.cpp:345` | NONE | `lmms.pattern.select(index)` | now (GUI-only; previous at `:350`) |

## Group 3. Piano roll / note editing

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| Draw mode | `src/gui/editors/PianoRoll.cpp:5230` | NONE | `lmms.pianoroll.setEditMode("draw")` | now (GUI-only) |
| Erase mode | `src/gui/editors/PianoRoll.cpp:5231` | NONE | `lmms.pianoroll.setEditMode("erase")` | now (GUI-only) |
| Select mode | `src/gui/editors/PianoRoll.cpp:5232` | NONE | `lmms.pianoroll.setEditMode("select")` | now (GUI-only) |
| Pitch-bend / note expression draw mode | `src/gui/editors/PianoRoll.cpp:5233` | NONE | `lmms.pianoroll.setEditMode("pitchbend")` | now (GUI-only) |
| Note edit parameter (volume / panning / pitch) | `src/gui/editors/PianoRoll.cpp:234` | NONE | `lmms.note.setParam(noteId, "volume", v)` | now (GUI-only) |
| Quantize notes | `src/gui/editors/PianoRoll.cpp:5248` | NONE | `lmms.clip.quantize(clipId, mode)` | now (GUI-only; core op `:5038`) |
| Quantize note positions | `src/gui/editors/PianoRoll.cpp:5249` | NONE | `lmms.clip.quantize(clipId, "positions")` | now (GUI-only) |
| Quantize note lengths | `src/gui/editors/PianoRoll.cpp:5250` | NONE | `lmms.clip.quantize(clipId, "lengths")` | now (GUI-only) |
| Import a MIDI clip into the piano roll | `src/gui/editors/PianoRoll.cpp:5278` | NONE | `lmms.clip.importMidi(clipId, path)` | now (GUI-only) |
| Export a piano-roll clip to MIDI | `src/gui/editors/PianoRoll.cpp:5280` | NONE | `lmms.clip.exportMidi(clipId, path)` | now (GUI-only) |
| Cut selected notes | `src/gui/editors/PianoRoll.cpp:5293` | NONE | `lmms.note.cut(selection)` | now (GUI-only; impl `:4774`) |
| Copy selected notes | `src/gui/editors/PianoRoll.cpp:5295` | NONE | `lmms.note.copy(selection)` | now (GUI-only; impl `:4761`) |
| Paste notes | `src/gui/editors/PianoRoll.cpp:5297` | NONE | `lmms.note.paste(clipId, tick)` | now (GUI-only; impl `:4806`) |
| Glue notes | `src/gui/editors/PianoRoll.cpp:5320` | NONE | `lmms.clip.glueNotes(clipId)` | now (GUI-only; impl `:658`) |
| Knife tool (split notes at a position) | `src/gui/editors/PianoRoll.cpp:5324` | NONE | `lmms.note.knife(clipId, tick)` | now (GUI-only) |
| Strum (spread chord note starts) | `src/gui/editors/PianoRoll.cpp:5328` | NONE | `lmms.note.strum(selection, ticks)` | now (GUI-only; impl `:3020`) |
| Slide notes | `src/gui/editors/PianoRoll.cpp:5332` | NONE | `lmms.note.slide(noteIds)` | now (GUI-only; tests `tests/src/core/SlideNotesTest.cpp`) |
| Fill (legato-fill note gaps) | `src/gui/editors/PianoRoll.cpp:5338` | NONE | `lmms.note.fill(clipId)` | now (GUI-only; impl `:720`) |
| Cut overlaps | `src/gui/editors/PianoRoll.cpp:5342` | NONE | `lmms.note.cutOverlaps(clipId)` | now (GUI-only) |
| Normalise lengths to the last note (min / max) | `src/gui/editors/PianoRoll.cpp:5346` | NONE | `lmms.note.normaliseLength(clipId, "min")` | now (GUI-only; max at `:5349`, impl `:775`) |
| Reverse notes | `src/gui/editors/PianoRoll.cpp:5352` | NONE | `lmms.clip.reverseNotes(clipId)` | now (GUI-only; impl `:797`) |
| Mark / unmark the current semitone | `src/gui/editors/PianoRoll.cpp:241` | NONE | `lmms.pianoroll.markSemitone(key)` | now (GUI-only) |
| Mark all corresponding octave semitones | `src/gui/editors/PianoRoll.cpp:242` | NONE | `lmms.pianoroll.markOctaveSemitones(key)` | now (GUI-only) |
| Mark the current scale | `src/gui/editors/PianoRoll.cpp:243` | NONE | `lmms.pianoroll.markScale(name)` | now (GUI-only) |
| Mark the current chord | `src/gui/editors/PianoRoll.cpp:244` | NONE | `lmms.pianoroll.markChord(name)` | now (GUI-only) |
| Unmark all semitones | `src/gui/editors/PianoRoll.cpp:245` | NONE | `lmms.pianoroll.unmarkAll()` | now (GUI-only) |
| Select all notes on one key | `src/gui/editors/PianoRoll.cpp:246` | NONE | `lmms.note.selectOnKey(clipId, key)` | now (GUI-only; impl `:4662`) |
| Select all notes | `src/gui/editors/PianoRoll.cpp:4590` | NONE | `lmms.note.selectAll(clipId)` | now (GUI-only) |
| Delete selected notes | `src/gui/editors/PianoRoll.cpp:4858` | NONE | `lmms.note.remove(noteIds)` | now (GUI-only) |
| Shift selected notes by semitones | `src/gui/editors/PianoRoll.cpp:1224` | NONE | `lmms.note.transpose(noteIds, semitones)` | now (GUI-only) |
| Shift selected notes in time | `src/gui/editors/PianoRoll.cpp:1249` | NONE | `lmms.note.move(noteIds, ticks)` | now (GUI-only) |
| Step recording | `src/gui/editors/PianoRoll.cpp:4432` | NONE | `lmms.record.setStepMode(on)` | now (GUI-only) |
| Record MIDI notes into a clip | `src/gui/editors/PianoRoll.cpp:4377` | NONE | `lmms.record.start(target)` | now (GUI-only) |
| Record while playing (accompany) | `src/gui/editors/PianoRoll.cpp:4400` | NONE | `lmms.record.start(target, {accompany:true})` | now (GUI-only) |
| Audition a note from the editor | `src/gui/editors/PianoRoll.cpp:2147` | Lua binding: lmms.midiOut():noteOn()/noteOff() | — (exists, routed to the engine) | now |
| Play a note by clicking the on-screen keys | `src/gui/editors/PianoRoll.cpp:2288` | Lua binding: lmms.midiOut():noteOn() | — (exists) | now |
| Play a chord from the chord selector | `src/gui/editors/PianoRoll.cpp:2197` | NONE | `lmms.midiOut.playChord(chord, velocity)` | now (GUI-only) |
| Set a ghost MIDI clip behind the piano roll | `src/gui/editors/PianoRoll.cpp:615` | NONE | `lmms.edit.setGhostClip(clipId)` | now (GUI-only; clear at `:650`) |
| Horizontal zoom | `src/gui/editors/PianoRoll.cpp:4977` | NONE | `lmms.pianoroll.setZoom(x)` | now (GUI-only; vertical at `:4992`) |
| Cycle snap mode | `src/gui/editors/PianoRoll.cpp:5205` | NONE | `lmms.pianoroll.setSnap(mode)` | now (GUI-only) |
| Set note length preset | `src/gui/editors/PianoRoll.cpp:5009` | NONE | `lmms.pianoroll.setNoteLength(ticks)` | now (GUI-only) |
| Set the quantize grid value | `src/gui/editors/PianoRoll.cpp:5004` | NONE | `lmms.pianoroll.setQuantize(value)` | now (GUI-only) |
| Read a note back (key/pos/length/volume/pan) | `src/core/ScriptBindings.cpp:445` | Lua binding: lmms.patternClip:note(i) | — (exists) | now |
| Set note key / position / length / volume / panning (headless) | `src/core/ScriptBindings.cpp:358` | Lua binding: lmms.Note:setKey/setPos/setLength/setVolume/setPanning | — (exists) | now |
| Set a pattern clip's length in ticks (headless) | `src/core/ScriptBindings.cpp:443` | Lua binding: lmms.patternClip:setLengthTicks(ticks) | — (exists) | now |
| Count notes in a pattern clip (headless) | `src/core/ScriptBindings.cpp:444` | Lua binding: lmms.patternClip:noteCount() | — (exists) | now |

## Group 4. Mixer, buses, routing, PDC

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| Create a mixer channel | `src/gui/MixerView.cpp:180` | NONE | `lmms.mixer.addChannel()` | now — `docs/STATUS.md` 'Have' (unbounded mixer) |
| Delete a mixer channel | `src/gui/MixerView.cpp:379` | NONE | `lmms.mixer.deleteChannel(index)` | now (GUI-only) |
| Remove all unused mixer channels | `src/gui/MixerView.cpp:422` | NONE | `lmms.mixer.deleteUnusedChannels()` | now (GUI-only) |
| Reorder a mixer channel left / right | `src/gui/MixerView.cpp:436` | NONE | `lmms.mixer.moveChannel(index, to)` | now (GUI-only) |
| Rename a mixer channel | `src/gui/MixerChannelView.cpp:256` | NONE | `lmms.mixer.setChannelName(index, name)` | now (GUI-only) |
| Mute a mixer channel | `src/gui/MixerChannelView.cpp:122` | NONE | `lmms.mixer.setChannelMute(index, on)` | now (GUI-only) |
| Solo a mixer channel | `src/core/Mixer.cpp:602` | NONE | `lmms.mixer.setChannelSolo(index, on)` | now (GUI-only) |
| Set a channel fader volume | `src/gui/MixerChannelView.cpp:138` | NONE | `lmms.mixer.setChannelVolume(index, value)` | now (GUI-only) |
| Set a channel colour | `src/gui/MixerChannelView.cpp:190` | NONE | `lmms.mixer.setChannelColor(index, hex)` | now (GUI-only) |
| Read channel peak / fader meters | `src/gui/MixerView.cpp:558` | NONE | `lmms.mixer.getChannelPeak(index)` | now (GUI-only) |
| Toggle a channel send (send / receive button) | `src/gui/SendButtonIndicator.cpp:22` | NONE | `lmms.mixer.setSend(from, to, on)` | now (GUI-only) |
| Create or update a channel send | `src/core/Mixer.cpp:816` | NONE | `lmms.mixer.setSend(from, to, amount)` | now (GUI-only) |
| Delete a channel send | `src/core/Mixer.cpp:999` | NONE | `lmms.mixer.deleteSend(from, to)` | now (GUI-only) |
| Pre-fader send flag | `include/Mixer.h:388` | NONE | `lmms.mixer.setSendPreFader(from, to, on)` | now — Phase D #587 |
| Refuse a send that would close a feedback loop | `src/core/Mixer.cpp:1041` | NONE | `lmms.mixer.canSend(from, to)` | now (GUI-only) |
| Sidechain send (audio never mixes into the receiver) | `src/core/Mixer.cpp:891` | NONE | `lmms.mixer.setSidechainSend(from, to, mode)` | now — Phase D #587 |
| Sidechain tap-point mode (PostFader / PreFx / PreFader / PostFaderNoGain) | `include/Mixer.h:53` | NONE | `lmms.mixer.setSidechainSend(from, to, mode)` | now — Phase D #587 |
| Refuse a sidechain-only cycle | `src/core/Mixer.cpp:1093` | NONE | `lmms.mixer.canSend(from, to)` | now — Phase D #587 |
| Create a parallel bus channel | `src/core/Mixer.cpp:868` | NONE | `lmms.mixer.createBus()` | now — Phase D #587 |
| Assign a track to a mixer channel | `src/gui/tracks/InstrumentTrackView.cpp:244` | NONE | `lmms.mixer.assignTrack(track, channel)` | now (GUI-only) |
| Create a mixer channel for a track from its menu | `src/gui/tracks/InstrumentTrackView.cpp:229` | NONE | `lmms.mixer.addChannel(track)` | now (GUI-only) |
| Add an effect to a channel's chain | `src/gui/EffectRackView.cpp:237` | NONE | `lmms.mixer.addEffect(channel, plugin)` | now (GUI-only) |
| Remove an effect from a chain | `src/gui/EffectRackView.cpp:143` | NONE | `lmms.mixer.removeEffect(channel, index)` | now (GUI-only) |
| Reorder effects in a chain | `src/gui/EffectRackView.cpp:104` | NONE | `lmms.mixer.moveEffect(channel, from, to)` | now (GUI-only) |
| Enable / disable a whole effect chain | `src/gui/EffectRackView.cpp:271` | NONE | `lmms.mixer.setChainEnabled(channel, on)` | now (GUI-only) |
| Bypass one effect in a chain | `src/gui/EffectView.cpp:60` | NONE | `lmms.mixer.setEffectEnabled(channel, index, on)` | now (GUI-only) |
| Effect wet / dry mix | `src/gui/EffectView.cpp:67` | NONE | `lmms.mixer.setEffectWetDry(channel, index, v)` | now (GUI-only) |
| Effect auto-quit / decay time | `src/gui/EffectView.cpp:73` | NONE | `lmms.mixer.setEffectAutoQuit(channel, index, ms)` | now (GUI-only) |
| Open an effect's controls window | `src/gui/EffectView.cpp:118` | NONE | `lmms.mixer.showEffectControls(channel, index)` | now (GUI-only) |
| Plugin delay compensation applied to chains and summing points | `src/core/Mixer.cpp:1257` | NONE | `lmms.mixer.getPdcFrames(channel)` | now — PDC landed #605 (`docs/STATUS.md:17`) |
| Query total mixer latency (source → master) | `include/Mixer.h:363` | NONE | `lmms.mixer.getTotalLatency()` | now — PDC #605 |
| PDC clamp warning when a chain exceeds the delay-line capacity | `src/core/EffectChain.cpp:73` | NONE | `lmms.mixer.getPdcWarnings()` | now (GUI-only) |
| Plugin audio-port ↔ track/channel routing (pin matrix) | `src/gui/PinConnector.cpp:58` | NONE | `lmms.plugin.setChannelRouting(plugin, map)` | now (GUI-only) |
| Patcher: node-graph routing (built, disarmed) | `src/core/RoutingGraph.cpp:48` | NONE | `lmms.routing.addNode(type)` / `connect(a,b)` | now but disarmed — `docs/STATUS.md:44` (no GUI or audio-path code includes `RoutingGraph`) |

## Group 5. Plugins and hosting

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| Discover plugin modules at startup (rescan the plugin dirs) | `src/core/PluginFactory.cpp:144` | NONE | `lmms.plugin.rescan()` | now (startup only) |
| Exclude plugins via an `LMMS_EXCLUDE_PLUGINS` pattern list | `src/core/PluginFactory.cpp:271` | NONE | `lmms.plugin.exclude(pattern)` | now (env-var only) |
| Verify a plugin binary is really dlopen-ed by the app | `src/core/PluginFactory.cpp:170` | MCP tool: lmms_plugin_check | — (exists) | now |
| Instantiate a plugin by name | `src/core/Plugin.cpp:208` | NONE | `lmms.plugin.instantiate(name)` | now (C++ only) |
| Instantiate an effect plugin | `src/core/Effect.cpp:246` | NONE | `lmms.plugin.instantiateEffect(name)` | now (C++ only) |
| Instantiate an instrument plugin | `src/core/Instrument.cpp:96` | NONE | `lmms.plugin.instantiateInstrument(name)` | now (C++ only) |
| Instantiate a tool plugin (level meter etc.) | `src/core/ToolPlugin.cpp:41` | NONE | `lmms.plugin.instantiateTool(name)` | now (C++ only) |
| Effect browser / picker dialog | `src/gui/modals/EffectSelectDialog.cpp:50` | NONE | `lmms.plugin.listEffects()` | now (GUI-only) |
| Instrument browser sidebar | `src/gui/PluginBrowser.cpp:48` | NONE | `lmms.plugin.listInstruments()` | now (GUI-only); tag/similarity search is W7 #604 |
| Drag an instrument from the browser onto a track | `src/gui/PluginBrowser.cpp:285` | NONE | `lmms.plugin.loadInstrument(track, name)` | now (GUI-only) |
| Send an instrument to a new instrument track | `src/gui/PluginBrowser.cpp:297` | NONE | `lmms.plugin.newInstrumentTrack(name)` | now (GUI-only) |
| Load an instrument into an existing track | `src/tracks/InstrumentTrack.cpp:1060` | NONE | `lmms.plugin.loadInstrument(track, name)` | now (C++ only) |
| Replace a track's instrument from a saved preset file | `src/tracks/InstrumentTrack.cpp:1022` | NONE | `lmms.plugin.loadPreset(track, path)` | now (GUI-only) |
| Save an instrument preset (`.xpf`) | `src/gui/instrument/InstrumentTrackWindow.cpp:405` | NONE | `lmms.plugin.savePreset(track, path)` | now (GUI-only) |
| VST3 hosting — effects only | `plugins/Vst3Effect/Vst3Effect.cpp:45` | NONE | `lmms.plugin.hostVst3(path, class)` | now — `docs/STATUS.md:18`; instrument hosting is a Bar-2 gap |
| VST3: load a module + audio class in-process | `plugins/Vst3Effect/Vst3Host.cpp:250` | NONE | `lmms.plugin.loadVst3Class(path, class)` | now |
| CLAP hosting — effects only | `plugins/ClapEffect/ClapEffect.cpp:44` | NONE | `lmms.plugin.hostClap(path, id)` | now — `docs/STATUS.md:19` |
| CLAP in-process host | `plugins/ClapEffect/ClapHost.h:67` | NONE | `lmms.plugin.hostClap(path, id)` | now |
| VST3 / CLAP instrument selector exists but is not exposed | `plugins/Vst3Effect/Vst3SubPluginFeatures.cpp:99` | NONE | `lmms.plugin.hostVst3Instrument(path, class)` | planned — Bar 2 gap (`docs/STATUS.md:89-90`) |
| VST2 instrument hosting via Vestige | `plugins/Vestige/Vestige.cpp:73` | NONE | `lmms.plugin.hostVst2(path)` | now — `docs/STATUS.md:26` |
| VST2 effect hosting via Vestige | `plugins/VstEffect/VstEffect.cpp:55` | NONE | `lmms.plugin.hostVst2Effect(path)` | now |
| Out-of-process VST plugin client | `plugins/VstBase/RemoteVstPlugin.cpp:500` | NONE | `lmms.plugin.hostVst2OutOfProcess(path)` | now (legacy remote path only) |
| Init the out-of-process host over shm + child process | `src/core/RemotePlugin.cpp:231` | NONE | `lmms.plugin.hostOutOfProcess(exe)` | now (C++ only) |
| Show / hide a remote plugin's own UI | `src/core/RemotePlugin.cpp:448` | NONE | `lmms.plugin.showUi(id)` | now (GUI-only) |
| ZynAddSubFx hosted out-of-process | `plugins/ZynAddSubFx/ZynAddSubFx.cpp:80` | NONE | `lmms.plugin.hostZynAddSubFx()` | now — build+unit-test only, `docs/STATUS.md:25` |
| LADSPA plugin index / browser | `src/core/LadspaManager.cpp:46` | NONE | `lmms.plugin.listLadspa()` | now — `docs/STATUS.md:26` |
| LADSPA parameter (control port) model | `src/core/LadspaControl.cpp:37` | NONE | `lmms.plugin.setLadspaParam(id, port, v)` | now |
| LADSPA browser tool plugin | `plugins/LadspaBrowser/LadspaBrowser.cpp:58` | NONE | `lmms.plugin.openTool("ladspabrowser")` | now |
| LV2 plugin discovery / init | `src/core/lv2/Lv2Manager.cpp:237` | NONE | `lmms.plugin.listLv2()` | now — `docs/STATUS.md:26` |
| LV2 unstable-plugin blocklist | `src/core/lv2/Lv2Manager.cpp:51` | NONE | `lmms.plugin.lv2Blocklist()` | now |
| LV2 native plugin UI host | `src/gui/Lv2ViewBase.cpp:140` | NONE | `lmms.plugin.showUi(id)` | now (GUI-only) |
| SoundFont2 instrument (FluidSynth player) | `plugins/Sf2Player/Sf2Player.cpp:67` | NONE | `lmms.plugin.hostSf2(path)` | now — `docs/STATUS.md:26` |
| GIG instrument (Gigasampler player) | `plugins/GigPlayer/GigPlayer.cpp:74` | NONE | `lmms.plugin.hostGig(path)` | now — `docs/STATUS.md:26` |
| Carla Rack — load a rack of third-party plugins | `plugins/CarlaRack/CarlaRack.cpp:46` | NONE | `lmms.plugin.hostCarlaRack()` | now |
| Carla Patchbay — load a patched plugin graph | `plugins/CarlaPatchbay/CarlaPatchbay.cpp:46` | NONE | `lmms.plugin.hostCarlaPatchbay()` | now |
| Plugin multi-channel port buffer / port routing | `include/PluginAudioPorts.h:258` | NONE | `lmms.plugin.setChannelCounts(plugin, in, out)` | now — `docs/STATUS.md:15` |
| Import-filter plugin hosting (MIDI / Hydrogen) | `src/core/ImportFilter.cpp:52` | CLI action: --import | — (exists) | now |
| RNNoise denoiser effect (AI DSP) | `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:58` | NONE | `lmms.plugin.addEffect(track, "rnnoisedenoiser")` | now — `docs/STATUS.md:21`; 0% test coverage (`:36`) |
| NeuralAmp (NAM) neural amplifier effect | `plugins/NeuralAmp/NeuralAmpEffect.cpp:51` | NONE | `lmms.plugin.addEffect(track, "neuralamp", {model})` | now — `docs/STATUS.md:21`; 0% test coverage (`:36`) |
| WASM effect plugin (sandboxed DSP) | `plugins/WasmEffect/WasmEffect.cpp:49` | NONE | `lmms.plugin.addEffect(track, "wasmeffect", {module})` | now but compiled out unless `WANT_WASM=ON` |

## Group 6. Automation

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| Automation clip — a curve of breakpoints bound to one or more automatable models | `src/core/AutomationClip.cpp:225` | NONE | `lmms.automation.addNode(clipId, tick, value)` | now (GUI-only) |
| Bind / unbind a model to an automation clip | `src/core/AutomationClip.cpp:88` | NONE | `lmms.automation.addObject(clipId, modelId)` | now (GUI-only) |
| Evaluate the automation curve at a time | `src/core/AutomationClip.cpp:560` | NONE | `lmms.automation.valueAt(clipId, tick)` | now (C++ only) |
| Query which models a clip automates | `src/core/AutomationClip.cpp:939` | NONE | `lmms.automation.listTargets(clipId)` | now (C++ only) |
| List every clip automating a given model | `src/core/AutomationClip.cpp:970` | NONE | `lmms.automation.clipsForModel(modelId)` | now (C++ only) |
| Song-global automation clip for a model not on any track | `src/core/AutomationClip.cpp:1009` | NONE | `lmms.automation.globalClip(modelId)` | now (C++ only) |
| Flip a curve vertically / horizontally | `src/core/AutomationClip.cpp:677` | NONE | `lmms.automation.flipY(clipId)` | now (GUI-only) |
| Edit one automation breakpoint's value | `src/core/AutomationNode.cpp:71` | NONE | `lmms.automation.setNode(nodeId, value)` | now (GUI-only) |
| Inline (per-control) automation stored inside the control | `src/core/InlineAutomation.cpp:33` | NONE | `lmms.automation.inlineValue(controlId)` | now (GUI-only) |
| Set an automatable model's value — the one automation-adjacent write Lua exposes | `src/core/ScriptBindings.cpp:387` | Lua binding: lmms.FloatModel:setValue(v) / BoolModel:setValue(v) | — (exists) | now |
| Attach a controller connection to a model | `src/core/AutomatableModel.cpp:488` | NONE | `lmms.model.setControllerConnection(modelId, controllerId)` | now (GUI-only) |
| Detach a model's controller connection | `src/core/AutomatableModel.cpp:624` | NONE | `lmms.model.clearControllerConnection(modelId)` | now (GUI-only) |
| Create a controller (LFO / Peak / MIDI / VST-sync) | `src/core/Controller.cpp:185` | NONE | `lmms.controller.add(type)` | now (GUI-only) |
| Add a connection on a controller (persisted) | `src/core/Controller.cpp:295` | NONE | `lmms.controller.addConnection(controllerId, modelId)` | now (GUI-only) |
| Retarget a controller connection | `src/core/ControllerConnection.cpp:99` | NONE | `lmms.controller.retarget(connectionId, controllerId)` | now (GUI-only) |
| LFO controller — per-sample value generation | `src/core/LfoController.cpp:88` | NONE | `lmms.controller.setParam(id, "freq", hz)` | now (GUI-only) |
| LFO waveform shape (sin / tri / saw / square …) | `src/core/LfoController.cpp:177` | NONE | `lmms.controller.setParam(id, "shape", name)` | now (GUI-only) |
| ADSR envelope + LFO level fill for instruments / effects | `src/core/EnvelopeAndLfoParameters.cpp:295` | NONE | `lmms.envelope.setParams(attack, decay, sustain, release)` | now (GUI-only) |
| Peak controller (signal follower driving a model) | `src/core/PeakController.cpp:78` | NONE | `lmms.controller.add("peak", modelId)` | now (GUI-only) |
| Linked model groups — morph / link a set of controls | `src/core/LinkedModelGroups.cpp:43` | NONE | `lmms.linkedGroup.linkControls(otherId)` | now (GUI-only) |
| Linked-model-group editing (add / remove a control) | `src/gui/LinkedModelGroupViews.cpp:84` | NONE | `lmms.linkedGroup.addModel(modelId, name)` | now (GUI-only) |
| Automation targets already exposed to Lua (track mute, volume, panning) | `src/core/ScriptBindings.cpp:433` | Lua binding: lmms.Track:muteModel():setValue() | — (exists; also `InstrumentTrack:volumeModel` `:419`, `panningModel` `:420`) | now |
| Controller connection dialog — pick an input controller | `src/gui/modals/ControllerConnectionDialog.cpp:193` | NONE | `lmms.model.connectToController(modelId, controllerId)` | now (GUI-only) |
| Model context menu — connect to / edit / remove a controller connection | `src/gui/AutomatableModelView.cpp:110` | NONE | `lmms.model.connectToController(modelId, controllerId)` | now (GUI-only) |
| Automate action on sliders and buttons | `src/gui/widgets/AutomatableSlider.cpp:61` | NONE | `lmms.model.automate(modelId)` | now (GUI-only) |
| Controller rack — add a controller | `src/gui/ControllerRackView.cpp:225` | NONE | `lmms.controller.add("lfo")` | now (GUI-only) |
| Controller view — move up / down, remove, rename | `src/gui/ControllerView.cpp:160` | NONE | `lmms.controller.rename(id, name)` | now (GUI-only) |
| MIDI CC rack — 16 CC knobs per instrument track | `src/gui/MidiCCRackView.cpp:94` | NONE | `lmms.track.setMidiCC(trackIndex, cc, value)` | now (GUI-only) |
| Automation editor — draw / erase / draw-out-values / tangent edit modes | `src/gui/editors/AutomationEditor.cpp:2033` | NONE | `lmms.automation.setEditMode(mode)` | now (GUI-only) |
| Automation editor — progression type, tension, quantization | `src/gui/editors/AutomationEditor.cpp:1754` | NONE | `lmms.automation.setProgression(clipId, type)` | now (GUI-only) |
| Automation track — drag-and-drop a model onto it to automate | `src/gui/tracks/AutomationTrackView.cpp:40` | NONE | `lmms.automation.addTrack()` | now (GUI-only; core op `include/Song.h:353`) |
| Per-controller settings dialog | `src/gui/ControllerDialog.cpp:33` | NONE | `lmms.controller.openDialog(id)` | now (GUI-only) |
| LFO controller dialog — BASE / FREQ (tempo-sync) / AMOUNT / PHASE / shape | `src/gui/LfoControllerDialog.cpp:70` | NONE | `lmms.controller.setParam(id, "amount", v)` | now (GUI-only) |
| Peak controller dialog — live peak read-out | `src/gui/PeakControllerDialog.cpp:39` | NONE | `lmms.controller.readPeak(id)` | now (GUI-only) |

## Group 7. Recording and capture

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| Two-track simultaneous capture (prototype owner object) | `src/core/audio/MultiTrackRecorder.cpp:34` | NONE | `lmms.record.start([tracks…])` | now (prototype; not wired to CLI / Lua / MCP) |
| Arm one track's capture stream (path, rate, input channel) | `src/core/audio/TrackRecorder.cpp:72` | NONE | `lmms.record.setArm(trackIndex, path)` | now (C++ only) |
| Disarm all capture streams | `src/core/audio/MultiTrackRecorder.cpp:79` | NONE | `lmms.record.stopAll()` | now (C++ only) |
| Capture overflow accounting (dropped frames) | `src/core/audio/MultiTrackRecorder.cpp:90` | NONE | `lmms.record.status()` | now (C++ only) |
| Backend capture availability (JACK and SDL capture; ALSA has no capture path) | `src/core/audio/AudioJack.cpp:108` | NONE | `lmms.audio.listCaptureDevices()` | now (SDL at `src/core/audio/AudioSdl.cpp:108`; ALSA output-only — no `snd_pcm_readi` in `src/core/audio`) |
| Record MIDI notes into a clip | `src/gui/editors/PianoRoll.cpp:4377` | NONE | `lmms.record.start(target)` | now (GUI-only) |
| Record while playing (accompany) | `src/gui/editors/PianoRoll.cpp:4400` | NONE | `lmms.record.start(target, {accompany:true})` | now (GUI-only) |
| Step recording | `src/gui/editors/PianoRoll.cpp:4432` | NONE | `lmms.record.setStepMode(on)` | now (GUI-only) |
| Song-level record entry (stub — `m_recording = true; // TODO: Implement`) | `src/core/Song.cpp:522` | NONE | `lmms.record.start()` | now as a stub — implementation is the Bar-2 gap |
| Play-and-record entry | `src/core/Song.cpp:531` | NONE | `lmms.record.start({playAlong:true})` | now (stub-level) |
| Song Editor — record samples from the audio device | `src/gui/editors/SongEditor.cpp:1098` | NONE | `lmms.record.start()` | now (GUI-only) |
| Song Editor — record samples while playing | `src/gui/editors/SongEditor.cpp:1108` | NONE | `lmms.record.start({playAlong:true})` | now (GUI-only) |
| Sample-track record-arm toggle | `src/core/SampleClip.cpp:178` | NONE | `lmms.record.setArm(trackIndex, on)` | now (GUI-only) |
| Punch in / out | `zene-remote/docs/STATUS.md:190` | NONE | `lmms.record.setPunch(inTick, outTick)` | planned — boarded #611; no `TakeLane`/punch code in `src/` |

## Group 8. Project and file I/O

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| New project (File ▸ New) | `src/gui/MainWindow.cpp:286` | NONE | `lmms.project.new()` | now (GUI-only) |
| New project from a template | `src/gui/menus/TemplatesMenu.cpp:29` | NONE | `lmms.project.newFromTemplate(path)` | now (GUI-only) |
| Open project (File ▸ Open…) | `src/gui/MainWindow.cpp:292` | CLI action: any command that takes `<project>` (render / rendertracks) | `lmms.project.open(path)` | now |
| Recent-projects menu | `src/gui/menus/RecentProjectsMenu.cpp:31` | NONE | `lmms.project.recent()` | now (GUI-only) |
| Save (File ▸ Save) | `src/gui/MainWindow.cpp:297` | Lua binding: lmms.song():saveProject(path) | — (exists) | now |
| Save As | `src/gui/MainWindow.cpp:300` | Lua binding: lmms.song():saveProject(path) | — (exists; path argument) | now |
| Save as New Version (auto-increment filename) | `src/gui/MainWindow.cpp:303` | NONE | `lmms.project.saveVersioned(path)` | now (GUI-only) |
| Save as default template | `src/gui/MainWindow.cpp:908` | NONE | `lmms.project.saveTemplate(path)` | now (GUI-only) |
| Save as Project Bundle with resources | `src/gui/modals/VersionedSaveDialog.cpp:189` | CLI action: makebundle | — (exists) | now |
| Write the project file (optionally with resources) | `src/core/Song.cpp:1215` | Lua binding: lmms.song():saveProject(path) | — (exists) | now |
| Load a project file | `src/core/Song.cpp:1008` | CLI action: render `-o` path | `lmms.project.open(path)` | now (core API) |
| Create a new empty project | `src/core/Song.cpp:931` | NONE | `lmms.project.new()` | now (C++ only) |
| Create a project from a template (core) | `src/core/Song.cpp:993` | NONE | `lmms.project.newFromTemplate(path)` | now (C++ only) |
| Import MIDI / Hydrogen file | `src/core/ImportFilter.cpp:52` | CLI action: --import [-e] | — (exists) | now |
| Import (GUI menu entry) | `src/gui/MainWindow.cpp:1648` | CLI action: --import | — (exists on CLI) | now |
| Run a Lua script from the File menu | `src/gui/MainWindow.cpp:778` | CLI action: --run-script | — (exists on CLI) | now (GUI action is GUI-only) |
| Export project (format / rate / bit depth dialog) | `src/gui/modals/ExportProjectDialog.cpp:54` | CLI action: render -f -s -a -b -m | — (exists) | now |
| Start an export render | `src/gui/modals/ExportProjectDialog.cpp:244` | CLI action: render | — (exists) | now |
| Render the whole project headlessly | `src/core/main.cpp:779` | CLI action: render | MCP tool: lmms_render | now |
| Export per-track files (`rendertracks`) | `src/gui/MainWindow.cpp:1643` | CLI action: rendertracks | MCP tool: lmms_render(out_dir, per_track=True) | now |
| Export project to MIDI | `src/core/Song.cpp:1382` | NONE | `lmms.project.exportMidi(path)` | now (GUI + C++ only) |
| Export project to MIDI (GUI menu entry) | `src/gui/MainWindow.cpp:1479` | NONE | `lmms.project.exportMidi(path)` | now (GUI-only) |
| Quit (File ▸ Quit) | `src/gui/MainWindow.cpp:331` | NONE | `lmms.app.quit()` | now (GUI-only) |
| Dump a compressed `.mmpz` to XML | `src/core/main.cpp:449` | CLI action: dump | — (exists) | now |
| Compress XML to `.mmpz` | `src/core/main.cpp:466` | CLI action: compress | — (exists) | now |
| Upgrade a project file to the current format | `src/core/main.cpp:391` | CLI action: upgrade | — (exists) | now |
| DataFile write (file, optionally with resources) | `src/core/DataFile.cpp:315` | NONE | `lmms.project.write(path, withResources)` | now (C++ only) |
| Copy external resources into a bundle | `src/core/DataFile.cpp:446` | CLI action: makebundle | — (exists) | now |
| Validate a project file's content | `src/core/DataFile.cpp:200` | NONE | `lmms.project.validate(path)` | now (C++ only) |
| Project format upgrade chain | `src/core/DataFile.cpp:73` | CLI action: upgrade | — (exists) | now |
| Open a project / audio file via the file dialog | `src/gui/modals/FileDialog.cpp:41` | NONE | `lmms.project.open(path)` | now (GUI-only; audio helper `:147`, waveform `:193`) |
| Rename dialog (clip / track naming) | `src/gui/modals/RenameDialog.cpp:35` | NONE | `lmms.clip.rename(clipId, name)` | now (GUI-only) |
| File browser for samples / presets / projects | `src/gui/FileBrowser.cpp:84` | NONE | `lmms.browser.list(dir)` | now (GUI-only); tags + similarity are W7 #604 |
| Persist / restore browser directory state | `src/gui/FileBrowser.cpp:208` | NONE | `lmms.browser.state()` | now (GUI-only) |
| Recently-opened-projects persistence | `src/core/ConfigManager.cpp:319` | NONE | `lmms.project.recent()` | now (C++ only) |
| Recovery-file path | `include/ConfigManager.h:205` | NONE | `lmms.project.recoveryFile()` | now (C++ only) |
| mmpz-git: dump (.mmpz → XML) | `tools/mmpz-git/mmpz_git.py:622` | CLI action: mmpz-git dump | — (exists) | now |
| mmpz-git: compress (XML → .mmpz) | `tools/mmpz-git/mmpz_git.py:635` | CLI action: mmpz-git compress | — (exists) | now |
| mmpz-git: verify (round-trip byte identity) | `tools/mmpz-git/mmpz_git.py:654` | CLI action: mmpz-git verify | — (exists) | now |
| mmpz-git: canonicalize (canonical element order) | `tools/mmpz-git/mmpz_git.py:670` | CLI action: mmpz-git canonicalize | — (exists) | now |
| mmpz-git: diff (XML-aware semantic diff) | `tools/mmpz-git/mmpz_git.py:397` | CLI action: mmpz-git diff | — (exists) | now |
| mmpz-git: merge (git merge driver) | `tools/mmpz-git/mmpz_git.py:577` | CLI action: mmpz-git merge | — (exists) | now |
| mmpz-git: info (summarise a project file) | `tools/mmpz-git/mmpz_git.py:694` | CLI action: mmpz-git info | — (exists) | now |
| mmpz-git: textconv (git textconv filter) | `tools/mmpz-git/mmpz_git.py:648` | CLI action: mmpz-git textconv | — (exists) | now |
| mmpz-git: install / uninstall git filters | `tools/mmpz-git/mmpz_git.py:718` | CLI action: mmpz-git install | — (exists; uninstall `:744`) | now |

## Group 9. Settings/preferences and app lifecycle

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| Settings dialog with 5 tabs (General / Performance / Audio / MIDI / Paths) | `src/gui/modals/SetupDialog.cpp:897` | NONE | `lmms.settings.open(tab)` | now (GUI-only; tabs at `:900`, `:903`, `:906`, `:909`) |
| Apply a settings change (write all keys) | `src/gui/modals/SetupDialog.cpp:975` | NONE | `lmms.settings.set(key, value)` | now (GUI-only) |
| Choose the audio interface / device | `src/gui/modals/SetupDialog.cpp:1248` | NONE | `lmms.settings.setAudioDevice(name)` | now (GUI-only) |
| Set the audio buffer size | `src/gui/modals/SetupDialog.cpp:1282` | NONE | `lmms.settings.setBufferSize(frames)` | now (GUI-only) |
| Choose the MIDI interface | `src/gui/modals/SetupDialog.cpp:1319` | NONE | `lmms.settings.setMidiDevice(name)` | now (GUI-only) |
| MIDI auto-quantization toggle | `src/gui/modals/SetupDialog.cpp:1330` | NONE | `lmms.settings.set("ui/midiautoquantization", on)` | now (GUI-only) |
| Working-directory setting | `src/gui/modals/SetupDialog.cpp:1349` | NONE | `lmms.settings.setWorkingDir(path)` | now (GUI-only) |
| VST plugin directory | `src/gui/modals/SetupDialog.cpp:1366` | NONE | `lmms.settings.setPluginDir("vst", path)` | now (GUI-only) |
| LADSPA plugin directory | `src/gui/modals/SetupDialog.cpp:1391` | NONE | `lmms.settings.setPluginDir("ladspa", path)` | now (GUI-only) |
| SoundFont (SF2) directory | `src/gui/modals/SetupDialog.cpp:1408` | NONE | `lmms.settings.setPluginDir("sf2", path)` | now (GUI-only) |
| Theme directory | `src/gui/modals/SetupDialog.cpp:1464` | NONE | `lmms.settings.setTheme(path)` | now (GUI-only) |
| Background artwork | `src/gui/modals/SetupDialog.cpp:1501` | NONE | `lmms.settings.setBackgroundArt(path)` | now (GUI-only) |
| UI language | `src/gui/modals/SetupDialog.cpp:1163` | NONE | `lmms.settings.setLanguage(lang)` | now (GUI-only) |
| Tooltips on / off | `src/gui/modals/SetupDialog.cpp:1076` | NONE | `lmms.settings.set("ui/tooltips", on)` | now (GUI-only) |
| Display waveform on / off | `src/gui/modals/SetupDialog.cpp:1082` | NONE | `lmms.settings.set("ui/displaywaveform", on)` | now (GUI-only) |
| Piano-roll note labels on / off | `src/gui/modals/SetupDialog.cpp:1088` | NONE | `lmms.settings.set("ui/pianonotelabels", on)` | now (GUI-only; View-menu toggle at `src/gui/MainWindow.cpp:1177`) |
| Autosave: on / off, interval, and running autosave | `src/gui/modals/SetupDialog.cpp:1189` | NONE | `lmms.settings.setAutosave(on, intervalSeconds)` | now (GUI-only; interval `:1177`, running `:1198`) |
| Autosave writes the recovery file | `src/gui/MainWindow.cpp:1466` | NONE | `lmms.project.autosave()` | now (timer-driven) |
| Edit menu ▸ Scales and keymaps (microtuner) | `src/gui/MainWindow.cpp:347` | NONE | `lmms.tuning.edit()` | now (GUI-only) |
| Edit menu ▸ Settings | `src/gui/MainWindow.cpp:349` | NONE | `lmms.settings.open()` | now (GUI-only) |
| Help ▸ Online help / homepage | `src/gui/MainWindow.cpp:384` | NONE | — (external URL, no tool needed) | now (GUI-only) |
| Help ▸ About dialog | `src/gui/MainWindow.cpp:396` | NONE | `lmms.app.version()` | now (GUI-only; dialog `src/gui/modals/AboutDialog.cpp:35`) |
| View menu — toggle each sub-window | `src/gui/MainWindow.cpp:1089` | NONE | `lmms.ui.showWindow(name)` | now (GUI-only) |
| View ▸ Fullscreen | `src/gui/MainWindow.cpp:1121` | NONE | `lmms.ui.setFullscreen(on)` | now (GUI-only) |
| View ▸ Detach all / Attach all sub-windows | `src/gui/MainWindow.cpp:1132` | NONE | `lmms.ui.detachAll(on)` | now (GUI-only; attach at `:1137`) |
| View ▸ Smooth scroll toggle | `src/gui/MainWindow.cpp:1163` | NONE | `lmms.settings.set("ui/smoothscroll", on)` | now (GUI-only) |
| Per-project Notes window (rich text) | `src/gui/ProjectNotes.cpp:52` | NONE | `lmms.project.notes(text)` | now (GUI-only) |
| Project-recovery prompt at startup | `src/core/main.cpp:818` | NONE | `lmms.project.checkRecovery()` | now (GUI-only) |
| Recovery choice — Recover or Discard | `src/core/main.cpp:862` | NONE | `lmms.project.recover(true/false)` | now (GUI-only) |
| Session cleanup (delete the recovery file) | `src/gui/MainWindow.cpp:1318` | NONE | `lmms.project.sessionCleanup()` | now (GUI-only) |
| Graceful shutdown on SIGINT | `src/gui/GuiApplication.cpp:266` | NONE | — (signal; no tool needed) | now |
| Themed application style + palette | `src/gui/LmmsStyle.cpp:174` | NONE | `lmms.settings.setTheme(path)` | now (GUI-only; palette `src/gui/LmmsPalette.cpp:74`) |
| HiDPI design-pixel scaling | `include/DpiHelper.h:48` | NONE | `lmms.settings.setDpiScale(factor)` | now — `docs/STATUS.md:23` |
| CLI `-c/--config <file>` — run with an alternate config | `src/core/main.cpp:178` | CLI action: --config | — (exists) | now |
| CLI `--allowroot` — bypass the root-user startup check | `src/core/main.cpp:176` | CLI action: --allowroot | — (exists) | now |
| CLI `--geometry <geom>` — main-window size / position | `src/core/main.cpp:182` | CLI action: --geometry | — (exists) | now |
| CLI `-h/--help` — usage and action list | `src/core/main.cpp:179` | CLI action: --help | — (exists) | now |
| CLI `-v/--version` — version and build options | `src/core/main.cpp:180` | CLI action: --version | — (exists) | now |

## Group 10. Scripting and AI DSP

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| Run a Lua script headlessly (`--run-script <file>`) | `src/core/main.cpp:206` | CLI action: --run-script | — (exists) | now |
| Run a Lua script from the GUI (File ▸ Run Script…) | `src/gui/MainWindow.cpp:317` | CLI action: --run-script | — (exists on CLI) | now (GUI action is GUI-only) |
| Script worker thread — fresh `lua_State` per invocation, no cross-script globals | `src/core/ScriptEngine.cpp:133` | CLI action: --run-script | — (exists) | now |
| Script sandbox — os / io / package / debug / require removed | `src/core/ScriptEngine.cpp:188` | CLI action: --run-script | — (exists) | now |
| Instruction budget aborts runaway scripts | `src/core/ScriptEngine.cpp:235` | CLI action: --run-script | — (exists; `lmms.setInstructionBudget(n)`) | now |
| Scripts must declare `--! lmms-api <major>.<minor>` or are refused | `src/core/ScriptEngine.cpp:358` | CLI action: --run-script | — (exists) | now |
| In-process script execution API (no file, C++ callers) | `src/core/ScriptEngine.cpp:378` | NONE | MCP tool: `lmms_run_script(source)` | now (C++ only) |
| Shipped example Lua scripts (hello, create-pattern, generative-bass, midi-router) | `data/scripts/hello.lua:1` | CLI action: --run-script | — (exists) | now |
| Lua API v0 version string | `src/core/ScriptBindings.cpp:334` | Lua binding: lmms.version() | — (exists) | now |
| Read project directory from Lua | `src/core/ScriptBindings.cpp:351` | Lua binding: lmms.projectDir() | — (exists) | now |
| Sandboxed file read / write / list inside the project dir | `src/core/ScriptBindings.cpp:483` | Lua binding: lmms.projectFile():read/write/list/exists | — (exists) | now |
| Script logging to stdout | `src/core/ScriptBindings.cpp:489` | Lua binding: lmms.log:info/warn/error/write | — (exists) | now |
| Receive MIDI input events in a script | `src/core/ScriptBindings.cpp:504` | Lua binding: lmms.midiIn():hasEvent()/next() | — (exists) | now |
| Emit MIDI note on / off from a script | `src/core/ScriptBindings.cpp:509` | Lua binding: lmms.midiOut():noteOn()/noteOff() | — (exists) | now |
| Script introspection of instruments (name, parameter count / names / models) | `src/core/ScriptBindings.cpp:400` | Lua binding: lmms.Instrument:parameterCount/parameterName/parameterModel | — (exists) | now |
| WASM DSP ABI v1 (exports `process`/`memory`/`abi`) | `src/wasm/WasmAbi.h:34` | NONE | `lmms.plugin.addEffect(track, "wasmeffect", {module})` | now (needs wasmtime; `WANT_WASM`) |
| WASM sandbox — `callProcess`, fuel budget, 16 MiB memory cap | `src/wasm/WasmSandbox.h:86` | NONE | `lmms.plugin.addEffect(track, "wasmeffect", {module})` | now |
| WasmEffect — hosts a WASM DSP module as an effect via a worker thread | `plugins/WasmEffect/WasmEffect.h:40` | NONE | `lmms.plugin.addEffect(track, "wasmeffect", {module})` | now (compiled out unless `WANT_WASM=ON`) |
| RNNoise real-time denoiser effect | `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h:37` | NONE | `lmms.plugin.addEffect(track, "rnnoisedenoiser")` | now — `docs/STATUS.md:21`; 0% coverage (`:36`) |
| NeuralAmp — real-time NAM (.nam) neural amplifier | `plugins/NeuralAmp/NeuralAmpEffect.h:68` | NONE | `lmms.plugin.addEffect(track, "neuralamp", {model})` | now — `docs/STATUS.md:21`; 0% coverage (`:36`) |
| Offline HTDemucs stem separation, in-process ONNX backend | `src/core/OnnxRuntimeStemSeparator.cpp:91` | NONE | CLI action: `--stem-split <clip>` (does not exist) | now (opt-in `-DWANT_STEM_SPLIT=ON`) |
| Offline stem separation, external-process backend | `src/core/ExternalProcessStemSeparator.cpp:160` | NONE | CLI action: `--stem-split <clip>` (does not exist) | now |
| Split a sample clip to stems (GUI job controller) | `src/gui/StemSplitController.cpp:102` | NONE | `lmms_sample_split_to_stems(clip, out_dir)` | now (GUI-only; menu entry `src/gui/clips/SampleClipView.cpp:109`) |
| MCP: headless render of a project | `mcp-lmms-lab/lmms_lab/tools.py:434` | MCP tool: lmms_render | — (exists) | now (sibling `mcp-lmms-lab/` server) |
| MCP: render a project N times for reproducibility comparison | `mcp-lmms-lab/lmms_lab/tools.py:1144` | MCP tool: lmms_render_matrix | — (exists) | now |
| MCP: configure + build a worktree | `mcp-lmms-lab/lmms_lab/tools.py:259` | MCP tool: lmms_build | — (exists) | now |
| MCP: run ctest for a worktree | `mcp-lmms-lab/lmms_lab/tools.py:349` | MCP tool: lmms_test | — (exists) | now |
| MCP: measure RMS / dBFS and the dB delta between two WAVs | `mcp-lmms-lab/lmms_lab/tools.py:469` | MCP tool: lmms_measure | — (exists) | now |
| MCP: prove a plugin .so is dlopen-ed (strace) | `mcp-lmms-lab/lmms_lab/tools.py:681` | MCP tool: lmms_plugin_check | — (exists) | now |
| MCP: read-only rebase inventory for base..head | `mcp-lmms-lab/lmms_lab/tools.py:847` | MCP tool: rebase_inventory | — (exists) | now |
| MCP: list worktrees | `mcp-lmms-lab/lmms_lab/tools.py:913` | MCP tool: worktree_list | — (exists) | now |
| MCP: create a worktree of the main clone | `mcp-lmms-lab/lmms_lab/tools.py:929` | MCP tool: worktree_create | — (exists) | now |
| MCP: remove a worktree | `mcp-lmms-lab/lmms_lab/tools.py:966` | MCP tool: worktree_remove | — (exists) | now |
| MCP: poll / tail a background job | `mcp-lmms-lab/lmms_lab/tools.py:1593` | MCP tool: lmms_job_status / lmms_job_log / lmms_job_list | — (exists) | now |

## Group 11. Planned waves and boarded backlog gaps

| Capability | Code anchor (file:line) | Agent-callable today? | Smallest honest agent tool | Exists now or planned |
|---|---|---|---|---|
| Session View — clip/scene data layer (`SessionModel` / `ClipSlot` / `Scene`) | `zene-remote/docs/STATUS.md:30` | NONE | `lmms.session.getState()` | planned W1 #594; code exists only on branch `feat/session-view-model` (PR #5) — grep of `src/` for `SessionModel`/`ClipSlot`/`Scene` → not found in the tree |
| Session View — grid UI (slot launch, scene column, drag-drop) | `ableton-gap/SPEC-zene-studio.md:56` | NONE | `lmms.session.slotCreate(track, slot)` | planned W1 #598 |
| Session View — launch engine with Trigger/Gate/Toggle/Repeat + quantisation grid | `ableton-gap/SPEC-zene-studio.md:57` | NONE | `lmms.session.launchClip(track, slot, quantise)` | planned W1 #595 |
| Session View — legato mode (incoming clip keeps the outgoing position) | `ableton-gap/SPEC-zene-studio.md:58` | NONE | `lmms.session.setLegato(track, slot, on)` | planned W1 #595 |
| Session View — scenes (row launch, per-scene tempo / time signature, capture-and-insert) | `ableton-gap/SPEC-zene-studio.md:59` | NONE | `lmms.session.launchScene(scene)` | planned W1 #598 |
| Session View — follow actions (10 types, chance A/B, linked/unlinked) | `ableton-gap/SPEC-zene-studio.md:60` | NONE | `lmms.session.setFollowAction(track, slot, spec)` | planned W1 #596 |
| Session View — arrangement record + back-to-arrangement | `ableton-gap/SPEC-zene-studio.md:61` | NONE | `lmms.arrangement.recordSession(on)` | planned W1 #596 |
| Session View — per-clip properties (loop region, gain, transpose/detune, RAM mode) | `ableton-gap/SPEC-zene-studio.md:62` | NONE | `lmms.clip.setLoopRegion(clipId, spec)` | planned W1 #594 |
| Warp — per-clip warp switch and mode (Beats / Tones / Texture / Re-Pitch / Complex) | `ableton-gap/SPEC-zene-studio.md:66` | NONE | `lmms.warp.setMode(clipId, mode)` | planned W2 #597; grep `warp` in `src/` → only PianoRoll's unrelated strum parameter (`src/gui/editors/PianoRoll.cpp:3020`) |
| Warp — markers and auto transient detection for Beats / long files | `ableton-gap/SPEC-zene-studio.md:67` | NONE | `lmms.warp.addMarker(clipId, srcTime, timelineTime)` | planned W2 #597; not found (`WarpMarker` → 0 hits in `src/`) |
| Warp — clip tempo leader / follower | `ableton-gap/SPEC-zene-studio.md:68` | NONE | `lmms.clip.setTempoLeader(clipId, on)` | planned W2 #597; not found (`tempo leader` → 0 hits) |
| Warp — slice to MIDI | `ableton-gap/SPEC-zene-studio.md:69` | NONE | `lmms.warp.sliceToMidi(clipId, instrument)` | planned W2 #597 (v1) |
| Racks — rack container with unlimited parallel chains, nesting | `ableton-gap/SPEC-zene-studio.md:73` | NONE | `lmms.rack.addChain(rackId)` | planned W3 #599; no `Rack`/`ChainSelector` class in `src/`; the DAG it needs exists but is disarmed (`src/core/RoutingGraph.cpp:48`, `docs/STATUS.md:44`) |
| Racks — chain zones (key, velocity, chain-select 0–127 with crossfade) | `ableton-gap/SPEC-zene-studio.md:74` | NONE | `lmms.chain.setZone(chainId, kind, spec)` | planned W3 #599 |
| Racks — 8 macros mapping any AutomatableModel (min/max, inverted) | `ableton-gap/SPEC-zene-studio.md:75` | NONE | `lmms.macro.map(macroId, spec)` | planned W3 #599 |
| Racks — Drum Rack-style pad container | `ableton-gap/SPEC-zene-studio.md:76` | NONE | `lmms.rack.createPad(note, device)` | planned W3 #599 (v1) |
| Comping — parallel take lanes + auto-lane on record | `ableton-gap/SPEC-zene-studio.md:80` | NONE | `lmms.take.createLane(track)` | planned W4 #600, gated by #611; not found (`TakeLane`/`comping` → 0 hits in `src/`) |
| Comping — region-level composite across lanes + per-lane audition | `ableton-gap/SPEC-zene-studio.md:81` | NONE | `lmms.comp.selectRegion(region, lane)` | planned W4 #600 |
| Comping — MIDI comping (clip-region selection) | `ableton-gap/SPEC-zene-studio.md:82` | NONE | `lmms.comp.selectRegion(region, lane, "midi")` | planned W4 #600 |
| MPE — per-note pitch, slide, pressure + release velocity capture | `ableton-gap/SPEC-zene-studio.md:86` | NONE | `lmms.note.setExpression(noteId, kind, points)` | planned W5a #601; not found (`MPE`/`noteExpression` → only a TODO at `src/core/Note.cpp:242`) |
| MPE — per-note expression storage in project XML + playback | `ableton-gap/SPEC-zene-studio.md:86` | NONE | `lmms.note.getExpression(noteId)` | planned W5a #601 |
| MPE — per-note expression editing view | `ableton-gap/SPEC-zene-studio.md:87` | NONE | `lmms.note.setExpression(noteId, "pressure", points)` | planned W5a #601 |
| Modulation — relative modulator envelopes layered over absolute automation | `ableton-gap/SPEC-zene-studio.md:91` | NONE | `lmms.modulator.setTarget(modId, modelId)` | planned W5b #602; not found (no modulation bus at `AutomatableModel` level) |
| Modulation — LFO / Shaper / Envelope-Follower modulator devices | `ableton-gap/SPEC-zene-studio.md:92` | NONE | `lmms.modulator.create(track, kind)` | planned W5b #602; the inherited LFO *controller* exists (`src/gui/LfoControllerDialog.cpp:76`) but is not a relative modulation layer |
| Modulation — clip-level modulation envelopes | `ableton-gap/SPEC-zene-studio.md:93` | NONE | `lmms.modulator.setEnvelope(clipId, points)` | planned W5b #602 (v1) |
| Ableton Link — tempo / phase sync, join-leave, Start-Stop Sync | `ableton-gap/SPEC-zene-studio.md:96` | NONE | `lmms.link.enable(on)` | planned W6 #603; not found (case-insensitive `ableton` across `src/` + `CMakeLists.txt` → 0 hits) |
| Link Audio — real-time audio streaming between peers | `ableton-gap/SPEC-zene-studio.md:97` | NONE | `lmms.link.connectAudioPeer(peer, bus)` | planned W6 #603 (v1) |
| Browser — collections, tags, filtered search, browser history | `ableton-gap/SPEC-zene-studio.md:101` | NONE | `lmms.browser.tag(assetId, tag)` | planned W7 #604; today only a plain name filter (`src/gui/FileBrowser.cpp:105`) |
| Browser — sound-similarity search (local ML embedding) | `ableton-gap/SPEC-zene-studio.md:102` | NONE | `lmms.browser.similarSounds(assetId, n)` | planned W7 #604; not found (`similarity` → 0 hits in `src/`) |
| Browser — auto-tagging for short samples | `ableton-gap/SPEC-zene-studio.md:103` | NONE | `lmms.browser.autoTag(assetId)` | planned W7 #604 (v1) |
| Clip editing — trim, slip, fades, crossfades, clip gain | `BACKLOG.md:137` | NONE | `lmms.clip.trim(clipId, spec)` | boarded #611 (xl), lands before #597/#598/#600 (`docs/STATUS.md:222`) |
| Capture depth — punch in/out, arbitrary input count, input monitoring | `BACKLOG.md:142` | NONE | `lmms.record.setPunch(inTick, outTick)` | boarded #611; today only the two-track prototype (`src/core/audio/MultiTrackRecorder.cpp:34`) |
| Third-party **instrument** hosting (MIDI in → audio out, state save/reload) | `BACKLOG.md:159` | NONE | `lmms.plugin.addInstrument(track, path)` | Bar-2 gap, no task; VST3/CLAP host effects only (`docs/STATUS.md:18-19`) |
| Plugin lifecycle — scanning, caching, blacklisting, out-of-process isolation | `BACKLOG.md:208` | NONE | `lmms.plugin.scan(dir)` / `lmms.plugin.setIsolated(pluginId, on)` | Bar-2 gaps, no task; only the legacy VST2 remote path isolates (`src/core/RemotePlugin.cpp:231`) |
| Automation modes + sample-accurate automation (Read/Touch/Latch/Write) | `BACKLOG.md:170` | NONE | `lmms.automation.setRecordMode(mode)` | Bar-2 gap, no task; the data model exists, the mode state machine does not |
| Freeze / bounce-in-place / stem export | `BACKLOG.md:179` | CLI action: render (whole project only) | `lmms.track.freeze(trackIndex, out)` | Bar-2 gap, no task; whole-project render is HAVE (`src/core/main.cpp:278`) and `rendertracks` exists (`src/core/main.cpp:282`) yet the register still files per-track export as not-HAVE — worth reconciling |
| LUFS metering | `zene-remote/docs/STATUS.md:194` | NONE | `lmms.meter.lufs(channelId)` | Bar-2 gap, no task |
| MIDI learn / controller surfaces (global learn mode, persisted binding) | `BACKLOG.md:227` | NONE | `lmms.midi.learnBind(cc, modelId)` | Bar-2 gap, no task; no binding-learning code in `src/` |
| Browser audition and drag-and-drop | `zene-remote/docs/STATUS.md:195` | NONE | `lmms.browser.audition(assetId)` | Bar-2 gap, no task |
| Groove pool, scale awareness, note probability | `zene-remote/docs/STATUS.md:196` | NONE | `lmms.note.setProbability(noteId, p)` | Bar-2 gap, no task |
| Modern stock devices (~30 instruments and effects) | `zene-remote/docs/STATUS.md:148` | NONE | `lmms.device.add(kind)` | Bar 3, items #34 |
| Factory content (curated samples + 500+ presets) | `zene-remote/docs/STATUS.md:152` | NONE | `lmms.content.install(pack)` | Bar 3, item #35 |
| Design system (component library retrofitting every Qt widget) | `zene-remote/docs/STATUS.md:154` | NONE | — (no agent tool; UI-only) | Bar 3, item #36 |
| Multicore graph scheduling | `zene-remote/docs/STATUS.md:193` | NONE | `lmms.engine.setThreads(n)` | Bar-2 gap, no task |
| Autosave recovery | `zene-remote/docs/STATUS.md:197` | NONE | `lmms.project.recoverFile(path)` | Bar-2 gap / polish, no task |
| Crash reporter | `zene-remote/docs/STATUS.md:197` | NONE | `lmms.app.crashReports()` | Bar-2 gap / polish, no task |
| Tempo automation and time-signature changes | `zene-remote/docs/STATUS.md:248` | NONE | `lmms.song.addTempoChange(tick, bpm)` | product-side gap, no task |
| Waveform rendering + peak cache | `zene-remote/docs/STATUS.md:247` | NONE | `lmms.sample.peaks(clipId)` | product-side gap, no task |
| Undo depth and drag coalescing | `zene-remote/docs/STATUS.md:248` | NONE | `lmms.undo.setDepth(n)` | product-side gap, no task |
| Plugin state save / restore | `zene-remote/docs/STATUS.md:248` | NONE | `lmms.plugin.saveState(pluginId)` | product-side gap, no task |
| MIDI clock / MTC output | `zene-remote/docs/STATUS.md:249` | NONE | `lmms.midi.setClockOut(port, on)` | product-side gap, no task |
| Dithering and sample-rate-conversion quality | `zene-remote/docs/STATUS.md:249` | NONE | `lmms.render.setDither(mode)` | product-side gap, no task |
| Recording crash recovery | `zene-remote/docs/STATUS.md:249` | NONE | `lmms.record.recover()` | product-side gap, no task |
| Real-time-safety verification (allocation counters) | `zene-remote/docs/STATUS.md:249` | NONE | MCP tool: `lmms_test(worktree, filter=rt)` | product-side gap, no task |
| Golden-audio integration tests | `zene-remote/docs/STATUS.md:250` | NONE | MCP tool: `lmms_measure(a, b)` | product-side gap, no task; `lmms_measure` exists but nothing drives it automatically |
| Soak testing (200 tracks, 8-hour sessions) | `zene-remote/docs/STATUS.md:250` | NONE | MCP tool: `lmms_soak(project, hours)` | product-side gap, no task |
| Keyboard navigation and accessibility | `zene-remote/docs/STATUS.md:250` | NONE | `lmms.ui.setAccessibility(on)` | product-side gap, no task |
| Auto-mastering wave 1 — candidate-variant mastering with objective gates | `zene-remote/docs/STATUS.md:185` | NONE | `lmms.mastering.run(candidates)` | boarded #610; no mastering code in `src/` or `plugins/` |
| mmpz-git depth (3-way merge, musical conflict presentation, audible-diff CLI, CI recipes) | `zene-remote/docs/STATUS.md:228` | CLI action: mmpz-git diff / merge | — (base tool exists) | boarded #612; the five depth items are planned |
| Lua API stabilisation (versioning policy, script-defined devices, console, packages, generated docs) | `zene-remote/docs/STATUS.md:229` | Lua binding: lmms.* (v0, `src/core/ScriptBindings.cpp:332`) | `lmms.script.run(path)` | boarded #613; v0 exists but there is no way to talk to a running instance |
| Documented WASM effect ABI for third parties | `zene-remote/docs/STATUS.md:230` | NONE | `lmms.plugin.addEffect(track, "wasmeffect", {module})` | boarded #614; the sandbox exists, the documented ABI is planned |
| Part C's last 4 remote-plugin families (Vestige, ZynAddSubFx, VstBase, VstEffect) | `zene-remote/docs/STATUS.md:202` | NONE | `lmms.plugin.loadHeadless("Vestige")` | #589 blocked on an owner decision; proven at build + unit-test level only (`docs/STATUS.md:25`) |
| A11 — one action, one implementation: every user-facing action is a registered command with a stable ID + JSON schema | `ableton-gap/SPEC-zene-studio.md:126` | NONE | `zene.command.list()` | binding requirement, SPEC §7 (authoritative per-feature table in `ableton-gap/AGENT-TOOLING.md`); `zene.command` → not found in the tree |
| A12 — line-delimited JSON-RPC over a local UNIX socket, MCP server as a thin outside bridge | `ableton-gap/SPEC-zene-studio.md:127` | NONE | `mcp-zene-control` (external bridge) | binding requirement; `mcp-zene-control` → not found in the tree |
| A13 — headless parity: every command runs with no display and no audio device, or refuses with a typed error | `ableton-gap/SPEC-zene-studio.md:128` | NONE | `zene.command.describe(id)` → `requires` | binding requirement |
| A14 — read-back and audio truth: `*.get_state` per command family + headless render/export | `ableton-gap/SPEC-zene-studio.md:129` | NONE | `zene.<family>.get_state()` | binding requirement; the only `get_state`-like surface today is `lmms.transport():position()` |
| A15 — anti-drift gate: an `agent_surface` ctest asserts every menu/toolbar action has a command ID | `ableton-gap/SPEC-zene-studio.md:130` | NONE | MCP tool: `lmms_test(worktree, filter=agent_surface)` | binding requirement; `agent_surface` → not found in the tree |
| Per-wave command groups (W1 `session.*`, W2 `warp.*`, W3 `rack.*`, W4 `comp.*`, W5 `note.expression.*`/`modulator.*`, W6 `link.*`, W7 `browser.*`) | `ableton-gap/SPEC-zene-studio.md:132` | NONE | `zene.command.list(group="warp")` | binding requirement for every wave |

## Counts

Machine-counted from the table rows above (a row is *agent-callable* when its third column does not start with `NONE`).

| Group | Capabilities | Agent-callable today | Not agent-callable |
|---|---|---|---|
| 1. Transport and playback | 27 | 12 | 15 |
| 2. Song / arrangement editing (clips, tracks, timeline) | 61 | 8 | 53 |
| 3. Piano roll / note editing | 46 | 6 | 40 |
| 4. Mixer, buses, routing, PDC | 34 | 0 | 34 |
| 5. Plugins and hosting | 40 | 2 | 38 |
| 6. Automation | 34 | 2 | 32 |
| 7. Recording and capture | 14 | 0 | 14 |
| 8. Project and file I/O | 45 | 27 | 18 |
| 9. Settings/preferences and app lifecycle | 38 | 5 | 33 |
| 10. Scripting and AI DSP | 34 | 25 | 9 |
| 11. Planned waves and boarded backlog gaps | 68 | 3 | 65 |
| **Total** | **441** | **90** | **351** |

- Total capabilities: **441**
- Agent-callable today: **90** (20.4% of the surface)
- Not agent-callable: **351** (79.6%)

### Where the 90 agent-callable capabilities come from

| Route | Rows | What it reaches |
|---|---|---|
| CLI action — the `zene-remote` binary | 30 | `render`, `rendertracks`, `dump`, `compress`, `upgrade`, `makebundle`, `--run-script`, `--import`, `--geometry`, `--config`, `--allowroot`, `--help`, `--version`, and the render options (`-a -b -f -l -m -o -p -s`) |
| CLI action — `tools/mmpz-git/mmpz_git.py` | 10 | `dump`, `compress`, `verify`, `canonicalize`, `diff`, `merge`, `info`, `textconv`, `install`, `uninstall` |
| Lua binding — `--run-script` (Lua API v0) | 38 | transport play / stop / position, tempo, master volume, pattern + note creation and read-back, track rename / volume / panning / mute, instrument parameters, sandboxed project-dir file I/O, MIDI in / out, logging |
| MCP tool — `mcp-lmms-lab` (outside the app) | 12 | `lmms_render`, `lmms_render_matrix`, `lmms_build`, `lmms_test`, `lmms_measure`, `lmms_plugin_check`, `rebase_inventory`, `worktree_list`/`create`/`remove`, `lmms_job_status`/`log`/`list` |

Rows are counted per capability, not per call, so several capabilities share one route. One row (`Freeze / bounce-in-place / stem export`) is marked callable only because *whole-project* render exists.

## Cross-cutting findings

1. **There is no command registry and no way to reach a running instance.** SPEC §7 A11 requires every user-facing action to be a registered command with a stable ID and a JSON schema; A12 requires line-delimited JSON-RPC on a local UNIX socket with an out-of-process `mcp-zene-control` bridge. Grep of the tree for `zene.command`, `mcp-zene-control` and `agent_surface` returns **not found** (`ableton-gap/AGENT-TOOLING.md:19-21`). Every editor, mixer and plugin row here that is not a CLI/Lua/MCP row is `NONE` for that same single reason.
2. **The mixer has no agent surface at all** — 34 capabilities, 0 callable. Channels, sends, sidechains, buses, effect chains and PDC are C++ + Qt only (`src/core/Mixer.cpp`, `src/gui/MixerView.cpp`). It is the largest single block of `NONE`s in the tree.
3. **Lua is a pattern-editing API, not a DAW-control API.** It adds and removes notes in Beat/Bassline pattern clips and sets tempo, master volume and track gain — and nothing else. It cannot add a mixer channel, load a plugin, move a Song-Editor clip, write automation, or start a recording.
4. **Two 'gaps' partly exist.** `rendertracks` (per-track export) is a working CLI action (`src/core/main.cpp:282`, `r->renderTracks()` at `:775`), yet the roadmap-gap register still files per-track / stem export as not-HAVE (`BACKLOG.md:179-181`). Worth reconciling before that gap is sized.
5. **`Song::record()` is a stub** — `src/core/Song.cpp:522` sets `m_recording = true` with a `// TODO: Implement` comment, while the two-track capture prototype (`src/core/audio/MultiTrackRecorder.cpp:34`) is not wired to the engine's record path. Recording depth is boarded as #611.
6. **`RoutingGraph` (the Patcher engine) is built, tested and disarmed** — no GUI or audio-path code includes it (`zene-remote/docs/STATUS.md:44`), so group 4's patcher row counts as `NONE` even though the code exists.
7. **The genuinely agent-first tools live outside the app**: `mmpz-git` (10 CLI subcommands, group 8) and the `lmms-lab` MCP server (12 tools, group 10). Both operate on files and builds, not on a live session.

## Method and limits

- Read-only: no source file was modified and no state-changing git command was run. The only artifact produced is this file.
- Every anchor was machine-verified after authoring: 441 anchors across 117 distinct files all resolve to an existing file and an in-range line. Anchor cells that carried a verbatim source snippet (150 of them) were additionally checked against the anchored line ±3 lines; the only three initial mismatches were ellipsis-truncated snippets and one nested-backtick quote, each confirmed correct by direct read. The `lmms-lab` MCP anchors from the first extraction pass were off by one and pointed at blank lines — they were re-derived from the source and corrected before this file was written.
- Planned items are marked from the wave and task ids in `ableton-gap/PLAN-zene-studio.md` and the three release bars in `zene-remote/docs/STATUS.md:101-266`; where a wave's code is absent from the tree that is stated explicitly (`not found`) rather than assumed.
- Deliberately **excluded** as implementation rather than user-facing capability: `AudioBus` / `MixHelpers` DSP primitives, `LatencyCompensation`'s delay-line internals, `ConfigManager` read/write plumbing, `Engine::init` / `destroy`, the `RemotePlugin` shm transport, the `RecordRingBuffer` / `TrackRecorder` internals, and `Model` / `AutomatableModel` base-class plumbing. Two roadmap items that are engineering, not capability — the green build matrix #609 and the `AudioBuffer` SharedMemory defect #608 (`zene-remote/docs/STATUS.md:186`, `:233`) — are outside the tables.
- `ableton-gap/SPEC-zene-studio.md` and `ableton-gap/AGENT-TOOLING.md` were being edited concurrently with this inventory (mtimes 21:59–22:00 on 2026-09-11); their line anchors are correct as of that revision.

