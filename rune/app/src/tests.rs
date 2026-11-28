//! Interaction checks against Rune's headless runtime: the same view, handlers and command
//! layer as the window, driven by scripted pointer, wheel and key input on a virtual clock.

use rune::{Env, Key, KeyEvent, Mods, Runtime, WheelEvent};

use crate::browser::visible_items;
use crate::command::{Command, parse};
use crate::demo::{self, T_BASS, T_DRUMS, T_KEYS, T_VOX};
use crate::engine::{Engine, SimEngine};
use crate::model::*;
use crate::state::*;
use crate::theme::{contrast, theme};
use crate::ui;

fn settle(rt: &mut Runtime<App>) {
    for _ in 0..3 {
        rt.frame(1.0 / 60.0);
    }
}

fn app_at(size: (f32, f32)) -> Runtime<App> {
    let mut rt = Runtime::new(App::new(), ui::view, size, Env { system_dark: true, scale: 1.0 });
    rt.on_key = Some(crate::on_key);
    for _ in 0..4 {
        rt.frame(1.0 / 60.0);
    }
    rt
}

fn app() -> Runtime<App> {
    app_at((1366.0, 768.0))
}

fn at(rt: &Runtime<App>, key: &str) -> (f32, f32) {
    rt.find(key).unwrap_or_else(|| panic!("no element with key {key}"))
}

fn click(rt: &mut Runtime<App>, key: &str) {
    let (x, y) = at(rt, key);
    rt.pointer_move(x, y);
    rt.pointer_down(x, y);
    rt.pointer_up(x, y);
    settle(rt);
}

fn drag(rt: &mut Runtime<App>, from: (f32, f32), by: (f32, f32)) {
    rt.pointer_move(from.0, from.1);
    rt.pointer_down(from.0, from.1);
    for i in 1..=6 {
        let k = i as f32 / 6.0;
        rt.pointer_move(from.0 + by.0 * k, from.1 + by.1 * k);
        rt.frame(1.0 / 60.0);
    }
    rt.pointer_up(from.0 + by.0, from.1 + by.1);
    settle(rt);
}

/// Change state the way a handler would, so the view re-runs.
fn act(rt: &mut Runtime<App>, f: impl FnOnce(&mut App)) {
    rt.update(|s, _tx| f(s));
    settle(rt);
}

fn key(rt: &mut Runtime<App>, k: Key) {
    rt.key(KeyEvent { key: k, mods: Mods::default() });
    settle(rt);
}

/// The platform's primary shortcut, as the OS reports it (Rune `Mods`): Ctrl+key off macOS, ⌘+key on macOS
/// (`cmd` and `logo`, no `ctrl`; with `ctrl` set too it is a different chord there, which failed the first
/// hosted macOS run on 2026-11-25).
fn ctrl(rt: &mut Runtime<App>, c: char) {
    let mac = cfg!(target_os = "macos");
    rt.key(KeyEvent { key: Key::Command(c), mods: Mods { cmd: true, ctrl: !mac, logo: mac, ..Mods::default() } });
    settle(rt);
}

fn type_text(rt: &mut Runtime<App>, s: &str) {
    for c in s.chars() {
        rt.key(KeyEvent { key: Key::Text(c.to_string()), mods: Mods::default() });
    }
    settle(rt);
}

fn clip_named(p: &Project, name: &str) -> Clip {
    p.clips.iter().find(|c| c.name == name).cloned().unwrap_or_else(|| panic!("no clip {name}"))
}

// ---- transport ---------------------------------------------------------------------------

#[test]
fn space_and_the_play_button_run_the_same_command() {
    let mut rt = app();
    assert!(!rt.state.playing());
    key(&mut rt, Key::Text(" ".into()));
    assert!(rt.state.playing(), "Space starts playback");
    click(&mut rt, "play");
    assert!(!rt.state.playing(), "the Play/Stop button stops it");
    click(&mut rt, "tempo-up");
    assert_eq!(rt.state.project.tempo, 109.0);
    ctrl(&mut rt, 'z');
    assert_eq!(rt.state.project.tempo, 108.0, "tempo is undoable");
}

