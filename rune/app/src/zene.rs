//! The `Engine` boundary implemented over a real Zene Studio engine, on top of the engine link
//! crate (`rune/link`, `zene_link`): the session there starts or attaches to the engine, keeps the
//! connection alive, reconnects, restarts and reports status; this module turns the UI model into
//! control-surface commands and the engine's project into the UI model.
//!
//! Shape (Phase 0, ported onto the link by Q050-506):
//!
//! - **Commands** go out on the session's connection. They still WAIT for their reply
//!   (`ZeneEngine::call`, `Connection::call`), from the UI thread, as the Phase 0 spike did: the UI
//!   edits its own model first (optimistic) and `sync` sends the difference. Q050-507 replaces these
//!   blocking calls (in `sync`, `load`, `reconcile`, `peaks`) with `Connection::send` and replies
//!   drained per frame; nothing else here blocks a frame. While the session is not ready nothing is
//!   sent: edits wait in the model and go out when the connection is back.
//! - **Telemetry** (transport position, playing flag, strip meters) is polled by a background thread
//!   on the same connection (blocking calls off the UI thread) and read once per frame.
//! - **Events** reach the session (unsaved-change tracking) and are handed on here, where their
//!   arrival times are kept for the S2 lag measurement.
//! - On every new `Ready` the project is read again: after a reconnect to the same engine the UI's
//!   unsent edits go out first; after a restart (a new engine process) the engine's project replaces
//!   the UI's (`take_reload`).
//! - `reconcile` rereads the engine and lists every field where it disagrees with the UI model.
//!
//! Units: the UI counts 480 ticks a beat, the engine 48, so ticks cross the boundary through
//! `TICK_SCALE`. Faders are decibels here and linear gain (0..2) there. Note velocity is 1..127
//! here and 0..200 there with 100 as the default.
//!
//! Nothing here plays sound by itself: the engine decides its audio device. Unix sockets only until
//! the link's named-pipe transport (Q050-524).

use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use serde_json::{Value, json};
use zene_link::session::ReadyKind;
use zene_link::{Connection, ExitCheck, ExitChoice, ExitOutcome, LinkError, Session, Status};

use crate::demo;
use crate::engine::{Engine, EngineCommand, EngineSnapshot, meter_scale};
use crate::model::*;

pub const TICK_SCALE: i64 = 10;
/// Strips that are mixer channels without a track (master, buses) get ids above this.
pub const CH_BASE: u32 = 5_000_000;
/// Ids the UI invents for new objects start here, clear of anything the engine assigns.
pub const LOCAL_BASE: u32 = 10_000_000;
/// How long a blocking command may take before the link reports it (Q050-507 removes the wait).
const CALL_TIMEOUT: Duration = Duration::from_secs(30);

fn id_num(id: &str) -> u32 {
    id.rsplit('-').next().and_then(|n| n.parse().ok()).unwrap_or(0)
}
fn to_engine(ticks: i64) -> i64 {
    (ticks as f64 / TICK_SCALE as f64).round() as i64
}
fn db_to_gain(db: f32) -> f64 {
    if db <= -60.0 { 0.0 } else { (10f64.powf(db as f64 / 20.0)).clamp(0.0, 2.0) }
}
fn gain_to_db(g: f64) -> f32 {
    if g <= 0.001 { -60.0 } else { (20.0 * g.log10()).clamp(-60.0, 6.0) as f32 }
}
fn vel_to_engine(v: u8) -> i64 {
    (v as f64 * 100.0 / 127.0).round() as i64
}
fn vel_from_engine(v: i64) -> u8 {
    ((v as f64 * 127.0 / 100.0).round() as i64).clamp(1, 127) as u8
}

#[derive(Clone, Debug, Default)]
pub struct Telemetry {
    pub playing: bool,
    /// Engine ticks.
    pub ticks: i64,
    pub tempo: f64,
    /// Channel id -> post-fader RMS (max of left and right), linear.
    pub levels: Vec<(String, f32)>,
    /// When this sample was taken, and how long the poll cycle that produced it took.
    pub at: Option<Instant>,
    pub cycle_ms: f32,
    pub samples: u64,
}

/// Everything the spike measures about the link. Shared with the telemetry thread.
#[derive(Default)]
pub struct LinkLog {
    /// Every command round trip: (command id, milliseconds).
    pub calls: Vec<(String, f32)>,
    pub errors: Vec<String>,
    /// Things the control surface could not express and the link had to emulate or drop.
    pub gaps: Vec<String>,
    /// Telemetry poll cycles, milliseconds each.
    pub cycles: Vec<f32>,
    /// Reply times of mutating commands by name, for pairing with events.
    pub acks: Vec<(String, Instant)>,
    /// Event arrivals: (command named by the event, when the UI saw it).
    pub events: Vec<(String, Instant)>,
    /// How old the telemetry sample was each time a frame read it, milliseconds.
    pub staleness: Vec<f32>,
}

#[derive(Clone, Debug, Default)]
struct TrackRef {
    trk: Option<String>,
    ch: Option<String>,
}

/// The connection the telemetry thread uses: replaced on every new `Ready`, `None` while down.
type ConnSlot = Arc<Mutex<Option<Connection>>>;

pub struct ZeneEngine {
    pub session: Session,
    /// The session's connection of the generation this model was loaded from.
    conn: Option<Connection>,
    loaded_generation: u64,
    /// A new model read after a (re)connect, for the UI to take.
    reload: Option<Project>,
    notices: Vec<String>,
    tele_conn: ConnSlot,
    snap: EngineSnapshot,
    tele: Arc<Mutex<Telemetry>>,
    /// Channels the telemetry thread reads meters for.
    meter_channels: Arc<Mutex<Vec<String>>>,
    stop: Arc<AtomicBool>,
    pub log: Arc<Mutex<LinkLog>>,
    /// The project as the engine was last told it is.
    shadow: Project,
    tracks: HashMap<TrackId, TrackRef>,
    clips: HashMap<ClipId, String>,
    notes: HashMap<NoteId, String>,
    /// Source file of an audio clip, where known (imports made through this link).
    clip_paths: HashMap<ClipId, String>,
    /// Real audio files standing behind the demo browser's audio items, in item order.
    pub library_audio: Vec<String>,
    last_advance: Option<f64>,
    seen_sample: u64,
    sample_at: Option<Instant>,
    /// UI ticks at the last telemetry sample whose position differed from the one before.
    base_position: f64,
    /// Clips whose overview was replaced by the file's real peaks and not yet handed to the UI.
    pending_peaks: Vec<ClipId>,
    /// A pointer gesture is in progress.
    in_gesture: bool,
    /// The engine's overlap mode before this link set it to `free`, put back on drop.
    overlap_before: Option<String>,
    /// Send clip moves, resizes, adds and removes on every frame of a drag (`ZENE_STREAM_CLIPS=1`)
    /// instead of once when the gesture ends. Measured in S2: streaming is fast enough, but the
    /// engine applies its overlap rule to every intermediate position, so it is off by default.
    stream_clips: bool,
}

