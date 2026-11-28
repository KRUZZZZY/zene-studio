//! The one command layer. A toolbar button, a menu item, a shortcut, the command palette and a
//! command script all build a `Command` and hand it to `App::run`; nothing else edits the project
//! (pointer gestures use the methods in `state.rs`, which record the same undo steps).
//!
//! Ids follow Zene's control surface where an equivalent exists (`transport.play`, `clip.move`,
//! `note.add`, `mixer.set_volume`, `track.set_mute`, `control.undo`), so a later engine bridge can
//! forward them unchanged. Ids with no counterpart there are view-only (`view.*`, `select.*`).

use rune::{Key, KeyEvent};

use crate::demo::{self, ItemKind};
use crate::engine::EngineCommand;
use crate::model::*;
use crate::state::*;

#[derive(Clone, Debug, PartialEq)]
pub enum Command {
    // transport
    TogglePlay,
    Play,
    Stop,
    Rewind,
    Seek(i64),
    SetTempo(f32),
    NudgeTempo(f32),
    // history
    Undo,
    Redo,
    // selection
    SelectClip(ClipId, bool),
    SelectTrack(TrackId),
    SelectAll,
    Deselect,
    // clips
    MoveClip { id: ClipId, start: i64, track: TrackId },
    ResizeClip { id: ClipId, len: i64 },
    NudgeSelection(i64),
    DuplicateSelection,
    DeleteSelection,
    SplitAtPlayhead,
    ToggleClipMute,
    SetClipGain(ClipId, f32),
    Rename(String),
    AddItem { item: usize, track: Option<TrackId>, tick: i64 },
    AddSelectedItem,
    NewMidiClip { track: TrackId, tick: i64 },
    // tracks and mixer
    SetVolume(TrackId, f32),
    SetPan(TrackId, f32),
    SetMute(TrackId, bool),
    SetSolo(TrackId, bool),
    ToggleMute,
    ToggleSolo,
    SetTrackColor(TrackId, usize),
    // notes
    OpenPianoRoll(ClipId),
    AddNote { clip: ClipId, start: i64, len: i64, pitch: u8, vel: u8 },
    RemoveNote { clip: ClipId, note: NoteId },
    SetVelocity { clip: ClipId, note: NoteId, vel: u8 },
    // grid
    ToggleSnap,
    SetSnap(Snap),
    // view
    ToggleBrowser,
    ToggleInspector,
    ToggleDock,
    ShowMixer,
    ShowPianoRoll,
    ZoomIn,
    ZoomOut,
    ZoomFit,
    SetDensity(Density),
    CycleTheme,
    OpenPalette,
    OpenMenu(MenuId),
    CloseOverlays,
    ResetDemo,
}

/// A key a command is bound to. `primary` is Ctrl (Cmd on macOS).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Chord {
    pub primary: bool,
    pub shift: bool,
    pub key: ChordKey,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ChordKey {
    Char(char),
    Space,
    Delete,
    Home,
    Left,
    Right,
    Escape,
    Enter,
}

const fn key(c: char) -> Option<Chord> {
    Some(Chord { primary: false, shift: false, key: ChordKey::Char(c) })
}
const fn ctrl(c: char) -> Option<Chord> {
    Some(Chord { primary: true, shift: false, key: ChordKey::Char(c) })
}
const fn ctrl_shift(c: char) -> Option<Chord> {
    Some(Chord { primary: true, shift: true, key: ChordKey::Char(c) })
}
const fn named(k: ChordKey) -> Option<Chord> {
    Some(Chord { primary: false, shift: false, key: k })
}

