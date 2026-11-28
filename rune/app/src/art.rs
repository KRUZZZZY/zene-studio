//! Demo artwork: a small subset of the CC0 Kenney pattern packs (assets/, see ASSETS.md),
//! embedded in the binary and decoded once on Rune's image worker, never on the frame path.
//! The patterns are black and white; views lay a translucent track colour over them.

use rune::{Image, ImageCache, Waker};

const PATTERNS: [&[u8]; 8] = [
    include_bytes!("../../assets/kenney-patterns/PNG/Default size/pattern02.png"),
    include_bytes!("../../assets/kenney-patterns/PNG/Default size/pattern27.png"),
    include_bytes!("../../assets/kenney-patterns/PNG/Default size/pattern08.png"),
    include_bytes!("../../assets/kenney-patterns/PNG/Default size/pattern30.png"),
    include_bytes!("../../assets/kenney-patterns/PNG/Default size/pattern16.png"),
    include_bytes!("../../assets/kenney-patterns/PNG/Default size/pattern61.png"),
    include_bytes!("../../assets/kenney-patterns/PNG/Default size/pattern44.png"),
    include_bytes!("../../assets/kenney-patterns/PNG/Default size/pattern70.png"),
];

#[derive(Default)]
pub struct Art {
    cache: Option<ImageCache>,
    images: Vec<Image>,
}

impl Art {
    pub fn new() -> Art {
        Art::default()
    }
    /// Start decoding. In a headless run the images are waited for, so snapshots are stable.
    pub fn load(&mut self, waker: Waker) {
        let cache = ImageCache::new(waker);
        self.images = PATTERNS.iter().map(|b| cache.load(*b)).collect();
        if rune::tooling::headless_run().is_some() {
            for i in &self.images {
                let _ = i.wait();
            }
        }
        self.cache = Some(cache);
    }
    /// Pattern `i` (wrapping), once loading has started.
    pub fn pattern(&self, i: usize) -> Option<&Image> {
        if self.images.is_empty() { None } else { self.images.get(i % self.images.len()) }
    }
}
