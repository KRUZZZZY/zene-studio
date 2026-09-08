#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""demo_edits.py — deterministic, minimal edits to a real LMMS project.

Demo helper for tools/mmpz-git/run-demo.sh. Every edit is applied to the DOM
and written back in the same container format as the input, so the resulting
file is a *real* project variant, not a hand-written fixture.

Usage:
  demo_edits.py set-bpm      FILE VALUE
  demo_edits.py add-note     FILE --track T --pattern P --key K --pos N [--vol V] [--len L]
  demo_edits.py set-note-vol FILE --track T --pattern P --pos N --key K --vol V
  demo_edits.py add-track    FILE --name NAME [--type 0]
  demo_edits.py set-track-attr FILE --track T --attr A --value V
  demo_edits.py remove-note  FILE --track T --pattern P --pos N --key K
  demo_edits.py move-note    FILE --track T --pattern P --pos N --key K --to-pos M [--to-key K2]
  demo_edits.py resolve      FILE            # strip mmpz-git conflict comments
"""
import argparse
import sys
from xml.dom import Node

import mmpz_git as M


def _find_tracks(root, name):
    return [t for t in root.getElementsByTagName("track")
            if t.getAttribute("name") == name]


def _find_pattern(track, name):
    for p in track.getElementsByTagName("pattern"):
        if p.getAttribute("name") == name:
            return p
    raise SystemExit("pattern %r not found in track %r" % (name, track.getAttribute("name")))


def _track(root, name):
    ts = _find_tracks(root, name)
    if not ts:
        raise SystemExit("track %r not found" % name)
    return ts[0]


def _write(path, doc, was_container):
    xml = M.to_bytes(doc, canonical=False)
    data = M.compress(xml) if was_container else xml
    with open(path, "wb") as fh:
        fh.write(data)


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)

    a = sub.add_parser("set-bpm"); a.add_argument("file"); a.add_argument("value")
    b = sub.add_parser("add-note")
    b.add_argument("file")
    b.add_argument("--track", required=True); b.add_argument("--pattern", required=True)
    b.add_argument("--key", required=True); b.add_argument("--pos", required=True)
    b.add_argument("--vol", default="100"); b.add_argument("--len", default="24")
    c = sub.add_parser("set-note-vol")
    c.add_argument("file")
    c.add_argument("--track", required=True); c.add_argument("--pattern", required=True)
    c.add_argument("--pos", required=True); c.add_argument("--key", required=True)
    c.add_argument("--vol", required=True)
    d = sub.add_parser("add-track"); d.add_argument("file")
    d.add_argument("--name", required=True); d.add_argument("--type", default="0")
    e = sub.add_parser("set-track-attr"); e.add_argument("file")
    e.add_argument("--track", required=True); e.add_argument("--attr", required=True)
    e.add_argument("--value", required=True)
    f = sub.add_parser("remove-note"); f.add_argument("file")
    f.add_argument("--track", required=True); f.add_argument("--pattern", required=True)
    f.add_argument("--pos", required=True); f.add_argument("--key", required=True)
    g = sub.add_parser("move-note"); g.add_argument("file")
    g.add_argument("--track", required=True); g.add_argument("--pattern", required=True)
    g.add_argument("--pos", required=True); g.add_argument("--key", required=True)
    g.add_argument("--to-pos", required=True); g.add_argument("--to-key")
    h = sub.add_parser("resolve"); h.add_argument("file")

    args = p.parse_args()
    with open(args.file, "rb") as fh:
        raw = fh.read()
    was_container = M.is_container(raw)
    doc = M.parse(M.load_any(args.file))
    root = doc.documentElement
    assert root is not None

    if args.cmd == "set-bpm":
        head = root.getElementsByTagName("head")[0]
        head.setAttribute("bpm", args.value)
    elif args.cmd == "add-note":
        pat = _find_pattern(_track(root, args.track), args.pattern)
        n = doc.createElement("note")
        for k, v in (("key", args.key), ("vol", args.vol), ("pos", args.pos),
                     ("pan", "0"), ("len", args.len)):
            n.setAttribute(k, v)
        pat.appendChild(n)
    elif args.cmd == "set-note-vol":
        pat = _find_pattern(_track(root, args.track), args.pattern)
        hit = 0
        for n in pat.getElementsByTagName("note"):
            if n.getAttribute("pos") == args.pos and n.getAttribute("key") == args.key:
                n.setAttribute("vol", args.vol); hit += 1
        if not hit:
            raise SystemExit("note not found")
    elif args.cmd == "add-track":
        container = None
        for tc in root.getElementsByTagName("trackcontainer"):
            if tc.getAttribute("type") == "song":
                container = tc
        if container is None:
            raise SystemExit("song trackcontainer not found")
        t = doc.createElement("track")
        t.setAttribute("type", args.type)
        t.setAttribute("muted", "0")
        t.setAttribute("name", args.name)
        t.setAttribute("solo", "0")
        container.appendChild(t)
    elif args.cmd == "set-track-attr":
        _track(root, args.track).setAttribute(args.attr, args.value)
    elif args.cmd in ("remove-note", "move-note"):
        pat = _find_pattern(_track(root, args.track), args.pattern)
        hit = 0
        for n in pat.getElementsByTagName("note"):
            if n.getAttribute("pos") == args.pos and n.getAttribute("key") == args.key:
                if args.cmd == "remove-note":
                    n.parentNode.removeChild(n)
                else:
                    n.setAttribute("pos", args.to_pos)
                    if args.to_key:
                        n.setAttribute("key", args.to_key)
                hit += 1
        if not hit:
            raise SystemExit("note not found")
    elif args.cmd == "resolve":
        # remove every comment node that carries the mmpz-git conflict banner
        stack = [doc]
        removed = 0
        while stack:
            n = stack.pop()
            for c in list(n.childNodes):
                if c.nodeType == Node.COMMENT_NODE and "mmpz-git" in c.data:
                    n.removeChild(c)
                    removed += 1
                elif c.nodeType == Node.ELEMENT_NODE:
                    stack.append(c)
        if not removed:
            raise SystemExit("no mmpz-git conflict comments found")

    _write(args.file, doc, was_container)
    return 0


if __name__ == "__main__":
    sys.exit(main())
