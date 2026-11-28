//! Phase 0 spikes S1 and S2: the prototype's real view and handlers, in Rune's headless runtime,
//! bound to a running Zene Studio engine. Scripted pointer and key input goes in at the top;
//! every effect is read back from the engine on a second, independent connection.
//!
//! These need a live engine, so they are `#[ignore]`d and read `ZENE_SOCKET`:
//!   `spikes/s1_run.py` starts the engine, builds the fixture project and runs them.
//! Results are written as JSON to `ZENE_OUT` (a directory).

use std::time::{Duration, Instant};

use rune::{Env, Key, KeyEvent, Mods, Runtime};
use serde_json::{Value, json};

use crate::command::Command;
use crate::model::*;
use crate::state::*;
use crate::ui;
use crate::zene::{self, Conn, TICK_SCALE, ZeneEngine};

const FRAME: f64 = 1.0 / 60.0;

struct Rig {
    rt: Runtime<App>,
    /// Independent read-back connection.
    v: Conn,
    checks: Vec<(bool, String)>,
}

fn socket() -> String {
    std::env::var("ZENE_SOCKET").expect("ZENE_SOCKET: the control socket of a running engine")
}
fn out_dir() -> String {
    std::env::var("ZENE_OUT").unwrap_or_else(|_| "/tmp".into())
}

impl Rig {
    fn new() -> Rig {
        let path = socket();
        let (project, engine) = zene::connect(&path).expect("connect");
        let mut app = App::with(project, Box::new(engine));
        app.zoom_fit();
        let mut rt = Runtime::new(app, ui::view, (1366.0, 768.0), Env { system_dark: true, scale: 1.0 });
        rt.on_key = Some(crate::on_key);
        rt.poll = Some(crate::poll);
        let mut r = Rig { rt, v: Conn::connect(&path).expect("verifier"), checks: Vec::new() };
        r.frames(6);
        r
    }
    fn link(&mut self) -> &mut ZeneEngine {
        self.rt.state.engine.as_any().downcast_mut::<ZeneEngine>().expect("the engine link")
    }
    fn frames(&mut self, n: usize) {
        for _ in 0..n {
            self.rt.frame(FRAME);
        }
    }
    /// Frames at real time, for anything that waits on the engine's clock.
    fn real(&mut self, ms: u64) {
        let end = Instant::now() + Duration::from_millis(ms);
        while Instant::now() < end {
            self.rt.frame(FRAME);
            std::thread::sleep(Duration::from_millis(16));
        }
    }
    fn check(&mut self, ok: bool, what: impl Into<String>) {
        let what = what.into();
        eprintln!("{} {what}", if ok { "PASS" } else { "FAIL" });
        self.checks.push((ok, what));
    }
    fn at(&self, key: &str) -> (f32, f32) {
        self.rt.find(key).unwrap_or_else(|| panic!("no element with key {key}"))
    }
    fn click_at(&mut self, x: f32, y: f32) {
        self.rt.pointer_move(x, y);
        self.rt.pointer_down(x, y);
        self.rt.pointer_up(x, y);
        self.frames(3);
    }
    fn click(&mut self, key: &str) {
        let (x, y) = self.at(key);
        self.click_at(x, y);
    }
    fn drag(&mut self, from: (f32, f32), by: (f32, f32), steps: usize) {
        self.rt.pointer_move(from.0, from.1);
        self.rt.pointer_down(from.0, from.1);
        for i in 1..=steps {
            let k = i as f32 / steps as f32;
            self.rt.pointer_move(from.0 + by.0 * k, from.1 + by.1 * k);
            self.rt.frame(FRAME);
        }
        self.rt.pointer_up(from.0 + by.0, from.1 + by.1);
        self.frames(3);
    }
    fn key(&mut self, k: Key) {
        self.rt.key(KeyEvent { key: k, mods: Mods::default() });
        self.frames(3);
    }
    fn ctrl(&mut self, c: char, shift: bool) {
        self.rt.key(KeyEvent { key: Key::Command(c), mods: Mods { cmd: true, ctrl: true, shift, ..Mods::default() } });
        self.frames(3);
    }
    fn act(&mut self, f: impl FnOnce(&mut App)) {
        self.rt.update(|s, _tx| f(s));
        self.frames(3);
    }

    // ---- read-backs from the engine ------------------------------------------------------

