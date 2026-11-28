//! The frame: menu bar, transport, the three regions, the help line, and the overlays
//! (menus, command palette, drag ghost). Also the small widget helpers every region uses.

use rune::{Cursor, Cx, El, Icon, Key, KeyEvent, Tx, col, el, icon, row, text, text_input};

use crate::command::{ACTIONS, Action, Command, menu_title};
use crate::demo;
use crate::icons;
use crate::model::*;
use crate::state::*;
use crate::theme::*;
use crate::{browser, dock, inspector, timeline};

pub const MENU_H: f32 = 28.0;
pub const TRANSPORT_H: f32 = 46.0;
pub const STATUS_H: f32 = 24.0;

pub fn th(s: &App, cx: &Cx<App>) -> Theme {
    theme(s.theme.unwrap_or(cx.system_dark()))
}

// ---- widgets ---------------------------------------------------------------------------

/// Show `what` in the help line while the pointer is over `e`. Moves a live value only.
pub fn hint(e: El, s: &App, what: &'static str) -> El {
    let live = s.live.hint.clone();
    e.on_hover_change_any(move |on, tx| {
        live.set(if on { what } else { IDLE_HINT });
        tx.live_only();
    })
}

/// Run `f` on press. A pointer press hands the keyboard back to the window, so Space keeps
/// meaning play; a keyboard press (Space or Enter on the focused control) keeps focus.
pub fn on_do(e: El, f: impl Fn(&mut App) + 'static) -> El {
    e.on_press_tx(move |s: &mut App, tx: &mut Tx| {
        f(s);
        if !tx.by_keys() {
            tx.blur();
        }
    })
}

pub fn on_cmd(e: El, c: Command) -> El {
    on_do(e, move |s| s.run(c.clone()))
}

pub fn label(s: impl Into<String>, style: rune::TextStyle, c: rune::Color) -> El {
    text(s).font(style).color(c)
}

pub fn button(t: &Theme) -> El {
    row().center().gap(6.0).h(TARGET + 2.0).px(8.0).radius(5.0).fill(t.control).hover(t.control_hover)
        .role("button").focusable(true).cursor(Cursor::Pointer)
}

pub fn icon_button(t: &Theme, ic: &'static Icon, name: &str) -> El {
    button(t).w(28.0).px(0.0).label(name).help(name).child(icon(ic, 16.0).color(t.text))
}

/// A two-state button. On: filled, with the state also carried by `checked` for assistive
/// tech and by the caller's text; off: an outline.
pub fn toggle(t: &Theme, on: bool, on_color: rune::Color) -> El {
    let e = row().center().gap(5.0).h(TARGET).px(7.0).radius(5.0).role("button").checked(on).focusable(true).cursor(Cursor::Pointer);
    if on { e.fill(on_color) } else { e.border(1.0, t.line).hover(t.control_hover) }
}

/// "M" or "S": a letter that inverts when on.
pub fn letter_toggle(t: &Theme, letter: &str, on: bool, on_color: rune::Color, name: &str) -> El {
    toggle(t, on, on_color).w(TARGET).px(0.0).label(name).help(name)
        .child(label(letter, T_SECTION, if on { t.on_accent } else { t.text_dim }))
}

pub fn section(t: &Theme, title: &str) -> El {
    label(title.to_uppercase(), T_SECTION, t.text_faint)
}

/// Label, minus, value, plus: a single-pointer alternative to every drag.
pub fn stepper(t: &Theme, name: &str, value: String, dec: Command, inc: Command) -> El {
    let b = |ic: &'static Icon, what: String, c: Command| {
        let key = format!("step-{}", what.to_lowercase().replace(' ', "-"));
        on_cmd(button(t).key(key).w(TARGET).h(TARGET).px(0.0).label(what.clone()).help(what).child(icon(ic, 13.0).color(t.text)), c)
    };
    row().center_y().gap(4.0).h(TARGET + 2.0)
        .child(label(name, T_LABEL, t.text_dim).w(62.0))
        .child(b(&icons::MINUS, format!("{name} down"), dec))
        .child(row().center().grow(1.0).h(TARGET).radius(4.0).fill(t.bg).border(1.0, t.line).child(label(value, T_NUM, t.text)))
        .child(b(&icons::PLUS, format!("{name} up"), inc))
}

