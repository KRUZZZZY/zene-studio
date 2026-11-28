//! Zene Studio: an independent Rune UI prototype. Deterministic demo data, simulated playback
//! (no audio is produced anywhere), and one command layer behind every button, key and script.
//!
//! `zene-rune` opens the window. `zene-rune --snap out.png --size 1366x768` renders it headless;
//! `--scene NAME` picks a prepared state (`--list-scenes`). `--commands FILE` runs a command
//! script (one command a line, see `command::parse`) before the first frame.

mod art;
mod brand;
mod browser;
mod command;
mod demo;
mod dock;
mod engine;
mod icons;
mod inspector;
mod model;
mod state;
mod theme;
mod timeline;
mod ui;
#[cfg(unix)]
mod zene;

#[cfg(test)]
mod tests;
#[cfg(all(test, unix))]
mod engine_tests;

use std::time::Duration;

use rune::{AppSpec, Key, KeyEvent, Tx, Waker};

use command::{Command, action_for_key};
use model::Clip;
use state::*;

/// `--engine SOCKET` (or `ZENE_CONTROL_SOCKET`) binds the UI to a running Zene Studio engine
/// instead of the demo project and the simulation.
fn engine_app(args: &[String]) -> Option<App> {
    let path = args.iter().position(|a| a == "--engine").and_then(|i| args.get(i + 1)).cloned().or_else(|| std::env::var("ZENE_CONTROL_SOCKET").ok())?;
    #[cfg(unix)]
    match zene::connect(&path) {
        Ok((project, engine)) => {
            let mut app = App::with(project, Box::new(engine));
            app.zoom_fit();
            app.status = format!("Connected to the engine at {path}");
            return Some(app);
        }
        Err(e) => eprintln!("zene-rune: --engine {path}: {e}"),
    }
    #[cfg(not(unix))]
    eprintln!("zene-rune: --engine {path}: the spike link is unix-socket only");
    None
}

fn init() -> App {
    let args = rune::extra_args();
    let mut app = engine_app(&args).unwrap_or_else(App::new);
    if let Some(path) = args.iter().position(|a| a == "--commands").and_then(|i| args.get(i + 1)) {
        match std::fs::read_to_string(path).map_err(|e| e.to_string()).and_then(|text| app.run_script(&text)) {
            Ok(n) => app.status = format!("Ran {n} commands from {path}"),
            Err(e) => {
                eprintln!("zene-rune: --commands {path}: {e}");
                app.status = format!("Command script failed: {e}");
            }
        }
    }
    app.sync_live();
    app
}

fn setup(s: &mut App, waker: Waker) {
    s.art.load(waker);
}

/// Before every frame: advance the engine and publish what it reports to the live values.
/// Returns true (re-run the view) only when something the view itself reads has changed.
pub fn poll(s: &mut App, now: f64, tx: &mut Tx) -> bool {
    s.engine.gesture(s.gesture.is_some());
    s.engine.advance(now, &s.project);
    let playing = s.playing();
    let mut changed = playing != s.was_playing;
    for (id, peaks) in s.engine.take_model_patch() {
        if let Some(Clip { kind: model::ClipKind::Audio { peaks: p, .. }, .. }) = s.project.clip_mut(id) {
            *p = peaks;
            changed = true;
        }
    }
    s.was_playing = playing;
    if playing {
        // Page the timeline when the playhead leaves it.
        let pos = s.engine.snapshot().position;
        let x = s.tick_to_x(pos);
        if x > s.lanes_width() - 12.0 || x < 0.0 {
            let before = s.scroll;
            s.set_scroll(pos - 24.0 / s.px_per_tick());
            changed |= s.scroll != before;
        }
    }
    s.sync_live();
    // S5 proposal: the readouts are live values the accessibility tree does not follow, so speak
    // what changed: the tempo, and where the playhead is when it stops or is moved while stopped.
    let pos = s.engine.snapshot().position;
    let tempo = s.project.tempo as f64;
    A11Y_LAST.with(|last| {
        let (was_playing, was_pos, was_tempo) = last.get();
        if was_tempo.is_some_and(|t| t != tempo) {
            tx.announce(format!("Tempo {tempo:.1} bpm"), rune::LiveRegion::Polite);
        } else if !playing && (was_playing || was_pos.is_some_and(|p| p != pos)) {
            tx.announce(format!("Position {}, {}", s.live.position.get(), s.live.clock.get()), rune::LiveRegion::Polite);
            changed = true;
        }
        last.set((playing, Some(pos), Some(tempo)));
    });
    if playing || !s.engine.snapshot().meters_idle() {
        tx.wake_after(Duration::from_millis(8));
    }
    if changed {
        tx.instant();
    }
    changed
}

