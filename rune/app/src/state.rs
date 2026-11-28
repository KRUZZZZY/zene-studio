//! Application state: the project, the engine behind its boundary, undo history, selection
//! and view state. Views read it; only `command.rs` and the gesture methods here change it.

use rune::{Live, LiveText, TextEdit};

use crate::art::Art;
use crate::demo;
use crate::engine::{Engine, EngineCommand, SimEngine};
use crate::model::*;

#[derive(Clone, Debug, PartialEq, Default)]
pub enum Selection {
    #[default]
    None,
    Track(TrackId),
    Clips(Vec<ClipId>),
}

impl Selection {
    pub fn has_clip(&self, id: ClipId) -> bool {
        matches!(self, Selection::Clips(v) if v.contains(&id))
    }
    pub fn clips(&self) -> &[ClipId] {
        match self {
            Selection::Clips(v) => v,
            _ => &[],
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DockTab {
    Mixer,
    PianoRoll,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Density {
    Compact,
    Standard,
    Tall,
}

impl Density {
    pub fn track_height(self) -> f32 {
        match self {
            Density::Compact => 40.0,
            Density::Standard => 56.0,
            Density::Tall => 80.0,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MenuId {
    File,
    Edit,
    View,
    Transport,
    Help,
    Snap,
}

/// A context menu opened at a window point.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ContextMenu {
    pub x: f32,
    pub y: f32,
}

/// A browser item being dragged, and where it would land.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ItemDrag {
    pub item: usize,
    /// Pointer, in window points.
    pub x: f32,
    pub y: f32,
    pub target: Option<DropTarget>,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DropTarget {
    /// The lane to drop on; `None` makes a new track below the last lane.
    pub track: Option<TrackId>,
    pub tick: i64,
}

pub struct Palette {
    pub query: TextEdit,
    pub picked: usize,
}

/// One strip's meter, bound to live values so playback redraws without re-running the view.
pub struct MeterLive {
    pub track: TrackId,
    /// Fill rect in the header's horizontal meter, `[x, y, w, h]` in its parent's points.
    pub header: Live<[f32; 4]>,
    /// Fill rect in the mixer strip's vertical meter.
    pub strip: Live<[f32; 4]>,
    /// Peak-hold line in the mixer strip.
    pub hold: Live<[f32; 4]>,
}

pub const HEADER_METER_W: f32 = 56.0;
pub const HEADER_METER_H: f32 = 4.0;
pub const STRIP_METER_W: f32 = 8.0;
pub const STRIP_METER_H: f32 = 98.0;

pub const IDLE_HINT: &str = "Hover a control to see what it does. Ctrl+K searches every command.";
pub const HEADER_W: f32 = 196.0;
pub const RULER_H: f32 = 24.0;
pub const VEL_H: f32 = 52.0;
pub const KEYS_W: f32 = 46.0;
pub const NOTE_H: f32 = 14.0;
pub const PITCH_LO: u8 = 24;
pub const PITCH_HI: u8 = 96;

pub struct Lives {
    pub playhead: Live<[f32; 2]>,
    pub pr_playhead: Live<[f32; 2]>,
    pub position: LiveText,
    pub clock: LiveText,
    /// The help line: what the hovered control does.
    pub hint: LiveText,
    pub meters: Vec<MeterLive>,
}

pub struct App {
    pub project: Project,
    pub engine: Box<dyn Engine>,
    pub undo: Vec<(Project, &'static str)>,
    pub redo: Vec<(Project, &'static str)>,
    /// The project as it was when the gesture in progress began.
    pub gesture: Option<(Project, &'static str)>,

    pub sel: Selection,
    /// Selected notes in the piano roll's clip. Selecting clips clears it and vice versa, so
    /// Delete and Duplicate always have one unambiguous target.
    pub sel_notes: Vec<NoteId>,

    pub snap_on: bool,
    pub snap: Snap,
    /// Arrangement zoom, in points per beat, and the tick at the lanes' left edge.
    pub ppb: f32,
    pub scroll: f64,
    pub density: Density,
    pub show_browser: bool,
    pub show_inspector: bool,
    pub show_dock: bool,
    pub dock_tab: DockTab,
    /// `None` follows the system; `Some(dark)` is the user's choice.
    pub theme: Option<bool>,

    pub pr_clip: Option<ClipId>,
    pub pr_ppb: f32,
    /// Ticks from the clip's start at the grid's left edge.
    pub pr_scroll: f64,
    pub last_note_len: i64,
    /// Vertical scroll of the piano roll's key rows, in points.
    pub pr_scroll_y: f32,
    /// Vertical scroll of the arrangement lanes, in points.
    pub scroll_y: f32,
    /// The notes a note drag started from (the drag applies deltas to these).
    pub note_origin: Vec<Note>,
    /// A marquee being dragged in the piano roll, `[x0, y0, x1, y1]` in grid points.
    pub marquee: Option<[f32; 4]>,
    pub last_vel: u8,
    /// Scroll position when a middle-button pan began.
    pub pan_origin: (f64, f32),
    /// Whether the last view was built while playing (poll re-runs the view when it flips).
    pub was_playing: bool,
    /// Edit target while the palette is closed (the field accessor must return something).
    pub scratch: TextEdit,

    pub menu: Option<MenuId>,
    pub context: Option<ContextMenu>,
    pub palette: Option<Palette>,

    pub search: TextEdit,
    pub filter_kind: Option<demo::ItemKind>,
    pub filter_pack: Option<usize>,
    pub browse_sel: Option<usize>,
    pub item_drag: Option<ItemDrag>,

    pub name_edit: TextEdit,
    /// What `name_edit` currently mirrors, so a selection change refills it once.
    pub name_for: Selection,

    pub hint: &'static str,
    pub status: String,

    /// Observed window rects, for hit-testing drops and sizing the visible range.
    pub lanes_rect: [f32; 4],
    pub pr_rect: [f32; 4],
    pub live: Lives,
    pub art: Art,
}

impl App {
    pub fn new() -> App {
        let project = demo::project();
        let engine = Box::new(SimEngine::new(project.tempo));
        App::with(project, engine)
    }

    /// The app on a given project and engine (the demo and the simulation, or a real engine).
    pub fn with(project: Project, engine: Box<dyn Engine>) -> App {
        let live = Lives {
            playhead: Live::new([0.0, 0.0]),
            pr_playhead: Live::new([-10.0, 0.0]),
            position: LiveText::new("1.1.000"),
            clock: LiveText::new("0:00.0"),
            hint: LiveText::new(IDLE_HINT),
            meters: project
                .tracks
                .iter()
                .map(|t| MeterLive {
                    track: t.id,
                    header: Live::new([0.0, 0.0, 0.0, HEADER_METER_H]),
                    strip: Live::new([0.0, STRIP_METER_H, STRIP_METER_W, 0.0]),
                    hold: Live::new([0.0, STRIP_METER_H, STRIP_METER_W, 0.0]),
                })
                .collect(),
        };
        let mut app = App {
            project,
            engine,
            undo: Vec::new(),
            redo: Vec::new(),
            gesture: None,
            sel: Selection::None,
            sel_notes: Vec::new(),
            snap_on: true,
            snap: Snap::Beat,
            ppb: 10.5,
            scroll: 0.0,
            density: Density::Standard,
            show_browser: true,
            show_inspector: true,
            show_dock: true,
            dock_tab: DockTab::Mixer,
            theme: None,
            pr_clip: None,
            pr_ppb: 44.0,
            pr_scroll: 0.0,
            last_note_len: TPB / 2,
            pr_scroll_y: 420.0,
            scroll_y: 0.0,
            note_origin: Vec::new(),
            marquee: None,
            last_vel: 96,
            pan_origin: (0.0, 0.0),
            was_playing: false,
            scratch: TextEdit::new(""),
            menu: None,
            context: None,
            palette: None,
            search: TextEdit::new(""),
            filter_kind: None,
            filter_pack: None,
            browse_sel: None,
            item_drag: None,
            name_edit: TextEdit::new(""),
            name_for: Selection::None,
            hint: "",
            status: String::new(),
            lanes_rect: [432.0, 98.0, 686.0, 390.0],
            pr_rect: [282.0, 518.0, 836.0, 174.0],
            live,
            art: Art::new(),
        };
        app.sync_live();
        app
    }

    // ---- time and geometry -------------------------------------------------------------

    pub fn px_per_tick(&self) -> f64 {
        self.ppb as f64 / TPB as f64
    }
    pub fn tick_to_x(&self, tick: f64) -> f32 {
        ((tick - self.scroll) * self.px_per_tick()) as f32
    }
    pub fn x_to_tick(&self, x: f32) -> f64 {
        self.scroll + x as f64 / self.px_per_tick()
    }
    pub fn pr_px_per_tick(&self) -> f64 {
        self.pr_ppb as f64 / TPB as f64
    }
    pub fn pr_tick_to_x(&self, tick: f64) -> f32 {
        ((tick - self.pr_scroll) * self.pr_px_per_tick()) as f32
    }
    pub fn pr_x_to_tick(&self, x: f32) -> f64 {
        self.pr_scroll + x as f64 / self.pr_px_per_tick()
    }
    /// The snap grid in ticks; 1 when snapping is off or bypassed.
    pub fn grid(&self, bypass: bool) -> i64 {
        if self.snap_on && !bypass { self.snap.ticks() } else { 1 }
    }
    pub fn track_height(&self) -> f32 {
        self.density.track_height()
    }
    pub fn lanes_width(&self) -> f32 {
        self.lanes_rect[2].max(100.0)
    }

    pub fn set_scroll(&mut self, tick: f64) {
        let visible = self.lanes_width() as f64 / self.px_per_tick();
        let max = (self.project.length as f64 + 4.0 * BAR as f64 - visible).max(0.0);
        self.scroll = tick.clamp(0.0, max);
    }
    /// Zoom by `factor`, keeping the tick under lane x `anchor` where it is.
    pub fn zoom_about(&mut self, factor: f32, anchor: f32) {
        let tick = self.x_to_tick(anchor);
        self.ppb = (self.ppb * factor).clamp(2.5, 160.0);
        self.set_scroll(tick - anchor as f64 / self.px_per_tick());
    }
    pub fn zoom_fit(&mut self) {
        self.ppb = (self.lanes_width() / (self.project.length as f32 / TPB as f32 + 2.0)).clamp(2.5, 160.0);
        self.scroll = 0.0;
    }
    pub fn pr_set_scroll(&mut self, tick: f64) {
        let len = self.pr_clip.and_then(|id| self.project.clip(id)).map_or(BAR, |c| c.len) as f64;
        let visible = self.pr_rect[2].max(100.0) as f64 / self.pr_px_per_tick();
        self.pr_scroll = tick.clamp(0.0, (len + BAR as f64 - visible).max(0.0));
    }
    pub fn pr_zoom_about(&mut self, factor: f32, anchor: f32) {
        let tick = self.pr_x_to_tick(anchor);
        self.pr_ppb = (self.pr_ppb * factor).clamp(8.0, 240.0);
        self.pr_set_scroll(tick - anchor as f64 / self.pr_px_per_tick());
    }

    // ---- undoable edits ----------------------------------------------------------------

    /// One undoable step: `f` edits the project; nothing is recorded if it changed nothing.
    pub fn edit(&mut self, label: &'static str, f: impl FnOnce(&mut Project)) {
        let before = self.project.clone();
        f(&mut self.project);
        if self.project != before {
            self.undo.push((before, label));
            self.redo.clear();
            self.after_project_change();
        }
    }
    /// A drag starts: remember the project, so the whole drag is one undo step.
    pub fn begin_gesture(&mut self, label: &'static str) {
        if self.gesture.is_none() {
            self.gesture = Some((self.project.clone(), label));
        }
    }
    pub fn end_gesture(&mut self) {
        if let Some((before, label)) = self.gesture.take() {
            if self.project != before {
                self.undo.push((before, label));
                self.redo.clear();
            }
            self.after_project_change();
        }
    }
    pub fn cancel_gesture(&mut self) {
        if let Some((before, _)) = self.gesture.take() {
            self.project = before;
            self.after_project_change();
        }
    }
    pub fn undo(&mut self) {
        if let Some((p, label)) = self.undo.pop() {
            self.redo.push((std::mem::replace(&mut self.project, p), label));
            self.status = format!("Undid: {label}");
            self.after_project_change();
        }
    }
    pub fn redo(&mut self) {
        if let Some((p, label)) = self.redo.pop() {
            self.undo.push((std::mem::replace(&mut self.project, p), label));
            self.status = format!("Redid: {label}");
            self.after_project_change();
        }
    }
    /// Drop selection and editor targets that no longer exist, and tell the engine what it
    /// needs to know.
    pub fn after_project_change(&mut self) {
        let p = &self.project;
        self.sel = match std::mem::take(&mut self.sel) {
            Selection::Clips(v) => {
                let v: Vec<ClipId> = v.into_iter().filter(|id| p.clip(*id).is_some()).collect();
                if v.is_empty() { Selection::None } else { Selection::Clips(v) }
            }
            Selection::Track(t) if p.track(t).is_none() => Selection::None,
            other => other,
        };
        if self.pr_clip.is_some_and(|id| p.clip(id).is_none_or(|c| !c.is_midi())) {
            self.pr_clip = None;
        }
        let notes = self.pr_clip.and_then(|id| p.clip(id)).map(|c| c.notes());
        self.sel_notes.retain(|n| notes.is_some_and(|ns| ns.iter().any(|x| x.id == *n)));
        self.engine.send(EngineCommand::SetTempo(self.project.tempo));
        self.sync_name_edit(true);
    }

    // ---- selection ---------------------------------------------------------------------

    pub fn select_clip(&mut self, id: ClipId, additive: bool) {
        self.sel_notes.clear();
        self.sel = match (&self.sel, additive) {
            (Selection::Clips(v), true) => {
                let mut v = v.clone();
                if let Some(i) = v.iter().position(|c| *c == id) { v.remove(i); } else { v.push(id); }
                if v.is_empty() { Selection::None } else { Selection::Clips(v) }
            }
            _ => Selection::Clips(vec![id]),
        };
        self.sync_name_edit(false);
    }
    pub fn select_track(&mut self, id: TrackId) {
        self.sel_notes.clear();
        self.sel = Selection::Track(id);
        self.sync_name_edit(false);
    }
    pub fn clear_selection(&mut self) {
        self.sel_notes.clear();
        self.sel = Selection::None;
        self.sync_name_edit(false);
    }
    /// The track the selection speaks for: the selected track, or the first selected clip's.
    pub fn focus_track(&self) -> Option<TrackId> {
        match &self.sel {
            Selection::Track(t) => Some(*t),
            Selection::Clips(v) => v.first().and_then(|id| self.project.clip(*id)).map(|c| c.track),
            Selection::None => None,
        }
    }
    /// Keep the inspector's name field showing the selected thing's name.
    pub fn sync_name_edit(&mut self, force: bool) {
        if !force && self.name_for == self.sel {
            return;
        }
        let name = match &self.sel {
            Selection::Track(t) => self.project.track(*t).map(|t| t.name.clone()),
            Selection::Clips(v) if v.len() == 1 => self.project.clip(v[0]).map(|c| c.name.clone()),
            _ => None,
        }
        .unwrap_or_default();
        if self.name_edit.text != name {
            let focused = self.name_edit.focused;
            self.name_edit = TextEdit::new(name);
            self.name_edit.focused = focused && self.name_for == self.sel;
        }
        self.name_for = self.sel.clone();
    }

    // ---- gestures the views drive ------------------------------------------------------

    /// Move the selected clips by a tick delta and a lane delta from where the gesture began.
    pub fn drag_clips(&mut self, dticks: i64, dlanes: i32, grid: i64) {
        let Some((origin, _)) = &self.gesture else { return };
        let ids = self.sel.clips().to_vec();
        let Some(lead) = ids.first().and_then(|id| origin.clip(*id)) else { return };
        // Snap the lead clip's start; the others keep their offsets from it.
        let lead_start = snap_to(lead.start + dticks, grid).max(0);
        let min_start = ids.iter().filter_map(|id| origin.clip(*id)).map(|c| c.start).min().unwrap_or(0);
        let delta = (lead_start - lead.start).max(-min_start);
        // A lane change applies only if every clip lands on a lane of its own kind.
        let lane_of = |id: ClipId| origin.clip(id).and_then(|c| origin.lane_index(c.track));
        let moved: Option<Vec<(ClipId, TrackId)>> = ids
            .iter()
            .map(|&id| {
                let lane = lane_of(id)? as i32 + dlanes;
                let track = origin.lane_at(usize::try_from(lane).ok()?)?;
                origin.accepts(track.id, origin.clip(id)?.is_midi()).then_some((id, track.id))
            })
            .collect();
        let mut next = origin.clone();
        for &id in &ids {
            let Some(c) = next.clip_mut(id) else { continue };
            c.start += delta;
            if let Some(m) = &moved {
                if let Some((_, t)) = m.iter().find(|(c, _)| *c == id) { c.track = *t; }
            }
        }
        self.project = next;
    }
    /// Resize one clip from its right edge.
    pub fn drag_clip_end(&mut self, id: ClipId, dticks: i64, grid: i64) {
        let Some((origin, _)) = &self.gesture else { return };
        let Some(o) = origin.clip(id) else { return };
        let min = if grid > 1 { grid } else { TPB / 4 };
        let end = snap_to(o.end() + dticks, grid).max(o.start + min);
        let len = end - o.start;
        if let Some(c) = self.project.clip_mut(id) { c.len = len; }
    }
    /// A clip gesture ends: apply the overlap rule, then record the step.
    pub fn finish_clip_gesture(&mut self) {
        if self.gesture.as_ref().is_some_and(|(before, _)| *before != self.project) {
            let ids = self.sel.clips().to_vec();
            self.project.trim_under(&ids);
        }
        self.end_gesture();
    }

    /// Where a browser item would land for a pointer at window point `(x, y)`.
    pub fn drop_target(&self, item: usize, x: f32, y: f32, scroll_y: f32, bypass_snap: bool) -> Option<DropTarget> {
        let [lx, ly, lw, lh] = self.lanes_rect;
        if x < lx || x > lx + lw || y < ly || y > ly + lh {
            return None;
        }
        let midi = demo::LIBRARY[item].kind == demo::ItemKind::Midi;
        let tick = snap_floor(self.x_to_tick(x - lx).max(0.0) as i64, self.grid(bypass_snap));
        let lane = ((y - ly + scroll_y) / self.track_height()).floor() as usize;
        match self.project.lane_at(lane) {
            Some(t) if self.project.accepts(t.id, midi) => Some(DropTarget { track: Some(t.id), tick }),
            Some(_) => None,
            None => Some(DropTarget { track: None, tick }),
        }
    }

    /// Keep the lanes' vertical scroll inside the track list.
    pub fn set_scroll_y(&mut self, y: f32) {
        let content = self.project.lanes().count() as f32 * self.track_height() + 28.0;
        self.scroll_y = y.clamp(0.0, (content - self.lanes_rect[3]).max(0.0));
    }
    pub fn pr_set_scroll_y(&mut self, y: f32) {
        let content = (PITCH_HI - PITCH_LO + 1) as f32 * NOTE_H;
        self.pr_scroll_y = y.clamp(0.0, (content - self.pr_rect[3]).max(0.0));
    }

    // ---- note gestures -----------------------------------------------------------------

    /// A note drag starts: remember the selected notes as they are.
    pub fn begin_note_gesture(&mut self, label: &'static str) {
        self.begin_gesture(label);
        let ids = self.sel_notes.clone();
        self.note_origin = self.pr_notes().iter().filter(|n| ids.contains(&n.id)).copied().collect();
    }
    /// Move the notes of the gesture by a tick and pitch delta; the first one snaps.
    pub fn drag_notes(&mut self, dticks: i64, dpitch: i32, grid: i64) {
        let Some(lead) = self.note_origin.first().copied() else { return };
        let Some(clip) = self.pr_clip else { return };
        let len = self.project.clip(clip).map_or(BAR, |c| c.len);
        let min_start = self.note_origin.iter().map(|n| n.start).min().unwrap_or(0);
        let max_start = self.note_origin.iter().map(|n| n.start).max().unwrap_or(0);
        let delta = (snap_to(lead.start + dticks, grid) - lead.start).clamp(-min_start, (len - 1 - max_start).max(-min_start));
        let (lo, hi) = self.note_origin.iter().fold((127i32, 0i32), |(lo, hi), n| (lo.min(n.pitch as i32), hi.max(n.pitch as i32)));
        let dp = dpitch.clamp(PITCH_LO as i32 - lo, PITCH_HI as i32 - hi);
        let origin = self.note_origin.clone();
        if let Some(notes) = self.project.clip_mut(clip).and_then(|c| c.notes_mut()) {
            for o in &origin {
                if let Some(n) = notes.iter_mut().find(|n| n.id == o.id) {
                    n.start = o.start + delta;
                    n.pitch = (o.pitch as i32 + dp) as u8;
                }
            }
        }
    }
    /// Resize the notes of the gesture from their right edges.
    pub fn resize_notes(&mut self, dticks: i64, grid: i64) {
        let Some(clip) = self.pr_clip else { return };
        let min = if grid > 1 { grid } else { TPB / 8 };
        let origin = self.note_origin.clone();
        let mut last = None;
        if let Some(notes) = self.project.clip_mut(clip).and_then(|c| c.notes_mut()) {
            for o in &origin {
                if let Some(n) = notes.iter_mut().find(|n| n.id == o.id) {
                    n.len = (snap_to(o.start + o.len + dticks, grid) - o.start).max(min);
                    last = Some(n.len);
                }
            }
        }
        if let Some(l) = last { self.last_note_len = l; }
    }

    // ---- live values -------------------------------------------------------------------

    /// Push engine state into the live values the playhead, readouts and meters are bound to.
    pub fn sync_live(&mut self) {
        let snap = self.engine.snapshot();
        let pos = snap.position;
        self.live.playhead.set([self.tick_to_x(pos), 0.0]);
        let pr_x = match self.pr_clip.and_then(|id| self.project.clip(id)) {
            Some(c) => self.pr_tick_to_x(pos - c.start as f64),
            None => -10.0,
        };
        self.live.pr_playhead.set([pr_x, 0.0]);
        self.live.position.set(bar_beat(pos as i64));
        self.live.clock.set(clock_text(pos, self.project.tempo));
        for m in &self.live.meters {
            let (level, hold) = snap.meter(m.track);
            m.header.set([0.0, 0.0, HEADER_METER_W * level, HEADER_METER_H]);
            m.strip.set([0.0, STRIP_METER_H * (1.0 - level), STRIP_METER_W, STRIP_METER_H * level]);
            let hy = (STRIP_METER_H * (1.0 - hold)).min(STRIP_METER_H - 1.5);
            m.hold.set([0.0, hy, STRIP_METER_W, if hold > 0.01 { 1.5 } else { 0.0 }]);
        }
    }
    pub fn meter(&self, id: TrackId) -> Option<&MeterLive> {
        self.live.meters.iter().find(|m| m.track == id)
    }
    /// A new strip needs live values of its own.
    pub fn ensure_meters(&mut self) {
        for t in &self.project.tracks {
            if !self.live.meters.iter().any(|m| m.track == t.id) {
                self.live.meters.push(MeterLive {
                    track: t.id,
                    header: Live::new([0.0, 0.0, 0.0, HEADER_METER_H]),
                    strip: Live::new([0.0, STRIP_METER_H, STRIP_METER_W, 0.0]),
                    hold: Live::new([0.0, STRIP_METER_H, STRIP_METER_W, 0.0]),
                });
            }
        }
    }

    pub fn playing(&self) -> bool {
        self.engine.snapshot().playing
    }
    pub fn position(&self) -> i64 {
        self.engine.snapshot().position as i64
    }
}

impl Default for App {
    fn default() -> Self {
        App::new()
    }
}

/// Fader travel (0 bottom, 1 top) to decibels: +6 at the top, 0 dB near three quarters,
/// minus infinity at the bottom.
pub fn fader_to_db(p: f32) -> f32 {
    let p = p.clamp(0.0, 1.0);
    if p <= 0.004 { -60.0 } else { (6.0 - 66.0 * (1.0 - p).powf(1.7)).max(-60.0) }
}

pub fn db_to_fader(db: f32) -> f32 {
    if db <= -60.0 { 0.0 } else { 1.0 - ((6.0 - db.min(6.0)) / 66.0).powf(1.0 / 1.7) }
}