/// A pattern from the art library under a translucent track colour.
pub fn cover(s: &App, t: &Theme, pattern: usize, color: usize, w: f32, h: f32, r: f32) -> El {
    let mut e = el().size(w, h).radius(r).clip_rounded(r).fill(t.track_wash(color, 0.5));
    if let Some(img) = s.art.pattern(pattern) {
        e = e.child(el().absolute(0.0, (h - w) / 2.0).size(w, w).pixels(img.pixels()));
    }
    e.child(el().absolute(0.0, 0.0).size(w, h).fill(t.track(color).alpha(if t.dark { 0.7 } else { 0.6 })))
}

// ---- the frame -------------------------------------------------------------------------

pub fn view(s: &App, cx: &mut Cx<App>) -> El {
    let t = th(s, cx);
    cx.focus_ring(2.0, 1.0, t.accent);
    let mut mid = row().grow(1.0).min_h(0.0).w_full();
    if s.show_browser {
        mid = mid.child(browser::view(s, &t));
    }
    let mut center = col().grow(1.0).min_w(0.0).h_full().min_h(0.0).child(timeline::view(s, &t));
    if s.show_dock {
        center = center.child(dock::view(s, &t));
    }
    mid = mid.child(center);
    if s.show_inspector {
        mid = mid.child(inspector::view(s, &t));
    }
    let mut root = col().w_full().h_full().fill(t.bg)
        .child(menu_bar(s, &t)).child(transport(s, &t)).child(mid).child(status(s, &t));
    if let Some(m) = s.menu {
        root = root.child(scrim(None)).child(dropdown(s, &t, m, cx));
    }
    if let Some(d) = &s.item_drag {
        root = root.child(drag_ghost(s, &t, d));
    }
    if let Some(p) = &s.palette {
        root = root.child(scrim(Some(t.scrim))).child(palette(s, &t, p));
    }
    root
}

/// Everything under an overlay: a press here closes it.
fn scrim(fill: Option<rune::Color>) -> El {
    let e = el().key("scrim").pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0)).hit()
        .on_press(|s: &mut App| s.run(Command::CloseOverlays));
    match fill {
        Some(c) => e.fill(c),
        None => e,
    }
}

const MENUS: [(MenuId, &str); 5] = [(MenuId::File, "menu-file"), (MenuId::Edit, "menu-edit"), (MenuId::View, "menu-view"), (MenuId::Transport, "menu-transport"), (MenuId::Help, "menu-help")];

fn menu_bar(s: &App, t: &Theme) -> El {
    let mut bar = row().center_y().w_full().h(MENU_H).px(8.0).gap(2.0).fill(t.bar).border_sides([0.0, 0.0, 1.0, 0.0], t.line)
        .child(row().key("brand").center_y().gap(6.0).mr(8.0).label("Zene Studio, Rune prototype").help("Zene Studio: Rune UI prototype (simulated playback)")
            .child(icon(&crate::brand::SONGBIRD, 18.0).color(t.accent))
            .child(label("Zene", T_STRONG, t.text)));
    for (m, key) in MENUS {
        let open = s.menu == Some(m);
        let b = row().key(key).center().h(22.0).px(8.0).radius(4.0).role("button").expanded(open).focusable(true).cursor(Cursor::Pointer)
            .hover(t.control_hover).child(label(menu_title(m), T_BODY, t.text));
        bar = bar.child(on_cmd(if open { b.fill(t.control) } else { b }, Command::OpenMenu(m)));
    }
    bar = bar.child(row().grow(1.0).center().child(label(s.project.name.clone(), T_LABEL, t.text_dim)));
    let panel = |ic: &'static Icon, name: &str, on: bool, c: Command, what: &'static str| {
        let b = toggle(t, on, t.control).h(22.0).label(name)
            .child(icon(ic, 14.0).color(if on { t.text } else { t.text_faint }))
            .child(label(name, T_LABEL, if on { t.text } else { t.text_faint }));
        let b = if on { b.border(1.0, t.text_dim) } else { b };
        hint(on_cmd(b, c), s, what)
    };
    bar.child(panel(&icons::PANEL_LEFT, "Browser", s.show_browser, Command::ToggleBrowser, "Show or hide the browser (B)"))
        .child(panel(&icons::SLIDERS_VERTICAL, "Mixer", s.show_dock && s.dock_tab == DockTab::Mixer, Command::ShowMixer, "Show the mixer in the dock (X)"))
        .child(panel(&icons::PIANO, "Piano Roll", s.show_dock && s.dock_tab == DockTab::PianoRoll, Command::ShowPianoRoll, "Show the piano roll in the dock (P)"))
        .child(panel(&icons::PANEL_RIGHT, "Inspector", s.show_inspector, Command::ToggleInspector, "Show or hide the inspector (I)"))
}

