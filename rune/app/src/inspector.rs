//! The inspector: always the current selection, with fields that edit it. Every field goes
//! through the same undoable command as the equivalent drag.

use rune::{Cursor, El, Tx, col, el, icon, row, text_input};

use crate::command::Command;
use crate::icons;
use crate::model::*;
use crate::state::*;
use crate::theme::*;
use crate::ui::{button, hint, label, on_cmd, section, stepper, toggle};

pub const INSPECTOR_W: f32 = 248.0;

fn field(t: &Theme, name: &str, value: impl Into<String>) -> El {
    row().center_y().gap(4.0).h(22.0).child(label(name, T_LABEL, t.text_dim).w(62.0)).child(label(value, T_LABEL, t.text).truncate().grow(1.0).min_w(0.0))
}

fn name_field(s: &App, t: &Theme) -> El {
    let input = text_input(&s.name_edit, T_BODY).key("inspector-name").single_line().grow(1.0).color(t.text)
        .caret(t.accent, t.accent.alpha(0.3)).label("Name")
        .on_edit(|s: &mut App| &mut s.name_edit)
        .on_submit(|s: &mut App, tx: &mut Tx| {
            let name = s.name_edit.text.clone();
            s.run(Command::Rename(name));
            tx.blur();
        })
        .on_input_key(|s: &mut App, ev: &rune::KeyEvent, tx: &mut Tx| {
            if ev.key == rune::Key::Escape {
                s.sync_name_edit(true);
                tx.blur();
                return true;
            }
            false
        });
    row().center_y().gap(4.0).h(28.0)
        .child(label("Name", T_LABEL, t.text_dim).w(62.0))
        .child(row().center_y().grow(1.0).min_w(0.0).h(26.0).px(7.0).radius(4.0).fill(t.bg).border(1.0, t.line).focus_within_ring(2.0, 0.0, t.accent).child(input))
}

fn check(t: &Theme, name: &str, on: bool, color: rune::Color, c: Command, key: &str) -> El {
    on_cmd(toggle(t, on, color).key(key).grow(1.0).label(name)
        .child(icon(if on { &icons::CHECK } else { &icons::MINUS }, 12.0).color(if on { t.on_accent } else { t.text_faint }))
        .child(label(format!("{name}: {}", if on { "on" } else { "off" }), T_LABEL, if on { t.on_accent } else { t.text_dim })), c)
}

fn head(t: &Theme, ic: &'static rune::Icon, kind: &str, color: Option<rune::Color>) -> El {
    let mut r = row().center_y().gap(7.0);
    if let Some(c) = color {
        r = r.child(el().size(10.0, 10.0).radius(2.0).fill(c));
    }
    r.child(icon(ic, 14.0).color(t.text_dim)).child(label(kind, T_STRONG, t.text))
}

