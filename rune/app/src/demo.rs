//! Deterministic demo content: one project and a small browser library. No audio exists
//! anywhere in this prototype; "peaks" are synthesised envelopes so audio clips have a
//! believable overview to draw and the simulated meters have something to follow.

use std::sync::Arc;

use crate::model::*;

/// What a synthesised overview should look like.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Flavor {
    Kit,
    Hats,
    Shaker,
    Bass,
    Keys,
    Pad,
    Vocal,
    Riser,
    Impact,
}

struct Rng(u32);

impl Rng {
    fn next(&mut self) -> f32 {
        // xorshift32: the same sequence on every machine and run.
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 17;
        self.0 ^= self.0 << 5;
        (self.0 >> 8) as f32 / (1u32 << 24) as f32
    }
}

pub fn synth_peaks(flavor: Flavor, beats: usize, seed: u32) -> Peaks {
    let n = beats * PEAKS_PER_BEAT;
    let mut rng = Rng(seed.wrapping_mul(2654435761).max(1));
    let mut out = vec![0.0f32; n];
    let ppb = PEAKS_PER_BEAT as f32;
    // A hit at `beat` with a decay time in beats.
    let mut hit = |out: &mut [f32], beat: f32, level: f32, decay: f32| {
        let start = (beat * ppb) as usize;
        let len = (decay * ppb * 4.0) as usize;
        for i in 0..len {
            if let Some(v) = out.get_mut(start + i) {
                *v = v.max(level * (-(i as f32) / (decay * ppb)).exp());
            }
        }
    };
    match flavor {
        Flavor::Kit => {
            for b in 0..beats {
                let b4 = b % 4;
                if b4 == 0 || b4 == 2 { hit(&mut out, b as f32, 0.95, 0.16); }
                if b4 == 1 || b4 == 3 { hit(&mut out, b as f32, 0.8, 0.22); }
                if b % 8 == 7 { hit(&mut out, b as f32 + 0.75, 0.7, 0.1); }
                for e in 0..2 { hit(&mut out, b as f32 + e as f32 * 0.5, 0.28 + rng.next() * 0.1, 0.05); }
            }
        }
        Flavor::Hats => {
            for i in 0..beats * 4 {
                let accent = if i % 4 == 2 { 0.55 } else { 0.3 };
                hit(&mut out, i as f32 * 0.25, accent + rng.next() * 0.1, 0.04);
            }
        }
        Flavor::Shaker => {
            for i in 0..beats * 4 {
                let accent = if i % 2 == 0 { 0.42 } else { 0.26 };
                hit(&mut out, i as f32 * 0.25, accent + rng.next() * 0.08, 0.07);
            }
        }
        Flavor::Bass => {
            for i in 0..beats * 2 {
                if rng.next() < 0.78 { hit(&mut out, i as f32 * 0.5, 0.62 + rng.next() * 0.2, 0.3); }
            }
        }
        Flavor::Keys => {
            for b in (0..beats).step_by(2) {
                hit(&mut out, b as f32, 0.6 + rng.next() * 0.15, 0.7);
                if rng.next() < 0.5 { hit(&mut out, b as f32 + 1.5, 0.4, 0.3); }
            }
        }
        Flavor::Pad => {
            for (i, v) in out.iter_mut().enumerate() {
                let t = i as f32 / ppb;
                *v = 0.34 + 0.16 * (t * 0.39).sin() + 0.06 * (t * 2.1).sin();
            }
        }
        Flavor::Vocal => {
            let mut b = 0.0f32;
            while b < beats as f32 {
                let phrase = 1.5 + rng.next() * 2.5;
                let mut s = b;
                while s < (b + phrase).min(beats as f32) {
                    let syl = 0.25 + rng.next() * 0.5;
                    hit(&mut out, s, 0.45 + rng.next() * 0.4, syl * 0.45);
                    s += syl;
                }
                b += phrase + 0.5 + rng.next() * 1.5;
            }
        }
        Flavor::Riser => {
            for (i, v) in out.iter_mut().enumerate() {
                let t = i as f32 / n.max(1) as f32;
                *v = (t * t) * (0.8 + rng.next() * 0.15);
            }
        }
        Flavor::Impact => hit(&mut out, 0.0, 1.0, 0.9),
    }
    // A little grain so no overview is a clean geometric shape.
    for v in &mut out {
        *v = (*v * (0.9 + rng.next() * 0.1)).clamp(0.0, 1.0);
    }
    Peaks(Arc::from(out))
}

