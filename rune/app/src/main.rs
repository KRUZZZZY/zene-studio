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
#[cfg(test)]
mod link_tests;

use std::time::Duration;

use rune::{AppSpec, Key, KeyEvent, Tx, Waker};

use command::{Command, action_for_key};
use model::Clip;
use state::*;

fn arg(args: &[String], name: &str) -> Option<String> {
    args.iter().position(|a| a == name).and_then(|i| args.get(i + 1)).cloned()
}

/// Bind the UI to a real engine instead of the demo project and the simulation:
///
/// - `--engine SOCKET` (or `ZENE_CONTROL_SOCKET`): attach to a running engine; closing the window
///   never stops it.
/// - `--engine-bin PATH` (or `ZENE_ENGINE_BIN`): start `PATH --engine` and supervise it, with
///   `--project FILE` to open, `--engine-config XML` for its settings, `--engine-sandbox` for a
///   silent throwaway world (its own HOME and a Dummy audio device: no sound, the user's settings
///   untouched) and `--engine-log-dir DIR` for the protocol log and the engine's output.
///
/// Whatever happens, the window opens; the engine chip and the status line say what happened.
#[cfg(unix)]
fn engine_app(args: &[String]) -> Option<App> {
    use engine::Engine;
    use zene_link::{EngineSpec, SessionOptions};
    let socket = arg(args, "--engine");
    let bin = arg(args, "--engine-bin");
    let engine = if let Some(path) = socket.clone().or_else(|| bin.is_none().then(|| std::env::var("ZENE_CONTROL_SOCKET").ok()).flatten()) {
        zene::ZeneEngine::start(zene_link::Session::attach(&path, SessionOptions::default()))
    } else {
        let bin = bin.or_else(|| std::env::var("ZENE_ENGINE_BIN").ok())?;
        let mut spec = EngineSpec { project: arg(args, "--project").map(Into::into), config: arg(args, "--engine-config").map(Into::into), ..EngineSpec::new(bin) };
        let log_dir = arg(args, "--engine-log-dir").map(std::path::PathBuf::from);
        if args.iter().any(|a| a == "--engine-sandbox") {
            let root = log_dir.clone().unwrap_or_else(std::env::temp_dir).join(format!("zene-rune-sandbox-{}", std::process::id()));
            match zene_link::runtime::Sandbox::create(&root) {
                Ok(sb) => {
                    spec.config = Some(sb.config.clone());
                    spec.env = sb.env.clone();
                    spec.cwd = Some(sb.root.clone());
                }
                Err(e) => eprintln!("zene-rune: --engine-sandbox {}: {e}", root.display()),
            }
        }
        zene::ZeneEngine::start(zene_link::Session::supervise(spec, SessionOptions { log_dir, ..Default::default() }))
    };
    let mut engine = engine;
    let model = engine.take_reload().unwrap_or_else(zene::empty_project);
    let sentence = engine.session.status().sentence();
    let notices = engine.take_notices();
    let mut app = App::with(model, Box::new(engine));
    app.zoom_fit();
    app.link_seen = app.engine.link_status().cloned();
    app.status = notices.last().cloned().unwrap_or(sentence);
    Some(app)
}

#[cfg(not(unix))]
fn engine_app(args: &[String]) -> Option<App> {
    if let Some(path) = arg(args, "--engine").or_else(|| arg(args, "--engine-bin")) {
        eprintln!("zene-rune: {path}: the engine link on Windows needs the named-pipe transport (Q050-524); showing the demo");
    }
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
    let mut changed = s.follow_link();
    if s.quit_requested {
        tx.quit();
    }
    let playing = s.playing();
    changed |= playing != s.was_playing;
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

thread_local! {
    /// The running app's handle, for reopening the window when closing it needs a question first.
    static HANDLE: std::cell::RefCell<Option<rune::AppHandle<App>>> = const { std::cell::RefCell::new(None) };
}

/// The window's close button (or Cmd-Q, or the window manager): close at once when nothing can be
/// lost; otherwise bring the window back with the save / discard / cancel question. Rune cannot
/// veto a close, so the window keeps running hidden (`keep_running`) and is shown again.
fn on_close(s: &mut App, tx: &mut Tx) {
    if !s.quit_requested && s.exit_prompt.is_none() {
        s.run(Command::Quit);
    }
    if s.quit_requested || rune::quitting() {
        tx.quit();
    } else {
        HANDLE.with(|h| {
            if let Some(h) = h.borrow().as_ref() {
                h.open();
            }
        });
    }
}

fn main() {
    let app = rune::App::new(AppSpec::<App> {
        min_size: Some((1100.0, 640.0)),
        setup: Some(setup),
        poll: Some(poll),
        on_key: Some(on_key),
        scenes: SCENES,
        ..AppSpec::new("Zene Studio (Rune prototype, simulated playback)", (1366.0, 768.0), init, ui::view)
    })
    .keep_running()
    .on_close(on_close);
    HANDLE.with(|h| *h.borrow_mut() = Some(app.handle()));
    app.run();
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