#[test]
fn simulated_playback_advances_the_position_and_stops_at_the_end() {
    let mut a = App::new();
    a.run(Command::Play);
    let p = a.project.clone();
    a.engine.advance(0.0, &p);
    a.engine.advance(1.0, &p);
    // The engine clamps one step to 0.25 s: at 108 bpm that is 0.45 beats.
    assert_eq!(a.position(), (0.25 * 108.0 / 60.0 * TPB as f64) as i64);
    a.run(Command::Seek(p.length - 10));
    a.engine.advance(1.2, &p);
    a.engine.advance(1.4, &p);
    assert!(!a.playing(), "playback stops at the end of the song");
}

#[test]
fn snap_state_is_visible_text_and_toggles() {
    let mut rt = app();
    assert!(rt.state.snap_on);
    click(&mut rt, "snap");
    assert!(!rt.state.snap_on);
    key(&mut rt, Key::Text("g".into()));
    assert!(rt.state.snap_on, "G toggles the same state");
}

// ---- arrangement -------------------------------------------------------------------------

#[test]
fn dragging_a_clip_moves_it_on_the_grid_as_one_undo_step() {
    let mut rt = app();
    let before = rt.state.project.clone();
    let c = clip_named(&before, "Keys intro");
    let from = at(&rt, &format!("clip-{}", c.id));
    // 8 beats to the right at the default zoom, plus a little slop the grid absorbs.
    let dx = rt.state.ppb * 8.0 + 3.0;
    drag(&mut rt, from, (dx, 0.0));
    let after = rt.state.project.clip(c.id).unwrap().clone();
    assert_eq!(after.start, c.start + 8 * TPB);
    assert_eq!(after.track, c.track);
    assert!(rt.state.sel.has_clip(c.id), "the dragged clip is selected");
    assert_eq!(rt.state.undo.len(), 1, "one drag is one undo step");
    ctrl(&mut rt, 'z');
    assert!(rt.state.project == before, "undo restores the whole project, trimmed neighbours included");
    rt.key(KeyEvent { key: Key::Command('z'), mods: Mods { cmd: true, ctrl: true, shift: true, ..Mods::default() } });
    settle(&mut rt);
    assert_eq!(rt.state.project.clip(c.id).unwrap().start, c.start + 8 * TPB, "redo");
}

#[test]
fn a_clip_only_changes_lane_onto_a_track_of_its_kind() {
    let mut rt = app();
    let c = clip_named(&rt.state.project, "Keys intro");
    let from = at(&rt, &format!("clip-{}", c.id));
    let th = rt.state.track_height();
    drag(&mut rt, from, (0.0, th)); // Glass Keys -> Pad: both instruments
    assert_eq!(rt.state.project.clip(c.id).unwrap().track, demo::T_PAD);
    ctrl(&mut rt, 'z');
    let from = at(&rt, &format!("clip-{}", c.id));
    drag(&mut rt, from, (0.0, th * 2.0)); // -> Lead Vox: audio, refused
    assert_eq!(rt.state.project.clip(c.id).unwrap().track, T_KEYS);
}

#[test]
fn the_right_edge_resizes_a_clip() {
    let mut rt = app();
    let c = clip_named(&rt.state.project, "Keys intro");
    let (x, y) = at(&rt, &format!("clip-end-{}", c.id));
    let dx = -rt.state.ppb * 4.0;
    drag(&mut rt, (x, y), (dx, 0.0));
    assert_eq!(rt.state.project.clip(c.id).unwrap().len, c.len - BAR);
    assert_eq!(rt.state.undo.last().map(|u| u.1), Some("Resize clip"));
}

