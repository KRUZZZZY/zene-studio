//! The arrangement: ruler, track headers and clip lanes. Scroll and zoom are app state
//! (ticks and points), so the wheel mapping is exactly the one the rationale promises:
//! wheel scrolls tracks, Shift+wheel scrolls time, Ctrl+wheel zooms at the pointer,
//! middle-drag pans. Clip drags apply instantly; nothing here animates.

use rune::shape::LineCap;
use rune::{Cursor, Drag, El, MagnifyEvent, Phase, PointerButton, Shape, Tx, WheelEvent, canvas, col, el, icon, row, text};

use crate::command::Command;
use crate::demo;
use crate::icons;
use crate::model::*;
use crate::state::*;
use crate::theme::*;
use crate::ui::{hint, label, letter_toggle, on_cmd};

pub fn view(s: &App, t: &Theme) -> El {
    let lanes = s.project.lanes().count();
    let corner = row().center_y().w(HEADER_W).h(RULER_H).px(10.0).gap(6.0).fill(t.bar).border_sides([0.0, 1.0, 1.0, 0.0], t.line)
        .child(label("TRACKS", T_SECTION, t.text_faint))
        .child(label(format!("{lanes}"), T_NUM_SMALL, t.text_faint));
    col().key("arrangement").grow(1.0).min_h(120.0).w_full()
        .child(row().w_full().h(RULER_H).child(corner).child(ruler(s, t)))
        .child(row().grow(1.0).min_h(0.0).w_full().child(headers(s, t)).child(lane_area(s, t)))
}

/// Wheel over the arrangement. Returns true: the timeline owns its scrolling.
fn wheel(s: &mut App, e: WheelEvent, x_in_lanes: Option<f32>) -> bool {
    if e.mods.cmd || e.mods.ctrl {
        s.zoom_about((e.dy * 0.004).exp().clamp(0.5, 2.0), x_in_lanes.unwrap_or(s.lanes_width() / 2.0));
    } else if e.mods.shift || e.dx.abs() > e.dy.abs() {
        let d = if e.dx.abs() > e.dy.abs() { e.dx } else { e.dy };
        s.set_scroll(s.scroll - d as f64 / s.px_per_tick());
    } else {
        s.set_scroll_y(s.scroll_y - e.dy);
    }
    true
}

fn ruler(s: &App, t: &Theme) -> El {
    let w = s.lanes_width();
    let bar_px = s.ppb * BEATS_PER_BAR as f32;
    let step = [1i64, 2, 4, 8, 16].into_iter().find(|n| bar_px * *n as f32 >= 46.0).unwrap_or(16);
    let mut e = el().key("ruler").grow(1.0).min_w(0.0).h(RULER_H).fill(t.bar).border_sides([0.0, 0.0, 1.0, 0.0], t.line).clip_rounded(0.0).cursor(Cursor::Pointer)
        .role("slider").label("Playhead position");
    let mut bar = (s.scroll / BAR as f64).floor() as i64;
    loop {
        let x = s.tick_to_x((bar * BAR) as f64);
        if x > w {
            break;
        }
        let major = bar % step == 0;
        e = e.child(el().absolute(x.round(), if major { 0.0 } else { RULER_H - 6.0 }).size(1.0, RULER_H).fill(if major { t.line } else { t.grid_bar }));
        if major && x > -30.0 {
            e = e.child(text(format!("{}", bar + 1)).font(T_NUM_SMALL).color(t.text_dim).absolute(x.round() + 5.0, 5.0));
        }
        bar += 1;
    }
    // The song's end.
    let end = s.tick_to_x(s.project.length as f64);
    if end < w {
        e = e.child(el().absolute(end.round(), 0.0).size(2.0, RULER_H).fill(t.text_faint).help("End of song"));
    }
    let head = el().key("playhead-cap").absolute(-5.0, RULER_H - 10.0).size(11.0, 10.0).corners([2.0, 2.0, 6.0, 6.0]).fill(t.accent).live_offset(&s.live.playhead);
    hint(e.child(head), s, "Click or drag the ruler to move the playhead")
        .on_drag(|s: &mut App, d: &mut Drag, tx: &mut Tx| {
            let tick = snap_to(s.x_to_tick(d.x).max(0.0) as i64, s.grid(tx.mods().alt));
            s.run(Command::Seek(tick));
            s.sync_live();
            if d.phase == Phase::Move {
                tx.live_only();
            }
        })
        .on_wheel(|s: &mut App, e: WheelEvent, _tx: &mut Tx| wheel(s, e, Some(e.x)))
}

