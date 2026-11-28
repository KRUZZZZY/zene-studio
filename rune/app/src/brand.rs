//! The prototype's mark: a small singing songbird. Original artwork drawn for this prototype
//! (not from Lucide or any other library), kept to flat shapes so it reads at 16 px and takes
//! the theme's accent like every other glyph. The same drawing, on a tile with a wing and a
//! perch, is the launcher icon in `assets/brand/zene-songbird.svg`.

use rune::Icon;

pub static SONGBIRD: Icon = Icon {
    name: "zene-songbird",
    view: 24.0,
    body: concat!(
        // Head, beak, body and tail as one silhouette, with the eye cut out.
        r#"<path fill="currentColor" fill-rule="evenodd" d="M12.4 6.9a4.1 4.1 0 0 1 7.6-1l3 1.3-2.7 1.2c.9 5-2.3 9.4-7.2 9.8-1.9.2-3.7-.3-5.1-1.3L2.2 21l-.9-1.4 4.2-5.3c.4-3.2 2.9-6.2 6.9-7.4ZM17.3 6.1a.95.95 0 1 0 0 1.9.95.95 0 0 0 0-1.9Z"/>"#,
        // The note it is singing.
        r#"<path fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round" d="M21.3 .9v3.3M21.3 .9c1 .2 1.7.8 1.9 1.7"/>"#,
        r#"<circle cx="20.3" cy="4.4" r="1.15" fill="currentColor"/>"#,
    ),
};
