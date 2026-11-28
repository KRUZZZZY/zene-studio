//! Design tokens. Every colour the views use comes from here, so a contrast test can cover
//! them (tests.rs) and a light theme is a second table, not a second set of views.

use std::sync::LazyLock;

use rune::{Color, TextStyle};

/// sRGB `0xRRGGBB` to Rune's linear colour.
pub fn rgb(hex: u32) -> Color {
    fn dec(v: u32) -> f32 {
        let c = v as f32 / 255.0;
        if c <= 0.04045 { c / 12.92 } else { ((c + 0.055) / 1.055).powf(2.4) }
    }
    Color { r: dec((hex >> 16) & 0xff), g: dec((hex >> 8) & 0xff), b: dec(hex & 0xff), a: 1.0 }
}

#[derive(Clone, Copy, Debug)]
pub struct Theme {
    pub dark: bool,
    /// Window background and the timeline's lane background.
    pub bg: Color,
    /// Side regions and the dock.
    pub panel: Color,
    /// Bars: menu, transport, tab strips, the ruler.
    pub bar: Color,
    /// Resting controls (buttons, fields) and alternate lanes.
    pub control: Color,
    pub control_hover: Color,
    /// Hairlines between regions.
    pub line: Color,
    /// Grid lines inside editors: bar lines, then beat lines.
    pub grid_bar: Color,
    pub grid_beat: Color,
    pub text: Color,
    pub text_dim: Color,
    pub text_faint: Color,
    /// The single interface accent: focus, the playhead, primary state.
    pub accent: Color,
    /// Text drawn on the accent.
    pub on_accent: Color,
    /// Selection outline on clips and notes (shape, not hue, marks selection).
    pub select: Color,
    pub mute: Color,
    pub solo: Color,
    pub meter: Color,
    pub meter_hot: Color,
    pub scrim: Color,
    /// Embroidery-thread track palette.
    pub tracks: [Color; 8],
}

pub const TRACK_COLOR_NAMES: [&str; 8] = ["Paprika", "Saffron", "Moss", "Teal", "Cornflower", "Plum", "Rose", "Clay"];

pub static DARK: LazyLock<Theme> = LazyLock::new(|| Theme {
    dark: true,
    bg: rgb(0x171412),
    panel: rgb(0x1e1a17),
    bar: rgb(0x231f1b),
    control: rgb(0x2e2823),
    control_hover: rgb(0x3a332c),
    line: rgb(0x3b342d),
    grid_bar: rgb(0x38312a),
    grid_beat: rgb(0x26211d),
    text: rgb(0xece4d9),
    text_dim: rgb(0xb9ad9e),
    text_faint: rgb(0x9f9485),
    accent: rgb(0xf0b040),
    on_accent: rgb(0x1b1611),
    select: rgb(0xfff6e6),
    mute: rgb(0x6fb3e8),
    solo: rgb(0xf0c94a),
    meter: rgb(0x86b85c),
    meter_hot: rgb(0xe8663f),
    scrim: Color { r: 0.0, g: 0.0, b: 0.0, a: 0.45 },
    tracks: [rgb(0xd9523f), rgb(0xe0a030), rgb(0x7fa650), rgb(0x3fa7a0), rgb(0x6a8fe0), rgb(0xa873c4), rgb(0xd86a8e), rgb(0xc08457)],
});

pub static LIGHT: LazyLock<Theme> = LazyLock::new(|| Theme {
    dark: false,
    bg: rgb(0xf6f1e8),
    panel: rgb(0xede6da),
    bar: rgb(0xe6dece),
    control: rgb(0xdad0be),
    control_hover: rgb(0xcfc4b0),
    line: rgb(0xc4b8a3),
    grid_bar: rgb(0xcbc0ac),
    grid_beat: rgb(0xe4dccd),
    text: rgb(0x2a231b),
    text_dim: rgb(0x52483b),
    text_faint: rgb(0x5c5245),
    accent: rgb(0x9a5a00),
    on_accent: rgb(0xfff8ea),
    select: rgb(0x1f1810),
    mute: rgb(0x1f5f96),
    solo: rgb(0x7a5a00),
    meter: rgb(0x3f7a22),
    meter_hot: rgb(0xb5391a),
    scrim: Color { r: 0.0, g: 0.0, b: 0.0, a: 0.3 },
    tracks: [rgb(0xb93a28), rgb(0x8f5a00), rgb(0x4f7a26), rgb(0x1f7f79), rgb(0x3f64c0), rgb(0x8650a6), rgb(0xb8456c), rgb(0x96592c)],
});

pub fn theme(dark: bool) -> Theme {
    if dark { *DARK } else { *LIGHT }
}

impl Theme {
    pub fn track(&self, i: usize) -> Color {
        self.tracks[i % self.tracks.len()]
    }
    /// A clip or strip body: the track hue laid thinly over the lane.
    pub fn track_wash(&self, i: usize, strength: f32) -> Color {
        self.bg.mix(self.track(i), strength)
    }
}

/// WCAG 2.x relative-luminance contrast between two opaque colours.
pub fn contrast(a: Color, b: Color) -> f32 {
    let (la, lb) = (a.luminance(), b.luminance());
    let (hi, lo) = if la > lb { (la, lb) } else { (lb, la) };
    (hi + 0.05) / (lo + 0.05)
}

// Type: one face, four sizes. Readouts use tabular figures so digits do not jitter.
pub const T_SMALL: TextStyle = TextStyle::new(11.0, 500.0);
pub const T_LABEL: TextStyle = TextStyle::new(12.0, 500.0);
pub const T_BODY: TextStyle = TextStyle::new(13.0, 400.0);
pub const T_STRONG: TextStyle = TextStyle::new(13.0, 600.0);
pub const T_SECTION: TextStyle = TextStyle::new(11.0, 650.0).tracking(0.5);
pub const T_NUM: TextStyle = TextStyle::new(12.0, 500.0).features("'tnum' 1");
pub const T_NUM_SMALL: TextStyle = TextStyle::new(11.0, 500.0).features("'tnum' 1");
pub const T_READOUT: TextStyle = TextStyle::new(17.0, 600.0).features("'tnum' 1");

/// Smallest pointer target, WCAG 2.2 SC 2.5.8.
pub const TARGET: f32 = 24.0;

impl Theme {
    /// Text drawn on a strong colour: whichever of the theme's two inks contrasts more.
    pub fn on_accent_for(&self, bg: Color) -> Color {
        let (dark, light) = (rgb(0x15110d), rgb(0xfff8ea));
        if contrast(dark, bg) >= contrast(light, bg) { dark } else { light }
    }
}
