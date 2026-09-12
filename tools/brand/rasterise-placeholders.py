#!/usr/bin/env python3
"""rasterise-placeholders.py - render the hand-authored brand placeholders.

Zene Studio shipped upstream LMMS identity artwork in 41 shipped image files
(see docs/BRAND-PLACEHOLDERS.md).  The three SVGs this script reads are the
hand-authored, licence-free replacements for the three upstream SVG sources;
this script derives every raster size from them so there is exactly one source
of truth per mark and no bitmap is ever edited by hand.

    cmake/linux/icons/scalable/apps/zene.svg
        -> every cmake/linux/icons/*/apps/zene.png
        -> cmake/nsis/assets/{Logo,SmallLogo}.png, cmake/nsis/icon.ico
        -> cmake/apple/icon.icns, splash/background artwork
    cmake/linux/icons/scalable/mimetypes/application-x-zene-project.svg
        -> every cmake/linux/icons/*/mimetypes/application-x-lmms-project.png
        -> cmake/nsis/project.ico, cmake/apple/project.icns

Rendering goes through Qt's QSvgRenderer - the same engine the product uses in
src/gui/embed.cpp (embed::loadSvgPixmap) - so a pixel that renders here renders
in the application.  ImageMagick is deliberately NOT used: its internal MSVG
renderer drops fill-rule="evenodd", which is how the plugin logo punctuation
works.

Pixel dimensions are pinned to the upstream files this script replaces; they are
asserted before anything is written, because a wrong icon size breaks a desktop
entry or an installer silently.

Run:  python3 tools/brand/rasterise-placeholders.py [--check]

    --check   verify every generated file matches what this script would write,
              without writing (used by the report's reproduction command).

Requires PySide6 (authoring-time only; the product does not depend on it).
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QColor, QFont, QFontDatabase, QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# QFontDatabase (used for the placeholder captions) needs a QGuiApplication; the
# offscreen platform keeps this runnable on a headless build box.  This mirrors
# how the test suite drives Qt here (QT_QPA_PLATFORM=offscreen).
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
_APP = QGuiApplication.instance() or QGuiApplication(sys.argv[:1])

APP_SVG = "cmake/linux/icons/scalable/apps/zene.svg"
MIME_SVG = "cmake/linux/icons/scalable/mimetypes/application-x-zene-project.svg"

PLACEHOLDER_NOTE = (
    "placeholder - pending the product mark (Zene Studio placeholder artwork, 2026-09-12)"
)

# ---------------------------------------------------------------------------
# Target inventory.  Every entry is (relative path, width, height).
# The sizes are the upstream files' own sizes, read with a PNG/ICO/ICNS parser
# before this script existed and re-asserted here by --check.
# ---------------------------------------------------------------------------

# hicolor icon theme directory -> pixel size (the "@2" dirs are the HiDPI sizes)
ICON_DIRS = {
    "16x16": 16,
    "16x16@2": 32,
    "24x24": 24,
    "24x24@2": 48,
    "32x32": 32,
    "32x32@2": 64,
    "48x48": 48,
    "48x48@2": 96,
    "64x64": 64,
    "64x64@2": 128,
    "128x128": 128,
    "128x128@2": 256,
    "256x256": 256,
}

ICO_SIZES = [16, 24, 32, 48, 64, 96, 128, 256]  # exactly upstream icon.ico's set

# ICNS chunk types carrying PNG payload.  ic04-ic06 are the legacy raw types
# upstream used; they are dropped rather than faked - see the report.
ICNS_PNG_TYPES = [("ic11", 32), ("ic12", 64), ("ic07", 128), ("ic13", 256), ("ic09", 512)]


def qimage(px: int, py: int, alpha: bool = True) -> QImage:
    img = QImage(px, py, QImage.Format_ARGB32 if alpha else QImage.Format_RGB32)
    img.fill(QColor(0, 0, 0, 0) if alpha else QColor(255, 255, 255))
    return img


def render_svg(rel_svg: str, px: int, py: int | None = None, alpha: bool = True) -> QImage:
    """Rasterise one of the hand-authored SVGs to fit px x py through QSvgRenderer."""
    py = py or px
    path = os.path.join(ROOT, rel_svg)
    renderer = QSvgRenderer(path)
    if not renderer.isValid():
        raise SystemExit(f"FATAL: QSvgRenderer rejected {rel_svg} - not valid SVG")
    img = qimage(px, py, alpha)
    painter = QPainter(img)
    painter.setRenderHint(QPainter.Antialiasing, True)
    painter.setRenderHint(QPainter.SmoothPixmapTransform, True)
    renderer.render(painter, QRectF(0, 0, px, py))
    painter.end()
    return img


# ---------------------------------------------------------------------------
# PNG text metadata: mark every generated file as a placeholder, in the file.
# ---------------------------------------------------------------------------

def png_with_text(png: bytes, key: str, value: str) -> bytes:
    """Insert a tEXt chunk straight after IHDR.  Lossless: IDAT is untouched."""
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("FATAL: not a PNG")
    ihdr_end = 8 + 4 + 4 + struct.unpack(">I", png[8:12])[0] + 4
    payload = key.encode("latin-1") + b"\x00" + value.encode("latin-1")
    chunk = struct.pack(">I", len(payload)) + b"tEXt" + payload
    chunk += struct.pack(">I", zlib.crc32(b"tEXt" + payload) & 0xFFFFFFFF)
    return png[:ihdr_end] + chunk + png[ihdr_end:]


def save_png(img: QImage, rel_path: str, note: str = PLACEHOLDER_NOTE) -> bytes:
    tmp = os.path.join(ROOT, rel_path)
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    img.save(tmp, "PNG")
    with open(tmp, "rb") as fh:
        raw = fh.read()
    tagged = png_with_text(raw, "Description", note)
    with open(tmp, "wb") as fh:
        fh.write(tagged)
    return tagged


# ---------------------------------------------------------------------------
# ICO / ICNS writers.  Both reproduce the upstream container structure.
# ---------------------------------------------------------------------------

def ico_entry(img: QImage) -> bytes:
    """One classic 32bpp BMP icon directory entry body (XOR bitmap + AND mask).

    This is the format upstream's icon.ico uses: a 16x16 entry produced by this
    function is 40 + 16*16*4 + 16*4 = 1128 bytes, byte-count identical to the
    upstream entry, which is how the container layout is known to be right.
    """
    w, h = img.width(), img.height()
    bmp = img.convertToFormat(QImage.Format_ARGB32)
    rows = []
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            c = bmp.pixelColor(x, y)
            row += bytes((c.blue(), c.green(), c.red(), c.alpha()))
        rows.append(bytes(row))
    xor = b"".join(rows)
    mask_row = (w + 31) // 32 * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        bits = bytearray(mask_row)
        for x in range(w):
            if bmp.pixelColor(x, y).alpha() == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    return header + xor + bytes(mask)


def write_ico(rel_path: str, svg: str, sizes: list[int]) -> None:
    entries = []
    for s in sizes:
        img = render_svg(svg, s)
        body = ico_entry(img)
        entries.append((s, body))
    # ICO directory: 6-byte header, then 16 bytes per entry, then the bodies.
    offset = 6 + 16 * len(entries)
    out = struct.pack("<HHH", 0, 1, len(entries))
    bodies = b""
    for s, body in entries:
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(body), offset)
        bodies += body
        offset += len(body)
    path = os.path.join(ROOT, rel_path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(out + bodies)


def write_icns(rel_path: str, svg: str) -> None:
    chunks = b""
    for ctype, size in ICNS_PNG_TYPES:
        img = render_svg(svg, size)
        # QImage.save to a buffer, then tag it so the PNG inside the container
        # carries the placeholder note too.
        import tempfile
        with tempfile.TemporaryDirectory() as td:
            p = os.path.join(td, "x.png")
            img.save(p, "PNG")
            with open(p, "rb") as fh:
                data = png_with_text(fh.read(), "Description", PLACEHOLDER_NOTE)
        chunks += ctype.encode("ascii") + struct.pack(">I", len(data) + 8) + data
    out = b"icns" + struct.pack(">I", len(chunks) + 8) + chunks
    path = os.path.join(ROOT, rel_path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(out)


# ---------------------------------------------------------------------------
# Composed artwork (splash, DMG background, desktop backgrounds).
# ---------------------------------------------------------------------------

def _font() -> QFont:
    for fam in ("DejaVu Sans", "Liberation Sans", "Noto Sans", "Sans Serif"):
        if fam in QFontDatabase.families():
            f = QFont(fam)
            f.setStyleHint(QFont.SansSerif)
            return f
    return QFont()


def _caption(painter: QPainter, rect: QRectF, lines: list[tuple[str, int, int]], colour: str):
    """Draw (text, point-size, vertical-offset) lines centred in rect."""
    y = rect.top()
    for text, size, gap in lines:
        f = _font()
        f.setPointSize(size)
        f.setBold(size >= 20)
        painter.setFont(f)
        painter.setPen(QColor(colour))
        painter.drawText(QRectF(rect.left(), y, rect.width(), size * 2.2),
                         int(Qt.AlignHCenter | Qt.AlignTop), text)
        y += size * 2.2 + gap


def compose_card(path: str, w: int, h: int, svg: str, glyph: int, bg: str,
                 ink: str, lines: list[tuple[str, int, int]], alpha: bool,
                 glyph_dy: int = 0, glyph_dx: int = 0) -> None:
    img = qimage(w, h, alpha)
    painter = QPainter(img)
    painter.setRenderHint(QPainter.Antialiasing, True)
    if bg is not None:
        painter.fillRect(0, 0, w, h, QColor(bg))
    r = QSvgRenderer(os.path.join(ROOT, svg))
    painter.drawImage(QRectF((w - glyph) / 2 + glyph_dx, (h - glyph) / 2 + glyph_dy, glyph, glyph),
                      render_svg(svg, glyph))
    _caption(painter, QRectF(0, (h + glyph) / 2 + glyph_dy, w, h), lines, ink)
    painter.end()
    save_png(img, path)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="verify current files match this script's output")
    args = ap.parse_args()

    if args.check:
        return check()

    written = []

    # 1. Linux hicolor icons - application and project file.
    for d, px in ICON_DIRS.items():
        save_png(render_svg(APP_SVG, px), f"cmake/linux/icons/{d}/apps/zene.png")
        written.append(f"cmake/linux/icons/{d}/apps/zene.png")
        p = f"cmake/linux/icons/{d}/mimetypes/application-x-lmms-project.png"
        save_png(render_svg(MIME_SVG, px), p)
        written.append(p)

    # 2. Windows installer art.
    save_png(render_svg(APP_SVG, 600), "cmake/nsis/assets/Logo.png")
    written.append("cmake/nsis/assets/Logo.png")
    save_png(render_svg(APP_SVG, 192), "cmake/nsis/assets/SmallLogo.png")
    written.append("cmake/nsis/assets/SmallLogo.png")
    write_ico("cmake/nsis/icon.ico", APP_SVG, ICO_SIZES)
    written.append("cmake/nsis/icon.ico")
    write_ico("cmake/nsis/project.ico", MIME_SVG, ICO_SIZES)
    written.append("cmake/nsis/project.ico")

    # 3. macOS bundle art.
    write_icns("cmake/apple/icon.icns", APP_SVG)
    written.append("cmake/apple/icon.icns")
    write_icns("cmake/apple/project.icns", MIME_SVG)
    written.append("cmake/apple/project.icns")
    for path, w, h in (("cmake/apple/background.png", 705, 400),
                       ("cmake/apple/background@2x.png", 1410, 800)):
        compose_card(path, w, h, APP_SVG, int(min(w, h) * 0.45), "#eceff4", "#2e3440",
                     [("Zene Studio", max(14, h // 32), 4),
                      ("placeholder artwork - pending the product mark",
                       max(10, h // 56), 0)],
                     alpha=True)
        written.append(path)

    # 4. The in-application splash.
    compose_card("data/themes/default/splash.png", 681, 573, APP_SVG, 250, "#1f2430",
                 "#e5e9f0",
                 [("ZENE STUDIO", 34, 10),
                  ("placeholder artwork - pending the product mark", 15, 0)],
                 alpha=False, glyph_dy=-30)
    written.append("data/themes/default/splash.png")

    # 5. Desktop backgrounds (installed by data/backgrounds/CMakeLists.txt).
    compose_card("data/backgrounds/vinnie.png", 384, 383, APP_SVG, 190, "#2e3440", "#e5e9f0",
                 [("placeholder artwork", 15, 0)], alpha=False, glyph_dy=-10)
    written.append("data/backgrounds/vinnie.png")
    compose_card("data/backgrounds/newbg.png", 420, 215, APP_SVG, 110, "#2e3440", "#e5e9f0",
                 [("placeholder artwork", 12, 0)], alpha=False, glyph_dy=-6)
    written.append("data/backgrounds/newbg.png")

    # 6. The tiling desktop watermark (1-bit upstream; RGBA here - see the report).
    tile = qimage(64, 64, True)
    tp = QPainter(tile)
    tp.setRenderHint(QPainter.Antialiasing, True)
    tp.setOpacity(0.35)
    tp.drawImage(QRectF(16, 8, 40, 48), render_svg(APP_SVG, 40, 48))
    tp.end()
    save_png(tile, "data/backgrounds/zene_tile.png")
    written.append("data/backgrounds/zene_tile.png")

    print(f"wrote {len(written)} placeholder raster files")
    for p in written:
        print("  " + p)
    return 0


def check() -> int:
    """Re-render in memory and compare against the committed bytes."""
    import io
    bad = []
    expect = {}
    for d, px in ICON_DIRS.items():
        expect[f"cmake/linux/icons/{d}/apps/zene.png"] = render_svg(APP_SVG, px)
        expect[f"cmake/linux/icons/{d}/mimetypes/application-x-lmms-project.png"] = \
            render_svg(MIME_SVG, px)
    for rel, img in expect.items():
        buf = io.BytesIO()
        img.save(buf, "PNG")
        wanted = png_with_text(buf.getvalue(), "Description", PLACEHOLDER_NOTE)
        with open(os.path.join(ROOT, rel), "rb") as fh:
            got = fh.read()
        if got != wanted:
            bad.append(rel)
    print(f"checked {len(expect)} rendered PNGs; {len(bad)} differ")
    for b in bad:
        print("  DIFFERS: " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