    fn q(&mut self, cmd: &str, args: Value) -> Value {
        // The engine retires a connection that has been idle for its timeout: reconnect once.
        match self.v.call(cmd, args.clone()) {
            Ok(v) => v,
            Err(_) => {
                self.v = Conn::connect(&socket()).expect("verifier reconnect");
                self.v.call(cmd, args).unwrap_or_else(|e| panic!("{e}"))
            }
        }
    }
    fn e_clip(&mut self, local: ClipId) -> Option<Value> {
        let id = self.link().engine_clip(local)?.to_string();
        let list = self.q("clip.list", json!({"limit": 500}));
        list["clips"].as_array()?.iter().find(|c| c["clip"] == id.as_str()).cloned()
    }
    fn e_notes(&mut self, local: ClipId) -> Vec<Value> {
        let Some(id) = self.link().engine_clip(local).map(String::from) else { return Vec::new() };
        self.q("note.list", json!({"clip": id}))["notes"].as_array().cloned().unwrap_or_default()
    }
    fn e_track(&mut self, local: TrackId) -> Value {
        let id = self.link().engine_track(local).expect("track").to_string();
        self.q("track.get_state", json!({"track": id}))
    }
    fn e_channel(&mut self, local: TrackId) -> Value {
        let id = self.link().engine_channel(local).expect("channel").to_string();
        let m = self.q("mixer.get_state", json!({}));
        m["channels"].as_array().and_then(|c| c.iter().find(|c| c["id"] == id.as_str()).cloned()).unwrap_or(Value::Null)
    }
    fn clip(&self, name: &str) -> Clip {
        self.rt.state.project.clips.iter().find(|c| c.name == name).cloned().unwrap_or_else(|| panic!("no clip {name}"))
    }
    fn track(&self, name: &str) -> Track {
        self.rt.state.project.tracks.iter().find(|t| t.name == name).cloned().unwrap_or_else(|| panic!("no track {name}"))
    }

    fn finish(self, file: &str) {
        self.finish_with(file, json!({}))
    }

    /// A pointer drag paced at 60 Hz of real time along `path(i)`; returns each frame's wall time
    /// in milliseconds (input, handlers, the link's commands, the view and layout: no GPU here).
    fn paced_drag(&mut self, from: (f32, f32), frames: usize, path: impl Fn(usize) -> (f32, f32)) -> Vec<f32> {
        let mut out = Vec::with_capacity(frames);
        self.rt.pointer_move(from.0, from.1);
        self.rt.pointer_down(from.0, from.1);
        let mut last = from;
        for i in 1..=frames {
            let t0 = Instant::now();
            let (dx, dy) = path(i);
            last = (from.0 + dx, from.1 + dy);
            self.rt.pointer_move(last.0, last.1);
            self.rt.frame(FRAME);
            out.push(t0.elapsed().as_secs_f32() * 1000.0);
            if let Some(rest) = Duration::from_micros(16_667).checked_sub(t0.elapsed()) {
                std::thread::sleep(rest);
            }
        }
        self.rt.pointer_up(last.0, last.1);
        self.frames(3);
        out
    }

    /// Round trips of `cmd` recorded since call index `from`.
    fn calls_since(&mut self, from: usize, cmd: &str) -> Vec<f32> {
        let log = self.link().log.clone();
        let log = log.lock().unwrap();
        log.calls[from..].iter().filter(|c| c.0 == cmd).map(|c| c.1).collect()
    }
    fn call_count(&mut self) -> usize {
        let log = self.link().log.clone();
        let n = log.lock().unwrap().calls.len();
        n
    }