fn readout(t: &Theme, live: &rune::LiveText, placeholder: &str, w: f32, caption: &str) -> El {
    col().center().w(w).h(34.0).radius(5.0).fill(t.bg).border(1.0, t.line)
        .child(text(placeholder).live_text(live).font(T_READOUT).color(t.text))
        .child(label(caption, T_SMALL, t.text_faint))
}

fn transport(s: &App, t: &Theme) -> El {
    let playing = s.playing();
    let play = button(t).key("play").w(84.0).h(32.0).label(if playing { "Stop" } else { "Play" })
        .child(icon(if playing { &icons::SQUARE } else { &icons::PLAY }, 16.0).color(if playing { t.on_accent } else { t.text }))
        .child(label(if playing { "Stop" } else { "Play" }, T_STRONG, if playing { t.on_accent } else { t.text }));
    let play = if playing { play.fill(t.accent).hover(t.accent) } else { play };
    let tempo = row().center_y().gap(4.0)
        .child(on_cmd(icon_button(t, &icons::MINUS, "Tempo down").key("tempo-down").w(TARGET).h(TARGET), Command::NudgeTempo(-1.0)))
        .child(col().center().w(64.0).h(34.0).radius(5.0).fill(t.bg).border(1.0, t.line)
            .child(label(format!("{:.1}", s.project.tempo), T_READOUT, t.text).key("tempo"))
            .child(label("bpm", T_SMALL, t.text_faint)))
        .child(on_cmd(icon_button(t, &icons::PLUS, "Tempo up").key("tempo-up").w(TARGET).h(TARGET), Command::NudgeTempo(1.0)));
    // Snap: the state is written out ("Snap 1/4" or "Snap off"), not left to colour.
    let snap_text = if s.snap_on { format!("Snap {}", s.snap.label()) } else { "Snap off".to_string() };
    let snap = row().center_y().gap(2.0)
        .child(hint(on_cmd(toggle(t, s.snap_on, t.control).key("snap").h(30.0).w(104.0).label(snap_text.clone())
            .child(icon(&icons::MAGNET, 15.0).color(if s.snap_on { t.accent } else { t.text_faint }))
            .child(label(snap_text, T_LABEL, if s.snap_on { t.text } else { t.text_faint })), Command::ToggleSnap),
            s, "Snap to grid (G). Hold Alt while dragging to bypass it once"))
        .child(hint(on_cmd(button(t).key("snap-menu").w(22.0).h(30.0).px(0.0).label("Grid size").expanded(s.menu == Some(MenuId::Snap))
            .child(icon(&icons::CHEVRON_DOWN, 14.0).color(t.text_dim)), Command::OpenMenu(MenuId::Snap)), s, "Choose the grid size"));
    let undo_name = s.undo.last().map_or("Nothing to undo".to_string(), |u| format!("Undo: {}", u.1));
    let redo_name = s.redo.last().map_or("Nothing to redo".to_string(), |u| format!("Redo: {}", u.1));
    let hist = |ic: &'static Icon, name: String, live: bool, c: Command, key: &str| {
        let b = icon_button(t, ic, &name).key(key).h(30.0).disabled(!live);
        if live { on_cmd(b, c) } else { b.opacity(0.4) }
    };
    row().center_y().w_full().h(TRANSPORT_H).px(10.0).gap(10.0).fill(t.bar).border_sides([0.0, 0.0, 1.0, 0.0], t.line)
        .child(hint(on_cmd(icon_button(t, &icons::SKIP_BACK, "Return to start").key("rewind").h(32.0), Command::Rewind), s, "Return to bar 1 (Home)"))
        .child(hint(on_cmd(play, Command::TogglePlay), s, "Play or stop simulated playback (Space). No audio is produced"))
        .child(readout(t, &s.live.position, "000.0.000", 104.0, "bar . beat . tick"))
        .child(readout(t, &s.live.clock, "00:00.0", 76.0, "min : sec"))
        .child(hint(tempo, s, "Tempo in beats per minute"))
        .child(col().center().w(40.0).h(34.0).child(label("4/4", T_READOUT, t.text_dim)).child(label("time", T_SMALL, t.text_faint)))
        .child(snap)
        .child(row().gap(4.0)
            .child(hist(&icons::UNDO_2, undo_name, !s.undo.is_empty(), Command::Undo, "undo"))
            .child(hist(&icons::REDO_2, redo_name, !s.redo.is_empty(), Command::Redo, "redo")))
        .child(el().grow(1.0))
        .child(hint(on_cmd(button(t).key("search-commands").h(30.0).border(1.0, t.line).fill(t.bg)
            .child(icon(&icons::SEARCH, 14.0).color(t.text_dim))
            .child(label("Search commands", T_LABEL, t.text_dim))
            .child(label("Ctrl+K", T_NUM_SMALL, t.text_faint)), Command::OpenPalette), s, "Find any command by name and see where it lives (Ctrl+K)"))
}

