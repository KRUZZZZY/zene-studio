#!/usr/bin/env python3
"""Resource-resolution sweep for Zene Studio artwork names.

Why this exists.  `embed::loadSvgPixmap()` (src/gui/embed.cpp:41) resolves a name by
appending `.svg`, opening it through the artwork search path, and - when it cannot - doing
`qWarning() << "Failed to open resource for SVG: "` and returning a 1x1 transparent QPixmap.
No exception, no non-zero exit, no failed build.  So a stale name is a blank image plus a log
line, and proving one loads needs a sweep of every name, not a build.

How a name resolves (verified against the tree, not assumed):

  * `PluginPixmapLoader("x")` / a plugin's own `getIconPixmap("x")` prefixes the name with
    PLUGIN_NAME (include/embed.h:104), i.e. `artwork:PLUGIN/x`.  cmake/modules/BuildPlugin.cmake:30
    generates a .qrc with `PREFIX artwork/${PLUGIN_NAME}` from the plugin's EMBEDDED_RESOURCES,
    and src/gui/GuiApplication.cpp:108 registers `:/artwork` - so the file resolves from the
    plugin's own source directory.
  * A bare `PixmapLoader("x")` resolves against the theme search path
    (GuiApplication.cpp:106-108: themeDir, defaultThemeDir, :/artwork).

This sweep therefore accepts, for the name N referenced from file F: N.<ext> in F's own plugin
directory (if it is in one) or in either shipped theme directory.  Extensions are the ones
QImageReader can open here: svg, png, xpm, jpg, jpeg.

Usage: python3 tests/brand-resource-sweep.py [--quiet]
"""

from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

THEME_DIRS = ["data/themes/default", "data/themes/classic"]
EXTS = [".svg", ".png", ".xpm", ".jpg", ".jpeg"]

# Vendored / frozen trees are not this repo's code and are deliberately out of scope:
# src/3rdparty, tests/reference (verbatim upstream copies, blob-pinned in ORIGIN.tsv).
SKIP_DIR_PARTS = ("src/3rdparty/", "tests/", "build/", ".git/")

CALL_RE = re.compile(
    r'(?:PixmapLoader|PluginPixmapLoader|getIconPixmap)\s*[({]\s*"([^"]+)"')


def first_party_files() -> list[str]:
    out = []
    for base in ("src", "include", "plugins"):
        for dirpath, dirnames, filenames in os.walk(os.path.join(ROOT, base)):
            rel_dir = os.path.relpath(dirpath, ROOT) + "/"
            if any(part in rel_dir for part in SKIP_DIR_PARTS):
                continue
            # Skip vendored plugin subtrees (submodules / third-party bundles).
            if re.search(r"/(carla|game-music-emu|portsmf|adplug|resid|exprtk|zynaddsubfx|"
                         r"rtneural|nam|rnnoise|swh|calf|cmt|tap)/", rel_dir):
                continue
            for fn in filenames:
                if fn.endswith((".cpp", ".h", ".hpp", ".cc")):
                    out.append(os.path.join(dirpath, fn))
    return sorted(out)


def plugin_dir_of(rel: str) -> str | None:
    """The plugin's own directory: the nearest ancestor whose CMakeLists.txt builds a plugin.

    BuildPlugin names the .qrc prefix after PLUGIN_NAME and the source directory, so this - not
    the file's first directory - is the root its embedded artwork resolves from.  A plugin whose
    sources sit in a subdirectory (plugins/Stk/Mallets) is the reason this walks up.
    """
    d = os.path.dirname(os.path.join(ROOT, rel))
    while d and d.startswith(ROOT) and d != ROOT:
        cl = os.path.join(d, "CMakeLists.txt")
        if os.path.isfile(cl):
            try:
                txt = open(cl, encoding="utf-8", errors="replace").read()
            except OSError:
                txt = ""
            if "build_plugin" in txt.lower():
                return os.path.relpath(d, ROOT)
        d = os.path.dirname(d)
    return None


