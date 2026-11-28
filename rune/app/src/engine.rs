//! The engine boundary. The UI never keeps its own transport clock or meter values: it sends
//! `EngineCommand`s and reads an `EngineSnapshot`. The only implementation here is `SimEngine`,
//! which produces NO AUDIO. It advances a clock and derives meter levels from the demo clips
//! under the playhead, so the interface has something honest to display.
//!
//! A later experiment can implement `Engine` over Zene's JSON-RPC control socket: the command
//! names follow that surface (`transport.play`, `transport.stop`, `transport.seek`,
//! `transport.set_tempo`), and the snapshot maps onto `transport.get_state` plus `meter.get_state`.

use crate::model::*;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum EngineCommand {
    Play,
    Stop,
    Seek(i64),
    SetTempo(f32),
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct EngineSnapshot {
    pub playing: bool,
    /// Fractional ticks, so the playhead moves smoothly between grid positions.
    pub position: f64,
    /// Per strip: (track, level 0..1, held peak 0..1).
    pub meters: Vec<(TrackId, f32, f32)>,
}

impl EngineSnapshot {
    pub fn meter(&self, id: TrackId) -> (f32, f32) {
        self.meters.iter().find(|m| m.0 == id).map_or((0.0, 0.0), |m| (m.1, m.2))
    }
    pub fn meters_idle(&self) -> bool {
        self.meters.iter().all(|m| m.1 < 0.002 && m.2 < 0.002)
    }
}

pub trait Engine {
    fn send(&mut self, command: EngineCommand);
    /// Advance to `now` (seconds on the app clock). Returns whether anything changed.
    fn advance(&mut self, now: f64, project: &Project) -> bool;
    fn snapshot(&self) -> &EngineSnapshot;
    /// One line for the status bar, so the UI never implies more than the engine does.
    fn describe(&self) -> &'static str;
    /// The short form shown in the status bar itself.
    fn label(&self) -> &'static str {
        "Simulated playback, no audio"
    }
    /// The line above the mixer: where its meter levels come from.
    fn meters_note(&self) -> &'static str {
        "Meters are simulated from demo data. Drag a fader, or focus it and use the arrow keys."
    }
    /// Model corrections the engine has for the UI since the last frame: real waveform
    /// overviews for clips the UI created from a file. Not an undo step.
    fn take_model_patch(&mut self) -> Vec<(ClipId, Peaks)> {
        Vec::new()
    }
    /// A pointer gesture is in progress (true) or over (false). An engine may hold back edits
    /// that only make sense once the gesture ends.
    fn gesture(&mut self, _active: bool) {}
    /// The engine link's status, for an engine behind a link (`None`: the simulation).
    fn link_status(&self) -> Option<&zene_link::Status> {
        None
    }
    /// A whole new model read from the engine after it (re)connected; replaces the UI's project.
    fn take_reload(&mut self) -> Option<Project> {
        None
    }
    /// Sentences the user must see once (what a crash lost, a refused save).
    fn take_notices(&mut self) -> Vec<String> {
        Vec::new()
    }
    /// What closing the window needs (see `zene_link::Session::exit_check`).
    fn exit_check(&mut self) -> zene_link::ExitCheck {
        zene_link::ExitCheck::NoEngine
    }
    fn exit(&mut self, _choice: zene_link::ExitChoice) -> Result<zene_link::ExitOutcome, zene_link::LinkError> {
        Ok(zene_link::ExitOutcome::Stopped { exit: None })
    }
    /// Stop the engine only if nothing is unsaved (see `zene_link::Session::stop_if_clean`).
    fn stop_if_clean(&mut self) -> Result<zene_link::ExitOutcome, zene_link::LinkError> {
        Ok(zene_link::ExitOutcome::Stopped { exit: None })
    }
    fn reconnect(&mut self) {}
    fn restart(&mut self) {}
    fn as_any(&mut self) -> &mut dyn std::any::Any;
}

pub struct SimEngine {
    snap: EngineSnapshot,
    tempo: f32,
    last: Option<f64>,
}

impl SimEngine {
    pub fn new(tempo: f32) -> Self {
        SimEngine { snap: EngineSnapshot::default(), tempo, last: None }
    }
}

/// Fader taper: decibels to a 0..1 level for the simulated meters.
fn gain(db: f32) -> f32 {
    if db <= -60.0 { 0.0 } else { 10f32.powf(db / 20.0) }
}