impl Chord {
    pub fn label(&self) -> String {
        let k = match self.key {
            ChordKey::Char('=') => "+".to_string(),
            ChordKey::Char(c) => c.to_uppercase().to_string(),
            ChordKey::Space => "Space".into(),
            ChordKey::Delete => "Del".into(),
            ChordKey::Home => "Home".into(),
            ChordKey::Left => "Left".into(),
            ChordKey::Right => "Right".into(),
            ChordKey::Escape => "Esc".into(),
            ChordKey::Enter => "Enter".into(),
        };
        format!("{}{}{k}", if self.primary { "Ctrl+" } else { "" }, if self.shift { "Shift+" } else { "" })
    }
    pub fn matches(&self, ev: &KeyEvent) -> bool {
        let m = ev.mods;
        match (&ev.key, self.key) {
            (Key::Command(c), ChordKey::Char(k)) => self.primary && c.eq_ignore_ascii_case(&k) && m.shift == self.shift && !m.alt,
            (Key::Text(t), ChordKey::Char(k)) => {
                !self.primary && !m.cmd && !m.alt && t.chars().count() == 1 && t.chars().next().is_some_and(|c| c.eq_ignore_ascii_case(&k)) && (m.shift == self.shift || !k.is_alphabetic())
            }
            (Key::Text(t), ChordKey::Space) => t == " " && !m.cmd && !self.primary,
            (Key::Delete | Key::Backspace, ChordKey::Delete) => !m.cmd,
            (Key::Home, ChordKey::Home) | (Key::Left, ChordKey::Left) | (Key::Right, ChordKey::Right) => !m.cmd && !m.shift,
            (Key::Escape, ChordKey::Escape) | (Key::Enter, ChordKey::Enter) => true,
            _ => false,
        }
    }
}

/// A command a person can find: it has a name, a menu it lives in and, usually, a key.
pub struct Action {
    pub id: &'static str,
    pub title: &'static str,
    pub menu: MenuId,
    pub chord: Option<Chord>,
    pub help: &'static str,
    pub command: Command,
    /// Start a new group in the menu above this item.
    pub divider: bool,
}

const fn act(id: &'static str, title: &'static str, menu: MenuId, chord: Option<Chord>, help: &'static str, command: Command) -> Action {
    Action { id, title, menu, chord, help, command, divider: false }
}
const fn div(mut a: Action) -> Action {
    a.divider = true;
    a
}

use Command as C;
use MenuId as M;