fn status(s: &App, t: &Theme) -> El {
    row().center_y().w_full().h(STATUS_H).px(10.0).gap(10.0).fill(t.bar).border_sides([1.0, 0.0, 0.0, 0.0], t.line)
        .child(icon(&icons::INFO, 13.0).color(t.text_faint))
        .child(text(IDLE_HINT).live_text(&s.live.hint).font(T_LABEL).color(t.text_dim).w(560.0).h(16.0))
        .child(row().grow(1.0).min_w(0.0).justify_end().child(label(s.status.clone(), T_LABEL, t.text).key("status").truncate()))
        .child(row().center_y().gap(5.0).h(18.0).px(6.0).radius(4.0).border(1.0, t.line)
            .help(s.engine.describe())
            .child(icon(&icons::VOLUME_X, 13.0).color(t.text_dim))
            .child(label(s.engine.label(), T_SMALL, t.text_dim)))
}

// ---- menus -----------------------------------------------------------------------------

/// Whether a command's state is on, for the ones that are toggles.
fn checked(s: &App, c: &Command) -> Option<bool> {
    Some(match c {
        Command::ToggleSnap => s.snap_on,
        Command::SetSnap(g) => s.snap == *g,
        Command::ToggleBrowser => s.show_browser,
        Command::ToggleInspector => s.show_inspector,
        Command::ShowMixer => s.show_dock && s.dock_tab == DockTab::Mixer,
        Command::ShowPianoRoll => s.show_dock && s.dock_tab == DockTab::PianoRoll,
        Command::SetDensity(d) => s.density == *d,
        _ => return None,
    })
}

fn available(s: &App, c: &Command) -> bool {
    match c {
        Command::Undo => !s.undo.is_empty(),
        Command::Redo => !s.redo.is_empty(),
        Command::DuplicateSelection | Command::DeleteSelection | Command::NudgeSelection(_) => s.notes_active() || !s.sel.clips().is_empty(),
        Command::SplitAtPlayhead | Command::ToggleClipMute => !s.sel.clips().is_empty(),
        Command::ToggleMute | Command::ToggleSolo => s.focus_track().is_some(),
        Command::AddSelectedItem => s.browse_sel.is_some(),
        _ => true,
    }
}

fn action_title(s: &App, a: &Action) -> String {
    match (&a.command, s.undo.last(), s.redo.last()) {
        (Command::Undo, Some(u), _) => format!("Undo {}", u.1),
        (Command::Redo, _, Some(r)) => format!("Redo {}", r.1),
        _ => a.title.to_string(),
    }
}

