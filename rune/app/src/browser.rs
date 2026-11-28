//! The browser: search, type chips, pack covers as a second filter, a result list, and a
//! preview strip. Items go to the timeline by drag, by double-click, or with Enter.

use rune::shape::LineCap;
use rune::{Cursor, Drag, El, Phase, ScrollSpec, Shape, Tx, canvas, col, el, icon, row, text_input};

use crate::command::Command;
use crate::demo::{self, ItemKind, LIBRARY, PACKS};
use crate::icons;
use crate::model::*;
use crate::state::*;
use crate::theme::*;
use crate::ui::{button, cover, hint, item_len, label, on_cmd, on_do, section};

pub const BROWSER_W: f32 = 236.0;

pub fn visible_items(s: &App) -> Vec<usize> {
    (0..LIBRARY.len())
        .filter(|&i| {
            let it = &LIBRARY[i];
            s.filter_kind.is_none_or(|k| it.kind == k) && s.filter_pack.is_none_or(|p| it.pack == p) && demo::item_matches(it, &s.search.text)
        })
        .collect()
}

fn kind_icon(k: ItemKind) -> &'static rune::Icon {
    match k {
        ItemKind::Loop => &icons::REPEAT,
        ItemKind::OneShot => &icons::AUDIO_LINES,
        ItemKind::Midi => &icons::PIANO,
    }
}

fn meta(it: &demo::LibItem) -> String {
    let mut m = item_len(it.beats);
    if let Some(b) = it.bpm {
        m.push_str(&format!(" · {b}"));
    }
    if let Some(k) = it.key {
        m.push_str(&format!(" · {k}"));
    }
    m
}

pub fn view(s: &App, t: &Theme) -> El {
    let field = text_input(&s.search, T_BODY).key("browser-search").single_line().grow(1.0).color(t.text)
        .placeholder("Search sounds, packs, #tags", t.text_faint).caret(t.accent, t.accent.alpha(0.3))
        .label("Search the browser")
        .on_edit(|s: &mut App| &mut s.search)
        .on_input_key(|s: &mut App, ev: &rune::KeyEvent, tx: &mut Tx| match ev.key {
            rune::Key::Escape => {
                if s.search.text.is_empty() { tx.blur() } else { s.search = rune::TextEdit::new(""); s.search.focused = true; }
                true
            }
            rune::Key::Enter | rune::Key::Down => {
                // Hand the keyboard to the list: the first result is highlighted, Enter adds it.
                s.browse_sel = visible_items(s).first().copied();
                tx.blur();
                true
            }
            _ => false,
        });
    let search = row().center_y().gap(7.0).h(30.0).px(8.0).radius(6.0).fill(t.bg).border(1.0, t.line).focus_within_ring(2.0, 0.0, t.accent)
        .child(icon(&icons::SEARCH, 14.0).color(t.text_dim)).child(field);

    let chip = |name: &str, k: Option<ItemKind>| {
        let on = s.filter_kind == k;
        let mut c = row().key(format!("chip-{name}")).center().gap(3.0).h(TARGET).px(7.0).radius(12.0).role("radio").checked(on).focusable(true).cursor(Cursor::Pointer).label(format!("Show {name}"));
        c = if on { c.fill(t.text).child(icon(&icons::CHECK, 11.0).color(t.bg)) } else { c.border(1.0, t.line).hover(t.control_hover) };
        on_do(c.child(label(name, T_LABEL, if on { t.bg } else { t.text_dim })), move |s| s.filter_kind = k)
    };
    let chips = row().gap(4.0).child(chip("All", None)).child(chip("Loops", Some(ItemKind::Loop))).child(chip("Hits", Some(ItemKind::OneShot))).child(chip("MIDI", Some(ItemKind::Midi)));

    let card_w = (BROWSER_W - 20.0 - 8.0) / 2.0;
    let mut packs = row().flex_wrap().gaps(8.0, 8.0);
    for (i, p) in PACKS.iter().enumerate() {
        let on = s.filter_pack == Some(i);
        let count = LIBRARY.iter().filter(|it| it.pack == i).count();
        let mut art = cover(s, t, i, p.color, card_w, 46.0, 5.0);
        if on {
            art = art.child(row().absolute(card_w - 20.0, 4.0).size(16.0, 16.0).radius(8.0).center().fill(t.select).child(icon(&icons::CHECK, 11.0).color(t.bg)));
        }
        let card = col().key(format!("pack-{i}")).w(card_w).gap(3.0).radius(6.0).role("button").checked(on).focusable(true).cursor(Cursor::Pointer)
            .label(format!("Pack {}: {}", p.name, p.blurb)).help(p.blurb)
            .child(if on { art.outline(2.0, 1.0, t.select) } else { art })
            .child(label(p.name, T_LABEL, if on { t.text } else { t.text_dim }).truncate())
            .child(label(format!("{count} items"), T_SMALL, t.text_faint));
        packs = packs.child(on_do(card, move |s| s.filter_pack = if s.filter_pack == Some(i) { None } else { Some(i) }));
    }

    let found = visible_items(s);
    let mut list = col().key("browser-list").grow(1.0).min_h(0.0).scroll(ScrollSpec { stick_bottom: false });
    for &i in &found {
        let it = &LIBRARY[i];
        let on = s.browse_sel == Some(i);
        let mut r = row().key(format!("item-{i}")).center_y().gap(7.0).h(32.0).px(6.0).radius(5.0).cursor(Cursor::Grab)
            .role("option").label(format!("{}, {}, {}", it.name, it.kind.label(), meta(it)))
            .child(cover(s, t, i + 3, PACKS[it.pack].color, 22.0, 22.0, 4.0))
            .child(col().grow(1.0).min_w(0.0)
                .child(label(it.name, T_LABEL, t.text).truncate())
                .child(row().center_y().gap(4.0).child(icon(kind_icon(it.kind), 10.0).color(t.text_faint)).child(label(format!("{} · {}", it.kind.label(), meta(it)), T_SMALL, t.text_faint).truncate())));
        r = if on { r.fill(t.control).border_sides([0.0, 0.0, 0.0, 3.0], t.accent) } else { r.hover(t.bar) };
        list = list.child(r.on_drag(move |s: &mut App, d: &mut Drag, tx: &mut Tx| {
            let [x, y] = d.window_point();
            match d.phase {
                Phase::Down => s.browse_sel = Some(i),
                Phase::Move => {
                    if s.item_drag.is_some() || d.dx.abs() + d.dy.abs() > 5.0 {
                        let target = s.drop_target(i, x, y, s.scroll_y, tx.mods().alt);
                        s.item_drag = Some(ItemDrag { item: i, x, y, target });
                    }
                }
                Phase::Up => {
                    if let Some(drag) = s.item_drag.take() {
                        if let (Some(tg), false) = (drag.target, d.cancelled()) {
                            s.run(Command::AddItem { item: i, track: tg.track, tick: tg.tick });
                        }
                    } else if d.clicks == 2 {
                        s.run(Command::AddSelectedItem);
                    }
                }
            }
            tx.instant();
        }));
    }
    if found.is_empty() {
        list = list.child(col().gap(6.0).padding(10.0)
            .child(label("Nothing matches.", T_BODY, t.text_dim))
            .child(on_do(button(t).child(label("Clear search and filters", T_LABEL, t.text)), |s| {
                s.search = rune::TextEdit::new("");
                s.filter_kind = None;
                s.filter_pack = None;
            })));
    }

    let mut panel = col().key("browser").w(BROWSER_W).h_full().min_h(0.0).padding(10.0).gap(9.0).fill(t.panel).border_sides([0.0, 1.0, 0.0, 0.0], t.line)
        .child(row().center_y().justify_between().child(section(t, "Browser")).child(label(format!("{} of {}", found.len(), LIBRARY.len()), T_NUM_SMALL, t.text_faint).key("browser-count")))
        .child(search).child(chips).child(section(t, "Packs")).child(packs)
        .child(section(t, "Sounds"))
        .child(hint(list, s, "Drag a sound onto a track, double-click it, or press Enter to add it at the playhead"));
    if let Some(i) = s.browse_sel {
        panel = panel.child(preview(s, t, i));
    }
    panel
}