def resolves(name: str, plugin_dir: str | None) -> str | None:
    """Return the resolved repo-relative path, or None.

    A plugin-prefixed name resolves from the plugin's own directory first (that is what its
    generated artwork/<PLUGIN_NAME> .qrc embeds); the shipped theme is the fallback, which is
    the whole answer for the non-plugin callers in src/.
    """
    dirs = list(THEME_DIRS)
    if plugin_dir:
        dirs.insert(0, plugin_dir)
    for d in dirs:
        for ext in EXTS:
            cand = os.path.join(ROOT, d, name + ext)
            if os.path.isfile(cand):
                return os.path.relpath(cand, ROOT)
    return None


# Names that do NOT resolve on the base branch either.  They are pinned here so this sweep is a
# RATCHET: it exits 0 while the unresolved set is exactly this, and fails the moment a new name
# stops resolving.  Every entry was measured pre-existing - see docs/BRAND-PLACEHOLDERS.md.
KNOWN_PREEXISTING = {
    # src/gui/LfoControllerDialog.cpp does not ship arp_down_on/arp_up_on; the file is
    # untouched by the brand-placeholder change (last upstream change ed0f288c8).
    ("arp_down_on", "src/gui/LfoControllerDialog.cpp"),
    ("arp_up_on", "src/gui/LfoControllerDialog.cpp"),
    # plugins/GranularPitchShifter has no logo.png at all, so its dialog's window icon has
    # always been the 1x1 fallback.  An upstream defect, reported not fixed.
    ("logo", "plugins/GranularPitchShifter/GranularPitchShifterControlDialog.cpp"),
}


def scan_call_sites() -> tuple[dict[str, set[str]], int]:
    """Every (name -> referencing files) pair in first-party code, and the raw call count."""
    names: dict[str, set[str]] = {}
    occurrences = 0
    for path in first_party_files():
        rel = os.path.relpath(path, ROOT)
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for m in CALL_RE.finditer(text):
            occurrences += 1
            names.setdefault(m.group(1), set()).add(rel)
    return names, occurrences


def resolve_call_sites(names: dict[str, set[str]]) -> tuple[list[tuple[str, str]], int]:
    """Resolve per CALL SITE, not per name.

    `logo` is used by ~36 plugins and each resolves it from its own directory, so a
    name-level answer is wrong in both directions: it hides one plugin's missing file
    behind another's present one, and it reports a staleness the product does not have.
    """
    unresolved: list[tuple[str, str]] = []
    resolved = 0
    for name, files in sorted(names.items()):
        for f in sorted(files):
            if resolves(name, plugin_dir_of(f)):
                resolved += 1
            else:
                unresolved.append((name, f))
    return unresolved, resolved


def print_report(names: dict[str, set[str]], occurrences: int,
                 unresolved: list[tuple[str, str]], resolved: int, quiet: bool) -> None:
    fresh = sorted(set(unresolved) - KNOWN_PREEXISTING)
    known = sorted(set(unresolved) & KNOWN_PREEXISTING)
    print(f"first-party resource names referenced: {len(names)}")
    print(f"call sites scanned:                    {occurrences}")
    print(f"call sites resolved:                   {resolved}")
    print(f"UNRESOLVED call sites:                 {len(unresolved)}"
          f"  ({len(known)} pre-existing baseline, {len(fresh)} NEW)")
    if known:
        print()
        print("  pre-existing (pinned in KNOWN_PREEXISTING; not this change's):")
        for name, f in known:
            print(f"    {name}  <- {f}")
    if fresh:
        print()
        print("  NEW - these fail the sweep:")
        for name, f in fresh:
            print(f"    {name}  <- {f}")
    if quiet:
        return
    print()
    for probe in ("zene-plugin-logo", "splash", "icon", "background_artwork"):
        print(f"    probe {probe!r:22} -> {resolves(probe, None)}")


def main() -> int:
    names, occurrences = scan_call_sites()
    unresolved, resolved = resolve_call_sites(names)
    strict = "--strict" in sys.argv
    print_report(names, occurrences, unresolved, resolved, "--quiet" in sys.argv)
    if strict:
        return 1 if unresolved else 0
    return 1 if set(unresolved) - KNOWN_PREEXISTING else 0


if __name__ == "__main__":
    sys.exit(main())