/// Am, F, C, G: one chord a bar, as (root pitch class offset from A2, third, fifth).
const CHORDS: [[u8; 3]; 4] = [[57, 60, 64], [53, 57, 60], [48, 52, 55], [55, 59, 62]];

pub fn synth_notes(flavor: Flavor, beats: usize, seed: u32, next: &mut u32) -> Vec<Note> {
    let mut rng = Rng(seed.wrapping_mul(747796405).max(1));
    let mut notes = Vec::new();
    let mut push = |start: i64, len: i64, pitch: u8, vel: u8| {
        *next += 1;
        notes.push(Note { id: *next, start, len, pitch, vel });
    };
    let bars = beats.div_ceil(4);
    for bar in 0..bars {
        let chord = CHORDS[bar % 4];
        let at = bar as i64 * BAR;
        match flavor {
            Flavor::Bass => {
                let root = chord[0] - 24;
                for e in 0..8 {
                    let r = rng.next();
                    if e == 0 || r < 0.62 {
                        let pitch = if e == 6 && r < 0.3 { root + 12 } else if e == 7 && r < 0.2 { root + 7 } else { root };
                        push(at + e * TPB / 2, TPB / 2 - 40, pitch, (88.0 + rng.next() * 30.0) as u8);
                    }
                }
            }
            Flavor::Pad => {
                push(at, BAR, chord[0], 62);
                push(at, BAR, chord[2] + 12, 54);
            }
            _ => {
                for (i, p) in chord.iter().enumerate() {
                    push(at, TPB * 2 - 30, p + 12, 78 + (i as u8) * 6);
                }
                let top = chord[2] + 12;
                push(at + TPB * 2, TPB - 20, top + 2, 70);
                push(at + TPB * 3, TPB / 2, top, 84);
                if rng.next() < 0.6 { push(at + TPB * 3 + TPB / 2, TPB / 2 - 20, top - 3, 66); }
            }
        }
    }
    notes
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ItemKind {
    Loop,
    OneShot,
    Midi,
}

impl ItemKind {
    pub fn label(self) -> &'static str {
        match self {
            ItemKind::Loop => "Loop",
            ItemKind::OneShot => "One-shot",
            ItemKind::Midi => "MIDI",
        }
    }
}

pub struct Pack {
    pub name: &'static str,
    pub blurb: &'static str,
    /// Track-palette index used to tint the cover.
    pub color: usize,
}

pub const PACKS: [Pack; 4] = [
    Pack { name: "Night Transit", blurb: "Dry kits and late-train percussion", color: 0 },
    Pack { name: "Glass Keys", blurb: "Felt pianos and bell tones", color: 4 },
    Pack { name: "Analog Forms", blurb: "Mono basses and square leads", color: 1 },
    Pack { name: "Low Tide", blurb: "Pads, voices and long tails", color: 3 },
];

pub struct LibItem {
    pub name: &'static str,
    pub pack: usize,
    pub kind: ItemKind,
    pub flavor: Flavor,
    pub beats: usize,
    pub bpm: Option<u32>,
    pub key: Option<&'static str>,
    pub tags: &'static [&'static str],
}