#[test]
fn wheel_scrolls_tracks_shift_scrolls_time_ctrl_zooms_at_the_pointer() {
    let mut rt = app();
    let (x, y) = at(&rt, "lanes");
    let (ppb, scroll) = (rt.state.ppb, rt.state.scroll);
    rt.wheel_event(WheelEvent::new(0.0, -40.0).at(x, y));
    settle(&mut rt);
    assert!(rt.state.scroll_y > 0.0, "the plain wheel scrolls tracks");
    assert_eq!((rt.state.ppb, rt.state.scroll), (ppb, scroll), "and never zooms or moves time");

    rt.wheel_event(WheelEvent::new(0.0, -60.0).at(x, y).mods(Mods { shift: true, ..Mods::default() }));
    settle(&mut rt);
    assert!(rt.state.scroll > scroll, "Shift+wheel scrolls time");
    assert_eq!(rt.state.ppb, ppb);

    let local = x - rt.state.lanes_rect[0];
    let under = rt.state.x_to_tick(local);
    rt.wheel_event(WheelEvent::new(0.0, 80.0).at(x, y).mods(Mods { cmd: true, ctrl: true, ..Mods::default() }));
    settle(&mut rt);
    assert!(rt.state.ppb > ppb, "Ctrl+wheel up zooms in");
    assert!((rt.state.x_to_tick(local) - under).abs() < 2.0, "the tick under the pointer stays put");
}

#[test]
fn the_ruler_moves_the_playhead() {
    let mut rt = app();
    let r = rt.state.lanes_rect;
    let (_, y) = at(&rt, "ruler");
    let x = r[0] + rt.state.ppb * 16.0 + 2.0;
    rt.pointer_move(x, y);
    rt.pointer_down(x, y);
    rt.pointer_up(x, y);
    settle(&mut rt);
    assert_eq!(rt.state.position(), 16 * TPB);
}

// ---- browser -----------------------------------------------------------------------------

#[test]
fn browser_search_and_filters_narrow_the_list() {
    let mut rt = app();
    assert_eq!(visible_items(&rt.state).len(), demo::LIBRARY.len());
    click(&mut rt, "browser-search");
    type_text(&mut rt, "bass");
    let found = visible_items(&rt.state);
    assert!(!found.is_empty() && found.len() < demo::LIBRARY.len());
    assert!(found.iter().all(|i| demo::item_matches(&demo::LIBRARY[*i], "bass")));
    assert!(rt.find("item-13").is_some() && rt.find("item-0").is_none(), "the list shows only matches");
    click(&mut rt, "chip-MIDI");
    assert!(visible_items(&rt.state).iter().all(|i| demo::LIBRARY[*i].kind == demo::ItemKind::Midi));
    click(&mut rt, "pack-3");
    assert!(visible_items(&rt.state).is_empty(), "no MIDI bass in Low Tide: the empty state shows");
}

#[test]
fn dragging_a_sound_onto_a_lane_adds_a_clip_there() {
    let mut rt = app();
    let before = rt.state.project.clips.len();
    let r = rt.state.lanes_rect;
    let th = rt.state.track_height();
    let from = at(&rt, "item-2"); // Platform Hats, an audio loop
    // Lead Vox is the sixth lane; drop at beat 4 (bar 2), which is empty there.
    let to = (r[0] + rt.state.ppb * 4.0 + 2.0, r[1] + th * 5.5);
    drag(&mut rt, from, (to.0 - from.0, to.1 - from.1));
    assert_eq!(rt.state.project.clips.len(), before + 1);
    let c = clip_named(&rt.state.project, "Platform Hats");
    let added = rt.state.project.clips.iter().find(|c| c.track == T_VOX && c.name == "Platform Hats").expect("clip on Lead Vox");
    assert_eq!(added.start, 4 * TPB);
    assert_ne!(added.id, c.id);
    assert!(rt.state.item_drag.is_none());
    ctrl(&mut rt, 'z');
    assert_eq!(rt.state.project.clips.len(), before, "the drop is undoable");

    // A MIDI item is refused by an audio lane.
    let from = at(&rt, "item-7");
    drag(&mut rt, from, (to.0 - from.0, to.1 - from.1));
    assert_eq!(rt.state.project.clips.len(), before);
}

#[test]
fn enter_adds_the_highlighted_sound_without_dragging() {
    let mut rt = app();
    act(&mut rt, |s| {
        s.run(Command::SelectTrack(T_BASS));
        s.browse_sel = Some(14); // Octave Walk, MIDI
    });
    let before = rt.state.project.clips.len();
    key(&mut rt, Key::Enter);
    assert_eq!(rt.state.project.clips.len(), before + 1);
    assert!(rt.state.project.clips.iter().any(|c| c.name == "Octave Walk" && c.track == T_BASS && c.start == 0));
}