/// The model shown while no engine project has been read (start failed, engine unreachable).
pub fn empty_project() -> Project {
    Project { name: "No engine".into(), tempo: 120.0, tracks: Vec::new(), clips: Vec::new(), length: 8 * BAR, next_id: LOCAL_BASE }
}

/// How long start-up waits for the engine before the window opens anyway (status shows the rest).
pub const STARTUP_WAIT: Duration = Duration::from_secs(20);

/// Attach to a running engine at `path` (it is never stopped by this UI). For the engine tests.
#[cfg(test)]
pub fn connect(path: &str) -> Result<(Project, ZeneEngine), String> {
    let e = ZeneEngine::start(Session::attach(path, zene_link::SessionOptions::default()));
    match e.session.status() {
        Status::Ready { .. } => {}
        s => return Err(s.sentence()),
    }
    let mut e = e;
    let p = e.reload.take().unwrap_or_else(empty_project);
    Ok((p, e))
}

impl ZeneEngine {
    /// Wrap a session: wait for it at start-up (blocking, start-up only), read the project when it
    /// is ready, and start the telemetry thread.
    pub fn start(mut session: Session) -> ZeneEngine {
        let _ = session.wait_ready(STARTUP_WAIT);
        let log = Arc::new(Mutex::new(LinkLog::default()));
        let tele = Arc::new(Mutex::new(Telemetry::default()));
        let meter_channels = Arc::new(Mutex::new(Vec::new()));
        let stop = Arc::new(AtomicBool::new(false));
        let tele_conn: ConnSlot = Arc::new(Mutex::new(None));
        let mut e = ZeneEngine {
            session,
            conn: None,
            loaded_generation: 0,
            reload: None,
            notices: Vec::new(),
            tele_conn: tele_conn.clone(),
            snap: EngineSnapshot::default(),
            tele: tele.clone(),
            meter_channels: meter_channels.clone(),
            stop: stop.clone(),
            log: log.clone(),
            shadow: empty_project(),
            tracks: HashMap::new(),
            clips: HashMap::new(),
            notes: HashMap::new(),
            clip_paths: HashMap::new(),
            library_audio: Vec::new(),
            last_advance: None,
            seen_sample: 0,
            sample_at: None,
            base_position: 0.0,
            pending_peaks: Vec::new(),
            in_gesture: false,
            overlap_before: None,
            stream_clips: std::env::var("ZENE_STREAM_CLIPS").is_ok_and(|v| v == "1"),
        };
        let period = Duration::from_micros(std::env::var("ZENE_TELEMETRY_US").ok().and_then(|v| v.parse().ok()).unwrap_or(8000));
        spawn_telemetry(tele_conn, tele, meter_channels, stop, log, period);
        e.follow_session(None);
        e
    }

    /// Tick the session; on a new `Ready`, read the project again (see the module comment).
    fn follow_session(&mut self, ui: Option<&Project>) {
        self.session.tick();
        self.notices.extend(self.session.take_notices());
        for ev in self.session.take_events() {
            self.log.lock().unwrap().events.push((ev["command"].as_str().unwrap_or("").to_string(), Instant::now()));
        }
        let Some(conn) = self.session.connection().cloned() else {
            if self.conn.take().is_some() {
                *self.tele_conn.lock().unwrap() = None;
            }
            return;
        };
        if self.session.generation() == self.loaded_generation {
            return;
        }
        let kind = self.session.ready_kind();
        self.conn = Some(conn.clone());
        *self.tele_conn.lock().unwrap() = Some(conn);
        if kind == ReadyKind::Reconnected {
            // Same engine: what the UI changed while the connection was down goes out first.
            if let Some(p) = ui {
                self.sync(p);
            }
        } else {
            self.clip_paths.clear();
        }
        if let Err(e) = self.on_ready() {
            self.log.lock().unwrap().errors.push(format!("reading the project: {e}"));
            self.notices.push(format!("The engine is connected but its project could not be read: {e}"));
        }
        self.loaded_generation = self.session.generation();
    }

    /// Engine setup and the project read, after every new `Ready` (blocking: Q050-507).
    fn on_ready(&mut self) -> Result<(), String> {
        if !self.stream_clips {
            // The UI applies its own overlap rule when a gesture ends and sends each resulting trim
            // as a command, so the engine must not trim a second time on its own. (A session
            // setting: it is not saved with the project and is no project_changed edit.)
            let before = self.rpc("arrangement.modes", json!({})).ok().and_then(|m| m["overlap"].as_str().map(String::from));
            if self.overlap_before.is_none() {
                self.overlap_before = before;
            }
            self.rpc("arrangement.modes", json!({"overlap": "free"}))?;
        }
        let project = self.load(true)?;
        self.shadow = project.clone();
        self.publish_meter_channels();
        if self.library_audio.is_empty() {
            if let Ok(q) = self.rpc("browser.query", json!({"kind": "audio", "root": "factory-samples", "limit": 64})) {
                self.library_audio = q["items"].as_array().into_iter().flatten().filter_map(|i| i["path"].as_str().map(String::from)).collect();
            }
        }
        self.reload = Some(project);
        Ok(())
    }

    /// One blocking command on the current connection (start-up, load and the Phase 0 sync: Q050-507).
    fn rpc(&self, cmd: &str, args: Value) -> Result<Value, String> {
        let c = self.conn.as_ref().ok_or_else(|| format!("{cmd}: the engine is not connected"))?;
        c.call(cmd, args.clone(), CALL_TIMEOUT).map_err(|e| match e {
            LinkError::Remote { kind, message, .. } => format!("{cmd} {args}: {message} ({kind})"),
            e => e.to_string(),
        })
    }
}

