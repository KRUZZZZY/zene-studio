//! The bottom dock: two labelled tabs, the mixer and the piano roll.

use rune::shape::LineCap;
use rune::{Cursor, Drag, El, MagnifyEvent, Phase, Shape, Tx, WheelEvent, canvas, col, el, icon, row, text};

use crate::command::Command;
use crate::icons;
use crate::model::*;
use crate::state::*;
use crate::theme::*;
use crate::ui::{button, hint, icon_button, label, letter_toggle, on_cmd};

pub const DOCK_H: f32 = 256.0;
const TABS_H: f32 = 30.0;
const STRIP_W: f32 = 76.0;
const THUMB_H: f32 = 12.0;

pub fn view(s: &App, t: &Theme) -> El {
    let tab = |name: &str, ic: &'static rune::Icon, which: DockTab, key: &str| {
        let on = s.dock_tab == which;
        let b = row().key(key).center().gap(6.0).h(TABS_H).px(12.0).role("tab").checked(on).focusable(true).cursor(Cursor::Pointer)
            .child(icon(ic, 14.0).color(if on { t.text } else { t.text_faint }))
            .child(label(name, if on { T_STRONG } else { T_BODY }, if on { t.text } else { t.text_dim }));
        // The open tab is bold and underlined; colour is not what marks it.
        let b = if on { b.border_sides([0.0, 0.0, 2.0, 0.0], t.accent).fill(t.panel) } else { b.hover(t.control_hover) };
        on_cmd(b, if which == DockTab::Mixer { Command::ShowMixer } else { Command::ShowPianoRoll })
    };
    let side = match s.dock_tab {
        DockTab::Mixer => label(s.engine.meters_note(), T_SMALL, t.text_faint),
        DockTab::PianoRoll => match s.pr_clip.and_then(|id| s.project.clip(id)) {
            Some(c) => label(format!("{} · {} notes · click adds, drag moves, right edge resizes, right-click deletes, Shift-drag selects", c.name, c.notes().len()), T_SMALL, t.text_dim),
            None => label("No clip open", T_SMALL, t.text_faint),
        },
    };
    let tabs = row().center_y().w_full().h(TABS_H).fill(t.bar).border_sides([1.0, 0.0, 1.0, 0.0], t.line)
        .child(tab("Mixer", &icons::SLIDERS_VERTICAL, DockTab::Mixer, "tab-mixer"))
        .child(tab("Piano Roll", &icons::PIANO, DockTab::PianoRoll, "tab-piano-roll"))
        .child(row().grow(1.0).min_w(0.0).px(12.0).child(side.truncate().grow(1.0).min_w(0.0)))
        .child(on_cmd(icon_button(t, &icons::CHEVRON_DOWN, "Hide the dock").key("dock-hide").h(22.0).fill(rune::Color::TRANSPARENT).mr(6.0), Command::ToggleDock));
    let body = match s.dock_tab {
        DockTab::Mixer => mixer(s, t),
        DockTab::PianoRoll => piano_roll(s, t),
    };
    col().key("dock").w_full().h(DOCK_H).fill(t.panel).child(tabs).child(body)
}

// ---- mixer -----------------------------------------------------------------------------