// ---- inspector ---------------------------------------------------------------------------

#[test]
fn the_inspector_follows_the_selection_and_edits_it() {
    let mut rt = app();
    let c = clip_named(&rt.state.project, "Verse 1 (take 3)");
    click(&mut rt, &format!("clip-{}", c.id));
    assert!(rt.state.sel.has_clip(c.id));
    assert_eq!(rt.state.name_edit.text, "Verse 1 (take 3)", "the name field shows the selected clip");
    click(&mut rt, "step-start-up");
    assert_eq!(rt.state.project.clip(c.id).unwrap().start, c.start + TPB, "Start + moves one grid step");
    click(&mut rt, "step-gain-down");
    assert_eq!(rt.state.project.clip(c.id).unwrap().gain_db, -1.0);
    click(&mut rt, "clip-mute");
    assert!(rt.state.project.clip(c.id).unwrap().muted);
    assert_eq!(rt.state.undo.len(), 3);

    // Rename through the field.
    click(&mut rt, "inspector-name");
    ctrl(&mut rt, 'a');
    type_text(&mut rt, "Lead take");
    key(&mut rt, Key::Enter);
    assert_eq!(rt.state.project.clip(c.id).unwrap().name, "Lead take");

    click(&mut rt, &format!("track-{T_DRUMS}"));
    assert_eq!(rt.state.sel, Selection::Track(T_DRUMS));
    assert_eq!(rt.state.name_edit.text, "Drums");
    click(&mut rt, "step-volume-up");
    assert_eq!(rt.state.project.track(T_DRUMS).unwrap().volume_db, -3.0);
}

// ---- mixer -------------------------------------------------------------------------------

#[test]
fn faders_mute_and_solo_work_and_are_undoable() {
    let mut rt = app();
    let from = at(&rt, &format!("fader-{T_DRUMS}"));
    drag(&mut rt, from, (0.0, -24.0));
    let v = rt.state.project.track(T_DRUMS).unwrap().volume_db;
    assert!(v > -4.0, "dragging a fader up raises the volume (now {v})");
    assert_eq!(rt.state.undo.len(), 1, "a fader drag is one step");
    click(&mut rt, &format!("strip-mute-{T_DRUMS}"));
    assert!(rt.state.project.track(T_DRUMS).unwrap().mute);
    click(&mut rt, &format!("strip-solo-{T_KEYS}"));
    assert!(rt.state.project.track(T_KEYS).unwrap().solo);
    // The header buttons are the same commands.
    click(&mut rt, &format!("mute-{T_DRUMS}"));
    assert!(!rt.state.project.track(T_DRUMS).unwrap().mute);
    ctrl(&mut rt, 'z');
    assert!(rt.state.project.track(T_DRUMS).unwrap().mute);
}

#[test]
fn simulated_meters_follow_clips_faders_mute_and_solo() {
    let mut p = demo::project();
    let mut e = SimEngine::new(p.tempo);
    e.send(crate::engine::EngineCommand::Seek(14 * BAR)); // the chorus: everything plays
    e.send(crate::engine::EngineCommand::Play);
    let level = |e: &mut SimEngine, p: &Project, t: f64| {
        for i in 0..400 {
            e.advance(t + i as f64 * 0.01, p);
        }
        (e.snapshot().meter(T_DRUMS).0, e.snapshot().meter(T_KEYS).0, e.snapshot().meter(demo::T_DRUM_BUS).0, e.snapshot().meter(demo::T_MASTER).0)
    };
    let (drums, keys, bus, master) = level(&mut e, &p, 0.0);
    assert!(drums > 0.2 && keys > 0.1 && bus > 0.1 && master > 0.2, "{drums} {keys} {bus} {master}");
    p.track_mut(T_DRUMS).unwrap().mute = true;
    let (drums, _, _, _) = level(&mut e, &p, 5.0);
    assert!(drums < 0.02, "a muted strip's meter falls to nothing ({drums})");
    p.track_mut(T_DRUMS).unwrap().mute = false;
    p.track_mut(T_KEYS).unwrap().solo = true;
    let (drums, keys, _, master) = level(&mut e, &p, 10.0);
    assert!(drums < 0.02 && keys > 0.1 && master > 0.05, "solo silences the others");
    assert!(e.describe().contains("no audio"));
}