/// Telemetry: transport and meters on the session's current connection, as fast as
/// `ZENE_TELEMETRY_US` asks (default 8 ms). Waits quietly while there is no connection.
fn spawn_telemetry(slot: ConnSlot, tele: Arc<Mutex<Telemetry>>, chans: Arc<Mutex<Vec<String>>>, stop: Arc<AtomicBool>, log: Arc<Mutex<LinkLog>>, period: Duration) {
    let t = Duration::from_secs(2);
    let _ = std::thread::Builder::new().name("zene-telemetry".into()).spawn(move || {
        while !stop.load(Ordering::Relaxed) {
            let t0 = Instant::now();
            let conn = slot.lock().unwrap().clone();
            let Some(conn) = conn.filter(|c| c.is_open()) else {
                std::thread::sleep(Duration::from_millis(50));
                continue;
            };
            let Ok(tr) = conn.call("transport.get_state", json!({}), t) else {
                std::thread::sleep(Duration::from_millis(50));
                continue;
            };
            let list: Vec<String> = chans.lock().unwrap().clone();
            let mut levels = Vec::with_capacity(list.len());
            for ch in list {
                if let Ok(m) = conn.call("mixer.strip_get", json!({"channel": ch}), t) {
                    let m = &m["meters"];
                    let l = m["rms_left"].as_f64().unwrap_or(0.0).max(m["rms_right"].as_f64().unwrap_or(0.0));
                    levels.push((ch, l as f32));
                }
            }
            let cycle = t0.elapsed().as_secs_f32() * 1000.0;
            {
                let mut t = tele.lock().unwrap();
                t.playing = tr["playing"].as_bool().unwrap_or(false);
                t.ticks = tr["position_ticks"].as_i64().unwrap_or(0);
                t.tempo = tr["tempo"].as_f64().unwrap_or(120.0);
                t.levels = levels;
                t.at = Some(Instant::now());
                t.cycle_ms = cycle;
                t.samples += 1;
            }
            log.lock().unwrap().cycles.push(cycle);
            if let Some(rest) = period.checked_sub(t0.elapsed()) {
                std::thread::sleep(rest);
            }
        }
    });
}

/// What `reconcile` found: one line per field where the engine and the UI model disagree.
pub type Mismatches = Vec<String>;

impl ZeneEngine {
    /// A blocking command from `sync` (Q050-507 sends these without waiting). `None` on failure,
    /// which is logged; nothing is sent while the session is not ready.
    fn call(&mut self, cmd: &str, args: Value) -> Option<Value> {
        let t0 = Instant::now();
        let r = self.rpc(cmd, args);
        let ms = t0.elapsed().as_secs_f32() * 1000.0;
        let mut log = self.log.lock().unwrap();
        log.calls.push((cmd.to_string(), ms));
        match r {
            Ok(v) => {
                log.acks.push((cmd.to_string(), Instant::now()));
                Some(v)
            }
            Err(e) => {
                log.errors.push(e);
                None
            }
        }
    }
    fn gap(&mut self, what: &str) {
        let mut log = self.log.lock().unwrap();
        if !log.gaps.iter().any(|g| g == what) {
            log.gaps.push(what.to_string());
        }
    }

    fn publish_meter_channels(&mut self) {
        let mut v: Vec<String> = self.tracks.values().filter_map(|t| t.ch.clone()).collect();
        v.sort();
        v.dedup();
        *self.meter_channels.lock().unwrap() = v;
    }

