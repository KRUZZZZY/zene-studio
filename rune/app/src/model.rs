//! The project model: plain data, cloneable, comparable. Nothing here knows about Rune.
//! Time is integer ticks so snapping and undo comparisons are exact.

use std::sync::Arc;

/// Ticks per quarter-note beat.
pub const TPB: i64 = 480;
pub const BEATS_PER_BAR: i64 = 4;
pub const BAR: i64 = TPB * BEATS_PER_BAR;
/// Peak samples kept per beat of synthesised demo audio.
pub const PEAKS_PER_BEAT: usize = 24;

pub type TrackId = u32;
pub type ClipId = u32;
pub type NoteId = u32;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TrackKind {
    Audio,
    Instrument,
    Bus,
    Master,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Track {
    pub id: TrackId,
    pub name: String,
    pub kind: TrackKind,
    /// Index into the theme's track palette.
    pub color: usize,
    pub volume_db: f32,
    /// -1 (left) to 1 (right).
    pub pan: f32,
    pub mute: bool,
    pub solo: bool,
    /// The bus this strip feeds; `None` feeds the master.
    pub output: Option<TrackId>,
    /// What plays the notes, for instrument tracks (a label only: nothing is hosted).
    pub source: String,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Note {
    pub id: NoteId,
    /// Ticks from the clip's start.
    pub start: i64,
    pub len: i64,
    pub pitch: u8,
    pub vel: u8,
}

/// Synthesised demo peaks standing in for an audio file's overview. Not audio.
#[derive(Clone, Debug)]
pub struct Peaks(pub Arc<[f32]>);

impl PartialEq for Peaks {
    fn eq(&self, other: &Self) -> bool {
        // The pointer check is the fast path undo comparisons take; the slices decide otherwise.
        Arc::ptr_eq(&self.0, &other.0) || self.0 == other.0
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum ClipKind {
    /// `offset` is how far into the source the clip starts, in ticks.
    Audio { peaks: Peaks, offset: i64 },
    Midi { notes: Vec<Note> },
}

#[derive(Clone, Debug, PartialEq)]
pub struct Clip {
    pub id: ClipId,
    pub track: TrackId,
    pub start: i64,
    pub len: i64,
    pub name: String,
    pub gain_db: f32,
    pub muted: bool,
    pub kind: ClipKind,
}

impl Clip {
    pub fn end(&self) -> i64 {
        self.start + self.len
    }
    pub fn is_midi(&self) -> bool {
        matches!(self.kind, ClipKind::Midi { .. })
    }
    pub fn notes(&self) -> &[Note] {
        match &self.kind {
            ClipKind::Midi { notes } => notes,
            ClipKind::Audio { .. } => &[],
        }
    }
    pub fn notes_mut(&mut self) -> Option<&mut Vec<Note>> {
        match &mut self.kind {
            ClipKind::Midi { notes } => Some(notes),
            ClipKind::Audio { .. } => None,
        }
    }
    /// The demo peak (0..1) at `tick` ticks into the clip.
    pub fn peak_at(&self, tick: i64) -> f32 {
        match &self.kind {
            ClipKind::Audio { peaks, offset } => {
                let i = ((tick + offset).max(0) as usize * PEAKS_PER_BEAT) / TPB as usize;
                peaks.0.get(i).copied().unwrap_or(0.0)
            }
            ClipKind::Midi { .. } => 0.0,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Project {
    pub name: String,
    pub tempo: f32,
    pub tracks: Vec<Track>,
    pub clips: Vec<Clip>,
    /// Song length in ticks (the ruler and playback end here).
    pub length: i64,
    pub next_id: u32,
}

impl Project {
    pub fn fresh_id(&mut self) -> u32 {
        self.next_id += 1;
        self.next_id
    }
    pub fn track(&self, id: TrackId) -> Option<&Track> {
        self.tracks.iter().find(|t| t.id == id)
    }
    pub fn track_mut(&mut self, id: TrackId) -> Option<&mut Track> {
        self.tracks.iter_mut().find(|t| t.id == id)
    }
    pub fn clip(&self, id: ClipId) -> Option<&Clip> {
        self.clips.iter().find(|c| c.id == id)
    }
    pub fn clip_mut(&mut self, id: ClipId) -> Option<&mut Clip> {
        self.clips.iter_mut().find(|c| c.id == id)
    }
    /// Tracks that own a timeline lane, in display order.
    pub fn lanes(&self) -> impl Iterator<Item = &Track> {
        self.tracks.iter().filter(|t| matches!(t.kind, TrackKind::Audio | TrackKind::Instrument))
    }
    pub fn lane_index(&self, id: TrackId) -> Option<usize> {
        self.lanes().position(|t| t.id == id)
    }
    pub fn lane_at(&self, index: usize) -> Option<&Track> {
        self.lanes().nth(index)
    }
    pub fn any_solo(&self) -> bool {
        self.tracks.iter().any(|t| t.solo)
    }
    /// Whether a strip is heard, given mute and solo across the project. A bus stays audible
    /// while any strip feeding it is soloed.
    pub fn audible(&self, t: &Track) -> bool {
        if t.mute {
            return false;
        }
        if !self.any_solo() || t.solo || t.kind == TrackKind::Master {
            return true;
        }
        match t.kind {
            TrackKind::Bus => self.tracks.iter().any(|c| c.output == Some(t.id) && c.solo),
            _ => t.output.and_then(|b| self.track(b)).is_some_and(|b| b.solo),
        }
    }
    /// A clip may live on a track of its own kind: audio on audio, notes on instruments.
    pub fn accepts(&self, track: TrackId, midi: bool) -> bool {
        self.track(track).is_some_and(|t| t.kind == if midi { TrackKind::Instrument } else { TrackKind::Audio })
    }

    /// The owner's overlap rule: a clip that lands on a neighbour trims it. `movers` win.
    pub fn trim_under(&mut self, movers: &[ClipId]) {
        let spans: Vec<(TrackId, i64, i64)> =
            movers.iter().filter_map(|&id| self.clip(id)).map(|c| (c.track, c.start, c.end())).collect();
        let mut added = Vec::new();
        for (track, s, e) in spans {
            let mut next = self.next_id;
            for c in self.clips.iter_mut().filter(|c| c.track == track && !movers.contains(&c.id)) {
                if c.end() <= s || c.start >= e {
                    continue;
                }
                if c.start >= s && c.end() <= e {
                    c.len = 0; // fully covered: removed below
                } else if c.start < s && c.end() > e {
                    // The mover lands inside: keep the head, and the tail as a new clip.
                    let mut tail = c.clone();
                    next += 1;
                    tail.id = next;
                    cut_head(&mut tail, e - c.start);
                    added.push(tail);
                    c.len = s - c.start;
                    drop_notes_past(c);
                } else if c.start < s {
                    c.len = s - c.start;
                    drop_notes_past(c);
                } else {
                    cut_head(c, e - c.start);
                }
            }
            self.next_id = next;
        }
        self.clips.retain(|c| c.len > 0);
        self.clips.extend(added);
    }
}

fn cut_head(c: &mut Clip, by: i64) {
    c.start += by;
    c.len -= by;
    match &mut c.kind {
        ClipKind::Audio { offset, .. } => *offset += by,
        ClipKind::Midi { notes } => {
            notes.retain(|n| n.start >= by);
            notes.iter_mut().for_each(|n| n.start -= by);
        }
    }
}

fn drop_notes_past(c: &mut Clip) {
    let len = c.len;
    if let ClipKind::Midi { notes } = &mut c.kind {
        notes.retain(|n| n.start < len);
    }
}

/// Bar.beat.tick, 1-based bars and beats, as every DAW ruler writes it.
pub fn bar_beat(tick: i64) -> String {
    let t = tick.max(0);
    format!("{}.{}.{:03}", t / BAR + 1, (t % BAR) / TPB + 1, t % TPB)
}

/// A length as bars and beats ("2 bars", "1 bar 2 beats", "3 beats", "1/16").
pub fn length_text(ticks: i64) -> String {
    let (bars, rest) = (ticks / BAR, ticks % BAR);
    let beats = rest as f32 / TPB as f32;
    let plural = |n: i64, w: &str| format!("{n} {w}{}", if n == 1 { "" } else { "s" });
    match (bars, rest) {
        (0, r) if r < TPB && r > 0 && TPB % r == 0 => format!("1/{}", BAR / r),
        (0, _) => format!("{} beat{}", trim_float(beats), if (beats - 1.0).abs() < 1e-3 { "" } else { "s" }),
        (b, 0) => plural(b, "bar"),
        (b, _) => format!("{} {} beat{}", plural(b, "bar"), trim_float(beats), if (beats - 1.0).abs() < 1e-3 { "" } else { "s" }),
    }
}

fn trim_float(v: f32) -> String {
    let s = format!("{v:.2}");
    s.trim_end_matches('0').trim_end_matches('.').to_string()
}

pub fn clock_text(tick: f64, tempo: f32) -> String {
    let secs = (tick / TPB as f64 * 60.0 / tempo as f64).max(0.0);
    format!("{}:{:04.1}", (secs / 60.0) as u32, secs % 60.0)
}

pub fn db_text(db: f32) -> String {
    if db <= -60.0 { "-inf".into() } else { format!("{db:+.1}") }
}

pub fn pan_text(pan: f32) -> String {
    let p = (pan * 100.0).round() as i32;
    match p {
        0 => "C".into(),
        p if p < 0 => format!("{}L", -p),
        p => format!("{p}R"),
    }
}

pub fn pitch_name(pitch: u8) -> String {
    const NAMES: [&str; 12] = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    format!("{}{}", NAMES[pitch as usize % 12], pitch as i32 / 12 - 1)
}

pub fn is_black_key(pitch: u8) -> bool {
    matches!(pitch % 12, 1 | 3 | 6 | 8 | 10)
}

/// The grid a drag or a click lands on.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Snap {
    Bar,
    Half,
    Beat,
    Eighth,
    Sixteenth,
}

impl Snap {
    pub const ALL: [Snap; 5] = [Snap::Bar, Snap::Half, Snap::Beat, Snap::Eighth, Snap::Sixteenth];
    pub fn ticks(self) -> i64 {
        match self {
            Snap::Bar => BAR,
            Snap::Half => BAR / 2,
            Snap::Beat => TPB,
            Snap::Eighth => TPB / 2,
            Snap::Sixteenth => TPB / 4,
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Snap::Bar => "1 bar",
            Snap::Half => "1/2",
            Snap::Beat => "1/4",
            Snap::Eighth => "1/8",
            Snap::Sixteenth => "1/16",
        }
    }
    pub fn finer(self) -> Snap {
        Snap::ALL[(Snap::ALL.iter().position(|s| *s == self).unwrap_or(0) + 1).min(4)]
    }
    pub fn coarser(self) -> Snap {
        Snap::ALL[Snap::ALL.iter().position(|s| *s == self).unwrap_or(0).saturating_sub(1)]
    }
}

pub fn snap_to(tick: i64, grid: i64) -> i64 {
    if grid <= 1 { tick } else { (tick as f64 / grid as f64).round() as i64 * grid }
}

pub fn snap_floor(tick: i64, grid: i64) -> i64 {
    if grid <= 1 { tick } else { tick.div_euclid(grid) * grid }
}