fn kind_icon(k: TrackKind) -> &'static rune::Icon {
    match k {
        TrackKind::Audio => &icons::AUDIO_LINES,
        TrackKind::Instrument => &icons::PIANO,
        TrackKind::Bus => &icons::LAYERS,
        TrackKind::Master => &icons::VOLUME_2,
    }
}

fn headers(s: &App, t: &Theme) -> El {
    let th = s.track_height();
    let mut e = el().key("headers").w(HEADER_W).h_full().fill(t.panel).border_sides([0.0, 1.0, 0.0, 0.0], t.line).clip_rounded(0.0)
        .on_wheel(|s: &mut App, e: WheelEvent, _tx: &mut Tx| wheel(s, e, None));
    for (i, tr) in s.project.lanes().enumerate() {
        let y = i as f32 * th - s.scroll_y;
        if y + th < 0.0 || y > s.lanes_rect[3] + th {
            continue;
        }
        let selected = s.focus_track() == Some(tr.id);
        let audible = s.project.audible(tr);
        let id = tr.id;
        let name_c = if audible { t.text } else { t.text_faint };
        let mut info = col().grow(1.0).min_w(0.0).justify_center().gap(if th < 48.0 { 2.0 } else { 5.0 })
            .child(row().center_y().gap(6.0)
                .child(icon(kind_icon(tr.kind), 13.0).color(t.text_dim))
                .child(label(tr.name.clone(), T_STRONG, name_c).truncate().grow(1.0).min_w(0.0)));
        if let Some(m) = s.meter(id) {
            let meter = el().size(HEADER_METER_W, HEADER_METER_H).radius(2.0).fill(t.control).clip_rounded(2.0)
                .child(el().absolute(0.0, 0.0).size(HEADER_METER_W, HEADER_METER_H).fill(t.meter).live_rect(&m.header));
            info = info.child(row().center_y().gap(7.0).child(meter).child(label(format!("{} dB", db_text(tr.volume_db)), T_NUM_SMALL, t.text_faint)));
        }
        let mut h = row().key(format!("track-{id}")).absolute(0.0, y).size(HEADER_W - 1.0, th).center_y().gap(6.0).pr(8.0)
            .border_sides([0.0, 0.0, 1.0, 0.0], t.line).role("row").label(format!("Track {}", tr.name)).cursor(Cursor::Pointer)
            .child(el().w(if selected { 6.0 } else { 4.0 }).h_full().fill(t.track(tr.color)))
            .child(info.pl(4.0))
            .child(on_cmd(letter_toggle(t, "M", tr.mute, t.mute, if tr.mute { "Unmute track" } else { "Mute track" }).label(format!("Mute {}", tr.name)).description("").key(format!("mute-{id}")), Command::SetMute(id, !tr.mute)))
            .child(on_cmd(letter_toggle(t, "S", tr.solo, t.solo, if tr.solo { "Unsolo track" } else { "Solo track" }).label(format!("Solo {}", tr.name)).description("").key(format!("solo-{id}")), Command::SetSolo(id, !tr.solo)));
        // Selection is a filled row plus a wider colour bar and an outline, not a hue change.
        h = if selected { h.fill(t.control).outline(1.0, -1.0, t.text_faint) } else { h.hover(t.bar) };
        e = e.child(hint(h, s, "Click to select the track. M mutes, S solos; the bar is a simulated level meter").on_press(move |s: &mut App| s.run(Command::SelectTrack(id))));
    }
    e
}

/// Vertical strokes for a clip's synthesised overview: one box per column, visible part only.
fn waveform(c: &Clip, x0: f32, w: f32, h: f32, px_per_tick: f64, vis: (f32, f32), color: rune::Color) -> Shape {
    let mut sh = Shape::new();
    let (from, to) = ((vis.0 - x0).max(0.0), (vis.1 - x0).min(w));
    let mid = h / 2.0;
    let gain = 10f32.powf(c.gain_db / 20.0);
    let mut x = (from / 3.0).floor() * 3.0;
    while x < to {
        // The loudest sample in this column, so hits never fall between columns.
        let (a, b) = ((x as f64 / px_per_tick) as i64, ((x + 3.0) as f64 / px_per_tick) as i64);
        let stride = ((b - a) / 6).max(1);
        let mut peak = 0f32;
        let mut tick = a;
        while tick <= b {
            peak = peak.max(c.peak_at(tick));
            tick += stride;
        }
        let half = (peak * gain).min(1.0) * (mid - 1.0);
        if half > 0.4 {
            sh = sh.line([x + 1.0, mid - half], [x + 1.0, mid + half], 2.0, LineCap::Butt, color);
        }
        x += 3.0;
    }
    sh
}