pub fn view(s: &App, t: &Theme) -> El {
    let mut p = col().key("inspector").w(INSPECTOR_W).h_full().min_h(0.0).padding(10.0).gap(8.0).fill(t.panel).border_sides([0.0, 0.0, 0.0, 1.0], t.line).clip_rounded(0.0)
        .child(section(t, "Inspector"));
    let grid = s.snap.ticks();
    match &s.sel {
        Selection::Clips(v) if v.len() == 1 => {
            let Some(c) = s.project.clip(v[0]) else { return p };
            let tr = s.project.track(c.track);
            let id = c.id;
            p = p.child(head(t, if c.is_midi() { &icons::PIANO } else { &icons::AUDIO_LINES }, if c.is_midi() { "MIDI clip" } else { "Audio clip" }, tr.map(|x| t.track(x.color))))
                .child(name_field(s, t))
                .child(field(t, "Track", tr.map_or("", |x| x.name.as_str())))
                .child(hint(stepper(t, "Start", bar_beat(c.start), Command::MoveClip { id, start: (c.start - grid).max(0), track: c.track }, Command::MoveClip { id, start: c.start + grid, track: c.track }), s, "Where the clip starts. The buttons move it one grid step"))
                .child(hint(stepper(t, "Length", length_text(c.len), Command::ResizeClip { id, len: (c.len - grid).max(grid) }, Command::ResizeClip { id, len: c.len + grid }), s, "How long the clip is. The buttons change it one grid step"))
                .child(hint(stepper(t, "Gain", format!("{} dB", db_text(c.gain_db)), Command::SetClipGain(id, c.gain_db - 1.0), Command::SetClipGain(id, c.gain_db + 1.0)), s, "Clip gain, applied before the track fader. The overview and the simulated meter follow it"))
                .child(row().child(check(t, "Muted", c.muted, t.mute, Command::ToggleClipMute, "clip-mute")));
            if c.is_midi() {
                p = p.child(field(t, "Notes", format!("{}", c.notes().len())))
                    .child(on_cmd(button(t).key("open-piano-roll").border(1.0, t.line).child(icon(&icons::PIANO, 13.0).color(t.text)).child(label("Edit notes in Piano Roll", T_LABEL, t.text)), Command::OpenPianoRoll(id)));
            } else {
                p = p.child(field(t, "Source", "Synthesised demo overview"));
            }
            p = p.child(note_section(s, t));
            p = p.child(el().grow(1.0)).child(row().gap(6.0)
                .child(on_cmd(button(t).grow(1.0).child(icon(&icons::COPY, 13.0).color(t.text)).child(label("Duplicate", T_LABEL, t.text)), Command::DuplicateSelection))
                .child(on_cmd(button(t).grow(1.0).child(icon(&icons::TRASH, 13.0).color(t.text)).child(label("Delete", T_LABEL, t.text)), Command::DeleteSelection)));
        }
        Selection::Clips(v) => {
            let muted = v.iter().filter_map(|id| s.project.clip(*id)).all(|c| c.muted);
            p = p.child(head(t, &icons::LAYERS, &format!("{} clips selected", v.len()), None))
                .child(label("Drag any of them to move all. Arrow keys nudge by one grid step.", T_LABEL, t.text_dim).wrap())
                .child(row().child(check(t, "Muted", muted, t.mute, Command::ToggleClipMute, "clip-mute")))
                .child(row().gap(6.0)
                    .child(on_cmd(button(t).grow(1.0).child(label("Duplicate", T_LABEL, t.text)), Command::DuplicateSelection))
                    .child(on_cmd(button(t).grow(1.0).child(label("Delete", T_LABEL, t.text)), Command::DeleteSelection)));
        }
        Selection::Track(id) => {
            let Some(tr) = s.project.track(*id) else { return p };
            let id = tr.id;
            let kind = match tr.kind {
                TrackKind::Audio => "Audio track",
                TrackKind::Instrument => "Instrument track",
                TrackKind::Bus => "Bus",
                TrackKind::Master => "Master",
            };
            let out = tr.output.and_then(|b| s.project.track(b)).map_or("Master", |b| b.name.as_str());
            p = p.child(head(t, &icons::SLIDERS_VERTICAL, kind, Some(t.track(tr.color)))).child(name_field(s, t));
            if tr.kind == TrackKind::Instrument {
                p = p.child(field(t, "Sound", format!("{} (label only)", tr.source)));
            }
            p = p
                .child(hint(stepper(t, "Volume", format!("{} dB", db_text(tr.volume_db)), Command::SetVolume(id, tr.volume_db - 1.0), Command::SetVolume(id, tr.volume_db + 1.0)), s, "Track volume. The same value as the mixer fader"))
                .child(hint(stepper(t, "Pan", pan_text(tr.pan), Command::SetPan(id, tr.pan - 0.1), Command::SetPan(id, tr.pan + 0.1)), s, "Stereo position, from 100L to 100R"))
                .child(row().gap(6.0).child(check(t, "Mute", tr.mute, t.mute, Command::SetMute(id, !tr.mute), "track-mute")).child(check(t, "Solo", tr.solo, t.solo, Command::SetSolo(id, !tr.solo), "track-solo")));
            if tr.kind != TrackKind::Master {
                p = p.child(row().center_y().gap(4.0).h(22.0).child(label("Output", T_LABEL, t.text_dim).w(62.0)).child(icon(&icons::ARROW_RIGHT, 12.0).color(t.text_faint)).child(label(out, T_LABEL, t.text)));
            }
            let mut sw = row().gap(5.0).flex_wrap();
            for i in 0..8 {
                let on = tr.color == i;
                let mut b = row().key(format!("color-{i}")).size(TARGET, TARGET).radius(5.0).center().fill(t.track(i)).role("radio").checked(on).focusable(true).cursor(Cursor::Pointer)
                    .label(TRACK_COLOR_NAMES[i]).help(TRACK_COLOR_NAMES[i]);
                if on {
                    b = b.outline(2.0, 1.0, t.select).child(icon(&icons::CHECK, 13.0).color(t.on_accent_for(t.track(i))));
                }
                sw = sw.child(on_cmd(b, Command::SetTrackColor(id, i)));
            }
            p = p.child(row().center_y().justify_between().child(label("Colour", T_LABEL, t.text_dim)).child(label(TRACK_COLOR_NAMES[tr.color % 8], T_LABEL, t.text))).child(sw);
            let clips = s.project.clips.iter().filter(|c| c.track == id).count();
            if matches!(tr.kind, TrackKind::Audio | TrackKind::Instrument) {
                p = p.child(field(t, "Clips", format!("{clips}")));
            }
        }
        Selection::None => {
            p = p.child(head(t, &icons::FILE_MUSIC, "Project", None))
                .child(field(t, "Name", s.project.name.clone()))
                .child(hint(stepper(t, "Tempo", format!("{:.1} bpm", s.project.tempo), Command::NudgeTempo(-1.0), Command::NudgeTempo(1.0)), s, "Project tempo"))
                .child(field(t, "Length", length_text(s.project.length)))
                .child(field(t, "Tracks", format!("{}", s.project.lanes().count())))
                .child(field(t, "Clips", format!("{}", s.project.clips.len())))
                .child(note_section(s, t))
                .child(el().h(4.0))
                .child(label("Select a clip or a track to see and edit its properties here.", T_LABEL, t.text_dim).wrap());
        }
    }
    p
}