pub static ACTIONS: &[Action] = &[
    act("file.reset_demo", "Reset Demo Project", M::File, None, "Throw away every edit and reload the demo project", C::ResetDemo),
    act("control.undo", "Undo", M::Edit, ctrl('z'), "Undo the last edit", C::Undo),
    act("control.redo", "Redo", M::Edit, ctrl_shift('z'), "Redo the edit you just undid", C::Redo),
    div(act("clip.duplicate", "Duplicate", M::Edit, ctrl('d'), "Copy the selected clips or notes directly after themselves", C::DuplicateSelection)),
    act("clip.delete", "Delete", M::Edit, named(ChordKey::Delete), "Delete the selected clips or notes", C::DeleteSelection),
    act("clip.split", "Split at Playhead", M::Edit, ctrl('e'), "Cut the selected clips where the playhead is", C::SplitAtPlayhead),
    act("clip.toggle_mute", "Mute Clip", M::Edit, key('0'), "Silence the selected clips without removing them", C::ToggleClipMute),
    act("clip.nudge_left", "Nudge Left", M::Edit, named(ChordKey::Left), "Move the selection one grid step earlier", C::NudgeSelection(-1)),
    act("clip.nudge_right", "Nudge Right", M::Edit, named(ChordKey::Right), "Move the selection one grid step later", C::NudgeSelection(1)),
    div(act("select.all", "Select All", M::Edit, ctrl('a'), "Select every clip, or every note when the piano roll has the selection", C::SelectAll)),
    act("select.none", "Deselect", M::Edit, named(ChordKey::Escape), "Clear the selection", C::Deselect),
    div(act("grid.toggle_snap", "Snap to Grid", M::Edit, key('g'), "Turn grid snapping on or off. Hold Alt while dragging to bypass it once", C::ToggleSnap)),
    act("grid.snap_bar", "Grid: 1 bar", M::Snap, None, "Snap to whole bars", C::SetSnap(Snap::Bar)),
    act("grid.snap_half", "Grid: 1/2", M::Snap, None, "Snap to half bars", C::SetSnap(Snap::Half)),
    act("grid.snap_beat", "Grid: 1/4", M::Snap, None, "Snap to beats", C::SetSnap(Snap::Beat)),
    act("grid.snap_eighth", "Grid: 1/8", M::Snap, None, "Snap to eighth notes", C::SetSnap(Snap::Eighth)),
    act("grid.snap_sixteenth", "Grid: 1/16", M::Snap, None, "Snap to sixteenth notes", C::SetSnap(Snap::Sixteenth)),
    act("view.browser", "Browser", M::View, key('b'), "Show or hide the browser", C::ToggleBrowser),
    act("view.inspector", "Inspector", M::View, key('i'), "Show or hide the inspector", C::ToggleInspector),
    act("view.mixer", "Mixer", M::View, key('x'), "Show the mixer in the dock, or hide the dock if it is showing", C::ShowMixer),
    act("view.piano_roll", "Piano Roll", M::View, key('p'), "Show the piano roll in the dock, or hide the dock if it is showing", C::ShowPianoRoll),
    div(act("view.zoom_in", "Zoom In", M::View, ctrl('='), "Zoom the timeline in. Ctrl+wheel zooms at the pointer", C::ZoomIn)),
    act("view.zoom_out", "Zoom Out", M::View, ctrl('-'), "Zoom the timeline out", C::ZoomOut),
    act("view.zoom_fit", "Zoom to Fit Song", M::View, key('f'), "Fit the whole song in the timeline", C::ZoomFit),
    div(act("view.tracks_compact", "Track Height: Compact", M::View, None, "Shorter lanes, more tracks on screen", C::SetDensity(Density::Compact))),
    act("view.tracks_standard", "Track Height: Standard", M::View, None, "The default lane height", C::SetDensity(Density::Standard)),
    act("view.tracks_tall", "Track Height: Tall", M::View, None, "Taller lanes for detailed clip work", C::SetDensity(Density::Tall)),
    div(act("view.theme", "Switch Theme (System, Light, Dark)", M::View, None, "Follow the system appearance, or force light or dark", C::CycleTheme)),
    act("transport.toggle", "Play / Stop", M::Transport, named(ChordKey::Space), "Start or stop simulated playback", C::TogglePlay),
    act("transport.rewind", "Return to Start", M::Transport, named(ChordKey::Home), "Move the playhead to bar 1", C::Rewind),
    div(act("transport.tempo_up", "Tempo +1", M::Transport, None, "Raise the tempo by 1 bpm", C::NudgeTempo(1.0))),
    act("transport.tempo_down", "Tempo -1", M::Transport, None, "Lower the tempo by 1 bpm", C::NudgeTempo(-1.0)),
    div(act("track.toggle_mute", "Mute Track", M::Transport, key('m'), "Mute or unmute the selected track", C::ToggleMute)),
    act("track.toggle_solo", "Solo Track", M::Transport, key('s'), "Solo or unsolo the selected track", C::ToggleSolo),
    act("help.commands", "Search Commands", M::Help, ctrl('k'), "Find any command by name and see where it lives", C::OpenPalette),
    act("browser.add_selected", "Add Browser Item to Track", M::File, named(ChordKey::Enter), "Add the highlighted browser item to the selected track at the playhead", C::AddSelectedItem),
];

pub fn action(id: &str) -> Option<&'static Action> {
    ACTIONS.iter().find(|a| a.id == id)
}

pub fn menu_title(m: MenuId) -> &'static str {
    match m {
        M::File => "File",
        M::Edit => "Edit",
        M::View => "View",
        M::Transport => "Transport",
        M::Help => "Help",
        M::Snap => "Edit > Grid",
    }
}

/// The action bound to a key event, if any.
pub fn action_for_key(ev: &KeyEvent) -> Option<&'static Action> {
    ACTIONS.iter().find(|a| a.chord.is_some_and(|c| c.matches(ev)))
}