// ---- piano roll --------------------------------------------------------------------------

fn open_keys(rt: &mut Runtime<App>) -> ClipId {
    let id = clip_named(&rt.state.project, "Keys verse").id;
    act(rt, |s| s.run(Command::OpenPianoRoll(id)));
    settle(rt);
    id
}

#[test]
fn piano_roll_adds_moves_resizes_and_deletes_notes() {
    let mut rt = app();
    let id = open_keys(&mut rt);
    let before = rt.state.pr_notes().len();
    let r = rt.state.pr_rect;
    // The top visible row, a beat and a bit in: empty in the demo clip.
    let (x, y) = (r[0] + rt.state.pr_ppb * 1.0 + 6.0, r[1] + 5.0);
    let pitch = (PITCH_HI as i32 - ((5.0 + rt.state.pr_scroll_y) / NOTE_H).floor() as i32) as u8;
    rt.pointer_move(x, y);
    rt.pointer_down(x, y);
    rt.pointer_up(x, y);
    settle(&mut rt);
    assert_eq!(rt.state.pr_notes().len(), before + 1, "a click on an empty cell adds a note");
    let n = *rt.state.pr_notes().last().unwrap();
    assert_eq!((n.start, n.pitch, n.len), (TPB, pitch, TPB / 2), "on the grid, at the row clicked, at the last-used length");
    assert_eq!(rt.state.sel_notes, vec![n.id]);
    assert_eq!(rt.state.undo.last().map(|u| u.1), Some("Add note"));

    // Drag it one beat later and one row down.
    let from = at(&rt, &format!("note-{}", n.id));
    let dx = rt.state.pr_ppb + 2.0;
    drag(&mut rt, (from.0 - 4.0, from.1), (dx, NOTE_H));
    let m = *rt.state.pr_notes().iter().find(|x| x.id == n.id).unwrap();
    assert_eq!((m.start, m.pitch), (2 * TPB, pitch - 1));

    // Resize from the right edge.
    let w = (m.len as f64 * rt.state.pr_px_per_tick()) as f32;
    let left = r[0] + rt.state.pr_tick_to_x(m.start as f64);
    let ny = at(&rt, &format!("note-{}", n.id)).1;
    let dx = rt.state.pr_ppb * 0.5;
    drag(&mut rt, (left + w - 3.0, ny), (dx, 0.0));
    let m2 = *rt.state.pr_notes().iter().find(|x| x.id == n.id).unwrap();
    assert_eq!(m2.len, TPB, "the right edge resizes on the grid");
    assert_eq!(rt.state.last_note_len, TPB, "and the next note takes that length");

    // Velocity lane: drag under the note.
    let (_, vy) = at(&rt, "pr-velocity");
    drag(&mut rt, (left + 2.0, vy), (0.0, -14.0));
    let m3 = *rt.state.pr_notes().iter().find(|x| x.id == n.id).unwrap();
    assert!(m3.vel > m2.vel, "dragging up in the velocity lane raises velocity ({} -> {})", m2.vel, m3.vel);

    // Right-click deletes.
    let (nx, ny) = at(&rt, &format!("note-{}", n.id));
    rt.secondary_down(nx - 3.0, ny);
    rt.secondary_up(nx - 3.0, ny);
    settle(&mut rt);
    assert_eq!(rt.state.pr_notes().len(), before);
    // Add, move, resize, velocity, delete: five steps, all undoable back to the start.
    for _ in 0..5 {
        ctrl(&mut rt, 'z');
    }
    assert!(rt.state.project == demo::project());
    let _ = id;
}