/// The selected item: what it is, an overview drawn from its synthesised peaks (audio only),
/// and the non-drag way to add it. Nothing is auditioned: there is no audio in the prototype.
fn preview(s: &App, t: &Theme, i: usize) -> El {
    let it = &LIBRARY[i];
    let w = BROWSER_W - 20.0 - 16.0;
    let hue = t.track(PACKS[it.pack].color);
    let mut sh = Shape::new();
    if it.kind == ItemKind::Midi {
        let mut next = 0;
        let notes = demo::synth_notes(it.flavor, it.beats, i as u32 * 7 + 3, &mut next);
        let (lo, hi) = notes.iter().fold((127u8, 0u8), |(lo, hi), n| (lo.min(n.pitch), hi.max(n.pitch)));
        let total = (it.beats as i64 * TPB) as f32;
        for n in &notes {
            let y = 3.0 + (1.0 - (n.pitch - lo) as f32 / (hi - lo).max(6) as f32) * 22.0;
            let x = n.start as f32 / total * w;
            sh = sh.line([x, y], [(x + n.len as f32 / total * w - 1.0).max(x + 1.5), y], 2.0, LineCap::Butt, hue);
        }
    } else {
        let peaks = demo::synth_peaks(it.flavor, it.beats, i as u32 * 7 + 3);
        let n = peaks.0.len().max(1);
        let cols = (w / 3.0) as usize;
        for c in 0..cols {
            let (a, b) = (c * n / cols, ((c + 1) * n / cols).max(c * n / cols + 1).min(n));
            let p = peaks.0[a..b].iter().copied().fold(0.0, f32::max);
            if p > 0.02 {
                sh = sh.line([c as f32 * 3.0 + 1.0, 14.0 - p * 13.0], [c as f32 * 3.0 + 1.0, 14.0 + p * 13.0], 2.0, LineCap::Butt, hue);
            }
        }
    }
    col().key("browser-preview").gap(6.0).padding(8.0).radius(6.0).fill(t.bg).border(1.0, t.line)
        .child(row().center_y().gap(8.0)
            .child(cover(s, t, i + 3, PACKS[it.pack].color, 30.0, 30.0, 4.0))
            .child(col().grow(1.0).min_w(0.0).child(label(it.name, T_STRONG, t.text).truncate()).child(label(format!("{} · {}", PACKS[it.pack].name, meta(it)), T_SMALL, t.text_dim).truncate())))
        .child(canvas(sh).size(w, 28.0))
        .child(label("Overview from demo data. Audition is not available: this prototype has no audio.", T_SMALL, t.text_faint).wrap())
        .child(hint(on_cmd(button(t).key("browser-add").border(1.0, t.line)
            .child(icon(&icons::CORNER_DOWN_LEFT, 13.0).color(t.text)).child(label("Add to track", T_LABEL, t.text)).child(label("Enter", T_NUM_SMALL, t.text_faint)), Command::AddSelectedItem),
            s, "Add this sound to the selected track at the playhead; without a matching track it gets a new one"))
}
