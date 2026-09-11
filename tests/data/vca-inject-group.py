#!/usr/bin/env python3
"""Inject a <vcagroup> into a copy of tests/data/vca-render-fixture.mmp.

Usage: vca-inject-group.py <in.mmp> <out.mmp> <vca-value> <member> [<member>...]

The group element is inserted as a child of <mixer>, which is where
Mixer::saveSettings() writes it, so the render exercises the real load path.
"""
import sys
from pathlib import Path
from xml.etree import ElementTree as ET

ET.register_namespace("", "")


def main():
    src, dst, vca = sys.argv[1], sys.argv[2], sys.argv[3]
    members = sys.argv[4:]
    text = Path(src).read_text()
    if "<vcagroup" in text:
        raise SystemExit(f"{src} already contains a group")
    group = [f'<vcagroup id="0" name="Group" vca="{vca}" muted="0" soloed="0">']
    for member in members:
        group.append(f'<member channel="{member}"/>')
    group.append("</vcagroup>")
    block = "\n".join("      " + line for line in group)
    marker = "    </mixer>"
    if marker not in text:
        raise SystemExit(f"{src}: no '{marker}' to insert before")
    out = text.replace(marker, block + "\n" + marker, 1)
    Path(dst).write_text(out)
    print(f"wrote {dst}: vca={vca} members={members}")


main()