#[test]
fn delete_key_removes_selected_notes_not_the_clip() {
    let mut rt = app();
    let id = open_keys(&mut rt);
    act(&mut rt, |s| s.run(Command::SelectClip(id, false)));
    let first = rt.state.pr_notes()[0].id;
    click(&mut rt, &format!("note-{first}"));
    assert_eq!(rt.state.sel_notes, vec![first]);
    let n = rt.state.pr_notes().len();
    key(&mut rt, Key::Delete);
    assert_eq!(rt.state.pr_notes().len(), n - 1);
    assert!(rt.state.project.clip(id).is_some(), "the clip stays");
}

#[test]
fn double_clicking_a_midi_clip_opens_it_in_the_piano_roll() {
    let mut rt = app();
    let c = clip_named(&rt.state.project, "Bass verse");
    let (x, y) = at(&rt, &format!("clip-{}", c.id));
    rt.pointer_move(x, y);
    for _ in 0..2 {
        rt.pointer_down(x, y);
        rt.pointer_up(x, y);
        rt.frame(0.05);
    }
    settle(&mut rt);
    assert_eq!(rt.state.pr_clip, Some(c.id));
    assert_eq!(rt.state.dock_tab, DockTab::PianoRoll);
    assert!(rt.state.undo.is_empty(), "opening an editor is not an edit");
}

// ---- commands ----------------------------------------------------------------------------

#[test]
fn buttons_keys_and_scripts_reach_the_same_state() {
    let mut by_button = app();
    click(&mut by_button, &format!("mute-{T_VOX}"));
    click(&mut by_button, "tempo-up");

    let mut by_key = app();
    act(&mut by_key, |s| s.run(Command::SelectTrack(T_VOX)));
    key(&mut by_key, Key::Text("m".into()));
    act(&mut by_key, |s| s.run(Command::NudgeTempo(1.0)));

    let mut by_script = App::new();
    assert_eq!(by_script.run_script("# comment\ntrack.set_mute 6 1\n\ntransport.set_tempo 109\n"), Ok(2));

    assert!(by_button.state.project == by_key.state.project);
    assert!(by_button.state.project == by_script.project);
    assert_eq!(by_script.undo.len(), 2);
}

#[test]
fn script_lines_parse_or_say_why_not() {
    assert_eq!(parse("clip.move 104 7680 4"), Ok(Command::MoveClip { id: 104, start: 7680, track: 4 }));
    assert_eq!(parse("control.undo"), Ok(Command::Undo));
    assert!(parse("clip.move 104").unwrap_err().contains("missing argument"));
    assert!(parse("nope.nothing").unwrap_err().contains("unknown command"));
    let mut a = App::new();
    assert!(a.run_script("transport.play\nbogus").unwrap_err().starts_with("line 2"));
}

#[test]
fn every_action_has_a_menu_home_and_no_two_share_a_key() {
    use crate::command::ACTIONS;
    for (i, a) in ACTIONS.iter().enumerate() {
        assert!(!a.title.is_empty() && !a.help.is_empty(), "{}", a.id);
        for b in &ACTIONS[i + 1..] {
            assert_ne!(a.id, b.id);
            if let (Some(x), Some(y)) = (a.chord, b.chord) {
                assert_ne!(x, y, "{} and {} share {}", a.id, b.id, x.label());
            }
        }
    }
}

#[test]
fn the_palette_finds_commands_and_runs_them() {
    let mut rt = app();
    ctrl(&mut rt, 'k');
    assert!(rt.state.palette.is_some());
    type_text(&mut rt, "zoom fit");
    let found = ui::palette_matches(&rt.state.palette.as_ref().unwrap().query.text);
    assert_eq!(found.first().map(|a| a.id), Some("view.zoom_fit"));
    let ppb = rt.state.ppb;
    key(&mut rt, Key::Enter);
    assert!(rt.state.palette.is_none());
    assert_ne!(rt.state.ppb, ppb, "Enter ran Zoom to Fit");
    // Typing in the palette never reached the single-key shortcuts.
    assert!(rt.state.show_browser && rt.state.show_inspector);
}