fn strip(s: &App, t: &Theme, tr: &Track) -> El {
    let id = tr.id;
    let p = db_to_fader(tr.volume_db);
    let travel = STRIP_METER_H - THUMB_H;
    let zero = (1.0 - db_to_fader(0.0)) * travel + THUMB_H / 2.0;
    let fader = el().key(format!("fader-{id}")).size(26.0, STRIP_METER_H).cursor(Cursor::ResizeRow)
        .role("slider").label(format!("{} volume", tr.name)).a11y_value(format!("{} dB", db_text(tr.volume_db))).range_value(tr.volume_db as f64, -60.0, 6.0).range_step(1.0).focusable(true)
        .child(el().absolute(11.0, 0.0).size(4.0, STRIP_METER_H).radius(2.0).fill(t.bg).border(1.0, t.line))
        .child(el().absolute(4.0, zero).size(18.0, 1.0).fill(t.text_faint))
        .child(el().absolute(2.0, (1.0 - p) * travel).size(22.0, THUMB_H).radius(3.0).fill(t.text_dim).border(1.0, t.bg)
            .child(el().absolute(3.0, THUMB_H / 2.0 - 1.0).size(16.0, 2.0).fill(t.bg)))
        .on_step(move |s: &mut App, steps: i32, _tx: &mut Tx| {
            let db = s.project.track(id).map_or(0.0, |t| t.volume_db);
            s.run(Command::SetVolume(id, (db + steps as f32).round()));
        })
        .on_drag(move |s: &mut App, d: &mut Drag, tx: &mut Tx| {
            match d.phase {
                Phase::Down => {
                    s.begin_gesture("Change volume");
                    d.data = db_to_fader(s.project.track(id).map_or(0.0, |t| t.volume_db));
                    if d.clicks == 2 {
                        if let Some(t) = s.project.track_mut(id) { t.volume_db = 0.0; }
                    }
                }
                Phase::Move => {
                    // Shift slows the fader for fine moves.
                    let k = if tx.mods().shift { 0.25 } else { 1.0 };
                    let db = (fader_to_db(d.data - d.dy * k / (STRIP_METER_H - THUMB_H)) * 10.0).round() / 10.0;
                    if let Some(t) = s.project.track_mut(id) { t.volume_db = db; }
                }
                Phase::Up => {
                    if d.cancelled() { s.cancel_gesture() } else { s.end_gesture() }
                }
            }
            tx.instant();
        });
    let mut meter = el().size(STRIP_METER_W, STRIP_METER_H).radius(2.0).fill(t.bg).border(1.0, t.line).clip_rounded(2.0);
    if let Some(m) = s.meter(id) {
        meter = meter
            .child(el().absolute(0.0, 0.0).size(STRIP_METER_W, STRIP_METER_H).fill(t.meter).live_rect(&m.strip))
            .child(el().absolute(0.0, 0.0).size(STRIP_METER_W, 1.5).fill(t.text).live_rect(&m.hold));
    }
    let pan = el().key(format!("pan-{id}")).size(56.0, 14.0).cursor(Cursor::ResizeColumn).role("slider").label(format!("{} pan", tr.name)).a11y_value(pan_text(tr.pan)).range_value((tr.pan * 100.0).round() as f64, -100.0, 100.0).range_step(5.0).focusable(true)
        .child(el().absolute(0.0, 5.0).size(56.0, 4.0).radius(2.0).fill(t.bg).border(1.0, t.line))
        .child(el().absolute(27.5, 3.0).size(1.0, 8.0).fill(t.text_faint))
        .child(el().absolute(28.0 + tr.pan * 25.0 - 3.0, 2.0).size(6.0, 10.0).radius(2.0).fill(t.text_dim))
        .on_step(move |s: &mut App, steps: i32, _tx: &mut Tx| {
            let pan = s.project.track(id).map_or(0.0, |t| t.pan);
            s.run(Command::SetPan(id, ((pan + steps as f32 * 0.05) * 100.0).round() / 100.0));
        })
        .on_drag(move |s: &mut App, d: &mut Drag, tx: &mut Tx| {
            match d.phase {
                Phase::Down => {
                    s.begin_gesture("Change pan");
                    d.data = s.project.track(id).map_or(0.0, |t| t.pan);
                    if d.clicks == 2 {
                        if let Some(t) = s.project.track_mut(id) { t.pan = 0.0; }
                    }
                }
                Phase::Move => {
                    let v = ((d.data + d.dx / 50.0).clamp(-1.0, 1.0) * 100.0).round() / 100.0;
                    if let Some(t) = s.project.track_mut(id) { t.pan = if v.abs() < 0.04 { 0.0 } else { v }; }
                }
                Phase::Up => {
                    if d.cancelled() { s.cancel_gesture() } else { s.end_gesture() }
                }
            }
            tx.instant();
        });
    let out = match tr.kind {
        TrackKind::Master => "Output".to_string(),
        _ => tr.output.and_then(|b| s.project.track(b)).map_or("Master".to_string(), |b| b.name.clone()),
    };
    let selected = s.sel == Selection::Track(id);
    let kind = match tr.kind {
        TrackKind::Bus => "BUS",
        TrackKind::Master => "MASTER",
        TrackKind::Audio => "AUDIO",
        TrackKind::Instrument => "INST",
    };
    let mut e = col().key(format!("strip-{id}")).role("group").label(format!("{} channel strip", tr.name)).w(STRIP_W).h_full().items_center().gap(3.0).pt(0.0).pb(4.0)
        .border_sides([0.0, 1.0, 0.0, 0.0], t.line)
        .child(el().w_full().h(if selected { 5.0 } else { 3.0 }).fill(t.track(tr.color)))
        .child(label(tr.name.clone(), T_LABEL, if s.project.audible(tr) { t.text } else { t.text_faint }).truncate().max_w(STRIP_W - 8.0)
            .cursor(Cursor::Pointer).on_press(move |s: &mut App| s.run(Command::SelectTrack(id))))
        .child(row().center_y().gap(4.0).child(hint(pan, s, "Pan. Drag sideways; double-click to centre")))
        .child(label(format!("{kind} · {}", pan_text(tr.pan)), T_SMALL, t.text_faint))
        .child(row().gap(6.0).child(meter).child(hint(fader, s, "Volume fader. Drag (Shift for fine moves); double-click for 0 dB; arrows when focused")))
        .child(label(format!("{} dB", db_text(tr.volume_db)), T_NUM_SMALL, t.text).key(format!("db-{id}")))
        .child(row().gap(4.0)
            .child(on_cmd(letter_toggle(t, "M", tr.mute, t.mute, if tr.mute { "Unmute" } else { "Mute" }).label(format!("Mute {}", tr.name)).description("").key(format!("strip-mute-{id}")), Command::SetMute(id, !tr.mute)))
            .child(if tr.kind == TrackKind::Master { el().size(TARGET, TARGET) } else {
                on_cmd(letter_toggle(t, "S", tr.solo, t.solo, if tr.solo { "Unsolo" } else { "Solo" }).label(format!("Solo {}", tr.name)).description("").key(format!("strip-solo-{id}")), Command::SetSolo(id, !tr.solo))
            }))
        .child(row().center_y().gap(3.0).child(icon(&icons::ARROW_RIGHT, 10.0).color(t.text_faint)).child(label(out, T_SMALL, t.text_dim).truncate().max_w(STRIP_W - 22.0)));
    if selected {
        e = e.fill(t.control);
    }
    if matches!(tr.kind, TrackKind::Bus | TrackKind::Master) {
        e = e.fill(if selected { t.control } else { t.bar });
    }
    e
}