/// A MIDI clip's notes as short bars across its pitch range.
fn mini_notes(c: &Clip, w: f32, h: f32, px_per_tick: f64, color: rune::Color) -> Shape {
    let mut sh = Shape::new();
    let notes = c.notes();
    let (lo, hi) = notes.iter().fold((127u8, 0u8), |(lo, hi), n| (lo.min(n.pitch), hi.max(n.pitch)));
    let span = (hi.saturating_sub(lo)).max(6) as f32;
    for n in notes.iter().filter(|n| n.start < c.len) {
        let x = (n.start as f64 * px_per_tick) as f32;
        let x2 = ((((n.start + n.len).min(c.len)) as f64 * px_per_tick) as f32 - 1.0).max(x + 1.5).min(w);
        let y = 2.0 + (1.0 - (n.pitch - lo) as f32 / span) * (h - 5.0);
        sh = sh.line([x, y], [x2, y], 2.0, LineCap::Butt, color);
    }
    sh
}

fn clip_el(s: &App, t: &Theme, c: &Clip, color: usize, x: f32, y: f32, w: f32, h: f32, lane_audible: bool) -> El {
    let id = c.id;
    let selected = s.sel.has_clip(id);
    let hue = t.track(color);
    let dim = c.muted || !lane_audible;
    let ink = if t.dark { t.bg.mix(hue, 0.25).mix(rune::Color::WHITE, 0.82) } else { t.bg.mix(hue, 0.6).mix(rune::Color::BLACK, 0.55) };
    let name_h = if h >= 34.0 { 15.0 } else { 0.0 };
    let body_h = h - name_h - 2.0;
    let vis = (0.0, s.lanes_width());
    let pxt = s.px_per_tick();
    let content = match &c.kind {
        ClipKind::Audio { .. } => canvas(waveform(c, x, w, body_h, pxt, vis, ink)),
        ClipKind::Midi { .. } => canvas(mini_notes(c, w, body_h, pxt, ink)),
    }
    .absolute(0.0, name_h + 1.0).size(w, body_h);
    let mut e = el().key(format!("clip-{id}")).absolute(x, y).size(w, h).radius(4.0).clip_rounded(4.0)
        .fill(t.track_wash(color, if selected { 0.52 } else { 0.34 }))
        .role("button").label(format!("{} clip {}{}", if c.is_midi() { "MIDI" } else { "Audio" }, c.name, if c.muted { ", muted" } else { "" }))
        .cursor(Cursor::Grab)
        .child(content);
    if name_h > 0.0 {
        let title = if c.muted { format!("Muted: {}", c.name) } else { c.name.clone() };
        e = e.child(el().absolute(0.0, 0.0).size(w, name_h).fill(hue.alpha(if selected { 0.95 } else { 0.7 })))
            .child(text(title).font(T_SMALL).color(t.on_accent_for(hue)).absolute(5.0 + (-x).clamp(0.0, (w - 40.0).max(0.0)), 0.5).w((w - 10.0).max(0.0)).truncate());
    }
    // Selected: a bright outline and a visible resize grip, as well as the stronger fill.
    e = if selected { e.border(2.0, t.select) } else { e.border(1.0, hue.alpha(0.8)) };
    if dim {
        e = e.opacity(0.42);
    }
    let grip = el().key(format!("clip-end-{id}")).absolute(w - 8.0, 0.0).size(8.0, h).cursor(Cursor::ResizeColumn).hit()
        .child(el().absolute(4.0, h / 2.0 - 7.0).size(2.0, 14.0).radius(1.0).fill(if selected { t.select } else { rune::Color::TRANSPARENT }))
        .on_drag(move |s: &mut App, d: &mut Drag, tx: &mut Tx| {
            match d.phase {
                Phase::Down => {
                    if !s.sel.has_clip(id) {
                        s.select_clip(id, false);
                    }
                    s.begin_gesture("Resize clip");
                }
                Phase::Move => {
                    let dt = (d.dx as f64 / s.px_per_tick()) as i64;
                    s.drag_clip_end(id, dt, s.grid(tx.mods().alt));
                }
                Phase::Up => {
                    if d.cancelled() { s.cancel_gesture() } else { s.finish_clip_gesture() }
                }
            }
            tx.instant();
        });
    hint(e.child(grip), s, "Drag to move (Alt bypasses snap), drag the right edge to resize, double-click a MIDI clip to edit its notes")
        .on_drag(move |s: &mut App, d: &mut Drag, tx: &mut Tx| {
            match d.phase {
                Phase::Down => {
                    let additive = tx.mods().shift;
                    if additive || !s.sel.has_clip(id) {
                        s.select_clip(id, additive);
                    } else if let Selection::Clips(v) = &mut s.sel {
                        // The pressed clip leads the drag: it is the one that snaps.
                        v.retain(|c| *c != id);
                        v.insert(0, id);
                    }
                    if s.sel.has_clip(id) {
                        s.begin_gesture("Move clip");
                    }
                }
                Phase::Move => {
                    let dt = (d.dx as f64 / s.px_per_tick()) as i64;
                    let dl = (d.dy / s.track_height()).round() as i32;
                    s.drag_clips(dt, dl, s.grid(tx.mods().alt));
                }
                Phase::Up => {
                    let moved = s.gesture.as_ref().is_some_and(|(before, _)| *before != s.project);
                    if d.cancelled() { s.cancel_gesture() } else { s.finish_clip_gesture() }
                    if d.clicks == 2 && !moved {
                        s.run(Command::OpenPianoRoll(id));
                    }
                }
            }
            tx.instant();
        })
        .on_secondary_press(move |s: &mut App, _p, _tx: &mut Tx| {
            if !s.sel.has_clip(id) {
                s.select_clip(id, false);
            }
            s.status = "Clip commands live in the Edit menu (Duplicate Ctrl+D, Split Ctrl+E, Mute 0, Delete)".into();
        })
}