    /// Read the engine's project into the UI model. With `own_channels`, a lane track that feeds
    /// the master directly (or shares a channel) is given a mixer channel of its own, because a
    /// strip with a fader and a meter needs one; each such change is counted in `gaps`.
    pub fn load(&mut self, own_channels: bool) -> Result<Project, String> {
        let state = self.rpc("project.get_state", json!({}))?;
        let mut overview = self.rpc("track.overview", json!({}))?;
        let mut mixer = self.rpc("mixer.get_state", json!({}))?;
        if own_channels {
            let mut uses: HashMap<String, usize> = HashMap::new();
            for t in overview["tracks"].as_array().into_iter().flatten() {
                *uses.entry(t["channel"].as_str().unwrap_or("").to_string()).or_default() += 1;
            }
            let master = mixer["channels"].as_array().into_iter().flatten().find(|c| c["is_master"] == true).and_then(|c| c["id"].as_str()).unwrap_or("ch-1").to_string();
            let mut changed = 0;
            for t in overview["tracks"].as_array().cloned().unwrap_or_default() {
                let (ty, ch, id) = (t["type"].as_str().unwrap_or(""), t["channel"].as_str().unwrap_or(""), t["id"].as_str().unwrap_or(""));
                if matches!(ty, "instrument" | "sample") && (ch.is_empty() || ch == master || uses.get(ch).copied().unwrap_or(0) > 1) {
                    let new = self.rpc("mixer.add_channel", json!({}))?;
                    let new = new["id"].as_str().or(new["channel"].as_str()).unwrap_or("").to_string();
                    self.rpc("mixer.rename_channel", json!({"channel": new, "name": t["name"]})).ok();
                    self.rpc("track.set_mixer_channel", json!({"track": id, "channel": new}))?;
                    changed += 1;
                }
            }
            if changed > 0 {
                self.gap(&format!("load: {changed} track(s) fed the master or shared a channel and were given their own mixer channel (no per-track fader or meter exists on the surface)"));
                overview = self.rpc("track.overview", json!({}))?;
                mixer = self.rpc("mixer.get_state", json!({}))?;
            }
        }
        let tempo = state["tempo"].as_f64().unwrap_or(120.0) as f32;
        let file = state["file"].as_str().unwrap_or("");
        let name = std::path::Path::new(file).file_stem().map(|s| s.to_string_lossy().to_string()).unwrap_or_else(|| "Untitled".into());
        let chans: Vec<Value> = mixer["channels"].as_array().cloned().unwrap_or_default();
        let chan = |id: &str| chans.iter().find(|c| c["id"] == id);

        self.tracks.clear();
        self.clips.clear();
        self.notes.clear();
        let mut tracks = Vec::new();
        let mut owned: Vec<String> = Vec::new();
        let mut skipped = 0;
        for (i, t) in overview["tracks"].as_array().into_iter().flatten().enumerate() {
            let kind = match t["type"].as_str().unwrap_or("") {
                "instrument" => TrackKind::Instrument,
                "sample" => TrackKind::Audio,
                _ => {
                    skipped += 1;
                    continue;
                }
            };
            let trk = t["id"].as_str().unwrap_or("").to_string();
            let ch = t["channel"].as_str().filter(|c| !c.is_empty()).map(String::from);
            let c = ch.as_deref().and_then(chan);
            let id = id_num(&trk);
            let output = c.and_then(|c| c["sends"].as_array()).and_then(|s| s.first()).and_then(|s| s["to"].as_str())
                .filter(|to| chan(to).is_some_and(|c| c["is_master"] != true)).map(|to| CH_BASE + id_num(to));
            tracks.push(Track {
                id,
                name: t["name"].as_str().unwrap_or("").to_string(),
                kind,
                color: i % 8,
                volume_db: c.map_or(0.0, |c| gain_to_db(c["volume"].as_f64().unwrap_or(1.0))),
                pan: c.map_or(0.0, |c| c["pan"].as_f64().unwrap_or(0.0) as f32),
                mute: t["muted"].as_bool().unwrap_or(false),
                solo: t["soloed"].as_bool().unwrap_or(false),
                output,
                source: t["instrument"].as_str().unwrap_or("").to_string(),
            });
            if let Some(ch) = &ch { owned.push(ch.clone()); }
            self.tracks.insert(id, TrackRef { trk: Some(trk), ch });
        }
        if skipped > 0 {
            self.gap(&format!("load: {skipped} pattern/automation/other track(s) have no surface in the prototype and are not shown"));
        }
        // Channels no track owns: buses, then the master last.
        for pass in [false, true] {
            for c in chans.iter().filter(|c| (c["is_master"] == true) == pass) {
                let ch = c["id"].as_str().unwrap_or("").to_string();
                if owned.contains(&ch) { continue; }
                let id = CH_BASE + id_num(&ch);
                tracks.push(Track {
                    id,
                    name: c["name"].as_str().unwrap_or("").to_string(),
                    kind: if pass { TrackKind::Master } else { TrackKind::Bus },
                    color: 7,
                    volume_db: gain_to_db(c["volume"].as_f64().unwrap_or(1.0)),
                    pan: c["pan"].as_f64().unwrap_or(0.0) as f32,
                    mute: c["muted"].as_bool().unwrap_or(false),
                    solo: c["soloed"].as_bool().unwrap_or(false),
                    output: None,
                    source: String::new(),
                });
                self.tracks.insert(id, TrackRef { trk: None, ch: Some(ch) });
            }
        }

        let mut clips = Vec::new();
        let mut other = 0;
        for t in tracks.iter().filter(|t| matches!(t.kind, TrackKind::Audio | TrackKind::Instrument)) {
            let trk = self.tracks[&t.id].trk.clone().unwrap_or_default();
            let list = self.rpc("clip.list", json!({"track": trk, "limit": 500}))?;
            for c in list["clips"].as_array().into_iter().flatten() {
                let cid = c["clip"].as_str().unwrap_or("").to_string();
                let id = id_num(&cid);
                let len = c["length"].as_i64().unwrap_or(0) * TICK_SCALE;
                let kind = match c["kind"].as_str().unwrap_or("") {
                    "midi" => {
                        let nl = self.rpc("note.list", json!({"clip": cid}))?;
                        let mut notes = Vec::new();
                        for n in nl["notes"].as_array().into_iter().flatten() {
                            let nid = n["note"].as_str().unwrap_or("").to_string();
                            let lid = id_num(&nid);
                            notes.push(Note {
                                id: lid,
                                start: n["position"].as_i64().unwrap_or(0) * TICK_SCALE,
                                len: n["length"].as_i64().unwrap_or(1).max(1) * TICK_SCALE,
                                pitch: n["key"].as_i64().unwrap_or(60).clamp(0, 127) as u8,
                                vel: vel_from_engine(n["velocity"].as_i64().unwrap_or(100)),
                            });
                            self.notes.insert(lid, nid);
                        }
                        ClipKind::Midi { notes }
                    }
                    "audio" => {
                        let peaks = self.clip_paths.get(&id).cloned().and_then(|p| self.peaks(&p, len, tempo));
                        ClipKind::Audio { peaks: peaks.unwrap_or_else(|| flat_peaks(len)), offset: 0 }
                    }
                    _ => {
                        other += 1;
                        continue;
                    }
                };
                clips.push(Clip { id, track: t.id, start: c["start"].as_i64().unwrap_or(0) * TICK_SCALE, len: len.max(TICK_SCALE), name: c["name"].as_str().unwrap_or("").to_string(), gain_db: 0.0, muted: false, kind });
                self.clips.insert(id, cid);
            }
        }
        if other > 0 {
            self.gap(&format!("load: {other} clip(s) of a kind the prototype cannot show"));
        }
        if clips.iter().any(|c| !c.is_midi()) {
            self.gap("load: clip.list reports neither a clip's gain, mute, source offset nor its source file, so those read as defaults and existing audio clips draw a flat overview (no per-clip peaks command)");
        }
        let end = clips.iter().map(|c| c.end()).max().unwrap_or(0);
        let length = ((end + BAR - 1) / BAR * BAR).max(8 * BAR);
        Ok(Project { name, tempo, tracks, clips, length, next_id: LOCAL_BASE })
    }

    /// Real waveform peaks for `len` UI ticks of the file at `path`, from `browser.peaks`.
    fn peaks(&mut self, path: &str, len: i64, tempo: f32) -> Option<Peaks> {
        let r = self.call("browser.peaks", json!({"path": path, "buckets": 2048}))?;
        let buckets = r["peaks"].as_array()?;
        let dur = r["duration_seconds"].as_f64()?.max(1e-6);
        let n = (len as usize * PEAKS_PER_BEAT / TPB as usize).max(1);
        let out: Vec<f32> = (0..n)
            .map(|i| {
                let secs = i as f64 / PEAKS_PER_BEAT as f64 * 60.0 / tempo as f64;
                let b = (secs / dur * buckets.len() as f64) as usize;
                buckets.get(b).map_or(0.0, |p| p["max"].as_f64().unwrap_or(0.0).abs().max(p["min"].as_f64().unwrap_or(0.0).abs()) as f32).min(1.0)
            })
            .collect();
        Some(Peaks(out.into()))
    }