    fn finish_with(mut self, file: &str, extra: Value) {
        let project = self.rt.state.project.clone();
        let mismatches = self.link().reconcile(&project);
        let log = self.link().log.clone();
        let log = log.lock().unwrap();
        let mut by: std::collections::BTreeMap<String, Vec<f32>> = Default::default();
        for (c, ms) in &log.calls {
            by.entry(c.clone()).or_default().push(*ms);
        }
        let all: Vec<f32> = log.calls.iter().map(|c| c.1).collect();
        // Subscription lag: the n-th event naming a command against the n-th reply to it.
        let mut lag = Vec::new();
        let mut names: Vec<&String> = log.events.iter().map(|e| &e.0).collect();
        names.sort();
        names.dedup();
        let mut unmatched = 0;
        for name in names {
            let acks: Vec<&Instant> = log.acks.iter().filter(|a| &a.0 == name).map(|a| &a.1).collect();
            let evs: Vec<&Instant> = log.events.iter().filter(|e| &e.0 == name).map(|e| &e.1).collect();
            if acks.len() != evs.len() {
                unmatched += evs.len();
                continue;
            }
            for (a, e) in acks.iter().zip(evs) {
                lag.push(if e >= *a { e.duration_since(**a).as_secs_f32() * 1000.0 } else { -(a.duration_since(*e).as_secs_f32() * 1000.0) });
            }
        }
        let mut ev_by: std::collections::BTreeMap<String, (usize, usize)> = Default::default();
        for e in &log.events { ev_by.entry(e.0.clone()).or_default().0 += 1; }
        for a in &log.acks { if let Some(x) = ev_by.get_mut(&a.0) { x.1 += 1; } }
        let report = json!({
            "extra": extra,
            "events_vs_replies_by_command": ev_by.iter().map(|(k, v)| (k.clone(), json!({"events": v.0, "replies": v.1}))).collect::<serde_json::Map<_, _>>(),
            "events_received": log.events.len(),
            "event_minus_reply_ms": stats(&lag),
            "events_not_paired": unmatched,
            "checks": self.checks.iter().map(|(ok, w)| json!({"ok": ok, "what": w})).collect::<Vec<_>>(),
            "passed": self.checks.iter().filter(|c| c.0).count(),
            "failed": self.checks.iter().filter(|c| !c.0).count(),
            "reconcile_mismatches": mismatches,
            "link_errors": log.errors,
            "surface_gaps": log.gaps,
            "commands_sent": all.len(),
            "round_trip_ms": stats(&all),
            "round_trip_ms_by_command": by.iter().map(|(k, v)| (k.clone(), stats(v))).collect::<serde_json::Map<_, _>>(),
            "telemetry_cycle_ms": stats(&log.cycles),
            "telemetry_staleness_ms": stats(&log.staleness),
        });
        let path = format!("{}/{file}", out_dir());
        std::fs::write(&path, serde_json::to_string_pretty(&report).unwrap()).expect("write report");
        eprintln!("report: {path}");
        let failed: Vec<&String> = self.checks.iter().filter(|c| !c.0).map(|c| &c.1).collect();
        assert!(failed.is_empty(), "failed checks: {failed:#?}");
    }
}

pub fn stats(v: &[f32]) -> Value {
    if v.is_empty() {
        return json!({"n": 0});
    }
    let mut s = v.to_vec();
    s.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let p = |q: f64| s[((s.len() - 1) as f64 * q).round() as usize];
    let r = |x: f32| (x as f64 * 1000.0).round() / 1000.0;
    json!({"n": s.len(), "p50": r(p(0.5)), "p95": r(p(0.95)), "p99": r(p(0.99)), "max": r(s[s.len() - 1]), "mean": r(s.iter().sum::<f32>() / s.len() as f32)})
}