fn mixer(s: &App, t: &Theme) -> El {
    let mut strips = row().key("mixer").grow(1.0).min_h(0.0).w_full().scroll_x();
    for tr in s.project.tracks.iter().filter(|t| matches!(t.kind, TrackKind::Audio | TrackKind::Instrument)) {
        strips = strips.child(strip(s, t, tr));
    }
    strips = strips.child(el().w(10.0).h_full().border_sides([0.0, 1.0, 0.0, 0.0], t.line));
    for tr in s.project.tracks.iter().filter(|t| matches!(t.kind, TrackKind::Bus | TrackKind::Master)) {
        strips = strips.child(strip(s, t, tr));
    }
    strips
}

// ---- piano roll ------------------------------------------------------------------------

fn pr_wheel(s: &mut App, e: WheelEvent) -> bool {
    if e.mods.cmd || e.mods.ctrl {
        s.pr_zoom_about((e.dy * 0.004).exp().clamp(0.5, 2.0), e.x);
    } else if e.mods.shift || e.dx.abs() > e.dy.abs() {
        let d = if e.dx.abs() > e.dy.abs() { e.dx } else { e.dy };
        s.pr_set_scroll(s.pr_scroll - d as f64 / s.pr_px_per_tick());
    } else {
        s.pr_set_scroll_y(s.pr_scroll_y - e.dy);
    }
    true
}

fn pitch_at(s: &App, y: f32) -> u8 {
    (PITCH_HI as i32 - ((y + s.pr_scroll_y) / NOTE_H).floor() as i32).clamp(PITCH_LO as i32, PITCH_HI as i32) as u8
}