    fn trk(&self, id: TrackId) -> Option<String> {
        self.tracks.get(&id).and_then(|t| t.trk.clone())
    }
    fn ch(&self, id: TrackId) -> Option<String> {
        self.tracks.get(&id).and_then(|t| t.ch.clone())
    }

    /// Send the engine whatever differs between the model it was last told and `project`.
    /// Returns how many commands went out.
    pub fn sync(&mut self, project: &Project) -> usize {
        if self.conn.is_none() || !self.session.status().is_ready() || *project == self.shadow {
            return 0;
        }
        // While a gesture is moving, clips keep the geometry the engine already has: only what is
        // heard during a drag (faders, pan, mute, solo, tempo) and note edits inside a clip go out.
        let held;
        let project = if self.in_gesture && !self.stream_clips {
            let mut p = project.clone();
            p.clips = self.shadow.clips.iter().map(|sc| match project.clip(sc.id) {
                Some(c) if c.start == sc.start && c.len == sc.len && c.track == sc.track => c.clone(),
                _ => sc.clone(),
            }).collect();
            if p == self.shadow {
                return 0;
            }
            held = p;
            &held
        } else {
            project
        };
        let before = self.log.lock().unwrap().calls.len();
        let old = std::mem::replace(&mut self.shadow, project.clone());

        if (project.tempo - old.tempo).abs() > 1e-3 {
            if project.tempo.fract().abs() > 1e-3 {
                self.gap("transport.set_tempo takes whole BPM; the UI's 0.1 BPM steps are rounded");
            }
            self.call("transport.set_tempo", json!({"bpm": project.tempo.round() as i64}));
        }

        // Tracks.
        for t in &project.tracks {
            match old.track(t.id) {
                None => self.add_track(t),
                Some(o) if o != t => self.update_track(o, t),
                _ => {}
            }
        }
        for o in &old.tracks {
            if project.track(o.id).is_none() {
                if let Some(trk) = self.trk(o.id) {
                    self.call("track.remove", json!({"track": trk}));
                }
                self.tracks.remove(&o.id);
            }
        }

        // Clips: removals first, so a recreated neighbour never collides with what it replaces.
        for o in &old.clips {
            if project.clip(o.id).is_none() {
                self.remove_clip(o);
            }
        }
        for c in &project.clips {
            match old.clip(c.id) {
                None => self.add_clip(c, project.tempo),
                Some(o) if o != c => self.update_clip(o, c, project.tempo),
                _ => {}
            }
        }
        self.publish_meter_channels();
        self.log.lock().unwrap().calls.len() - before
    }

    fn add_track(&mut self, t: &Track) {
        let ty = match t.kind {
            TrackKind::Instrument => "instrument",
            TrackKind::Audio => "sample",
            _ => return,
        };
        let mut args = json!({"type": ty, "name": t.name});
        if t.kind == TrackKind::Instrument {
            args["instrument"] = json!("tripleoscillator");
        }
        let Some(r) = self.call("track.add", args) else { return };
        let trk = r["id"].as_str().unwrap_or("").to_string();
        let ch = self.call("mixer.add_channel", json!({})).and_then(|c| c["id"].as_str().or(c["channel"].as_str()).map(String::from));
        if let Some(ch) = &ch {
            self.call("mixer.rename_channel", json!({"channel": ch, "name": t.name}));
            self.call("track.set_mixer_channel", json!({"track": trk, "channel": ch}));
            self.call("mixer.set_volume", json!({"channel": ch, "volume": db_to_gain(t.volume_db)}));
        }
        self.tracks.insert(t.id, TrackRef { trk: Some(trk), ch });
    }

    fn update_track(&mut self, o: &Track, t: &Track) {
        let (trk, ch) = (self.trk(t.id), self.ch(t.id));
        if o.name != t.name {
            match (&trk, &ch) {
                (Some(trk), _) => self.call("track.rename", json!({"track": trk, "name": t.name})),
                (None, Some(ch)) => self.call("mixer.rename_channel", json!({"channel": ch, "name": t.name})),
                _ => None,
            };
        }
        if o.mute != t.mute {
            match (&trk, &ch) {
                (Some(trk), _) => self.call("track.set_mute", json!({"track": trk, "muted": t.mute})),
                (None, Some(ch)) => self.call("mixer.set_many", json!({"channels": [ch], "muted": t.mute})),
                _ => None,
            };
        }
        if o.solo != t.solo {
            match (&trk, &ch) {
                (Some(trk), _) => self.call("track.set_solo", json!({"track": trk, "solo": t.solo})),
                (None, Some(ch)) => self.call("mixer.set_many", json!({"channels": [ch], "soloed": t.solo})),
                _ => None,
            };
        }
        if let Some(ch) = &ch {
            if o.volume_db != t.volume_db {
                self.call("mixer.set_volume", json!({"channel": ch, "volume": db_to_gain(t.volume_db)}));
            }
            if o.pan != t.pan {
                self.call("mixer.set_pan", json!({"channel": ch, "pan": t.pan}));
            }
        }
        if o.color != t.color {
            self.gap("track colour is a palette index in the UI; not forwarded (track.set_color takes a colour value)");
        }
    }

    fn remove_clip(&mut self, o: &Clip) {
        if let Some(cid) = self.clips.remove(&o.id) {
            self.call("clip.delete", json!({"clip": cid}));
        }
        for n in o.notes() {
            self.notes.remove(&n.id);
        }
        self.clip_paths.remove(&o.id);
    }