/// S1: the six surfaces on the fixture project (`spikes/s1_fixture.py`, small).
#[test]
#[ignore = "needs a running engine: spikes/s1_run.py"]
fn s1_six_surfaces_drive_the_real_engine() {
    let mut r = Rig::new();
    let p = r.rt.state.project.clone();
    r.check(p.lanes().count() == 4 && p.clips.len() == 5, format!("load: the engine's project is the UI's model ({} lanes, {} clips, tempo {})", p.lanes().count(), p.clips.len(), p.tempo));
    r.check(p.clips.iter().filter(|c| c.is_midi()).map(|c| c.notes().len()).sum::<usize>() == 28, "load: 28 notes read from the engine's three MIDI clips");

    // ---- transport ------------------------------------------------------------------------
    r.click("play");
    r.real(700);
    let t = r.q("transport.get_state", json!({}));
    r.check(t["playing"] == true && t["position_ticks"].as_i64().unwrap_or(0) > 20, format!("transport: the Play button starts the engine (engine says playing={}, position_ticks={})", t["playing"], t["position_ticks"]));
    let ui_pos = r.rt.state.position();
    let e_pos = r.q("transport.get_state", json!({}))["position_ticks"].as_i64().unwrap_or(0) * TICK_SCALE;
    r.check(r.rt.state.playing() && (ui_pos - e_pos).abs() <= 3 * 48 * TICK_SCALE / 10, format!("transport: the UI playhead follows the engine (ui {ui_pos}, engine {e_pos} ui-ticks)"));
    let loud = r.rt.state.engine.snapshot().meters.iter().filter(|m| m.1 > 0.05).count();
    let bass_id = r.track("Bass").id;
    let bass_ch = r.link().engine_channel(bass_id).unwrap().to_string();
    let mut peak = 0.0f64;
    for _ in 0..20 {
        let m = r.q("mixer.strip_get", json!({"channel": bass_ch}));
        peak = peak.max(m["meters"]["rms_left"].as_f64().unwrap_or(0.0));
        r.real(20);
    }
    r.check(loud >= 2 && peak > 0.001, format!("audio: {loud} UI strip meters move while playing, fed by the engine's own meters (Bass channel post-fader RMS peaked at {peak:.4})"));
    r.key(Key::Text(" ".into()));
    r.real(120);
    let stopped = r.q("transport.get_state", json!({}))["playing"] == false;
    r.check(stopped, "transport: Space stops the engine");
    // Ruler click at beat 8.
    let lanes = r.rt.state.lanes_rect;
    let (_, ry) = r.at("ruler");
    let x = lanes[0] + r.rt.state.tick_to_x(8.0 * TPB as f64) + 1.0;
    r.click_at(x, ry);
    r.real(60);
    let pos = r.q("transport.get_state", json!({}))["position_ticks"].as_i64().unwrap_or(-1);
    r.check(pos == 8 * 48, format!("transport: a click on the ruler at beat 9 seeks the engine (position_ticks {pos}, expected 384)"));
    r.click("tempo-up");
    let tempo = r.q("transport.get_state", json!({}))["tempo"].as_i64().unwrap_or(0);
    r.check(tempo == 121, format!("transport: Tempo +1 reaches the engine (tempo {tempo})"));
    r.ctrl('z', false);
    let tempo = r.q("transport.get_state", json!({}))["tempo"].as_i64().unwrap_or(0);
    r.check(tempo == 120, format!("undo: Ctrl+Z puts the engine's tempo back ({tempo})"));
    r.key(Key::Home);
    r.real(60);

    // ---- arrangement ----------------------------------------------------------------------
    let c = r.clip("Bass B");
    let from = r.at(&format!("clip-{}", c.id));
    let dx = (4.0 * TPB as f64 * r.rt.state.px_per_tick()) as f32 + 2.0;
    r.drag(from, (dx, 0.0), 12);
    let ui = r.clip("Bass B");
    let e = r.e_clip(c.id).unwrap_or(Value::Null);
    r.check(ui.start == c.start + 4 * TPB && e["start"].as_i64() == Some(ui.start / TICK_SCALE), format!("arrangement: dragging a clip four beats moves it in the engine (ui start {}, engine start {})", ui.start, e["start"]));
    r.ctrl('z', false);
    let e = r.e_clip(c.id).unwrap_or(Value::Null);
    r.check(e["start"].as_i64() == Some(c.start / TICK_SCALE), format!("undo: Ctrl+Z moves the engine's clip back ({})", e["start"]));
    r.ctrl('z', true);
    let e = r.e_clip(c.id).unwrap_or(Value::Null);
    r.check(e["start"].as_i64() == Some((c.start + 4 * TPB) / TICK_SCALE), format!("redo: Ctrl+Shift+Z moves it forward again ({})", e["start"]));
    let c = r.clip("Bass B");
    let (ex, ey) = r.at(&format!("clip-end-{}", c.id));
    let dx = (4.0 * TPB as f64 * r.rt.state.px_per_tick()) as f32;
    r.drag((ex, ey), (dx, 0.0), 8);
    let e = r.e_clip(c.id).unwrap_or(Value::Null);
    let ui = r.clip("Bass B");
    r.check(ui.len == c.len + 4 * TPB && e["length"].as_i64() == Some(ui.len / TICK_SCALE), format!("arrangement: the right edge resizes the engine's clip (ui {}, engine {})", ui.len, e["length"]));
    // Lane change, Bass -> Keys (both instruments), at a free spot.
    let from = r.at(&format!("clip-{}", c.id));
    let th = r.rt.state.track_height();
    r.drag(from, (0.0, th), 8);
    let ui = r.clip("Bass B");
    let keys = r.track("Keys");
    let e = r.e_clip(ui.id).unwrap_or(Value::Null);
    let keys_trk = r.link().engine_track(keys.id).unwrap().to_string();
    let n = r.e_notes(ui.id).len();
    r.check(ui.track == keys.id && e["track"] == keys_trk.as_str() && n == 8, format!("arrangement: dragging a clip to another lane lands it on that track in the engine with its notes (engine track {}, {n} notes) [emulated: see gaps]", e["track"]));

    // ---- inspector ------------------------------------------------------------------------
    let c = r.clip("Bass A");
    r.click(&format!("clip-{}", c.id));
    r.click("step-start-up");
    let ui = r.clip("Bass A");
    let e = r.e_clip(c.id).unwrap_or(Value::Null);
    r.check(ui.start > c.start && e["start"].as_i64() == Some(ui.start / TICK_SCALE), format!("inspector: the Start stepper moves the selected clip in the engine (ui {}, engine {})", ui.start, e["start"]));
    let bass = r.track("Bass");
    r.click(&format!("track-{}", bass.id));
    r.click("step-volume-up");
    let ui = r.track("Bass");
    let vol = r.e_channel(bass.id)["volume"].as_f64().unwrap_or(-1.0);
    let want = 10f64.powf(ui.volume_db as f64 / 20.0);
    r.check(ui.volume_db > bass.volume_db && (vol - want).abs() < 0.002, format!("inspector: the Volume stepper sets the engine's fader ({:.2} dB in the UI, gain {vol:.4} in the engine, expected {want:.4})", ui.volume_db));
    r.click("inspector-name");
    r.ctrl('a', false);
    for ch in "Sub Bass".chars() {
        r.rt.key(KeyEvent { key: Key::Text(ch.to_string()), mods: Mods::default() });
    }
    r.key(Key::Enter);
    let ui_name = r.rt.state.project.track(bass.id).map(|t| t.name.clone()).unwrap_or_default();
    let e_name = r.e_track(bass.id)["name"].as_str().unwrap_or("").to_string();
    r.check(ui_name == e_name && e_name != "Bass", format!("inspector: renaming the track in the name field renames it in the engine (ui {ui_name:?}, engine {e_name:?})"));

    // ---- mixer ----------------------------------------------------------------------------
    r.act(|s| s.run(Command::ShowMixer));
    if !(r.rt.state.show_dock && r.rt.state.dock_tab == DockTab::Mixer) {
        r.act(|s| s.run(Command::ShowMixer));
    }
    let keys = r.track("Keys");
    let from = r.at(&format!("fader-{}", keys.id));
    r.drag(from, (0.0, 30.0), 15);
    let ui = r.track("Keys");
    let vol = r.e_channel(keys.id)["volume"].as_f64().unwrap_or(-1.0);
    let want = 10f64.powf(ui.volume_db as f64 / 20.0);
    r.check(ui.volume_db < keys.volume_db - 1.0 && (vol - want).abs() < 0.002, format!("mixer: dragging a fader sets the engine's channel gain ({:.2} dB, engine {vol:.4}, expected {want:.4})", ui.volume_db));
    r.ctrl('z', false);
    let vol = r.e_channel(keys.id)["volume"].as_f64().unwrap_or(-1.0);
    r.check((vol - 1.0).abs() < 0.002, format!("undo: the whole fader drag is one step and the engine's gain is back at {vol:.4}"));
    let drums = r.track("Drums");
    r.click(&format!("strip-mute-{}", drums.id));
    let muted = r.e_track(drums.id)["muted"] == true;
    r.check(muted, "mixer: the strip's Mute button mutes the engine's track");
    r.click(&format!("mute-{}", drums.id));
    let unmuted = r.e_track(drums.id)["muted"] == false;
    r.check(unmuted, "arrangement: the track header's Mute button unmutes it (same command)");
    r.click(&format!("strip-solo-{}", keys.id));
    let solo_on = r.e_track(keys.id)["soloed"] == true;
    let others_muted = r.e_track(drums.id)["muted"] == true;
    r.click(&format!("strip-solo-{}", keys.id));
    let solo_off = r.e_track(keys.id)["soloed"] == false && r.e_track(drums.id)["muted"] == false;
    r.check(solo_on && solo_off, format!("mixer: Solo on and off reach the engine (while soloed the engine reports the other tracks muted={others_muted}: its solo is implemented as mutes)"));

    // ---- piano roll -----------------------------------------------------------------------
    let c = r.clip("Keys A");
    r.act(|s| s.run(Command::OpenPianoRoll(c.id)));
    let before = r.e_notes(c.id).len();
    let pr = r.rt.state.pr_rect;
    let (x, y) = (pr[0] + r.rt.state.pr_ppb * 1.0 + 6.0, pr[1] + 5.0);
    let pitch = (PITCH_HI as i32 - ((5.0 + r.rt.state.pr_scroll_y) / NOTE_H).floor() as i32) as i64;
    r.click_at(x, y);
    let notes = r.e_notes(c.id);
    let added = notes.iter().find(|n| n["key"].as_i64() == Some(pitch) && n["position"].as_i64() == Some(48)).cloned();
    r.check(notes.len() == before + 1 && added.is_some(), format!("piano roll: a click adds a note in the engine (key {pitch} at tick 48; {} -> {} notes)", before, notes.len()));
    let n = *r.rt.state.pr_notes().last().unwrap();
    let from = r.at(&format!("note-{}", n.id));
    let dx = r.rt.state.pr_ppb + 2.0;
    r.drag((from.0 - 4.0, from.1), (dx, NOTE_H), 10);
    let notes = r.e_notes(c.id);
    let moved = notes.iter().any(|x| x["key"].as_i64() == Some(pitch - 1) && x["position"].as_i64() == Some(96));
    r.check(moved && notes.len() == before + 1, format!("piano roll: dragging the note one beat later and one key down moves it in the engine (key {} at tick 96)", pitch - 1));
    let m = *r.rt.state.pr_notes().iter().find(|x| x.id == n.id).unwrap();
    let w = (m.len as f64 * r.rt.state.pr_px_per_tick()) as f32;
    let left = pr[0] + r.rt.state.pr_tick_to_x(m.start as f64);
    let ny = r.at(&format!("note-{}", n.id)).1;
    r.drag((left + w - 3.0, ny), (r.rt.state.pr_ppb * 0.5, 0.0), 6);
    let notes = r.e_notes(c.id);
    let len = notes.iter().find(|x| x["key"].as_i64() == Some(pitch - 1) && x["position"].as_i64() == Some(96)).and_then(|x| x["length"].as_i64());
    r.check(len == Some(48), format!("piano roll: the right edge resizes the note in the engine (length {len:?}, expected 48)"));
    let (_, vy) = r.at("pr-velocity");
    r.drag((left + 2.0, vy), (0.0, -14.0), 6);
    let ui_vel = r.rt.state.pr_notes().iter().find(|x| x.id == n.id).map(|x| x.vel).unwrap_or(0);
    let notes = r.e_notes(c.id);
    let vel = notes.iter().find(|x| x["key"].as_i64() == Some(pitch - 1) && x["position"].as_i64() == Some(96)).and_then(|x| x["velocity"].as_i64());
    r.check(vel == Some((ui_vel as f64 * 100.0 / 127.0).round() as i64), format!("piano roll: the velocity lane sets the engine's note velocity (ui {ui_vel}/127, engine {vel:?}/200 scale)"));
    let (nx, ny) = r.at(&format!("note-{}", n.id));
    r.rt.secondary_down(nx - 3.0, ny);
    r.rt.secondary_up(nx - 3.0, ny);
    r.frames(3);
    let after = r.e_notes(c.id).len();
    r.check(after == before, "piano roll: right-click deletes the note in the engine");

    // ---- browser --------------------------------------------------------------------------
    let vox = r.track("Vox");
    let clips_before = r.q("clip.list", json!({"limit": 500}))["count"].as_i64().unwrap_or(0);
    let lanes = r.rt.state.lanes_rect;
    let th = r.rt.state.track_height();
    let lane = r.rt.state.project.lane_index(vox.id).unwrap() as f32;
    let from = r.at("item-2"); // an audio loop in the demo library
    let to = (lanes[0] + r.rt.state.tick_to_x(4.0 * TPB as f64) + 2.0, lanes[1] + th * (lane + 0.5));
    r.drag(from, (to.0 - from.0, to.1 - from.1), 10);
    r.frames(3);
    let added = r.rt.state.project.clips.iter().find(|c| c.track == vox.id).cloned();
    let e = added.as_ref().and_then(|c| r.e_clip(c.id));
    let vox_trk = r.link().engine_track(vox.id).unwrap().to_string();
    let ok = e.as_ref().is_some_and(|e| e["kind"] == "audio" && e["track"] == vox_trk.as_str() && e["start"].as_i64() == Some(4 * 48));
    let real_peaks = added.as_ref().is_some_and(|c| matches!(&c.kind, ClipKind::Audio { peaks, .. } if peaks.0.iter().any(|p| *p > 0.0) && *peaks != crate::demo::synth_peaks(crate::demo::LIBRARY[2].flavor, crate::demo::LIBRARY[2].beats, 2 * 7 + 3)));
    let count = r.q("clip.list", json!({"limit": 500}))["count"].as_i64().unwrap_or(0);
    r.check(ok && count == clips_before + 1, format!("browser: dragging a sound onto a lane imports a real file as an audio clip in the engine (engine clip {:?})", e.map(|e| e["clip"].clone())));
    r.check(real_peaks, "browser: the new clip's overview is the file's real waveform from browser.peaks, not the synthesised one");
    let bass = r.rt.state.project.tracks.iter().find(|t| t.kind == TrackKind::Instrument).cloned().unwrap();
    r.act(|s| {
        s.run(Command::SelectTrack(bass.id));
        s.run(Command::Seek(16 * TPB));
        s.browse_sel = Some(14); // a MIDI item
    });
    r.real(60);
    r.key(Key::Enter);
    let added = r.rt.state.project.clips.iter().find(|c| c.track == bass.id && c.start == 16 * TPB).cloned();
    let n_ui = added.as_ref().map_or(0, |c| c.notes().len());
    let n_e = added.as_ref().map_or(0, |c| r.e_notes(c.id).len());
    r.check(added.is_some() && n_ui > 0 && n_e == n_ui, format!("browser: Enter adds the highlighted MIDI item as a clip with {n_e} notes in the engine (ui {n_ui})"));

    // ---- audio, by an offline render of what the UI built ------------------------------------
    let out = format!("{}/s1-render.wav", out_dir());
    let _ = std::fs::remove_file(&out);
    let t0 = Instant::now();
    let rr = r.link().raw("render.render", json!({"out": out, "format": "wav"}));
    let secs = t0.elapsed().as_secs_f64();
    let m = r.link().raw("meter.measure_file", json!({"path": out})).unwrap_or(Value::Null);
    let size = std::fs::metadata(&out).map(|m| m.len()).unwrap_or(0);
    std::fs::write(format!("{}/s1-render.json", out_dir()), serde_json::to_string_pretty(&json!({"render": rr.clone().unwrap_or(Value::Null), "render_error": rr.clone().err(), "measure": m, "wall_s": secs, "bytes": size})).unwrap()).ok();
    let lufs = m["integrated_lufs"].as_f64().or(m["lufs_integrated"].as_f64()).or(m["integrated"].as_f64());
    r.check(rr.is_ok() && size > 100_000, format!("audio: render.render wrote {size} bytes in {secs:.2} s; the engine's own BS.1770 meter reads {lufs:?} LUFS-I on the file (independent sample check: spikes/s1_run.py)"));

    r.finish("s1-report.json");
}