/// Parse one line of a command script: an id, then its arguments.
/// `transport.set_tempo 120`, `clip.move 104 7680 1`, `mixer.set_volume 3 -9.5`.
pub fn parse(line: &str) -> Result<Command, String> {
    let line = line.trim();
    let (id, rest) = line.split_once(char::is_whitespace).unwrap_or((line, ""));
    let args: Vec<&str> = rest.split_whitespace().collect();
    let n = |i: usize| -> Result<i64, String> {
        args.get(i).ok_or(format!("{id}: missing argument {}", i + 1))?.parse::<i64>().map_err(|e| format!("{id}: {e}"))
    };
    let f = |i: usize| -> Result<f32, String> {
        args.get(i).ok_or(format!("{id}: missing argument {}", i + 1))?.parse::<f32>().map_err(|e| format!("{id}: {e}"))
    };
    Ok(match id {
        "transport.play" => C::Play,
        "transport.stop" => C::Stop,
        "transport.seek" => C::Seek(n(0)?),
        "transport.set_tempo" => C::SetTempo(f(0)?),
        "select.clip" => C::SelectClip(n(0)? as u32, false),
        "select.track" => C::SelectTrack(n(0)? as u32),
        "clip.move" => C::MoveClip { id: n(0)? as u32, start: n(1)?, track: n(2)? as u32 },
        "clip.resize" => C::ResizeClip { id: n(0)? as u32, len: n(1)? },
        "clip.set_gain" => C::SetClipGain(n(0)? as u32, f(1)?),
        "clip.rename" => C::Rename(rest.to_string()),
        "clip.add" => C::AddItem { item: n(0)? as usize, track: Some(n(1)? as u32), tick: n(2)? },
        "mixer.set_volume" => C::SetVolume(n(0)? as u32, f(1)?),
        "mixer.set_pan" => C::SetPan(n(0)? as u32, f(1)?),
        "track.set_mute" => C::SetMute(n(0)? as u32, n(1)? != 0),
        "track.set_solo" => C::SetSolo(n(0)? as u32, n(1)? != 0),
        "note.add" => C::AddNote { clip: n(0)? as u32, start: n(1)?, len: n(2)?, pitch: n(3)? as u8, vel: n(4)? as u8 },
        "note.remove" => C::RemoveNote { clip: n(0)? as u32, note: n(1)? as u32 },
        "note.velocity_set" => C::SetVelocity { clip: n(0)? as u32, note: n(1)? as u32, vel: n(2)? as u8 },
        "window.note_editor_set" => C::OpenPianoRoll(n(0)? as u32),
        _ => action(id).map(|a| a.command.clone()).ok_or(format!("unknown command: {id}"))?,
    })
}