const fn item(
    name: &'static str, pack: usize, kind: ItemKind, flavor: Flavor, beats: usize, bpm: Option<u32>,
    key: Option<&'static str>, tags: &'static [&'static str],
) -> LibItem {
    LibItem { name, pack, kind, flavor, beats, bpm, key, tags }
}

use Flavor as F;
use ItemKind as K;

pub const LIBRARY: [LibItem; 26] = [
    item("Transit Kit A", 0, K::Loop, F::Kit, 16, Some(108), None, &["drums", "kit", "dry"]),
    item("Transit Kit B", 0, K::Loop, F::Kit, 16, Some(108), None, &["drums", "kit", "fill"]),
    item("Platform Hats", 0, K::Loop, F::Hats, 8, Some(108), None, &["drums", "hats"]),
    item("Shaker 16ths", 0, K::Loop, F::Shaker, 8, Some(108), None, &["perc", "shaker"]),
    item("Ticket Clap", 0, K::OneShot, F::Impact, 1, None, None, &["drums", "clap"]),
    item("Carriage Kick", 0, K::OneShot, F::Impact, 1, None, None, &["drums", "kick"]),
    item("Last Train Fill", 0, K::Loop, F::Kit, 4, Some(108), None, &["drums", "fill"]),
    item("Felt Chords Am", 1, K::Midi, F::Keys, 16, None, Some("A min"), &["keys", "chords"]),
    item("Felt Chords Wide", 1, K::Midi, F::Keys, 32, None, Some("A min"), &["keys", "chords"]),
    item("Bell Motif", 1, K::Loop, F::Keys, 8, Some(108), Some("A min"), &["keys", "bell", "melody"]),
    item("Music Box Run", 1, K::Loop, F::Keys, 16, Some(96), Some("C maj"), &["keys", "melody"]),
    item("Glass Stab", 1, K::OneShot, F::Impact, 1, None, Some("A"), &["keys", "stab"]),
    item("Upright Loop", 1, K::Loop, F::Keys, 16, Some(108), Some("A min"), &["keys", "piano"]),
    item("Sub Pluck Line", 2, K::Midi, F::Bass, 16, None, Some("A min"), &["bass", "pluck"]),
    item("Octave Walk", 2, K::Midi, F::Bass, 8, None, Some("A min"), &["bass"]),
    item("Square Bass 108", 2, K::Loop, F::Bass, 16, Some(108), Some("A min"), &["bass", "square"]),
    item("Rubber Bass", 2, K::Loop, F::Bass, 8, Some(100), Some("E min"), &["bass"]),
    item("Saw Riser 2 bars", 2, K::Loop, F::Riser, 8, Some(108), None, &["fx", "riser"]),
    item("Mono Lead Phrase", 2, K::Loop, F::Vocal, 16, Some(108), Some("A min"), &["lead", "synth"]),
    item("Slow Glass Pad", 3, K::Midi, F::Pad, 32, None, Some("A min"), &["pad", "chords"]),
    item("Harbour Pad", 3, K::Loop, F::Pad, 32, Some(108), Some("A min"), &["pad", "texture"]),
    item("Tide Vox Ahh", 3, K::Loop, F::Vocal, 16, Some(108), Some("A min"), &["vocal", "ahh"]),
    item("Shoreline Hum", 3, K::Loop, F::Vocal, 8, Some(90), Some("D min"), &["vocal", "hum"]),
    item("Deep Impact", 3, K::OneShot, F::Impact, 4, None, None, &["fx", "impact"]),
    item("Breath In", 3, K::OneShot, F::Riser, 2, None, None, &["fx", "vocal"]),
    item("Wash Out", 3, K::Loop, F::Riser, 16, Some(108), None, &["fx", "riser", "texture"]),
];

/// Case-insensitive match of every search word against name, pack, type and tags.
/// A word starting with `#` must equal a tag.
pub fn item_matches(it: &LibItem, query: &str) -> bool {
    query.split_whitespace().all(|w| {
        let w = w.to_lowercase();
        if let Some(tag) = w.strip_prefix('#') {
            return tag.is_empty() || it.tags.iter().any(|t| t.starts_with(tag));
        }
        it.name.to_lowercase().contains(&w)
            || PACKS[it.pack].name.to_lowercase().contains(&w)
            || it.kind.label().to_lowercase().contains(&w)
            || it.tags.iter().any(|t| t.contains(&w))
            || it.key.is_some_and(|k| k.to_lowercase().contains(&w))
    })
}

pub const T_DRUMS: TrackId = 1;
pub const T_PERC: TrackId = 2;
pub const T_BASS: TrackId = 3;
pub const T_KEYS: TrackId = 4;
pub const T_PAD: TrackId = 5;
pub const T_VOX: TrackId = 6;
pub const T_DBL: TrackId = 7;
pub const T_FX: TrackId = 8;
pub const T_DRUM_BUS: TrackId = 9;
pub const T_MASTER: TrackId = 10;

pub fn project() -> Project {
    let track = |id, name: &str, kind, color, volume_db, pan, output, source: &str| Track {
        id, name: name.into(), kind, color, volume_db, pan, mute: false, solo: false, output, source: source.into(),
    };
    use TrackKind::*;
    let tracks = vec![
        track(T_DRUMS, "Drums", Audio, 0, -4.0, 0.0, Some(T_DRUM_BUS), ""),
        track(T_PERC, "Perc", Audio, 7, -9.0, 0.2, Some(T_DRUM_BUS), ""),
        track(T_BASS, "Bass", Instrument, 1, -6.0, 0.0, None, "Sub Pluck"),
        track(T_KEYS, "Glass Keys", Instrument, 4, -8.0, -0.15, None, "Felt Piano"),
        track(T_PAD, "Pad", Instrument, 3, -12.0, 0.0, None, "Slow Glass"),
        track(T_VOX, "Lead Vox", Audio, 6, -5.0, 0.0, None, ""),
        track(T_DBL, "Vox Double", Audio, 5, -11.0, 0.3, None, ""),
        track(T_FX, "FX", Audio, 2, -14.0, 0.0, None, ""),
        track(T_DRUM_BUS, "Drum Bus", Bus, 0, -2.0, 0.0, None, ""),
        track(T_MASTER, "Master", Master, 7, 0.0, 0.0, None, ""),
    ];
    let mut p = Project { name: "Night Transit (demo)".into(), tempo: 108.0, tracks, clips: Vec::new(), length: 32 * BAR, next_id: 100 };
    // Bars are 1-based here, as on the ruler.
    let audio = |p: &mut Project, t, name: &str, bar: i64, bars: i64, f, seed| {
        let id = p.fresh_id();
        let peaks = synth_peaks(f, (bars * BEATS_PER_BAR) as usize, seed);
        p.clips.push(Clip { id, track: t, start: (bar - 1) * BAR, len: bars * BAR, name: name.into(), gain_db: 0.0, muted: false, kind: ClipKind::Audio { peaks, offset: 0 } });
    };
    let midi = |p: &mut Project, t, name: &str, bar: i64, bars: i64, f, seed| {
        let id = p.fresh_id();
        let mut next = p.next_id;
        let notes = synth_notes(f, (bars * BEATS_PER_BAR) as usize, seed, &mut next);
        p.next_id = next;
        p.clips.push(Clip { id, track: t, start: (bar - 1) * BAR, len: bars * BAR, name: name.into(), gain_db: 0.0, muted: false, kind: ClipKind::Midi { notes } });
    };
    for (bar, name, seed) in [(5, "Transit Kit A", 11), (9, "Transit Kit A", 11), (13, "Transit Kit B", 12), (17, "Transit Kit B", 13), (25, "Transit Kit B", 12), (29, "Transit Kit B", 14)] {
        audio(&mut p, T_DRUMS, name, bar, 4, F::Kit, seed);
    }
    audio(&mut p, T_PERC, "Platform Hats", 1, 4, F::Hats, 21);
    audio(&mut p, T_PERC, "Shaker 16ths", 5, 8, F::Shaker, 22);
    audio(&mut p, T_PERC, "Shaker 16ths", 13, 8, F::Shaker, 23);
    audio(&mut p, T_PERC, "Platform Hats", 21, 4, F::Hats, 24);
    audio(&mut p, T_PERC, "Shaker 16ths", 25, 8, F::Shaker, 22);
    for (bar, bars, name, seed) in [(5, 4, "Bass verse", 31), (9, 4, "Bass verse", 31), (13, 8, "Bass chorus", 32), (25, 8, "Bass chorus", 32)] {
        midi(&mut p, T_BASS, name, bar, bars, F::Bass, seed);
    }
    for (bar, bars, name, seed) in [(1, 4, "Keys intro", 41), (5, 8, "Keys verse", 42), (13, 8, "Keys chorus", 43), (21, 4, "Keys bridge", 44), (25, 8, "Keys chorus", 43)] {
        midi(&mut p, T_KEYS, name, bar, bars, F::Keys, seed);
    }
    for (bar, bars, seed) in [(1, 8, 51), (13, 12, 52), (25, 8, 51)] {
        midi(&mut p, T_PAD, "Slow Glass", bar, bars, F::Pad, seed);
    }
    for (bar, bars, name, seed) in [(5, 8, "Verse 1 (take 3)", 61), (13, 8, "Chorus lead", 62), (21, 4, "Bridge ad-lib", 63), (25, 8, "Chorus lead", 62)] {
        audio(&mut p, T_VOX, name, bar, bars, F::Vocal, seed);
    }
    audio(&mut p, T_DBL, "Chorus double", 13, 8, F::Vocal, 71);
    audio(&mut p, T_DBL, "Chorus double", 25, 8, F::Vocal, 71);
    for (bar, bars, name, f, seed) in [(11, 2, "Saw Riser", F::Riser, 81), (13, 1, "Deep Impact", F::Impact, 82), (23, 2, "Saw Riser", F::Riser, 83), (25, 1, "Deep Impact", F::Impact, 82)] {
        audio(&mut p, T_FX, name, bar, bars, f, seed);
    }
    p
}