    fn add_clip(&mut self, c: &Clip, tempo: f32) {
        let Some(trk) = self.trk(c.track) else { return };
        match &c.kind {
            ClipKind::Midi { notes } => {
                let Some(r) = self.call("clip.add", json!({"track": trk, "position": to_engine(c.start), "length": to_engine(c.len).max(1), "name": c.name})) else { return };
                let cid = r["id"].as_str().or(r["clip"].as_str()).unwrap_or("").to_string();
                self.clips.insert(c.id, cid.clone());
                if !notes.is_empty() {
                    self.add_notes(&cid, notes);
                }
            }
            ClipKind::Audio { .. } => {
                // The file behind it: the one this clip was made from, else the browser item of its name.
                let path = self.clip_paths.get(&c.id).cloned().or_else(|| {
                    let i = demo::LIBRARY.iter().filter(|it| it.kind != demo::ItemKind::Midi).position(|it| it.name == c.name)?;
                    if self.library_audio.is_empty() { None } else { Some(self.library_audio[i % self.library_audio.len()].clone()) }
                });
                let Some(path) = path else {
                    self.gap("an audio clip with no known source file could not be created in the engine");
                    return;
                };
                let Some(r) = self.call("clip.import_file", json!({"path": path, "track": trk, "position": to_engine(c.start), "loop_mode": "keep_unstretched"})) else { return };
                let cid = r["clip"].as_str().unwrap_or("").to_string();
                self.clips.insert(c.id, cid.clone());
                self.clip_paths.insert(c.id, path.clone());
                // The UI chose the length (the demo item's bar count); tell the engine, and give the
                // UI the file's real overview in place of the synthesised one.
                self.call("clip.resize", json!({"clip": cid, "length": to_engine(c.len).max(1)}));
                if let Some(p) = self.peaks(&path, c.len, tempo) {
                    if let Some(sc) = self.shadow.clip_mut(c.id) {
                        sc.kind = ClipKind::Audio { peaks: p, offset: 0 };
                    }
                    self.pending_peaks.push(c.id);
                }
            }
        }
        if c.muted {
            self.call("clip.set_mute", json!({"clip": self.clips[&c.id], "muted": true}));
        }
        if c.gain_db != 0.0 {
            self.call("clip.set_gain", json!({"clip": self.clips[&c.id], "gain_db": c.gain_db}));
        }
    }

    fn add_notes(&mut self, cid: &str, notes: &[Note]) {
        let list: Vec<Value> = notes.iter().map(|n| json!({"key": n.pitch, "position": to_engine(n.start), "length": to_engine(n.len).max(1), "velocity": vel_to_engine(n.vel)})).collect();
        let r = if notes.len() == 1 {
            let n = &notes[0];
            self.call("note.add", json!({"clip": cid, "key": n.pitch, "position": to_engine(n.start), "length": to_engine(n.len).max(1), "velocity": vel_to_engine(n.vel)}))
        } else {
            self.call("note.add_many", json!({"clip": cid, "notes": list}))
        };
        let Some(r) = r else { return };
        // Ids: one for note.add; a list for note.add_many where the reply carries one.
        if let Some(id) = r["id"].as_str().or(r["note"].as_str()) {
            self.notes.insert(notes[0].id, id.to_string());
            return;
        }
        let ids: Vec<String> = r["notes"].as_array().or(r["ids"].as_array()).into_iter().flatten()
            .filter_map(|v| v.as_str().map(String::from).or_else(|| v["note"].as_str().map(String::from)).or_else(|| v["id"].as_str().map(String::from)))
            .collect();
        if ids.len() == notes.len() {
            for (n, id) in notes.iter().zip(ids) {
                self.notes.insert(n.id, id);
            }
        } else {
            self.relink_notes(cid, notes);
        }
    }

    /// Match local notes to the engine's by content, after an edit whose reply named no ids.
    fn relink_notes(&mut self, cid: &str, notes: &[Note]) {
        let Some(nl) = self.call("note.list", json!({"clip": cid})) else { return };
        let mut pool: Vec<(String, i64, i64, i64)> = nl["notes"].as_array().into_iter().flatten()
            .map(|n| (n["note"].as_str().unwrap_or("").to_string(), n["position"].as_i64().unwrap_or(0), n["key"].as_i64().unwrap_or(0), n["length"].as_i64().unwrap_or(0)))
            .collect();
        let known: Vec<&String> = self.notes.values().collect();
        pool.retain(|p| !known.contains(&&p.0));
        for n in notes {
            if let Some(i) = pool.iter().position(|p| p.1 == to_engine(n.start) && p.2 == n.pitch as i64 && p.3 == to_engine(n.len).max(1)) {
                self.notes.insert(n.id, pool.remove(i).0);
            }
        }
    }

    fn update_clip(&mut self, o: &Clip, c: &Clip, tempo: f32) {
        if o.track != c.track {
            // The surface has no way to move a clip to another track.
            self.gap("clip.move has no track argument: a lane change is emulated by deleting the clip and recreating it on the other track (fades, gain envelope and warp markers would be lost)");
            if !c.is_midi() {
                if let Some(p) = self.clip_paths.get(&o.id).cloned() {
                    self.remove_clip(o);
                    self.clip_paths.insert(c.id, p);
                    return self.add_clip(c, tempo);
                }
            }
            self.remove_clip(o);
            return self.add_clip(c, tempo);
        }
        let Some(cid) = self.clips.get(&c.id).cloned() else { return };
        let offset = |k: &ClipKind| if let ClipKind::Audio { offset, .. } = k { *offset } else { 0 };
        if offset(&o.kind) != offset(&c.kind) && o.end() == c.end() {
            // The head was cut (the overlap rule): start, length and source offset move together.
            self.call("clip.trim", json!({"clip": cid, "start": to_engine(c.start)}));
        } else {
            if o.start != c.start {
                self.call("clip.move", json!({"clip": cid, "position": to_engine(c.start)}));
            }
            if o.len != c.len {
                self.call("clip.resize", json!({"clip": cid, "length": to_engine(c.len).max(1)}));
            }
        }
        if o.gain_db != c.gain_db {
            self.call("clip.set_gain", json!({"clip": cid, "gain_db": c.gain_db}));
        }
        if o.muted != c.muted {
            self.call("clip.set_mute", json!({"clip": cid, "muted": c.muted}));
        }
        if o.name != c.name {
            self.gap("no command renames a clip (clip.add takes a name; nothing changes it afterwards)");
        }
        if let (ClipKind::Midi { notes: on }, ClipKind::Midi { notes: cn }) = (&o.kind, &c.kind) {
            if on != cn {
                self.update_notes(&cid, on, cn);
            }
        }
    }