/// S2: command round trips, frame cost, telemetry rate and event lag while the engine plays a
/// large project (`spikes/s1_fixture.py --tracks N`) and the UI drags at 60 Hz.
#[test]
#[ignore = "needs a running engine: spikes/s2_run.py"]
fn s2_latency_under_load() {
    let t0 = Instant::now();
    let mut r = Rig::new();
    let connect_ms = t0.elapsed().as_secs_f64() * 1000.0;
    let p = r.rt.state.project.clone();
    let (lanes, clips, notes) = (p.lanes().count(), p.clips.len(), p.clips.iter().map(|c| c.notes().len()).sum::<usize>());
    let strips = p.tracks.len();
    eprintln!("project: {lanes} lanes, {clips} clips, {notes} notes, {strips} strips; connect+load {connect_ms:.0} ms");
    let frames: usize = std::env::var("ZENE_S2_FRAMES").ok().and_then(|v| v.parse().ok()).unwrap_or(600);

    // Free drags: with snap off every pointer move changes the model, so every frame sends.
    r.act(|s| s.run(Command::ToggleSnap));
    r.act(|s| s.run(Command::ShowMixer));
    if !(r.rt.state.show_dock && r.rt.state.dock_tab == DockTab::Mixer) {
        r.act(|s| s.run(Command::ShowMixer));
    }
    r.click("play");
    r.real(500);
    let playing = r.q("transport.get_state", json!({}))["playing"] == true;
    r.check(playing, "the engine is playing the project during the measurements");

    // 1. Fader drag.
    let t = p.lanes().next().cloned().unwrap();
    let from = r.at(&format!("fader-{}", t.id));
    let n0 = r.call_count();
    let fader_frames = r.paced_drag(from, frames, |i| (0.0, 30.0 * (i as f32 * 0.21).sin()));
    let fader = r.calls_since(n0, "mixer.set_volume");

    // 2. Clip drag across its neighbours on the first lane.
    let c = p.clips.iter().filter(|c| c.track == t.id).min_by_key(|c| c.start).cloned().unwrap();
    let from = r.at(&format!("clip-{}", c.id));
    let span = (2.0 * BAR as f64 * r.rt.state.px_per_tick()) as f32;
    let n0 = r.call_count();
    let clip_frames = r.paced_drag(from, frames, |i| (span * (0.5 - 0.5 * (i as f32 * 0.05).cos()) + i as f32 * 0.013, 0.0));
    let clip_move = r.calls_since(n0, "clip.move");
    let release_cmds = r.call_count() - n0 - clip_move.len();
    let after_clip_drag = { let pr = r.rt.state.project.clone(); r.link().reconcile(&pr) };

    // 3. Note drag in the piano roll.
    let mc = r.rt.state.project.clips.iter().filter(|c| c.is_midi() && c.notes().len() > 4).max_by_key(|c| c.notes().len()).cloned().unwrap();
    r.act(|s| s.run(Command::OpenPianoRoll(mc.id)));
    r.frames(3);
    let pr = r.rt.state.pr_rect;
    let note = r.rt.state.pr_notes().iter().filter_map(|n| r.rt.find(&format!("note-{}", n.id)).map(|at| (*n, at))).find(|(_, at)| at.0 > pr[0] + 20.0 && at.0 < pr[0] + pr[2] - 120.0 && at.1 > pr[1] + 20.0 && at.1 < pr[1] + pr[3] - 60.0);
    let (note_edit, note_frames) = match note {
        Some((_, at)) => {
            let n0 = r.call_count();
            let f = r.paced_drag((at.0 - 2.0, at.1), frames, |i| (40.0 * (i as f32 * 0.17).sin() + 41.0, 0.0));
            (r.calls_since(n0, "note.edit"), f)
        }
        None => (Vec::new(), Vec::new()),
    };
    let streamed = std::env::var("ZENE_STREAM_CLIPS").is_ok_and(|v| v == "1");
    r.check(fader.len() > frames / 2 && note_edit.len() > frames / 2 && (clip_move.len() > frames / 2) == streamed, format!("drags sent a command on most frames (fader {}, note {} of {frames}); clip moves sent during the drag: {} (streaming {})", fader.len(), note_edit.len(), clip_move.len(), streamed));

    // 4. Head-of-line blocking: what telemetry sees while the engine runs one long command.
    let gap = |r: &mut Rig, cmd: &str, args: Value| -> Value {
        let cycles0 = { let l = r.link().log.clone(); let n = l.lock().unwrap().cycles.len(); n };
        let t0 = Instant::now();
        let res = r.link().raw(cmd, args);
        let ms = t0.elapsed().as_secs_f64() * 1000.0;
        std::thread::sleep(Duration::from_millis(50));
        let l = r.link().log.clone();
        let l = l.lock().unwrap();
        let worst = l.cycles[cycles0.min(l.cycles.len())..].iter().cloned().fold(0.0f32, f32::max);
        json!({"command": cmd, "ok": res.is_ok(), "error": res.err(), "command_ms": ms, "longest_telemetry_cycle_ms": worst})
    };
    let dir = out_dir();
    let hol = vec![
        gap(&mut r, "project.save", json!({"path": format!("{dir}/s2-hol-save.mmpz")})),
        gap(&mut r, "render.render", json!({"out": format!("{dir}/s2-hol-render.wav"), "format": "wav", "start_ticks": 0, "end_ticks": 4 * 192})),
        gap(&mut r, "track.overview", json!({})),
        gap(&mut r, "control.commands_list", json!({})),
    ];

    // 5. A full reread of the project, as a UI must do after an event gap.
    let t0 = Instant::now();
    let pr2 = r.rt.state.project.clone();
    let final_mismatches = r.link().reconcile(&pr2);
    let reread_ms = t0.elapsed().as_secs_f64() * 1000.0;

    r.key(Key::Text(" ".into()));
    let tele = r.link().telemetry();
    let load = std::fs::read_to_string("/proc/loadavg").unwrap_or_default();
    let cpu = r.q("mixer.cpu_load", json!({}));
    let extra = json!({
        "project": {"lanes": lanes, "clips": clips, "notes": notes, "strips": strips},
        "connect_and_load_ms": connect_ms,
        "frames_per_drag": frames,
        "loadavg": load.trim(),
        "engine_load": cpu.get("engine").or(cpu.get("engine_percent")).or(cpu.get("total")).cloned().unwrap_or(cpu["engine_cpu_percent"].clone()),
        "drag_round_trip_ms": {"mixer.set_volume": stats(&fader), "clip.move": stats(&clip_move), "note.edit": stats(&note_edit)},
        "frame_wall_ms": {"fader_drag": stats(&fader_frames), "clip_drag": stats(&clip_frames), "note_drag": stats(&note_frames)},
        "clip_drag_release_commands": release_cmds,
        "clips_streamed_during_drag": std::env::var("ZENE_STREAM_CLIPS").is_ok_and(|v| v == "1"),
        "reconcile_after_clip_drag": after_clip_drag,
        "head_of_line": hol,
        "full_reread_ms": reread_ms,
        "telemetry_channels_per_cycle": strips,
        "telemetry_samples": tele.samples,
        "telemetry_last_cycle_ms": tele.cycle_ms,
    });
    let _ = final_mismatches;
    r.finish_with("s2-report.json", extra);
}