/// The selected note, when the piano roll has exactly one.
fn note_section(s: &App, t: &Theme) -> El {
    let mut e = col().gap(6.0);
    if !s.notes_active() {
        return e;
    }
    let Some(clip) = s.pr_clip else { return e };
    e = e.child(el().h(1.0).my(2.0).fill(t.line));
    if let [id] = s.sel_notes[..] {
        let Some(n) = s.pr_notes().iter().find(|n| n.id == id).copied() else { return e };
        e = e.child(head(t, &icons::KEYBOARD_MUSIC, &format!("Note {}", pitch_name(n.pitch)), None))
            .child(field(t, "Start", bar_beat(n.start)))
            .child(field(t, "Length", length_text(n.len)))
            .child(hint(stepper(t, "Velocity", format!("{}", n.vel), Command::SetVelocity { clip, note: id, vel: n.vel.saturating_sub(5).max(1) }, Command::SetVelocity { clip, note: id, vel: (n.vel + 5).min(127) }), s, "How hard the note is played, 1 to 127"));
    } else {
        e = e.child(head(t, &icons::KEYBOARD_MUSIC, &format!("{} notes selected", s.sel_notes.len()), None))
            .child(label("Delete removes them, Ctrl+D repeats them, arrows nudge.", T_LABEL, t.text_dim).wrap());
    }
    e
}