/// Meter deflection for a linear level, on a dB scale from -48 to +6.
pub fn meter_scale(level: f32) -> f32 {
    if level <= 0.0 { return 0.0; }
    let db = 20.0 * level.log10();
    ((db + 48.0) / 54.0).clamp(0.0, 1.0)
}

impl Engine for SimEngine {
    fn send(&mut self, command: EngineCommand) {
        match command {
            EngineCommand::Play => self.snap.playing = true,
            EngineCommand::Stop => self.snap.playing = false,
            EngineCommand::Seek(t) => self.snap.position = t.max(0) as f64,
            EngineCommand::SetTempo(t) => self.tempo = t,
        }
    }

    fn advance(&mut self, now: f64, project: &Project) -> bool {
        let dt = self.last.map_or(0.0, |l| (now - l).clamp(0.0, 0.25));
        self.last = Some(now);
        let before = self.snap.clone();
        if self.snap.playing {
            self.snap.position += dt * self.tempo as f64 / 60.0 * TPB as f64;
            if self.snap.position >= project.length as f64 {
                self.snap.position = project.length as f64;
                self.snap.playing = false;
            }
        }
        let pos = self.snap.position as i64;
        // Source level for each lane: the loudest unmuted clip under the playhead.
        let mut raw: Vec<(TrackId, f32)> = Vec::with_capacity(project.tracks.len());
        for t in &project.tracks {
            let mut level = 0.0f32;
            if self.snap.playing && project.audible(t) {
                for c in project.clips.iter().filter(|c| c.track == t.id && !c.muted && c.start <= pos && pos < c.end()) {
                    let rel = pos - c.start;
                    let src = match &c.kind {
                        ClipKind::Audio { .. } => c.peak_at(rel),
                        ClipKind::Midi { notes } => notes
                            .iter()
                            .filter(|n| n.start <= rel && rel < n.start + n.len)
                            .map(|n| {
                                // A plucked envelope per note, scaled by velocity.
                                let age = (rel - n.start) as f32 / TPB as f32;
                                n.vel as f32 / 127.0 * (0.45 + 0.55 * (-age * 1.6).exp())
                            })
                            .fold(0.0, f32::max),
                    };
                    level = level.max(src * gain(c.gain_db));
                }
                level *= gain(t.volume_db);
            }
            raw.push((t.id, level));
        }
        // Buses carry the loudest strip feeding them; the master carries everything unrouted.
        let feed = |target: Option<TrackId>, raw: &[(TrackId, f32)]| {
            project.tracks.iter().filter(|t| t.output == target && t.kind != TrackKind::Master)
                .filter_map(|t| raw.iter().find(|r| r.0 == t.id)).map(|r| r.1).fold(0.0, f32::max)
        };
        for t in project.tracks.iter().filter(|t| t.kind == TrackKind::Bus) {
            let v = if project.audible(t) { feed(Some(t.id), &raw) * gain(t.volume_db) } else { 0.0 };
            if let Some(r) = raw.iter_mut().find(|r| r.0 == t.id) { r.1 = v; }
        }
        for t in project.tracks.iter().filter(|t| t.kind == TrackKind::Master) {
            let v = if t.mute { 0.0 } else { feed(None, &raw) * gain(t.volume_db) };
            if let Some(r) = raw.iter_mut().find(|r| r.0 == t.id) { r.1 = v; }
        }
        // Ballistics: instant attack, ~18 dB/s release; the peak hold falls more slowly.
        let fall = (dt as f32) * 0.34;
        let mut meters = Vec::with_capacity(raw.len());
        for (id, level) in raw {
            let target = meter_scale(level);
            let (old, hold) = self.snap.meter(id);
            let now_level = if target >= old { target } else { (old - fall).max(target) };
            let now_hold = if now_level >= hold { now_level } else { (hold - fall * 0.35).max(now_level) };
            meters.push((id, now_level, now_hold));
        }
        self.snap.meters = meters;
        self.snap != before
    }

    fn snapshot(&self) -> &EngineSnapshot {
        &self.snap
    }

    fn describe(&self) -> &'static str {
        "Simulated playback: no audio engine, meters are derived from demo data"
    }

    fn as_any(&mut self) -> &mut dyn std::any::Any {
        self
    }
}