fn menu_row(s: &App, t: &Theme, a: &'static Action) -> El {
    let live = available(s, &a.command);
    let tick = match checked(s, &a.command) {
        Some(true) => icon(&icons::CHECK, 13.0).color(t.text),
        _ => el().size(13.0, 13.0),
    };
    let c = if live { t.text } else { t.text_faint };
    let mut r = row().key(format!("mi-{}", a.id)).center_y().gap(8.0).h(26.0).px(8.0).radius(4.0).role("menuitem").disabled(!live)
        .child(tick).child(label(action_title(s, a), T_BODY, c).grow(1.0));
    if let Some(ch) = a.chord {
        r = r.child(label(ch.label(), T_NUM_SMALL, t.text_faint));
    }
    if let Some(on) = checked(s, &a.command) {
        r = r.checked(on);
    }
    if !live {
        return r;
    }
    let cmd = a.command.clone();
    hint(r.hover(t.control_hover).focusable(true).cursor(Cursor::Pointer), s, a.help).on_press_tx(move |s: &mut App, tx: &mut Tx| {
        s.menu = None;
        s.run(cmd.clone());
        tx.blur();
    })
}

fn dropdown(s: &App, t: &Theme, m: MenuId, cx: &Cx<App>) -> El {
    let key = MENUS.iter().find(|(id, _)| *id == m).map_or("snap-menu", |(_, k)| *k);
    let at = cx.layout(key).unwrap_or([60.0, 0.0, 40.0, MENU_H]);
    let mut list = col().key("menu").absolute(at[0].min(1000.0), at[1] + at[3] + 3.0).w(if m == MenuId::Snap { 190.0 } else { 300.0 }).padding(4.0).radius(7.0)
        .fill(t.panel).border(1.0, t.line).shadow(6.0, 18.0, rune::Color::BLACK.alpha(0.35)).role("menu");
    for a in ACTIONS.iter().filter(|a| a.menu == m) {
        if a.divider {
            list = list.child(el().h(1.0).my(4.0).fill(t.line));
        }
        list = list.child(menu_row(s, t, a));
    }
    if m == MenuId::Edit {
        list = list.child(label("GRID SIZE", T_SECTION, t.text_faint).px(8.0).pt(6.0).pb(2.0));
        for a in ACTIONS.iter().filter(|a| a.menu == MenuId::Snap) {
            list = list.child(menu_row(s, t, a));
        }
    }
    list
}

// ---- command palette -------------------------------------------------------------------

pub fn palette_matches(query: &str) -> Vec<&'static Action> {
    let words: Vec<String> = query.split_whitespace().map(|w| w.to_lowercase()).collect();
    ACTIONS.iter().filter(|a| {
        let hay = format!("{} {} {} {}", a.title, menu_title(a.menu), a.id, a.help).to_lowercase();
        words.iter().all(|w| hay.contains(w))
    }).collect()
}

pub fn palette_key(s: &mut App, ev: &KeyEvent, _tx: &mut Tx) -> bool {
    let Some(p) = &mut s.palette else { return false };
    let found = palette_matches(&p.query.text);
    match ev.key {
        Key::Down if !found.is_empty() => p.picked = (p.picked + 1) % found.len(),
        Key::Up if !found.is_empty() => p.picked = (p.picked + found.len() - 1) % found.len(),
        Key::Escape => s.palette = None,
        Key::Enter => {
            let pick = found.get(p.picked.min(found.len().saturating_sub(1))).map(|a| a.command.clone());
            s.palette = None;
            if let Some(c) = pick {
                s.run(c);
            }
        }
        _ => return false,
    }
    true
}