#[test]
fn menus_open_run_and_close() {
    let mut rt = app();
    click(&mut rt, "menu-view");
    assert_eq!(rt.state.menu, Some(MenuId::View));
    click(&mut rt, "mi-view.inspector");
    assert_eq!(rt.state.menu, None);
    assert!(!rt.state.show_inspector);
    click(&mut rt, "menu-edit");
    key(&mut rt, Key::Escape);
    assert_eq!(rt.state.menu, None);
}

#[test]
fn typing_in_a_field_does_not_fire_single_key_shortcuts() {
    let mut rt = app();
    click(&mut rt, "browser-search");
    type_text(&mut rt, "bmx ");
    assert_eq!(rt.state.search.text, "bmx ");
    assert!(rt.state.show_browser && !rt.state.playing());
}

// ---- layout and legibility ---------------------------------------------------------------

/// Every region is on screen and none overlaps its neighbour, at the laptop size and larger.
#[test]
fn regions_fit_without_overlap_at_laptop_and_desktop_sizes() {
    for size in [(1366.0, 768.0), (1920.0, 1080.0), (1100.0, 640.0)] {
        let mut rt = app_at(size);
        settle(&mut rt);
        let rect = |k: &str| rt.presented(k).unwrap_or_else(|| panic!("{k} missing at {size:?}")).0;
        let (b, a, i, d) = (rect("browser"), rect("arrangement"), rect("inspector"), rect("dock"));
        assert!(b[0] >= 0.0 && i[0] + i[2] <= size.0 + 0.5, "{size:?}");
        assert!(b[0] + b[2] <= a[0] + 0.5 && a[0] + a[2] <= i[0] + 0.5, "browser | arrangement | inspector at {size:?}");
        assert!(a[1] + a[3] <= d[1] + 0.5 && d[1] + d[3] <= size.1 - 20.0, "arrangement over dock over status at {size:?}");
        let lanes = rt.state.lanes_rect;
        assert!(lanes[2] >= 400.0 && lanes[3] >= 150.0, "the timeline keeps room at {size:?}: {lanes:?}");
        for k in ["play", "snap", "search-commands", "undo", "browser-search", "tab-mixer", "tab-piano-roll"] {
            let r = rect(k);
            assert!(r[0] >= 0.0 && r[0] + r[2] <= size.0 + 0.5 && r[1] + r[3] <= size.1 + 0.5, "{k} is clipped at {size:?}: {r:?}");
        }
    }
}

#[test]
fn theme_tokens_meet_wcag_contrast_in_both_themes() {
    for dark in [true, false] {
        let t = theme(dark);
        for (name, bg) in [("bg", t.bg), ("panel", t.panel), ("bar", t.bar), ("control", t.control)] {
            for (ink, c) in [("text", t.text), ("text_dim", t.text_dim), ("text_faint", t.text_faint)] {
                let r = contrast(c, bg);
                assert!(r >= 4.5, "{ink} on {name} is {r:.2}:1 (dark={dark})");
            }
        }
        assert!(contrast(t.accent, t.bg) >= 3.0 && contrast(t.select, t.bg) >= 3.0, "accent and selection vs lanes (dark={dark})");
        for (name, c) in [("accent", t.accent), ("mute", t.mute), ("solo", t.solo)] {
            let r = contrast(t.on_accent, c);
            assert!(r >= 4.5, "on_accent on {name} is {r:.2}:1 (dark={dark})");
        }
        for (i, c) in t.tracks.iter().enumerate() {
            assert!(contrast(*c, t.bg) >= 3.0, "track colour {i} vs lanes is {:.2}:1 (dark={dark})", contrast(*c, t.bg));
            assert!(contrast(t.on_accent_for(*c), *c) >= 4.5, "clip title on track colour {i} (dark={dark})");
        }
        assert!(contrast(t.meter, t.bg) >= 3.0);
    }
}

#[test]
fn the_demo_project_is_deterministic() {
    assert!(demo::project() == demo::project());
    let (a, b) = (demo::synth_peaks(demo::Flavor::Kit, 16, 11), demo::synth_peaks(demo::Flavor::Kit, 16, 11));
    assert_eq!(a.0.as_ref(), b.0.as_ref());
}
