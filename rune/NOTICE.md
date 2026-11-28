# Notices for reused material in rune/

- **Rune** (MIT, (c) Anomaly): the UI framework, fetched at build time into `vendor/rune` at
  `b53fe7236ab11225108f6aee837c2d9942f63991` by `fetch-rune.sh` and never committed here. Its licence
  is `vendor/rune/LICENSE` in that checkout. Rune embeds the Inter typeface (SIL Open Font License 1.1).
- **Lucide icons** (ISC, with MIT notices for Feather-derived symbols): 67 icons are compiled into
  `app/src/icons.rs` (generated in the prototype by its `tools/gen_icons.py` from Lucide revision
  `03eae6069edfc373673bd05d723491f05ba936e3`). The full licence text is `assets/lucide/LICENSE` and
  must travel with any redistribution. Lucide is not CC0.
- **Kenney Pattern Pack** (CC0 1.0, by Kenney, kenney.nl): eight patterns under
  `assets/kenney-patterns/PNG/Default size/` are embedded by `app/src/art.rs` as pack covers and
  browser thumbnails, tinted in the app. Attribution is optional under CC0; credit is given here and
  the original `assets/kenney-patterns/License.txt` is kept beside the art.
- **Songbird mark** (`app/src/brand.rs`, `assets/brand/zene-songbird.svg`): original artwork drawn for
  the prototype; GPL-2.0-or-later with the rest of the code.
- The code in `app/` and `ci/` is GPL-2.0-or-later, matching Zene Studio.

No audio samples, fonts or artwork from any commercial DAW are included.