fn piano_roll(s: &App, t: &Theme) -> El {
    let Some(clip) = s.pr_clip.and_then(|id| s.project.clip(id)) else {
        let target = s.focus_track().filter(|id| s.project.accepts(*id, true));
        let mut e = col().grow(1.0).center().gap(8.0)
            .child(icon(&icons::PIANO, 22.0).color(t.text_faint))
            .child(label("No MIDI clip is open.", T_STRONG, t.text))
            .child(label("Double-click a MIDI clip in the timeline, or select one and press P.", T_LABEL, t.text_dim));
        if let Some(id) = target {
            let tick = snap_floor(s.position(), BAR);
            e = e.child(on_cmd(button(t).key("pr-new-clip").border(1.0, t.line).child(icon(&icons::PLUS, 13.0).color(t.text))
                .child(label(format!("New clip on {} at the playhead", s.project.track(id).map_or("", |t| t.name.as_str())), T_LABEL, t.text)), Command::NewMidiClip { track: id, tick }));
        }
        return e;
    };
    let cid = clip.id;
    let color = s.project.track(clip.track).map_or(0, |t| t.color);
    let hue = t.track(color);
    let (w, h) = (s.pr_rect[2].max(100.0), s.pr_rect[3].max(40.0));
    let sy = s.pr_scroll_y;

    // Keys.
    let mut keys = el().key("pr-keys").w(KEYS_W).h_full().fill(t.bg).border_sides([0.0, 1.0, 0.0, 0.0], t.line).clip_rounded(0.0)
        .on_wheel(|s: &mut App, mut e: WheelEvent, _tx: &mut Tx| {
            e.mods = rune::Mods::default();
            pr_wheel(s, e)
        });
    let white = if t.dark { t.text.mix(t.bg, 0.22) } else { rune::Color::WHITE };
    let black = if t.dark { t.bg } else { t.text };
    let mut rows = Shape::new();
    for p in PITCH_LO..=PITCH_HI {
        let y = (PITCH_HI - p) as f32 * NOTE_H - sy;
        if y + NOTE_H < 0.0 || y > h {
            continue;
        }
        let is_black = is_black_key(p);
        keys = keys.child(el().absolute(0.0, y).size(if is_black { KEYS_W * 0.62 } else { KEYS_W - 1.0 }, NOTE_H - 1.0).corners([0.0, 2.0, 2.0, 0.0]).fill(if is_black { black } else { white }));
        if p % 12 == 0 {
            keys = keys.child(text(pitch_name(p)).font(T_SMALL).color(if t.dark { t.bg } else { t.text }).absolute(KEYS_W - 23.0, y - 0.5));
            rows = rows.line([0.0, y + NOTE_H - 0.5], [w, y + NOTE_H - 0.5], 1.0, LineCap::Butt, t.grid_bar);
        }
        if is_black {
            rows = rows.fill(vec![[0.0, y], [w, y], [w, y + NOTE_H], [0.0, y + NOTE_H]], t.text.alpha(if t.dark { 0.045 } else { 0.06 }));
        }
    }
    // Time grid.
    let g = s.snap.ticks();
    let sub = if s.pr_ppb * g as f32 / TPB as f32 >= 8.0 { g } else if s.pr_ppb >= 8.0 { TPB } else { BAR };
    let mut tick = snap_floor(s.pr_scroll as i64, sub);
    loop {
        let x = s.pr_tick_to_x(tick as f64).round() + 0.5;
        if x > w {
            break;
        }
        if x >= 0.0 {
            let abs = tick + clip.start;
            let c = if abs % BAR == 0 { t.line } else if abs % TPB == 0 { t.grid_bar } else { t.grid_beat };
            rows = rows.line([x, 0.0], [x, h], 1.0, LineCap::Butt, c);
        }
        tick += sub;
    }
    let mut grid = el().key("pr-grid").grow(1.0).min_w(0.0).h_full().fill(t.bg).clip_rounded(0.0).cursor(Cursor::Crosshair)
        .on_layout(|s: &mut App, r: [f32; 4]| {
            s.pr_rect = r;
            s.pr_set_scroll_y(s.pr_scroll_y);
        })
        .child(canvas(rows).absolute(0.0, 0.0).size(w, h));
    let end_x = s.pr_tick_to_x(clip.len as f64);
    if end_x < w {
        grid = grid.child(el().absolute(end_x.max(0.0), 0.0).size(w - end_x.max(0.0), h).fill(t.bar.alpha(0.7)).help("Past the end of the clip"));
    }
    let mut vel = el().key("pr-velocity").grow(1.0).min_w(0.0).h(VEL_H).fill(t.bg).border_sides([1.0, 0.0, 0.0, 0.0], t.line).clip_rounded(0.0).cursor(Cursor::ResizeRow);
    for n in clip.notes() {
        let x = s.pr_tick_to_x(n.start as f64);
        let nw = (n.len as f64 * s.pr_px_per_tick()) as f32;
        if x > w || x + nw < 0.0 {
            continue;
        }
        let selected = s.sel_notes.contains(&n.id);
        // Velocity bar: height is the value; the selected note's bar is outlined and labelled.
        let vh = (n.vel as f32 / 127.0 * (VEL_H - 8.0)).max(2.0);
        let mut bar = el().absolute(x, VEL_H - vh).size(5.0, vh).corners([2.0, 2.0, 0.0, 0.0]).fill(t.bg.mix(hue, 0.4 + 0.6 * n.vel as f32 / 127.0));
        if selected {
            bar = bar.border(1.0, t.select);
            if s.sel_notes.len() == 1 {
                vel = vel.child(text(format!("{}", n.vel)).font(T_NUM_SMALL).color(t.text).absolute(x + 8.0, (VEL_H - vh - 2.0).clamp(0.0, VEL_H - 14.0)));
            }
        }
        vel = vel.child(bar);
        let y = (PITCH_HI - n.pitch) as f32 * NOTE_H - sy;
        if y + NOTE_H < 0.0 || y > h {
            continue;
        }
        let nid = n.id;
        // Lightness follows velocity, so loudness reads without the lane.
        let mut ne = el().key(format!("note-{nid}")).absolute(x, y + 1.0).size(nw.max(4.0) - 1.0, NOTE_H - 2.0).radius(2.0)
            .fill(t.bg.mix(hue, 0.4 + 0.6 * n.vel as f32 / 127.0)).cursor(Cursor::Grab)
            .role("button").label(format!("Note {} at {}, velocity {}", pitch_name(n.pitch), bar_beat(n.start), n.vel));
        ne = if selected { ne.border(2.0, t.select) } else { ne.border(1.0, t.bg.alpha(0.6)) };
        let grip = el().absolute((nw.max(4.0) - 7.0).max(1.0), 0.0).size(6.0, NOTE_H - 2.0).cursor(Cursor::ResizeColumn).hit()
            .on_drag(move |s: &mut App, d: &mut Drag, tx: &mut Tx| {
                match d.phase {
                    Phase::Down => {
                        if !s.sel_notes.contains(&nid) { s.sel_notes = vec![nid]; }
                        s.begin_note_gesture("Resize note");
                    }
                    Phase::Move => s.resize_notes((d.dx as f64 / s.pr_px_per_tick()) as i64, s.grid(tx.mods().alt)),
                    Phase::Up => {
                        if d.cancelled() { s.cancel_gesture() } else { s.end_gesture() }
                    }
                }
                tx.instant();
            });
        grid = grid.child(ne.child(grip)
            .on_drag(move |s: &mut App, d: &mut Drag, tx: &mut Tx| {
                match d.phase {
                    Phase::Down => {
                        if tx.mods().shift {
                            if let Some(i) = s.sel_notes.iter().position(|n| *n == nid) { s.sel_notes.remove(i); } else { s.sel_notes.push(nid); }
                        } else if !s.sel_notes.contains(&nid) {
                            s.sel_notes = vec![nid];
                        }
                        // The pressed note leads: it is the one that snaps.
                        if let Some(i) = s.sel_notes.iter().position(|n| *n == nid) {
                            s.sel_notes.swap(0, i);
                        }
                        s.begin_note_gesture("Move notes");
                        if let Some(i) = s.note_origin.iter().position(|n| n.id == nid) { s.note_origin.swap(0, i); }
                    }
                    Phase::Move => {
                        let dt = (d.dx as f64 / s.pr_px_per_tick()) as i64;
                        s.drag_notes(dt, -(d.dy / NOTE_H).round() as i32, s.grid(tx.mods().alt));
                    }
                    Phase::Up => {
                        if d.cancelled() { s.cancel_gesture() } else { s.end_gesture() }
                    }
                }
                tx.instant();
            })
            .on_secondary_press(move |s: &mut App, _p, _tx: &mut Tx| s.run(Command::RemoveNote { clip: cid, note: nid })));
    }
    if let Some([x0, y0, x1, y1]) = s.marquee {
        grid = grid.child(el().absolute(x0.min(x1), y0.min(y1)).size((x1 - x0).abs(), (y1 - y0).abs()).fill(t.select.alpha(0.12)).border(1.0, t.select).border_dash(4.0, 3.0).inert());
    }
    grid = grid.child(el().key("pr-playhead").absolute(0.0, 0.0).size(1.5, h).fill(t.accent).live_offset(&s.live.pr_playhead).inert());
    let grid = hint(grid, s, "Click an empty cell to add a note and keep dragging to place it. Wheel scrolls pitch, Shift+wheel time, Ctrl+wheel zooms")
        .on_wheel(|s: &mut App, e: WheelEvent, _tx: &mut Tx| pr_wheel(s, e))
        .on_magnify(|s: &mut App, e: MagnifyEvent, _tx: &mut Tx| {
            s.pr_zoom_about((1.0 + e.delta).clamp(0.5, 2.0), e.x);
            true
        })
        .on_drag(move |s: &mut App, d: &mut Drag, tx: &mut Tx| {
            match d.phase {
                Phase::Down if tx.mods().shift => {
                    s.marquee = Some([d.x, d.y, d.x, d.y]);
                    s.sel_notes.clear();
                }
                Phase::Down => {
                    let len = s.project.clip(cid).map_or(0, |c| c.len);
                    let start = snap_floor(s.pr_x_to_tick(d.x).max(0.0) as i64, s.grid(tx.mods().alt));
                    if start >= len {
                        s.status = "That is past the end of the clip. Lengthen the clip in the timeline first".into();
                        return;
                    }
                    let pitch = pitch_at(s, d.y);
                    s.begin_gesture("Add note");
                    let id = s.project.fresh_id();
                    let note = Note { id, start, len: s.last_note_len, pitch, vel: s.last_vel };
                    if let Some(notes) = s.project.clip_mut(cid).and_then(|c| c.notes_mut()) { notes.push(note); }
                    s.sel_notes = vec![id];
                    s.note_origin = vec![note];
                }
                Phase::Move => {
                    if let Some(m) = &mut s.marquee {
                        m[2] = d.x;
                        m[3] = d.y;
                        let (m, sy) = (*m, s.pr_scroll_y);
                        let (t0, t1) = (s.pr_x_to_tick(m[0].min(m[2])) as i64, s.pr_x_to_tick(m[0].max(m[2])) as i64);
                        let (ya, yb) = (m[1].min(m[3]) + sy, m[1].max(m[3]) + sy);
                        s.sel_notes = s.pr_notes().iter().filter(|n| {
                            let y = (PITCH_HI - n.pitch) as f32 * NOTE_H;
                            n.start < t1 && n.start + n.len > t0 && y < yb && y + NOTE_H > ya
                        }).map(|n| n.id).collect();
                    } else if s.gesture.is_some() {
                        let dt = (d.dx as f64 / s.pr_px_per_tick()) as i64;
                        s.drag_notes(dt, -(d.dy / NOTE_H).round() as i32, s.grid(tx.mods().alt));
                    }
                }
                Phase::Up => {
                    s.marquee = None;
                    if d.cancelled() { s.cancel_gesture() } else { s.end_gesture() }
                }
            }
            tx.instant();
        });
    let vel = hint(vel, s, "Velocity. Drag up or down under a note to set how hard it is played")
        .on_wheel(|s: &mut App, mut e: WheelEvent, _tx: &mut Tx| {
            // Over the velocity lane the wheel only scrolls time.
            e.dx = if e.dx != 0.0 { e.dx } else { e.dy };
            e.dy = 0.0;
            pr_wheel(s, e)
        })
        .on_drag(move |s: &mut App, d: &mut Drag, tx: &mut Tx| {
            match d.phase {
                Phase::Down | Phase::Move => {
                    if d.phase == Phase::Down {
                        s.begin_gesture("Change velocity");
                    }
                    // The note whose bar is nearest the pointer; a selected note wins a tie.
                    let tick = s.pr_x_to_tick(d.x) as i64;
                    let reach = (10.0 / s.pr_px_per_tick()) as i64;
                    let pick = s.pr_notes().iter().filter(|n| tick >= n.start - reach && tick < n.start + n.len.max(reach))
                        .min_by_key(|n| ((n.start - tick).abs(), !s.sel_notes.contains(&n.id))).map(|n| n.id);
                    if let Some(id) = pick {
                        let v = ((1.0 - (d.y - 4.0) / (VEL_H - 8.0)) * 127.0).round().clamp(1.0, 127.0) as u8;
                        if d.phase == Phase::Down { s.sel_notes = vec![id]; }
                        s.last_vel = v;
                        if let Some(n) = s.project.clip_mut(cid).and_then(|c| c.notes_mut()).and_then(|ns| ns.iter_mut().find(|n| n.id == id)) { n.vel = v; }
                    }
                }
                Phase::Up => {
                    if d.cancelled() { s.cancel_gesture() } else { s.end_gesture() }
                }
            }
            tx.instant();
        });
    col().grow(1.0).min_h(0.0).w_full()
        .child(row().grow(1.0).min_h(0.0).w_full().child(keys).child(grid))
        .child(row().w_full().h(VEL_H)
            .child(col().center().w(KEYS_W).h(VEL_H).fill(t.panel).border_sides([1.0, 1.0, 0.0, 0.0], t.line).child(label("Vel", T_SMALL, t.text_dim)).child(label("1-127", T_SMALL, t.text_faint)))
            .child(vel))
}