impl App {
    /// Run a script: one command a line; blank lines and `#` comments are skipped.
    pub fn run_script(&mut self, text: &str) -> Result<usize, String> {
        let mut ran = 0;
        for (i, line) in text.lines().enumerate() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let c = parse(line).map_err(|e| format!("line {}: {e}", i + 1))?;
            self.run(c);
            ran += 1;
        }
        Ok(ran)
    }

    pub fn run(&mut self, command: Command) {
        match command {
            C::TogglePlay => {
                let c = if self.playing() { C::Stop } else { C::Play };
                self.run(c);
            }
            C::Play => {
                if self.position() >= self.project.length {
                    self.engine.send(EngineCommand::Seek(0));
                }
                self.engine.send(EngineCommand::Play);
            }
            C::Stop => self.engine.send(EngineCommand::Stop),
            C::Rewind => {
                self.engine.send(EngineCommand::Seek(0));
                self.set_scroll(0.0);
            }
            C::Seek(t) => self.engine.send(EngineCommand::Seek(t.clamp(0, self.project.length))),
            C::SetTempo(bpm) => self.edit("Change tempo", |p| p.tempo = (bpm * 10.0).round().clamp(400.0, 2400.0) / 10.0),
            C::NudgeTempo(d) => {
                let t = self.project.tempo + d;
                self.run(C::SetTempo(t));
            }
            C::Undo => self.undo(),
            C::Redo => self.redo(),
            C::SelectClip(id, additive) => {
                if self.project.clip(id).is_some() { self.select_clip(id, additive); }
            }
            C::SelectTrack(id) => {
                if self.project.track(id).is_some() { self.select_track(id); }
            }
            C::SelectAll => {
                if self.notes_active() {
                    self.sel_notes = self.pr_notes().iter().map(|n| n.id).collect();
                } else {
                    self.sel_notes.clear();
                    self.sel = Selection::Clips(self.project.clips.iter().map(|c| c.id).collect());
                    self.sync_name_edit(false);
                }
            }
            C::Deselect => self.clear_selection(),
            C::MoveClip { id, start, track } => {
                let ok = self.project.clip(id).is_some_and(|c| self.project.accepts(track, c.is_midi()));
                if ok {
                    self.edit("Move clip", |p| {
                        if let Some(c) = p.clip_mut(id) {
                            c.start = start.max(0);
                            c.track = track;
                        }
                        p.trim_under(&[id]);
                    });
                }
            }
            C::ResizeClip { id, len } => self.edit("Resize clip", |p| {
                if let Some(c) = p.clip_mut(id) { c.len = len.max(TPB / 4); }
                p.trim_under(&[id]);
            }),
            C::NudgeSelection(steps) => {
                let d = steps * self.snap.ticks();
                if self.notes_active() {
                    let (clip, ids) = (self.pr_clip, self.sel_notes.clone());
                    self.edit("Nudge notes", |p| {
                        let Some(c) = clip.and_then(|id| p.clip_mut(id)) else { return };
                        let len = c.len;
                        let Some(notes) = c.notes_mut() else { return };
                        if notes.iter().filter(|n| ids.contains(&n.id)).all(|n| n.start + d >= 0 && n.start + d < len) {
                            notes.iter_mut().filter(|n| ids.contains(&n.id)).for_each(|n| n.start += d);
                        }
                    });
                } else {
                    let ids = self.sel.clips().to_vec();
                    self.edit("Nudge clips", |p| {
                        if ids.iter().filter_map(|id| p.clip(*id)).all(|c| c.start + d >= 0) {
                            for id in &ids {
                                if let Some(c) = p.clip_mut(*id) { c.start += d; }
                            }
                            p.trim_under(&ids);
                        }
                    });
                }
            }
            C::DuplicateSelection => self.duplicate(),
            C::DeleteSelection => {
                if self.notes_active() {
                    let (clip, ids) = (self.pr_clip, std::mem::take(&mut self.sel_notes));
                    self.edit("Delete notes", |p| {
                        if let Some(notes) = clip.and_then(|id| p.clip_mut(id)).and_then(|c| c.notes_mut()) {
                            notes.retain(|n| !ids.contains(&n.id));
                        }
                    });
                } else {
                    let ids = self.sel.clips().to_vec();
                    if !ids.is_empty() {
                        self.edit("Delete clips", |p| p.clips.retain(|c| !ids.contains(&c.id)));
                    }
                }
            }
            C::SplitAtPlayhead => {
                let (at, ids) = (self.position(), self.sel.clips().to_vec());
                self.edit("Split clips", |p| {
                    for id in ids {
                        let Some(c) = p.clip(id).filter(|c| c.start < at && at < c.end()).cloned() else { continue };
                        let new_id = p.fresh_id();
                        let mut tail = c.clone();
                        tail.id = new_id;
                        tail.start = at;
                        tail.len = c.end() - at;
                        let cut = at - c.start;
                        match &mut tail.kind {
                            ClipKind::Audio { offset, .. } => *offset += cut,
                            ClipKind::Midi { notes } => {
                                notes.retain(|n| n.start >= cut);
                                notes.iter_mut().for_each(|n| n.start -= cut);
                                let mut next = p.next_id;
                                notes.iter_mut().for_each(|n| { next += 1; n.id = next; });
                                p.next_id = next;
                            }
                        }
                        if let Some(head) = p.clip_mut(id) {
                            head.len = cut;
                            if let Some(notes) = head.notes_mut() { notes.retain(|n| n.start < cut); }
                        }
                        p.clips.push(tail);
                    }
                });
            }
            C::ToggleClipMute => {
                let ids = self.sel.clips().to_vec();
                self.edit("Mute clips", |p| {
                    let all = ids.iter().filter_map(|id| p.clip(*id)).all(|c| c.muted);
                    for id in &ids {
                        if let Some(c) = p.clip_mut(*id) { c.muted = !all; }
                    }
                });
            }
            C::SetClipGain(id, db) => self.edit("Change clip gain", |p| {
                if let Some(c) = p.clip_mut(id) { c.gain_db = db.clamp(-24.0, 12.0); }
            }),
            C::Rename(name) => {
                let name = name.trim().to_string();
                if name.is_empty() { return; }
                match self.sel.clone() {
                    Selection::Track(t) => self.edit("Rename track", |p| {
                        if let Some(t) = p.track_mut(t) { t.name = name; }
                    }),
                    Selection::Clips(v) if v.len() == 1 => self.edit("Rename clip", |p| {
                        if let Some(c) = p.clip_mut(v[0]) { c.name = name; }
                    }),
                    _ => {}
                }
            }
            C::AddItem { item, track, tick } => self.add_item(item, track, tick),
            C::AddSelectedItem => {
                let Some(item) = self.browse_sel else { return };
                let midi = demo::LIBRARY[item].kind == ItemKind::Midi;
                let track = self.focus_track().filter(|t| self.project.accepts(*t, midi));
                let tick = snap_floor(self.position(), self.grid(false));
                self.add_item(item, track, tick);
            }
            C::NewMidiClip { track, tick } => {
                if !self.project.accepts(track, true) { return; }
                let mut made = None;
                self.edit("New clip", |p| {
                    let id = p.fresh_id();
                    p.clips.push(Clip { id, track, start: tick, len: BAR, name: "New clip".into(), gain_db: 0.0, muted: false, kind: ClipKind::Midi { notes: Vec::new() } });
                    p.trim_under(&[id]);
                    made = Some(id);
                });
                if let Some(id) = made {
                    self.select_clip(id, false);
                    self.run(C::OpenPianoRoll(id));
                }
            }
            C::SetVolume(id, db) => self.edit("Change volume", |p| {
                if let Some(t) = p.track_mut(id) { t.volume_db = db.clamp(-60.0, 6.0); }
            }),
            C::SetPan(id, pan) => self.edit("Change pan", |p| {
                if let Some(t) = p.track_mut(id) { t.pan = pan.clamp(-1.0, 1.0); }
            }),
            C::SetMute(id, on) => self.edit(if on { "Mute track" } else { "Unmute track" }, |p| {
                if let Some(t) = p.track_mut(id) { t.mute = on; }
            }),
            C::SetSolo(id, on) => self.edit(if on { "Solo track" } else { "Unsolo track" }, |p| {
                if let Some(t) = p.track_mut(id) { t.solo = on; }
            }),
            C::ToggleMute => {
                if let Some(t) = self.focus_track().and_then(|t| self.project.track(t)) {
                    let c = C::SetMute(t.id, !t.mute);
                    self.run(c);
                } else {
                    self.status = "Select a track to mute it".into();
                }
            }
            C::ToggleSolo => {
                if let Some(t) = self.focus_track().and_then(|t| self.project.track(t)) {
                    let c = C::SetSolo(t.id, !t.solo);
                    self.run(c);
                } else {
                    self.status = "Select a track to solo it".into();
                }
            }
            C::SetTrackColor(id, color) => self.edit("Change track colour", |p| {
                if let Some(t) = p.track_mut(id) { t.color = color; }
            }),
            C::OpenPianoRoll(id) => {
                let Some(c) = self.project.clip(id).filter(|c| c.is_midi()) else {
                    self.status = "The piano roll edits MIDI clips. Audio clips have no notes".into();
                    return;
                };
                // Centre the clip's notes vertically; an empty clip opens around middle C.
                let (lo, hi) = c.notes().iter().fold((127u8, 0u8), |(lo, hi), n| (lo.min(n.pitch), hi.max(n.pitch)));
                let mid = if lo > hi { 60.0 } else { (lo as f32 + hi as f32) / 2.0 };
                self.pr_set_scroll_y((PITCH_HI as f32 - mid) * NOTE_H - 70.0);
                if self.pr_clip != Some(id) {
                    self.sel_notes.clear();
                    self.pr_scroll = 0.0;
                }
                self.pr_clip = Some(id);
                self.show_dock = true;
                self.dock_tab = DockTab::PianoRoll;
            }
            C::AddNote { clip, start, len, pitch, vel } => {
                self.edit("Add note", |p| {
                    let id = p.fresh_id();
                    if let Some(notes) = p.clip_mut(clip).and_then(|c| c.notes_mut()) {
                        notes.push(Note { id, start: start.max(0), len: len.max(TPB / 8), pitch: pitch.clamp(PITCH_LO, PITCH_HI), vel: vel.clamp(1, 127) });
                    }
                });
            }
            C::RemoveNote { clip, note } => {
                self.sel_notes.retain(|n| *n != note);
                self.edit("Delete note", |p| {
                    if let Some(notes) = p.clip_mut(clip).and_then(|c| c.notes_mut()) { notes.retain(|n| n.id != note); }
                });
            }
            C::SetVelocity { clip, note, vel } => self.edit("Change velocity", |p| {
                if let Some(n) = p.clip_mut(clip).and_then(|c| c.notes_mut()).and_then(|ns| ns.iter_mut().find(|n| n.id == note)) {
                    n.vel = vel.clamp(1, 127);
                }
            }),
            C::ToggleSnap => {
                self.snap_on = !self.snap_on;
                self.status = if self.snap_on { format!("Snap on: {}", self.snap.label()) } else { "Snap off".into() };
            }
            C::SetSnap(s) => {
                self.snap = s;
                self.snap_on = true;
            }
            C::ToggleBrowser => self.show_browser = !self.show_browser,
            C::ToggleInspector => self.show_inspector = !self.show_inspector,
            C::ToggleDock => self.show_dock = !self.show_dock,
            C::ShowMixer => self.show_tab(DockTab::Mixer),
            C::ShowPianoRoll => {
                // Bring the selected MIDI clip along, so the key does what the eye expects.
                if let Some(id) = self.sel.clips().first().copied().filter(|id| self.project.clip(*id).is_some_and(|c| c.is_midi())) {
                    if self.pr_clip != Some(id) || !self.show_dock || self.dock_tab != DockTab::PianoRoll {
                        return self.run(C::OpenPianoRoll(id));
                    }
                }
                self.show_tab(DockTab::PianoRoll);
            }
            C::ZoomIn => self.zoom_about(1.25, self.lanes_width() / 2.0),
            C::ZoomOut => self.zoom_about(0.8, self.lanes_width() / 2.0),
            C::ZoomFit => self.zoom_fit(),
            C::SetDensity(d) => self.density = d,
            C::CycleTheme => {
                self.theme = match self.theme {
                    None => Some(false),
                    Some(false) => Some(true),
                    Some(true) => None,
                };
                self.status = match self.theme {
                    None => "Theme: follow system",
                    Some(false) => "Theme: light",
                    Some(true) => "Theme: dark",
                }
                .into();
            }
            C::OpenPalette => {
                self.menu = None;
                self.context = None;
                let mut query = rune::TextEdit::new("");
                query.focused = true;
                self.palette = Some(Palette { query, picked: 0 });
            }
            C::OpenMenu(m) => {
                self.context = None;
                self.menu = if self.menu == Some(m) { None } else { Some(m) };
            }
            C::CloseOverlays => {
                self.menu = None;
                self.context = None;
                self.palette = None;
            }
            C::ResetDemo => {
                let fresh = demo::project();
                self.edit("Reset demo project", |p| *p = fresh);
                self.clear_selection();
                self.engine.send(EngineCommand::Stop);
                self.engine.send(EngineCommand::Seek(0));
            }
        }
        self.ensure_meters();
    }

    fn show_tab(&mut self, tab: DockTab) {
        if self.show_dock && self.dock_tab == tab {
            self.show_dock = false;
        } else {
            self.show_dock = true;
            self.dock_tab = tab;
        }
    }

    /// Whether Delete, Duplicate, Nudge and Select All speak for notes rather than clips.
    pub fn notes_active(&self) -> bool {
        !self.sel_notes.is_empty() && self.show_dock && self.dock_tab == DockTab::PianoRoll && self.pr_clip.is_some()
    }
    pub fn pr_notes(&self) -> &[Note] {
        self.pr_clip.and_then(|id| self.project.clip(id)).map_or(&[], |c| c.notes())
    }

    fn duplicate(&mut self) {
        if self.notes_active() {
            let (clip, ids) = (self.pr_clip, self.sel_notes.clone());
            let mut made = Vec::new();
            self.edit("Duplicate notes", |p| {
                let mut next = p.next_id;
                let Some(notes) = clip.and_then(|id| p.clip_mut(id)).and_then(|c| c.notes_mut()) else { return };
                let picked: Vec<Note> = notes.iter().filter(|n| ids.contains(&n.id)).copied().collect();
                let (lo, hi) = picked.iter().fold((i64::MAX, 0), |(lo, hi), n| (lo.min(n.start), hi.max(n.start + n.len)));
                for mut n in picked {
                    next += 1;
                    n.id = next;
                    n.start += hi - lo;
                    made.push(n.id);
                    notes.push(n);
                }
                p.next_id = next;
            });
            if !made.is_empty() { self.sel_notes = made; }
            return;
        }
        let ids = self.sel.clips().to_vec();
        if ids.is_empty() { return; }
        let mut made = Vec::new();
        self.edit("Duplicate clips", |p| {
            let picked: Vec<Clip> = ids.iter().filter_map(|id| p.clip(*id)).cloned().collect();
            let (lo, hi) = picked.iter().fold((i64::MAX, 0), |(lo, hi), c| (lo.min(c.start), hi.max(c.end())));
            for mut c in picked {
                c.id = p.fresh_id();
                c.start += hi - lo;
                if let Some(notes) = c.notes_mut() {
                    for n in notes { n.id = { p.next_id += 1; p.next_id }; }
                }
                made.push(c.id);
                p.clips.push(c);
            }
            p.trim_under(&made);
        });
        if !made.is_empty() {
            self.sel = Selection::Clips(made);
            self.sync_name_edit(false);
        }
    }

    /// Put a browser item on the timeline; with no usable track it gets a new one.
    pub fn add_item(&mut self, item: usize, track: Option<TrackId>, tick: i64) {
        let Some(it) = demo::LIBRARY.get(item) else { return };
        let midi = it.kind == ItemKind::Midi;
        let track = track.filter(|t| self.project.accepts(*t, midi));
        let mut made = None;
        self.edit("Add clip from browser", |p| {
            let track = track.unwrap_or_else(|| {
                let id = p.fresh_id();
                let lanes = p.lanes().count();
                let at = p.tracks.iter().rposition(|t| matches!(t.kind, TrackKind::Audio | TrackKind::Instrument)).map_or(0, |i| i + 1);
                p.tracks.insert(at, Track {
                    id,
                    name: it.name.split_whitespace().next().unwrap_or("Track").to_string(),
                    kind: if midi { TrackKind::Instrument } else { TrackKind::Audio },
                    color: (demo::PACKS[it.pack].color + lanes) % 8,
                    volume_db: -6.0,
                    pan: 0.0,
                    mute: false,
                    solo: false,
                    output: None,
                    source: if midi { demo::PACKS[it.pack].name.to_string() } else { String::new() },
                });
                id
            });
            let id = p.fresh_id();
            let seed = item as u32 * 7 + 3;
            let kind = if midi {
                let mut next = p.next_id;
                let notes = demo::synth_notes(it.flavor, it.beats, seed, &mut next);
                p.next_id = next;
                ClipKind::Midi { notes }
            } else {
                ClipKind::Audio { peaks: demo::synth_peaks(it.flavor, it.beats, seed), offset: 0 }
            };
            p.clips.push(Clip { id, track, start: tick.max(0), len: it.beats as i64 * TPB, name: it.name.into(), gain_db: 0.0, muted: false, kind });
            p.trim_under(&[id]);
            p.length = p.length.max(p.clips.iter().map(|c| c.end()).max().unwrap_or(0));
            made = Some(id);
        });
        if let Some(id) = made {
            self.ensure_meters();
            self.select_clip(id, false);
            self.status = format!("Added {}", it.name);
        }
    }
}