fn lane_area(s: &App, t: &Theme) -> El {
    let th = s.track_height();
    let (w, h) = (s.lanes_width(), s.lanes_rect[3].max(60.0));
    let lanes = s.project.lanes().count();
    let mut e = el().key("lanes").grow(1.0).min_w(0.0).h_full().fill(t.bg).clip_rounded(0.0)
        .on_layout(|s: &mut App, r: [f32; 4]| {
            s.lanes_rect = r;
            s.set_scroll(s.scroll);
            s.set_scroll_y(s.scroll_y);
        });
    // Grid: bars always; beats and finer lines appear as the zoom makes room for them.
    let mut grid = Shape::new();
    let beat_px = s.ppb;
    let sub = if s.snap_on && s.snap.ticks() < TPB && beat_px * s.snap.ticks() as f32 / TPB as f32 >= 9.0 { s.snap.ticks() } else if beat_px >= 9.0 { TPB } else { BAR };
    let mut tick = snap_floor(s.scroll as i64, sub);
    let content_h = (lanes as f32 * th - s.scroll_y).min(h);
    loop {
        let x = s.tick_to_x(tick as f64).round() + 0.5;
        if x > w {
            break;
        }
        if x >= 0.0 {
            let c = if tick % BAR == 0 { t.grid_bar } else { t.grid_beat };
            grid = grid.line([x, 0.0], [x, content_h], 1.0, LineCap::Butt, c);
        }
        tick += sub;
    }
    e = e.child(canvas(grid).absolute(0.0, 0.0).size(w, h));
    for (i, tr) in s.project.lanes().enumerate() {
        let y = i as f32 * th - s.scroll_y;
        if y + th < 0.0 || y > h {
            continue;
        }
        e = e.child(el().absolute(0.0, y + th - 1.0).size(w, 1.0).fill(t.line));
        if s.focus_track() == Some(tr.id) {
            e = e.child(el().absolute(0.0, y).size(w, th - 1.0).fill(t.text.alpha(0.035)));
        }
        let audible = s.project.audible(tr);
        for c in s.project.clips.iter().filter(|c| c.track == tr.id) {
            let x = s.tick_to_x(c.start as f64);
            let cw = (c.len as f64 * s.px_per_tick()) as f32;
            if x + cw < 0.0 || x > w {
                continue;
            }
            e = e.child(clip_el(s, t, c, tr.color, x, y + 2.0, cw.max(4.0), th - 5.0, audible));
        }
    }
    // Past the song's end.
    let end = s.tick_to_x(s.project.length as f64);
    if end < w {
        e = e.child(el().absolute(end, 0.0).size(w - end, h).fill(t.bar.alpha(0.55)));
    }
    // Where a dragged browser item would land.
    if let Some(d) = &s.item_drag {
        let it = &demo::LIBRARY[d.item];
        if let Some(tg) = d.target {
            let lane = tg.track.and_then(|id| s.project.lane_index(id)).unwrap_or(lanes);
            let y = lane as f32 * th - s.scroll_y;
            let gw = (it.beats as f32 * s.ppb).max(24.0);
            e = e.child(row().key("drop-ghost").absolute(s.tick_to_x(tg.tick as f64), y + 2.0).size(gw, th - 5.0).radius(4.0).px(6.0).center_y().gap(5.0)
                .fill(t.accent.alpha(0.22)).border(2.0, t.accent).border_dash(5.0, 3.0)
                .child(icon(&icons::PLUS, 12.0).color(t.text))
                .child(label(if tg.track.is_none() { format!("New track: {}", it.name) } else { it.name.to_string() }, T_SMALL, t.text).truncate().grow(1.0).min_w(0.0)));
        }
    }
    let below = lanes as f32 * th - s.scroll_y;
    if below < h {
        e = e.child(row().absolute(10.0, below + 6.0).gap(6.0).center_y()
            .child(icon(&icons::PLUS, 12.0).color(t.text_faint))
            .child(label("Drag a browser item here to add a track", T_SMALL, t.text_faint)));
    }
    // The playhead: a live value, so playback never re-runs this view.
    e = e.child(el().key("playhead").absolute(0.0, 0.0).size(1.5, h).fill(t.accent).live_offset(&s.live.playhead).inert());
    // Scroll position indicators (the wheel and middle-drag do the scrolling).
    let total = s.project.length as f64 + 4.0 * BAR as f64;
    let (fx, fw) = ((s.scroll / total) as f32, (w as f64 / s.px_per_tick() / total).min(1.0) as f32);
    if fw < 0.999 {
        e = e.child(el().absolute(fx * w, h - 4.0).size((fw * w).max(24.0), 3.0).radius(1.5).fill(t.text_faint.alpha(0.7)).inert());
    }
    let content = lanes as f32 * th + 28.0;
    if content > h {
        e = e.child(el().absolute(w - 4.0, s.scroll_y / content * h).size(3.0, (h / content * h).max(24.0)).radius(1.5).fill(t.text_faint.alpha(0.7)).inert());
    }
    hint(e, s, "Wheel: scroll tracks. Shift+wheel: scroll time. Ctrl+wheel: zoom at the pointer. Middle-drag: pan")
        .pointer_buttons([PointerButton::Middle])
        .on_wheel(|s: &mut App, e: WheelEvent, _tx: &mut Tx| wheel(s, e, Some(e.x)))
        .on_magnify(|s: &mut App, e: MagnifyEvent, _tx: &mut Tx| {
            s.zoom_about((1.0 + e.delta).clamp(0.5, 2.0), e.x);
            true
        })
        .on_drag(|s: &mut App, d: &mut Drag, tx: &mut Tx| {
            if tx.button() == PointerButton::Middle {
                match d.phase {
                    Phase::Down => s.pan_origin = (s.scroll, s.scroll_y),
                    Phase::Move => {
                        s.set_scroll(s.pan_origin.0 - d.dx as f64 / s.px_per_tick());
                        s.set_scroll_y(s.pan_origin.1 - d.dy);
                    }
                    Phase::Up => {}
                }
                tx.instant();
                return;
            }
            if d.phase != Phase::Down {
                return;
            }
            // An empty part of a lane: select its track; a double-click on an instrument
            // lane makes a clip there.
            let lane = ((d.y + s.scroll_y) / s.track_height()).floor().max(0.0) as usize;
            match s.project.lane_at(lane).map(|t| (t.id, t.kind)) {
                Some((id, kind)) => {
                    s.select_track(id);
                    if d.clicks == 2 {
                        if kind == TrackKind::Instrument {
                            let tick = snap_floor(s.x_to_tick(d.x).max(0.0) as i64, BAR);
                            s.run(Command::NewMidiClip { track: id, tick });
                        } else {
                            s.status = "Audio tracks take clips from the browser. Double-click an instrument lane for a new MIDI clip".into();
                        }
                    }
                }
                None => s.clear_selection(),
            }
        })
}