    fn update_notes(&mut self, cid: &str, old: &[Note], new: &[Note]) {
        let gone: Vec<String> = old.iter().filter(|o| !new.iter().any(|n| n.id == o.id)).filter_map(|o| self.notes.remove(&o.id)).collect();
        match gone.len() {
            0 => {}
            1 => {
                self.call("note.remove", json!({"clip": cid, "note": gone[0]}));
            }
            _ => {
                self.call("note.remove_many", json!({"clip": cid, "notes": gone}));
            }
        }
        // Changed notes travel as one note.edit: one command and one undo step per frame of a drag.
        let mut edits = Vec::new();
        for n in new {
            let Some(o) = old.iter().find(|o| o.id == n.id) else { continue };
            if o == n { continue; }
            let Some(nid) = self.notes.get(&n.id) else { continue };
            let mut e = json!({"note": nid});
            if o.start != n.start { e["position"] = json!(to_engine(n.start)); }
            if o.len != n.len { e["length"] = json!(to_engine(n.len).max(1)); }
            if o.pitch != n.pitch { e["key"] = json!(n.pitch); }
            if o.vel != n.vel { e["velocity"] = json!(vel_to_engine(n.vel)); }
            edits.push((n.id, e));
        }
        if !edits.is_empty() {
            let list: Vec<Value> = edits.iter().map(|e| e.1.clone()).collect();
            if let Some(r) = self.call("note.edit", json!({"clip": cid, "edits": list})) {
                // A move can renumber: take the ids the reply reports, where it reports them.
                let ids: Vec<String> = r["notes"].as_array().into_iter().flatten().filter_map(|v| v["note"].as_str().or(v["id"].as_str()).or(v.as_str()).map(String::from)).collect();
                if ids.len() == edits.len() {
                    for ((lid, _), id) in edits.iter().zip(ids) {
                        self.notes.insert(*lid, id);
                    }
                }
            }
        }
        let added: Vec<Note> = new.iter().filter(|n| !old.iter().any(|o| o.id == n.id)).copied().collect();
        if !added.is_empty() {
            self.add_notes(cid, &added);
        }
    }

    /// Reread the engine and list where it disagrees with `project`. Positions compare at engine
    /// resolution; fields the surface cannot report (clip gain, mute, name edits) are not compared.
    pub fn reconcile(&mut self, project: &Project) -> Mismatches {
        let mut out = Vec::new();
        let (tracks, clips, notes, paths) = (self.tracks.clone(), self.clips.clone(), self.notes.clone(), self.clip_paths.clone());
        let truth = match self.load(false) {
            Ok(t) => t,
            Err(e) => return vec![format!("reload failed: {e}")],
        };
        // `load` rebuilt the id maps from engine ids; reconcile compares through the old maps.
        let by_engine_track: HashMap<String, TrackId> = tracks.iter().filter_map(|(k, v)| v.trk.clone().or(v.ch.clone()).map(|e| (e, *k))).collect();
        let truth_track = |local: TrackId| -> Option<&Track> {
            let r = tracks.get(&local)?;
            let eid = match (&r.trk, &r.ch) {
                (Some(t), _) => id_num(t),
                (None, Some(c)) => CH_BASE + id_num(c),
                _ => return None,
            };
            truth.track(eid)
        };
        if (truth.tempo - project.tempo.round()).abs() > 1e-3 {
            out.push(format!("tempo: ui {} engine {}", project.tempo, truth.tempo));
        }
        for t in &project.tracks {
            let Some(e) = truth_track(t.id) else {
                out.push(format!("track {} ({}): not in the engine", t.id, t.name));
                continue;
            };
            if e.name != t.name { out.push(format!("track {} name: ui {:?} engine {:?}", t.id, t.name, e.name)); }
            if e.mute != t.mute { out.push(format!("track {} ({}) mute: ui {} engine {}", t.id, t.name, t.mute, e.mute)); }
            if e.solo != t.solo { out.push(format!("track {} ({}) solo: ui {} engine {}", t.id, t.name, t.solo, e.solo)); }
            if (e.volume_db - t.volume_db).abs() > 0.05 { out.push(format!("track {} ({}) volume: ui {:.2} dB engine {:.2} dB", t.id, t.name, t.volume_db, e.volume_db)); }
            if (e.pan - t.pan).abs() > 0.005 { out.push(format!("track {} ({}) pan: ui {:.3} engine {:.3}", t.id, t.name, t.pan, e.pan)); }
        }
        let lanes = |p: &Project| p.tracks.iter().filter(|t| matches!(t.kind, TrackKind::Audio | TrackKind::Instrument)).count();
        if lanes(&truth) != lanes(project) {
            out.push(format!("lane count: ui {} engine {}", lanes(project), lanes(&truth)));
        }
        for c in &project.clips {
            let Some(e) = clips.get(&c.id).and_then(|cid| truth.clip(id_num(cid))) else {
                out.push(format!("clip {} ({}): not in the engine", c.id, c.name));
                continue;
            };
            let track_ok = tracks.get(&c.track).and_then(|r| r.trk.as_deref()).map(id_num) == Some(e.track);
            if !track_ok { out.push(format!("clip {} ({}) track: ui {} engine trk-{}", c.id, c.name, c.track, e.track)); }
            if to_engine(c.start) * TICK_SCALE != e.start { out.push(format!("clip {} ({}) start: ui {} engine {}", c.id, c.name, c.start, e.start)); }
            if to_engine(c.len).max(1) * TICK_SCALE != e.len { out.push(format!("clip {} ({}) length: ui {} engine {}", c.id, c.name, c.len, e.len)); }
            if c.is_midi() {
                if c.notes().len() != e.notes().len() {
                    out.push(format!("clip {} ({}) note count: ui {} engine {}", c.id, c.name, c.notes().len(), e.notes().len()));
                }
                for n in c.notes() {
                    let Some(en) = notes.get(&n.id).and_then(|nid| e.notes().iter().find(|x| x.id == id_num(nid))) else {
                        out.push(format!("clip {} note {}: not in the engine", c.id, n.id));
                        continue;
                    };
                    if to_engine(n.start) * TICK_SCALE != en.start || to_engine(n.len).max(1) * TICK_SCALE != en.len || n.pitch != en.pitch || vel_to_engine(n.vel) != vel_to_engine(en.vel) {
                        out.push(format!("clip {} note {}: ui {:?} engine {:?}", c.id, n.id, (n.start, n.len, n.pitch, n.vel), (en.start, en.len, en.pitch, en.vel)));
                    }
                }
            }
        }
        if truth.clips.len() != project.clips.len() {
            out.push(format!("clip count: ui {} engine {}", project.clips.len(), truth.clips.len()));
        }
        let _ = by_engine_track;
        (self.tracks, self.clips, self.notes, self.clip_paths) = (tracks, clips, notes, paths);
        out
    }