thread_local! {
    static A11Y_LAST: std::cell::Cell<(bool, Option<f64>, Option<f64>)> = const { std::cell::Cell::new((false, None, None)) };
}

pub fn on_key(s: &mut App, ev: &KeyEvent, tx: &mut Tx) -> bool {
    if s.palette.is_some() {
        return ui::palette_key(s, ev, tx);
    }
    if ev.key == Key::Escape && s.menu.is_some() {
        s.run(Command::CloseOverlays);
        return true;
    }
    // Arrow keys in the browser list and on selected notes.
    if s.notes_active() {
        let dp = match ev.key {
            Key::Up => 1,
            Key::Down => -1,
            _ => 0,
        };
        if dp != 0 {
            s.begin_note_gesture("Transpose notes");
            s.drag_notes(0, if ev.mods.shift { dp * 12 } else { dp }, 1);
            s.end_gesture();
            return true;
        }
    } else if let Some(cur) = s.browse_sel {
        let found = browser::visible_items(s);
        let i = found.iter().position(|i| *i == cur);
        let next = match (&ev.key, i) {
            (Key::Down, Some(i)) => found.get(i + 1).copied(),
            (Key::Up, Some(i)) if i > 0 => found.get(i - 1).copied(),
            _ => None,
        };
        if let Some(n) = next {
            s.browse_sel = Some(n);
            tx.scroll_into_view(format!("item-{n}"));
            return true;
        }
    }
    if ev.key == Key::Escape && s.browse_sel.is_some() && s.sel == Selection::None && s.sel_notes.is_empty() {
        s.browse_sel = None;
        return true;
    }
    if let Some(a) = action_for_key(ev) {
        s.menu = None;
        s.run(a.command.clone());
        return true;
    }
    false
}

fn main() {
    rune::run(AppSpec::<App> {
        min_size: Some((1100.0, 640.0)),
        setup: Some(setup),
        poll: Some(poll),
        on_key: Some(on_key),
        scenes: SCENES,
        ..AppSpec::new("Zene Studio (Rune prototype, simulated playback)", (1366.0, 768.0), init, ui::view)
    });
}

/// Prepared states for snapshots and checks. Each goes through the command layer.
const SCENES: &[(&str, fn(&mut App))] = &[
    ("clip", |s| {
        let id = s.project.clips.iter().find(|c| c.name == "Verse 1 (take 3)").map(|c| c.id).unwrap_or(0);
        s.run(Command::SelectClip(id, false));
        s.browse_sel = Some(2);
    }),
    ("piano", |s| {
        let id = s.project.clips.iter().find(|c| c.is_midi() && c.name == "Keys verse").map(|c| c.id).unwrap_or(0);
        s.run(Command::SelectClip(id, false));
        s.run(Command::OpenPianoRoll(id));
        let first = s.pr_notes().iter().min_by_key(|n| n.start).map(|n| n.id);
        s.sel_notes = first.into_iter().collect();
    }),
    ("track", |s| {
        s.run(Command::SelectTrack(demo::T_KEYS));
        s.run(Command::SetSolo(demo::T_KEYS, true));
        s.run(Command::SetMute(demo::T_FX, true));
    }),
    ("playing", |s| {
        s.run(Command::Seek(13 * model::BAR));
        s.run(Command::Play);
    }),
    ("menu", |s| s.run(Command::OpenMenu(MenuId::Edit))),
    ("palette", |s| {
        s.run(Command::OpenPalette);
        if let Some(p) = &mut s.palette {
            p.query = rune::TextEdit::new("mute");
            p.query.focused = true;
        }
    }),
    ("search", |s| {
        s.search = rune::TextEdit::new("bass");
        s.browse_sel = Some(13);
    }),
    ("light", |s| {
        s.theme = Some(false);
        s.run(Command::SelectClip(105, false));
    }),
    ("compact", |s| {
        s.run(Command::ToggleBrowser);
        s.run(Command::ToggleInspector);
        s.run(Command::SetDensity(Density::Compact));
    }),
];