fn palette(s: &App, t: &Theme, p: &Palette) -> El {
    let found = palette_matches(&p.query.text);
    let picked = p.picked.min(found.len().saturating_sub(1));
    let field = text_input(&p.query, T_BODY).key("palette-query").single_line().grow(1.0).color(t.text)
        .placeholder("Type a command name, e.g. \"mute\" or \"zoom\"", t.text_faint)
        .caret(t.accent, t.accent.alpha(0.3))
        .on_edit(|s: &mut App| match s.palette {
            Some(ref mut p) => &mut p.query,
            None => &mut s.scratch,
        })
        .on_input_change(|s: &mut App, _tx: &mut Tx| {
            if let Some(p) = &mut s.palette {
                p.picked = 0;
            }
        })
        .on_input_key(palette_key);
    let mut list = col().gap(1.0);
    for (i, a) in found.iter().take(9).enumerate() {
        let on = i == picked;
        let live = available(s, &a.command);
        let cmd = a.command.clone();
        let mut r = row().key(format!("pal-{}", a.id)).center_y().gap(10.0).h(34.0).px(10.0).radius(5.0).cursor(Cursor::Pointer)
            .child(col().grow(1.0).min_w(0.0)
                .child(label(action_title(s, a), T_STRONG, if live { t.text } else { t.text_faint }))
                .child(label(format!("{} > {}", menu_title(a.menu), a.title), T_SMALL, t.text_faint)));
        if let Some(ch) = a.chord {
            r = r.child(row().center().h(20.0).px(6.0).radius(4.0).border(1.0, t.line).child(label(ch.label(), T_NUM_SMALL, t.text_dim)));
        }
        r = if on { r.fill(t.control).border(1.0, t.accent) } else { r.hover(t.control_hover) };
        list = list.child(r.on_press(move |s: &mut App| {
            s.palette = None;
            s.run(cmd.clone());
        }));
    }
    if found.is_empty() {
        list = list.child(label("No command matches. Try a shorter word.", T_BODY, t.text_dim).padding(10.0));
    }
    row().key("palette").pin(Some(0.0), Some(96.0), Some(0.0), None).justify_center().child(
        col().w(520.0).padding(8.0).gap(6.0).radius(9.0).fill(t.panel).border(1.0, t.line).shadow(10.0, 30.0, rune::Color::BLACK.alpha(0.4)).role("dialog").label("Search commands")
            .child(row().center_y().gap(8.0).h(36.0).px(10.0).radius(6.0).fill(t.bg).border(1.0, t.line)
                .child(icon(&icons::SEARCH, 15.0).color(t.text_dim)).child(field))
            .child(list)
            .child(label("Up/Down to choose, Enter to run, Esc to close. Each row shows the command's menu and key.", T_SMALL, t.text_faint).px(4.0)),
    )
}

// ---- drag ghost ------------------------------------------------------------------------

fn drag_ghost(s: &App, t: &Theme, d: &ItemDrag) -> El {
    let it = &demo::LIBRARY[d.item];
    let midi = it.kind == demo::ItemKind::Midi;
    let (ic, msg, ok): (&'static Icon, String, bool) = match d.target {
        Some(DropTarget { track: Some(id), tick }) => (&icons::CORNER_DOWN_LEFT, format!("Drop on {} at {}", s.project.track(id).map_or("track", |t| t.name.as_str()), bar_beat(tick)), true),
        Some(DropTarget { track: None, tick }) => (&icons::PLUS, format!("Drop for a new track at {}", bar_beat(tick)), true),
        None if d.x >= s.lanes_rect[0] && d.y >= s.lanes_rect[1] => (&icons::X, if midi { "MIDI goes on instrument tracks".into() } else { "Audio goes on audio tracks".into() }, false),
        None => (&icons::ARROW_RIGHT, "Drag onto a track".into(), false),
    };
    col().key("drag-ghost").absolute(d.x + 14.0, d.y + 12.0).inert().padding(7.0).gap(3.0).radius(6.0)
        .fill(t.panel).border(1.0, if ok { t.accent } else { t.line }).shadow(4.0, 12.0, rune::Color::BLACK.alpha(0.35))
        .child(label(it.name, T_STRONG, t.text))
        .child(row().center_y().gap(5.0).child(icon(ic, 12.0).color(if ok { t.accent } else { t.text_dim })).child(label(msg, T_SMALL, t.text_dim)))
}

/// Length in bars, for lists ("4 bars", "1 beat").
pub fn item_len(beats: usize) -> String {
    length_text(beats as i64 * TPB)
}