    /// Run any command on the link's own connection (tests and the bench use it for renders).
    pub fn raw(&mut self, cmd: &str, args: Value) -> Result<Value, String> {
        self.rpc(cmd, args)
    }
    pub fn engine_clip(&self, id: ClipId) -> Option<&str> {
        self.clips.get(&id).map(String::as_str)
    }
    pub fn engine_track(&self, id: TrackId) -> Option<&str> {
        self.tracks.get(&id).and_then(|t| t.trk.as_deref())
    }
    pub fn engine_channel(&self, id: TrackId) -> Option<&str> {
        self.tracks.get(&id).and_then(|t| t.ch.as_deref())
    }
    pub fn telemetry(&self) -> Telemetry {
        self.tele.lock().unwrap().clone()
    }
}

fn flat_peaks(len: i64) -> Peaks {
    Peaks(vec![0.35f32; (len as usize * PEAKS_PER_BEAT / TPB as usize).max(1)].into())
}

impl Drop for ZeneEngine {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
        if self.conn.as_ref().is_some_and(|c| c.is_open()) {
            if let Some(mode) = self.overlap_before.take() {
                self.rpc("arrangement.modes", json!({"overlap": mode})).ok();
            }
        }
        // `session` drops after this: an engine this UI started is stopped only if nothing is
        // unsaved (zene_link::session, rule 4); an attached engine is never stopped.
    }
}

impl Engine for ZeneEngine {
    fn send(&mut self, command: EngineCommand) {
        match command {
            EngineCommand::Play => {
                self.call("transport.play", json!({}));
                self.snap.playing = true;
            }
            EngineCommand::Stop => {
                self.call("transport.stop", json!({}));
                self.snap.playing = false;
            }
            EngineCommand::Seek(t) => {
                self.call("transport.seek", json!({"ticks": to_engine(t)}));
                self.snap.position = (to_engine(t) * TICK_SCALE) as f64;
                // Telemetry older than this seek must not pull the playhead back.
                self.sample_at = None;
                self.seen_sample = self.tele.lock().unwrap().samples + 1;
            }
            // Tempo is part of the project and travels with `sync`.
            EngineCommand::SetTempo(_) => {}
        }
    }

    fn advance(&mut self, now: f64, project: &Project) -> bool {
        let before = self.snap.clone();
        self.follow_session(Some(project));
        // A fresh read waits for the UI to take it; until then the old model must not be pushed.
        if self.reload.is_none() {
            self.sync(project);
        }
        let dt = self.last_advance.map_or(0.0, |l| (now - l).clamp(0.0, 0.25)) as f32;
        self.last_advance = Some(now);

        let t = self.tele.lock().unwrap().clone();
        if let Some(at) = t.at {
            self.log.lock().unwrap().staleness.push(at.elapsed().as_secs_f32() * 1000.0);
        }
        if t.samples > self.seen_sample {
            self.snap.playing = t.playing;
            // The engine reports whole ticks (48 a beat); between two reports the playhead runs
            // on at the tempo, capped at one engine tick so it never passes the next report.
            if self.sample_at.is_none() || (t.ticks * TICK_SCALE) as f64 != self.base_position {
                self.base_position = (t.ticks * TICK_SCALE) as f64;
                self.sample_at = t.at;
            }
            self.seen_sample = t.samples;
        }
        if let (true, Some(at)) = (self.snap.playing, self.sample_at) {
            let run = at.elapsed().as_secs_f64() * t.tempo / 60.0 * TPB as f64;
            self.snap.position = self.base_position + run.min(TICK_SCALE as f64);
        } else if self.sample_at.is_some() {
            self.snap.position = self.base_position;
        }

        // Meters: the engine's post-fader RMS per channel, with the same ballistics as the simulation.
        let fall = dt * 0.34;
        let mut meters = Vec::with_capacity(project.tracks.len());
        for tr in &project.tracks {
            let level = self.tracks.get(&tr.id).and_then(|r| r.ch.as_ref()).and_then(|ch| t.levels.iter().find(|l| &l.0 == ch)).map_or(0.0, |l| l.1);
            // RMS reads about 3 dB under a sine's peak; the scale is the UI's own (-48..+6 dB).
            let target = meter_scale(level * 1.41);
            let (old, hold) = self.snap.meter(tr.id);
            let now_level = if target >= old { target } else { (old - fall).max(target) };
            let now_hold = if now_level >= hold { now_level } else { (hold - fall * 0.35).max(now_level) };
            meters.push((tr.id, now_level, now_hold));
        }
        self.snap.meters = meters;
        self.snap != before
    }

    fn snapshot(&self) -> &EngineSnapshot {
        &self.snap
    }

    fn describe(&self) -> &'static str {
        "Zene Studio engine over the control socket: transport, meters and edits are the engine's"
    }

    fn label(&self) -> &'static str {
        "Zene engine (control socket)"
    }

    fn meters_note(&self) -> &'static str {
        "Meters are the engine's post-fader RMS per channel. Drag a fader, or focus it and use the arrow keys."
    }

    fn take_model_patch(&mut self) -> Vec<(ClipId, Peaks)> {
        let ids = std::mem::take(&mut self.pending_peaks);
        ids.into_iter().filter_map(|id| match self.shadow.clip(id).map(|c| &c.kind) {
            Some(ClipKind::Audio { peaks, .. }) => Some((id, peaks.clone())),
            _ => None,
        }).collect()
    }

    fn gesture(&mut self, active: bool) {
        self.in_gesture = active;
    }

    fn link_status(&self) -> Option<&Status> {
        Some(self.session.status())
    }

    fn take_reload(&mut self) -> Option<Project> {
        self.reload.take()
    }

    fn take_notices(&mut self) -> Vec<String> {
        std::mem::take(&mut self.notices)
    }

    fn exit_check(&mut self) -> ExitCheck {
        self.session.exit_check()
    }

    fn exit(&mut self, choice: ExitChoice) -> Result<ExitOutcome, LinkError> {
        let r = self.session.exit(choice);
        self.notices.extend(self.session.take_notices());
        r
    }

    fn stop_if_clean(&mut self) -> Result<ExitOutcome, LinkError> {
        self.session.stop_if_clean()
    }

    fn reconnect(&mut self) {
        self.session.reconnect();
    }

    fn restart(&mut self) {
        self.session.restart();
    }

    fn as_any(&mut self) -> &mut dyn std::any::Any {
        self
    }
}
